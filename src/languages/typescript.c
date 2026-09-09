#include "../nodes.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);
const TSLanguage *tree_sitter_javascript(void);

static TSNode binding_name_node(TSNode node);
static TSNode import_local_name_node(TSNode node);
static int is_function_node(TSNode node);

static int global_binding(TSNode node, const char *source, const char *name) {
  if (sl_text_is(binding_name_node(node), source, name)) return 1;
  if (sl_text_is(import_local_name_node(node), source, name)) return 1;
  return sl_node_is(node, "assignment_expression") &&
         sl_text_is(sl_field(node, "left"), source, name);
}

static int shadows_global(TSNode node, const char *source, const char *name) {
  if (global_binding(node, source, name)) return 1;
  if (is_function_node(node)) return 0;
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  TSNode child;
  int started = 0, shadowed = 0;
  while (!shadowed && !ts_node_is_null(child = sl_next_named_child(&cursor, &started)))
    shadowed = shadows_global(child, source, name);
  ts_tree_cursor_delete(&cursor);
  return shadowed;
}

static int global_available(TSNode node, const char *source, const char *name) {
  TSNode parent = ts_node_parent(node);
  while (!ts_node_is_null(parent)) {
    node = parent;
    parent = ts_node_parent(node);
  }
  return !shadows_global(node, source, name);
}

static int module_exports(TSNode node, const char *source) {
  return sl_node_is(node, "member_expression") &&
         sl_text_is(sl_field(node, "object"), source, "module") &&
         sl_text_is(sl_field(node, "property"), source, "exports");
}

static int export_target(TSNode left, const char *source) {
  if (module_exports(left, source) || module_exports(sl_field(left, "object"), source)) return 1;
  return sl_node_is(left, "member_expression") &&
         sl_text_is(sl_field(left, "object"), source, "exports");
}

static int conflicting_export(TSNode left, TSNode other, const char *source) {
  if (!export_target(other, source)) return 0;
  if (module_exports(left, source) || module_exports(other, source)) return 1;
  const TSNode key = sl_field(left, "property"), other_key = sl_field(other, "property");
  if (ts_node_is_null(key) || ts_node_is_null(other_key)) return 1;
  const size_t size = ts_node_end_byte(key) - ts_node_start_byte(key);
  if (size != ts_node_end_byte(other_key) - ts_node_start_byte(other_key)) return 0;
  return memcmp(source + ts_node_start_byte(key), source + ts_node_start_byte(other_key), size) ==
         0;
}

static int stable_export(TSNode node, const char *source) {
  const TSNode root = ts_node_parent(ts_node_parent(node));
  TSTreeCursor cursor = ts_tree_cursor_new(root);
  int started = 0, stable = 1;
  TSNode statement;
  while (stable && !ts_node_is_null(statement = sl_next_named_child(&cursor, &started))) {
    if (!sl_node_is(statement, "expression_statement")) continue;
    const TSNode other = ts_node_named_child(statement, 0);
    if (ts_node_eq(node, other) || !sl_node_is(other, "assignment_expression")) continue;
    stable = !conflicting_export(sl_field(node, "left"), sl_field(other, "left"), source);
  }
  ts_tree_cursor_delete(&cursor);
  return stable;
}

static TSNode commonjs_assignment(TSNode node, const char *source) {
  if (sl_node_is(node, "expression_statement")) node = ts_node_named_child(node, 0);
  if (!sl_node_is(node, "assignment_expression")) return (TSNode){0};
  const TSNode statement = ts_node_parent(node);
  if (!sl_node_is(ts_node_parent(statement), "program")) return (TSNode){0};
  const TSNode left = sl_field(node, "left");
  const TSNode object = sl_field(left, "object");
  const int module = module_exports(left, source) || module_exports(object, source);
  const int exports =
      sl_node_is(left, "member_expression") && sl_text_is(object, source, "exports");
  if (!module && !exports) return (TSNode){0};
  const char *name = module ? "module" : "exports";
  return global_available(node, source, name) && stable_export(node, source) ? node : (TSNode){0};
}

static int require_pattern(TSNode node) {
  if (sl_node_is(node, "identifier")) return 1;
  if (!sl_node_is(node, "object_pattern")) return 0;
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (sl_node_is(child, "shorthand_property_identifier_pattern")) continue;
    if (!sl_node_is(child, "pair_pattern")) return 0;
    if (!sl_node_is(sl_field(child, "key"), "property_identifier")) return 0;
    if (!sl_node_is(sl_field(child, "value"), "identifier")) return 0;
  }
  return 1;
}

static TSNode require_source(TSNode node, const char *source) {
  if (!sl_node_is(node, "variable_declarator")) return (TSNode){0};
  if (!require_pattern(sl_name_node(node))) return (TSNode){0};
  const TSNode value = sl_field(node, "value");
  if (!sl_node_is(value, "call_expression")) return (TSNode){0};
  if (!sl_text_is(sl_field(value, "function"), source, "require")) return (TSNode){0};
  const TSNode arguments = sl_field(value, "arguments");
  if (ts_node_named_child_count(arguments) != 1) return (TSNode){0};
  const TSNode path = ts_node_named_child(arguments, 0);
  if (!sl_node_is(path, "string")) return (TSNode){0};
  const uint32_t start = ts_node_start_byte(path), end = ts_node_end_byte(path);
  if (memchr(source + start, '\\', end - start) != NULL) return (TSNode){0};
  return global_available(node, source, "require") ? path : (TSNode){0};
}

static TSNode declaration_node(TSNode node) {
  if (sl_node_is(node, "expression_statement")) node = ts_node_named_child(node, 0);
  if (!sl_node_is(node, "export_statement")) return node;
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (!sl_node_is(child, "string")) return child;
  }
  return node;
}

static TSNode variable_declarator(TSNode node) {
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (sl_node_is(child, "variable_declarator")) return child;
  }
  return (TSNode){0};
}

static int is_function_value(TSNode node) {
  return sl_node_is(node, "arrow_function") || sl_node_is(node, "function_expression") ||
         sl_node_is(node, "generator_function");
}

static int variable_is_function(TSNode node) {
  const TSNode declarator = variable_declarator(node);
  const TSNode value = sl_field(declarator, "value");
  return is_function_value(value);
}

static SlDeclarationKind declaration_kind(TSNode input, const char *source) {
  const TSNode node = declaration_node(input);
  if (sl_node_is(node, "assignment_expression") && is_function_value(sl_field(node, "right")) &&
      !ts_node_is_null(commonjs_assignment(input, source)))
    return SL_DECLARATION_FUNCTION;
  if (sl_node_is(node, "import_statement")) return SL_DECLARATION_IMPORT;
  if (sl_node_is(node, "type_alias_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "interface_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "enum_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "class_declaration")) return SL_DECLARATION_TYPE;
  const int variable =
      sl_node_is(node, "lexical_declaration") || sl_node_is(node, "variable_declaration");
  if (variable && !ts_node_is_null(require_source(variable_declarator(node), source)))
    return SL_DECLARATION_IMPORT;
  if (variable && variable_is_function(node)) return SL_DECLARATION_FUNCTION;
  if (variable) return SL_DECLARATION_CONSTANT;
  if (sl_node_is(input, "export_statement") && is_function_value(node))
    return SL_DECLARATION_FUNCTION;
  if (sl_node_is(node, "function_declaration")) return SL_DECLARATION_FUNCTION;
  if (sl_node_is(node, "generator_function_declaration")) return SL_DECLARATION_FUNCTION;
  return SL_DECLARATION_NONE;
}

static TSNode lexical_name_node(TSNode node) {
  const TSNode declarator = variable_declarator(node);
  return sl_name_node(declarator);
}

static TSNode name_node(TSNode node) {
  if (sl_node_is(node, "assignment_expression")) return sl_field(node, "left");
  const int variable =
      sl_node_is(node, "lexical_declaration") || sl_node_is(node, "variable_declaration");
  if (variable) return lexical_name_node(node);
  return sl_name_node(node);
}

static TSNode function_node(TSNode node) {
  if (sl_node_is(node, "assignment_expression")) return sl_field(node, "right");
  const int variable =
      sl_node_is(node, "lexical_declaration") || sl_node_is(node, "variable_declaration");
  if (!variable) return node;
  const TSNode declarator = variable_declarator(node);
  return sl_field(declarator, "value");
}

static int is_function_node(TSNode node) {
  return sl_node_is(node, "arrow_function") || sl_node_is(node, "function_expression") ||
         sl_node_is(node, "function_declaration") || sl_node_is(node, "generator_function") ||
         sl_node_is(node, "generator_function_declaration");
}

static int binding_owner(TSNode parent, TSNode child) {
  if (sl_node_is(parent, "formal_parameters")) return 1;
  if (sl_node_is(parent, "arrow_function")) return sl_field_is(parent, "parameter", child);
  if (sl_node_is(parent, "variable_declarator")) return sl_field_is(parent, "name", child);
  if (sl_node_is(parent, "catch_clause")) return sl_field_is(parent, "parameter", child);
  const int parameter =
      sl_node_is(parent, "required_parameter") || sl_node_is(parent, "optional_parameter");
  if (!parameter) return 0;
  return sl_field_is(parent, "name", child) || sl_field_is(parent, "pattern", child);
}

static int binding_pattern_parent(TSNode parent, TSNode child) {
  if (sl_node_is(parent, "array_pattern") || sl_node_is(parent, "object_pattern")) return 1;
  if (sl_node_is(parent, "rest_pattern")) return 1;
  if (sl_node_is(parent, "pair_pattern")) return sl_field_is(parent, "value", child);
  const int assignment =
      sl_node_is(parent, "assignment_pattern") || sl_node_is(parent, "object_assignment_pattern");
  return assignment && sl_field_is(parent, "left", child);
}

static int is_binding_pattern_leaf(TSNode node) {
  const int leaf =
      sl_node_is(node, "identifier") || sl_node_is(node, "shorthand_property_identifier_pattern");
  if (!leaf) return 0;
  TSNode child = node;
  for (TSNode parent = ts_node_parent(child); !ts_node_is_null(parent);
       child = parent, parent = ts_node_parent(parent)) {
    if (binding_owner(parent, child)) return 1;
    if (!binding_pattern_parent(parent, child)) return 0;
  }
  return 0;
}

static TSNode binding_name_node(TSNode node) {
  if (sl_node_is(node, "function_declaration") ||
      sl_node_is(node, "generator_function_declaration"))
    return sl_name_node(node);
  if (sl_node_is(node, "class_declaration")) return sl_name_node(node);
  return is_binding_pattern_leaf(node) ? node : (TSNode){0};
}

static TSNode called_name_node(TSNode node) {
  if (!sl_node_is(node, "call_expression")) return (TSNode){0};
  const TSNode callee = sl_field(node, "function");
  if (sl_node_is(callee, "identifier")) return callee;
  if (sl_node_is(callee, "member_expression") &&
      sl_node_is(sl_field(callee, "object"), "identifier") &&
      sl_node_is(sl_field(callee, "property"), "property_identifier"))
    return sl_field(callee, "object");
  return (TSNode){0};
}

static TSNode import_source_node(TSNode node, const char *source) {
  if (!sl_node_is(node, "import_statement")) return require_source(node, source);
  return sl_field(node, "source");
}

static void normalize_import_source(char *source) {
  const size_t length = strlen(source);
  if (length < 2) return;
  memmove(source, source + 1, length - 2);
  source[length - 2] = '\0';
}

static char *join_path(const char *directory, const char *name) {
  const size_t directory_length = strlen(directory);
  const size_t name_length = strlen(name);
  char *path = malloc(directory_length + name_length + 2);
  if (path == NULL) return NULL;
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1, name, name_length + 1);
  return path;
}

static char *append_text(const char *value, const char *suffix) {
  const size_t value_length = strlen(value);
  const size_t suffix_length = strlen(suffix);
  char *result = malloc(value_length + suffix_length + 1);
  if (result == NULL) return NULL;
  memcpy(result, value, value_length);
  memcpy(result + value_length, suffix, suffix_length + 1);
  return result;
}

static char *path_directory(const char *path) {
  const char *separator = strrchr(path, '/');
  if (separator == NULL) return strdup(".");
  const size_t length = (size_t)(separator - path);
  if (length == 0) return strdup("/");
  return strndup(path, length);
}

static char *regular_path(const char *path) {
  char *resolved = realpath(path, NULL);
  if (resolved == NULL) return NULL;
  struct stat metadata = {0};
  const int regular = stat(resolved, &metadata) == 0 && S_ISREG(metadata.st_mode);
  if (regular) return resolved;
  free(resolved);
  return NULL;
}

static const char *const typescript_extensions[] = {".ts", ".mts", ".cts"};
static const char *const tsx_extensions[] = {".tsx"};
static const char *const javascript_extensions[] = {".js", ".jsx", ".mjs", ".cjs"};
static const char *const extensions[] = {".ts", ".tsx", ".mts", ".cts",
                                         ".js", ".jsx", ".mjs", ".cjs"};
static const char *const require_extensions[] = {".js", ".json", ".node"};

static char *resolve_with_extensions(const char *base, int required) {
  const char *const *suffixes = required ? require_extensions : extensions;
  const size_t count = required ? sizeof(require_extensions) / sizeof(*require_extensions)
                                : sizeof(extensions) / sizeof(*extensions);
  for (size_t index = 0; index < count; index++) {
    char *candidate = append_text(base, suffixes[index]);
    if (candidate == NULL) return NULL;
    char *resolved = regular_path(candidate);
    free(candidate);
    if (resolved != NULL) return resolved;
  }
  return NULL;
}

static int has_package_manifest(const char *base) {
  char *manifest = join_path(base, "package.json");
  if (manifest == NULL) return 1;
  char *resolved = regular_path(manifest);
  free(manifest);
  const int found = resolved != NULL;
  free(resolved);
  return found;
}

static char *resolve_index_file(const char *base, int required) {
  if (required && has_package_manifest(base)) return NULL;
  char *index_base = join_path(base, "index");
  if (index_base == NULL) return NULL;
  char *resolved = resolve_with_extensions(index_base, required);
  free(index_base);
  return resolved;
}

static char *resolve_import(const char *path, const char *source, TSNode declaration) {
  const int local_source = source[0] == '.' || source[0] == '/';
  if (!local_source) return NULL;
  char *directory = path_directory(path);
  if (directory == NULL) return NULL;
  char *base = source[0] == '/' ? strdup(source) : join_path(directory, source);
  free(directory);
  if (base == NULL) return NULL;
  const int required = sl_node_is(declaration, "variable_declarator");
  char *resolved = regular_path(base);
  if (resolved == NULL) resolved = resolve_with_extensions(base, required);
  if (resolved == NULL) resolved = resolve_index_file(base, required);
  free(base);
  return resolved;
}

static int is_default_import(TSNode node) {
  if (!sl_node_is(node, "identifier")) return 0;
  return sl_node_is(ts_node_parent(node), "import_clause");
}

static TSNode import_local_name_node(TSNode node) {
  if (is_default_import(node)) return node;
  if (sl_node_is(node, "variable_declarator") && sl_node_is(sl_name_node(node), "identifier"))
    return sl_name_node(node);
  if (sl_node_is(node, "namespace_import")) return ts_node_named_child(node, 0);
  if (sl_node_is(node, "shorthand_property_identifier_pattern")) return node;
  if (sl_node_is(node, "pair_pattern") && sl_node_is(sl_field(node, "value"), "identifier"))
    return sl_field(node, "value");
  if (!sl_node_is(node, "import_specifier")) return (TSNode){0};
  const TSNode alias = sl_field(node, "alias");
  if (!ts_node_is_null(alias)) return alias;
  return sl_name_node(node);
}

static TSNode imported_name_node(TSNode node) {
  if (sl_node_is(node, "shorthand_property_identifier_pattern")) return node;
  if (sl_node_is(node, "pair_pattern") && sl_node_is(sl_field(node, "key"), "property_identifier"))
    return sl_field(node, "key");
  if (!sl_node_is(node, "import_specifier")) return (TSNode){0};
  return sl_name_node(node);
}

static const char *implicit_imported_name(TSNode node) {
  if (sl_node_is(node, "variable_declarator")) return "module.exports";
  if (sl_node_is(node, "namespace_import")) return "*";
  return is_default_import(node) ? "default" : NULL;
}

static TSNode commonjs_reference(TSNode node, const char *source) {
  const TSNode assignment = commonjs_assignment(node, source);
  if (!ts_node_is_null(assignment)) {
    const TSNode right = sl_field(assignment, "right");
    return sl_node_is(right, "identifier") ? right : (TSNode){0};
  }
  const TSNode object = ts_node_parent(node);
  const TSNode owner = commonjs_assignment(ts_node_parent(object), source);
  if (!sl_node_is(object, "object") || ts_node_is_null(owner)) return (TSNode){0};
  if (!module_exports(sl_field(owner, "left"), source)) return (TSNode){0};
  if (sl_node_is(node, "shorthand_property_identifier")) return node;
  const TSNode value = sl_field(node, "value");
  return sl_node_is(value, "identifier") ? value : (TSNode){0};
}

static TSNode exported_reference_name_node(TSNode node, const char *source) {
  if (!sl_node_is(node, "export_specifier")) return commonjs_reference(node, source);
  const TSNode clause = ts_node_parent(node);
  const TSNode statement = ts_node_parent(clause);
  const TSNode module_source = sl_field(statement, "source");
  if (!ts_node_is_null(module_source)) return (TSNode){0};
  return sl_name_node(node);
}

static TSNode exported_name_node(TSNode node, const char *source) {
  const TSNode assignment = commonjs_assignment(node, source);
  if (!ts_node_is_null(assignment)) {
    const TSNode left = sl_field(assignment, "left");
    return module_exports(left, source) ? (TSNode){0} : sl_field(left, "property");
  }
  if (sl_node_is(node, "shorthand_property_identifier")) return node;
  if (sl_node_is(node, "pair") && sl_node_is(sl_field(node, "key"), "property_identifier"))
    return sl_field(node, "key");
  if (!sl_node_is(node, "export_specifier")) return (TSNode){0};
  const TSNode alias = sl_field(node, "alias");
  if (!ts_node_is_null(alias)) return alias;
  return sl_name_node(node);
}

static int has_child_type(TSNode node, const char *type) {
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    if (sl_node_is(ts_node_child(node, index), type)) return 1;
  }
  return 0;
}

static const char *implicit_export_name(TSNode input, const char *source) {
  const TSNode assignment = commonjs_assignment(input, source);
  if (!ts_node_is_null(assignment) && module_exports(sl_field(assignment, "left"), source))
    return "default";
  if (!sl_node_is(input, "export_statement")) return NULL;
  return has_child_type(input, "default") ? "default" : NULL;
}

static int is_exported(TSNode input, const char *source) {
  return sl_node_is(input, "export_statement") ||
         !ts_node_is_null(commonjs_assignment(input, source));
}

static const SlImportFact *find_import(const SlFileFact *file, const char *local_name,
                                       size_t length) {
  const SlImportFact *match = NULL;
  for (size_t index = 0; index < file->import_count; index++) {
    const SlImportFact *candidate = &file->imports[index];
    if (strlen(candidate->local_name) != length ||
        strncmp(candidate->local_name, local_name, length) != 0)
      continue;
    if (match != NULL) return NULL;
    match = candidate;
  }
  return match;
}

int sl_script_resolve_call(const SlCallResolutionRequest *request, SlResolvedFunction *result) {
  const SlFileFact *file = request->caller_file;
  const char *name = request->called_name;
  if (sl_resolve_local_call(request, result)) return 1;
  const char *member = strchr(name, '.');
  const size_t length = member == NULL ? strlen(name) : (size_t)(member - name);
  const SlImportFact *import = find_import(file, name, length);
  if (import == NULL || import->target_path == NULL) return 0;
  const int required = strcmp(import->imported_name, "module.exports") == 0;
  const int namespace = required || strcmp(import->imported_name, "*") == 0;
  if (member != NULL && !namespace) return 0;
  const char *exported = required ? "default" : import->imported_name;
  if (member != NULL) exported = member + 1;
  return request->lookup(request->lookup_context, import->target_path, exported, 1, result);
}

static char *call_name(TSNode node, const char *source) {
  const TSNode name = called_name_node(node);
  const TSNode callee = sl_field(node, "function");
  if (sl_node_is(callee, "identifier"))
    return strndup(source + ts_node_start_byte(name),
                   ts_node_end_byte(name) - ts_node_start_byte(name));
  const TSNode property = sl_field(callee, "property");
  const size_t left = ts_node_end_byte(name) - ts_node_start_byte(name);
  const size_t right = ts_node_end_byte(property) - ts_node_start_byte(property);
  char *text = malloc(left + right + 2);
  if (text == NULL) return NULL;
  memcpy(text, source + ts_node_start_byte(name), left);
  text[left] = '.';
  memcpy(text + left + 1, source + ts_node_start_byte(property), right);
  text[left + right + 1] = '\0';
  return text;
}

#define SCRIPT_PACK(grammar, label)                                                                \
  {                                                                                                \
      .id = #grammar,                                                                              \
      .parse_error_message = "could not parse " label " source",                                   \
      .line_comment_prefix = "//",                                                                 \
      .extensions = grammar##_extensions,                                                          \
      .extension_count = sizeof(grammar##_extensions) / sizeof(*grammar##_extensions),             \
      .tree_sitter_language = tree_sitter_##grammar,                                               \
      .declaration_node = declaration_node,                                                        \
      .declaration_kind = declaration_kind,                                                        \
      .name_node = name_node,                                                                      \
      .function_node = function_node,                                                              \
      .is_function_node = is_function_node,                                                        \
      .binding_name_node = binding_name_node,                                                      \
      .called_name_node = called_name_node,                                                        \
      .call_name = call_name,                                                                      \
      .import_source_node = import_source_node,                                                    \
      .normalize_import_source = normalize_import_source,                                          \
      .resolve_import = resolve_import,                                                            \
      .import_local_name_node = import_local_name_node,                                            \
      .imported_name_node = imported_name_node,                                                    \
      .implicit_imported_name = implicit_imported_name,                                            \
      .exported_reference_name_node = exported_reference_name_node,                                \
      .exported_name_node = exported_name_node,                                                    \
      .implicit_export_name = implicit_export_name,                                                \
      .is_exported = is_exported,                                                                  \
      .is_entrypoint_name = sl_is_main,                                                            \
      .resolve_call = sl_script_resolve_call,                                                      \
  }

const SlLanguagePack sl_typescript_pack = SCRIPT_PACK(typescript, "TypeScript");
const SlLanguagePack sl_tsx_pack = SCRIPT_PACK(tsx, "TSX");
const SlLanguagePack sl_javascript_pack = SCRIPT_PACK(javascript, "JavaScript");
