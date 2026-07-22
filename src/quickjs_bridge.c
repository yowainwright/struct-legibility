#include "quickjs_bridge.h"

#include "quickjs-libc.h"
#include "struct_legibility.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  const char **items;
  size_t count;
} JsPathList;

static int runtime_exit_code = 0;

static int add_safe_intrinsics(JSContext *context) {
  if (JS_AddIntrinsicBaseObjects(context)) return 0;
  if (JS_AddIntrinsicDate(context)) return 0;
  if (JS_AddIntrinsicRegExp(context)) return 0;
  if (JS_AddIntrinsicJSON(context)) return 0;
  if (JS_AddIntrinsicProxy(context)) return 0;
  if (JS_AddIntrinsicMapSet(context)) return 0;
  if (JS_AddIntrinsicTypedArrays(context)) return 0;
  if (JS_AddIntrinsicPromise(context)) return 0;
  if (JS_AddIntrinsicWeakRef(context)) return 0;
  if (JS_AddIntrinsicAToB(context)) return 0;
  if (JS_AddPerformance(context)) return 0;
  return 1;
}

JSContext *sl_quickjs_new_context(JSRuntime *runtime) {
  JSContext *context = JS_NewContextRaw(runtime);
  if (context == NULL) return NULL;
  if (add_safe_intrinsics(context)) return context;
  JS_FreeContext(context);
  return NULL;
}

static JSValue evaluate_binary_object(JSContext *context, JSValue object) {
  if (!JS_IsModule(object)) return JS_EvalFunction(context, object);
  if (JS_ResolveModule(context, object) < 0) {
    JS_FreeValue(context, object);
    return JS_EXCEPTION;
  }
  if (js_module_set_import_meta(context, object, false, true) < 0) {
    JS_FreeValue(context, object);
    return JS_EXCEPTION;
  }
  return js_std_await(context, JS_EvalFunction(context, object));
}

int sl_quickjs_eval_binary(JSContext *context, const uint8_t *bytes, size_t length, int load_only) {
  if (load_only) return 2;
  JSValue object = JS_ReadObject(context, bytes, length, JS_READ_OBJ_BYTECODE);
  if (JS_IsException(object)) return 2;
  JSValue result = evaluate_binary_object(context, object);
  if (JS_IsException(result)) return 2;
  JS_FreeValue(context, result);
  return 0;
}

static void js_path_list_free(JSContext *context, JsPathList *paths) {
  for (size_t index = 0; index < paths->count; index++) {
    JS_FreeCString(context, paths->items[index]);
  }
  free(paths->items);
  *paths = (JsPathList){0};
}

static int js_path_list_read(JSContext *context, JSValueConst value, JsPathList *paths) {
  int64_t count = 0;
  if (!JS_IsArray(value) || JS_GetLength(context, value, &count) != 0) return 0;
  if (count < 1 || count > UINT32_MAX) return 0;
  paths->items = calloc((size_t)count, sizeof(*paths->items));
  if (paths->items == NULL) return 0;
  for (uint32_t index = 0; index < (uint32_t)count; index++) {
    JSValue item = JS_GetPropertyUint32(context, value, index);
    if (!JS_IsString(item)) {
      JS_FreeValue(context, item);
      return js_path_list_free(context, paths), 0;
    }
    const char *path = JS_ToCString(context, item);
    JS_FreeValue(context, item);
    if (path == NULL) return js_path_list_free(context, paths), 0;
    paths->items[paths->count++] = path;
  }
  return 1;
}

static void set_string(JSContext *context, JSValueConst object, const char *name,
                       const char *value) {
  JS_SetPropertyStr(context, object, name, JS_NewString(context, value));
}

static JSValue diagnostic_to_js(JSContext *context, const SlDiagnostic *diagnostic) {
  JSValue object = JS_NewObject(context);
  set_string(context, object, "path", diagnostic->path);
  set_string(context, object, "ruleId", diagnostic->rule_id);
  set_string(context, object, "message", diagnostic->message);
  JS_SetPropertyStr(context, object, "line", JS_NewInt64(context, diagnostic->line));
  JS_SetPropertyStr(context, object, "column", JS_NewInt64(context, diagnostic->column));
  JS_SetPropertyStr(context, object, "fixedError", JS_NewBool(context, diagnostic->fixed_error));
  return object;
}

static JSValue strings_to_js(JSContext *context, char *const *items, size_t count) {
  JSValue values = JS_NewArray(context);
  for (size_t index = 0; index < count; index++) {
    JSValue value = JS_NewString(context, items[index]);
    JS_SetPropertyUint32(context, values, (uint32_t)index, value);
  }
  return values;
}

static JSValue declaration_to_js(JSContext *context, const SlDeclarationFact *declaration) {
  JSValue object = JS_NewObject(context);
  set_string(context, object, "kind", declaration->kind);
  set_string(context, object, "name", declaration->name);
  JS_SetPropertyStr(context, object, "exported", JS_NewBool(context, declaration->exported));
  JS_SetPropertyStr(context, object, "entrypoint", JS_NewBool(context, declaration->entrypoint));
  JSValue export_names =
      strings_to_js(context, declaration->export_names, declaration->export_name_count);
  JS_SetPropertyStr(context, object, "exportNames", export_names);
  JS_SetPropertyStr(context, object, "line", JS_NewInt64(context, declaration->line));
  JS_SetPropertyStr(context, object, "column", JS_NewInt64(context, declaration->column));
  JSValue calls = strings_to_js(context, declaration->calls, declaration->call_count);
  JSValue suppressions =
      strings_to_js(context, declaration->suppressions, declaration->suppression_count);
  JS_SetPropertyStr(context, object, "calls", calls);
  JS_SetPropertyStr(context, object, "suppressions", suppressions);
  return object;
}

static JSValue declarations_to_js(JSContext *context, const SlFileFact *file) {
  JSValue declarations = JS_NewArray(context);
  for (size_t index = 0; index < file->declaration_count; index++) {
    JSValue declaration = declaration_to_js(context, &file->declarations[index]);
    JS_SetPropertyUint32(context, declarations, (uint32_t)index, declaration);
  }
  return declarations;
}

static JSValue import_to_js(JSContext *context, const SlImportFact *import) {
  JSValue object = JS_NewObject(context);
  set_string(context, object, "localName", import->local_name);
  set_string(context, object, "importedName", import->imported_name);
  set_string(context, object, "source", import->source);
  JSValue target =
      import->target_path == NULL ? JS_NULL : JS_NewString(context, import->target_path);
  JS_SetPropertyStr(context, object, "targetPath", target);
  return object;
}

static JSValue imports_to_js(JSContext *context, const SlFileFact *file) {
  JSValue imports = JS_NewArray(context);
  for (size_t index = 0; index < file->import_count; index++) {
    JSValue import = import_to_js(context, &file->imports[index]);
    JS_SetPropertyUint32(context, imports, (uint32_t)index, import);
  }
  return imports;
}

static JSValue file_to_js(JSContext *context, const SlFileFact *file) {
  JSValue object = JS_NewObject(context);
  set_string(context, object, "path", file->path);
  JS_SetPropertyStr(context, object, "declarations", declarations_to_js(context, file));
  JS_SetPropertyStr(context, object, "imports", imports_to_js(context, file));
  return object;
}

static JSValue files_to_js(JSContext *context, const SlReport *report) {
  JSValue files = JS_NewArray(context);
  for (size_t index = 0; index < report->file_count; index++) {
    JS_SetPropertyUint32(context, files, (uint32_t)index,
                         file_to_js(context, &report->files[index]));
  }
  return files;
}

static JSValue call_fact_to_js(JSContext *context, const SlCallFact *call) {
  JSValue object = JS_NewObject(context);
  set_string(context, object, "callerPath", call->caller_path);
  set_string(context, object, "callerName", call->caller_name);
  set_string(context, object, "calleePath", call->callee_path);
  set_string(context, object, "calleeName", call->callee_name);
  return object;
}

static JSValue call_facts_to_js(JSContext *context, const SlReport *report) {
  JSValue calls = JS_NewArray(context);
  for (size_t index = 0; index < report->call_count; index++) {
    JSValue call = call_fact_to_js(context, &report->calls[index]);
    JS_SetPropertyUint32(context, calls, (uint32_t)index, call);
  }
  return calls;
}

static JSValue project_to_js(JSContext *context, const SlReport *report) {
  JSValue project = JS_NewObject(context);
  JS_SetPropertyStr(context, project, "files", files_to_js(context, report));
  JS_SetPropertyStr(context, project, "calls", call_facts_to_js(context, report));
  return project;
}

static JSValue report_to_js(JSContext *context, const SlReport *report) {
  JSValue diagnostics = JS_NewArray(context);
  for (size_t index = 0; index < report->count; index++) {
    JSValue diagnostic = diagnostic_to_js(context, &report->diagnostics[index]);
    JS_SetPropertyUint32(context, diagnostics, (uint32_t)index, diagnostic);
  }
  JSValue object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "diagnostics", diagnostics);
  JS_SetPropertyStr(context, object, "project", project_to_js(context, report));
  return object;
}

static JSValue analyze_paths(JSContext *context, const JsPathList *paths, int use_gitignore,
                             int collect_facts) {
  const SlRequest request = {paths->items, paths->count, use_gitignore, collect_facts};
  SlReport report = {0};
  const SlStatus status = sl_analyze(&request, &report);
  if (status != SL_OK) {
    sl_report_free(&report);
    return JS_ThrowInternalError(context, "analysis failed (%d)", status);
  }
  JSValue result = report_to_js(context, &report);
  sl_report_free(&report);
  return result;
}

static JSValue js_analyze(JSContext *context, JSValueConst this_value, int argument_count,
                          JSValueConst *arguments) {
  (void)this_value;
  if (argument_count != 3) return JS_ThrowTypeError(context, "analyze requires paths and modes");
  JsPathList paths = {0};
  if (!js_path_list_read(context, arguments[0], &paths))
    return js_path_list_free(context, &paths),
           JS_ThrowTypeError(context, "paths must be a non-empty string array");
  const int modes_are_boolean = JS_IsBool(arguments[1]) && JS_IsBool(arguments[2]);
  if (!modes_are_boolean) {
    js_path_list_free(context, &paths);
    return JS_ThrowTypeError(context, "analysis modes must be boolean");
  }
  const int use_gitignore = JS_VALUE_GET_BOOL(arguments[1]);
  const int collect_facts = JS_VALUE_GET_BOOL(arguments[2]);
  JSValue result = analyze_paths(context, &paths, use_gitignore, collect_facts);
  js_path_list_free(context, &paths);
  return result;
}

static JSValue js_write(JSContext *context, JSValueConst this_value, int argument_count,
                        JSValueConst *arguments) {
  (void)this_value;
  if (argument_count != 2) return JS_ThrowTypeError(context, "write requires text and stream");
  const int types_are_valid = JS_IsString(arguments[0]) && JS_IsBool(arguments[1]);
  if (!types_are_valid) return JS_ThrowTypeError(context, "write requires string and boolean");
  const char *text = JS_ToCString(context, arguments[0]);
  if (text == NULL) return JS_EXCEPTION;
  const int use_stderr = JS_VALUE_GET_BOOL(arguments[1]);
  FILE *stream = use_stderr ? stderr : stdout;
  fputs(text, stream);
  JS_FreeCString(context, text);
  return JS_UNDEFINED;
}

static int read_exit_code(JSContext *context, JSValueConst value, int *code) {
  if (!JS_IsNumber(value)) return 0;
  double number = 0;
  if (JS_ToFloat64(context, &number, value) != 0) return 0;
  const int in_range = number >= 0.0 && number <= 2.0;
  if (!in_range) return 0;
  const int converted = (int)number;
  if ((double)converted != number) return 0;
  *code = converted;
  return 1;
}

static JSValue js_set_exit_code(JSContext *context, JSValueConst this_value, int argument_count,
                                JSValueConst *arguments) {
  (void)this_value;
  if (argument_count != 1) return JS_ThrowTypeError(context, "exit code is required");
  int code = 0;
  if (!read_exit_code(context, arguments[0], &code))
    return JS_ThrowTypeError(context, "exit code must be 0, 1, or 2");
  runtime_exit_code = code;
  return JS_UNDEFINED;
}

static JSValue js_default_profile(JSContext *context, JSValueConst this_value, int argument_count,
                                  JSValueConst *arguments) {
  (void)this_value;
  (void)arguments;
  if (argument_count != 0) return JS_ThrowTypeError(context, "default profile takes no arguments");
  const char *profile = getenv("STRUCT_LEGIBILITY_PROFILE");
  if (profile == NULL || profile[0] == '\0') profile = "local";
  return JS_NewString(context, profile);
}

static void register_function(JSContext *context, JSValueConst global, const char *name,
                              JSCFunction *function, int argument_count) {
  JSValue value = JS_NewCFunction(context, function, name, argument_count);
  JS_SetPropertyStr(context, global, name, value);
}

void sl_quickjs_register(JSContext *context) {
  JSValue global = JS_GetGlobalObject(context);
  register_function(context, global, "__slAnalyze", js_analyze, 3);
  register_function(context, global, "__slWrite", js_write, 2);
  register_function(context, global, "__slSetExitCode", js_set_exit_code, 1);
  register_function(context, global, "__slDefaultProfile", js_default_profile, 0);
  JS_FreeValue(context, global);
}

int sl_quickjs_exit_code(void) { return runtime_exit_code; }
