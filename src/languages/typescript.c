#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const TSLanguage *tree_sitter_typescript(void);
const TSLanguage *tree_sitter_tsx(void);
const TSLanguage *tree_sitter_javascript(void);

static TSNode declaration_node(TSNode node) {
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
  (void)source;
  const TSNode node = declaration_node(input);
  if (sl_node_is(node, "import_statement")) return SL_DECLARATION_IMPORT;
  if (sl_node_is(node, "type_alias_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "interface_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "enum_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "class_declaration")) return SL_DECLARATION_TYPE;
  const int variable =
      sl_node_is(node, "lexical_declaration") || sl_node_is(node, "variable_declaration");
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
  const int variable =
      sl_node_is(node, "lexical_declaration") || sl_node_is(node, "variable_declaration");
  if (variable) return lexical_name_node(node);
  return sl_name_node(node);
}

static TSNode function_node(TSNode node) {
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
  return (TSNode){0};
}

static TSNode import_source_node(TSNode node) {
  if (!sl_node_is(node, "import_statement")) return (TSNode){0};
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

static char *resolve_with_extensions(const char *base) {
  for (size_t index = 0; index < sizeof(extensions) / sizeof(*extensions); index++) {
    char *candidate = append_text(base, extensions[index]);
    if (candidate == NULL) return NULL;
    char *resolved = regular_path(candidate);
    free(candidate);
    if (resolved != NULL) return resolved;
  }
  return NULL;
}

static char *resolve_index_file(const char *base) {
  char *index_base = join_path(base, "index");
  if (index_base == NULL) return NULL;
  char *resolved = resolve_with_extensions(index_base);
  free(index_base);
  return resolved;
}

static char *resolve_import(const char *path, const char *source) {
  const int local_source = source[0] == '.' || source[0] == '/';
  if (!local_source) return NULL;
  char *directory = path_directory(path);
  if (directory == NULL) return NULL;
  char *base = source[0] == '/' ? strdup(source) : join_path(directory, source);
  free(directory);
  if (base == NULL) return NULL;
  char *resolved = regular_path(base);
  if (resolved == NULL) resolved = resolve_with_extensions(base);
  if (resolved == NULL) resolved = resolve_index_file(base);
  free(base);
  return resolved;
}

static int is_default_import(TSNode node) {
  if (!sl_node_is(node, "identifier")) return 0;
  return sl_node_is(ts_node_parent(node), "import_clause");
}

static TSNode import_local_name_node(TSNode node) {
  if (is_default_import(node)) return node;
  if (!sl_node_is(node, "import_specifier")) return (TSNode){0};
  const TSNode alias = sl_field(node, "alias");
  if (!ts_node_is_null(alias)) return alias;
  return sl_name_node(node);
}

static TSNode imported_name_node(TSNode node) {
  if (!sl_node_is(node, "import_specifier")) return (TSNode){0};
  return sl_name_node(node);
}

static const char *implicit_imported_name(TSNode node) {
  return is_default_import(node) ? "default" : NULL;
}

static TSNode exported_reference_name_node(TSNode node) {
  if (!sl_node_is(node, "export_specifier")) return (TSNode){0};
  const TSNode clause = ts_node_parent(node);
  const TSNode statement = ts_node_parent(clause);
  const TSNode source = sl_field(statement, "source");
  if (!ts_node_is_null(source)) return (TSNode){0};
  return sl_name_node(node);
}

static TSNode exported_name_node(TSNode node) {
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

static const char *implicit_export_name(TSNode input) {
  if (!sl_node_is(input, "export_statement")) return NULL;
  return has_child_type(input, "default") ? "default" : NULL;
}

static int is_exported(TSNode input, const char *source) {
  (void)source;
  return sl_node_is(input, "export_statement");
}

static const SlImportFact *find_import(const SlFileFact *file, const char *local_name) {
  const SlImportFact *match = NULL;
  for (size_t index = 0; index < file->import_count; index++) {
    const SlImportFact *candidate = &file->imports[index];
    if (strcmp(candidate->local_name, local_name) != 0) continue;
    if (match != NULL) return NULL;
    match = candidate;
  }
  return match;
}

static int resolve_call(const SlCallResolutionRequest *request, SlResolvedFunction *result) {
  const SlFileFact *file = request->caller_file;
  const char *name = request->called_name;
  if (sl_resolve_local_call(request, result)) return 1;
  const SlImportFact *import = find_import(file, name);
  if (import == NULL || import->target_path == NULL) return 0;
  return request->lookup(request->lookup_context, import->target_path, import->imported_name, 1,
                         result);
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
      .resolve_call = resolve_call,                                                                \
  }

const SlLanguagePack sl_typescript_pack = SCRIPT_PACK(typescript, "TypeScript");
const SlLanguagePack sl_tsx_pack = SCRIPT_PACK(tsx, "TSX");
const SlLanguagePack sl_javascript_pack = SCRIPT_PACK(javascript, "JavaScript");
