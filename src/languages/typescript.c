#include "language.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const TSLanguage *tree_sitter_typescript(void);

static int node_is(TSNode node, const char *type) { return strcmp(ts_node_type(node), type) == 0; }

static int field_equals(TSNode parent, const char *field, uint32_t length, TSNode child) {
  const TSNode value = ts_node_child_by_field_name(parent, field, length);
  return !ts_node_is_null(value) && ts_node_eq(value, child);
}

static TSNode declaration_node(TSNode node) {
  if (!node_is(node, "export_statement")) return node;
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    TSNode child = ts_node_named_child(node, index);
    if (!node_is(child, "string")) return child;
  }
  return node;
}

static TSNode variable_declarator(TSNode node) {
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (node_is(child, "variable_declarator")) return child;
  }
  return (TSNode){0};
}

static int is_function_value(TSNode node) {
  return node_is(node, "arrow_function") || node_is(node, "function_expression") ||
         node_is(node, "generator_function");
}

static int variable_is_function(TSNode node) {
  const TSNode declarator = variable_declarator(node);
  const TSNode value = ts_node_child_by_field_name(declarator, "value", 5);
  return is_function_value(value);
}

static SlDeclarationKind declaration_kind(TSNode input) {
  const TSNode node = declaration_node(input);
  if (node_is(node, "import_statement")) return SL_DECLARATION_IMPORT;
  if (node_is(node, "type_alias_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "interface_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "enum_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "class_declaration")) return SL_DECLARATION_TYPE;
  const int variable =
      node_is(node, "lexical_declaration") || node_is(node, "variable_declaration");
  if (variable && variable_is_function(node)) return SL_DECLARATION_FUNCTION;
  if (variable) return SL_DECLARATION_CONSTANT;
  if (node_is(input, "export_statement") && is_function_value(node)) return SL_DECLARATION_FUNCTION;
  if (node_is(node, "function_declaration")) return SL_DECLARATION_FUNCTION;
  if (node_is(node, "generator_function_declaration")) return SL_DECLARATION_FUNCTION;
  return SL_DECLARATION_NONE;
}

static TSNode lexical_name_node(TSNode node) {
  const TSNode declarator = variable_declarator(node);
  return ts_node_child_by_field_name(declarator, "name", 4);
}

static TSNode name_node(TSNode node) {
  const int variable =
      node_is(node, "lexical_declaration") || node_is(node, "variable_declaration");
  if (variable) return lexical_name_node(node);
  return ts_node_child_by_field_name(node, "name", 4);
}

static TSNode function_node(TSNode node) {
  const int variable =
      node_is(node, "lexical_declaration") || node_is(node, "variable_declaration");
  if (!variable) return node;
  const TSNode declarator = variable_declarator(node);
  return ts_node_child_by_field_name(declarator, "value", 5);
}

static int is_function_node(TSNode node) {
  return node_is(node, "arrow_function") || node_is(node, "function_expression") ||
         node_is(node, "function_declaration") || node_is(node, "generator_function") ||
         node_is(node, "generator_function_declaration");
}

static int binding_owner(TSNode parent, TSNode child) {
  if (node_is(parent, "formal_parameters")) return 1;
  if (node_is(parent, "arrow_function")) return field_equals(parent, "parameter", 9, child);
  if (node_is(parent, "variable_declarator")) return field_equals(parent, "name", 4, child);
  if (node_is(parent, "catch_clause")) return field_equals(parent, "parameter", 9, child);
  const int parameter =
      node_is(parent, "required_parameter") || node_is(parent, "optional_parameter");
  if (!parameter) return 0;
  return field_equals(parent, "name", 4, child) || field_equals(parent, "pattern", 7, child);
}

static int binding_pattern_parent(TSNode parent, TSNode child) {
  if (node_is(parent, "array_pattern") || node_is(parent, "object_pattern")) return 1;
  if (node_is(parent, "rest_pattern")) return 1;
  if (node_is(parent, "pair_pattern")) return field_equals(parent, "value", 5, child);
  const int assignment =
      node_is(parent, "assignment_pattern") || node_is(parent, "object_assignment_pattern");
  return assignment && field_equals(parent, "left", 4, child);
}

static int is_binding_pattern_leaf(TSNode node) {
  const int leaf =
      node_is(node, "identifier") || node_is(node, "shorthand_property_identifier_pattern");
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
  if (node_is(node, "function_declaration") || node_is(node, "generator_function_declaration"))
    return ts_node_child_by_field_name(node, "name", 4);
  if (node_is(node, "class_declaration")) return ts_node_child_by_field_name(node, "name", 4);
  return is_binding_pattern_leaf(node) ? node : (TSNode){0};
}

static TSNode called_name_node(TSNode node) {
  if (!node_is(node, "call_expression")) return (TSNode){0};
  const TSNode callee = ts_node_child_by_field_name(node, "function", 8);
  if (node_is(callee, "identifier")) return callee;
  return (TSNode){0};
}

static TSNode import_source_node(TSNode node) {
  if (!node_is(node, "import_statement")) return (TSNode){0};
  return ts_node_child_by_field_name(node, "source", 6);
}

static void normalize_import_source(char *source) {
  const size_t length = strlen(source);
  if (length < 2) return;
  memmove(source, source + 1, length - 2);
  source[length - 2] = '\0';
}

static char *copy_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, value, length);
  return copy;
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
  if (separator == NULL) return copy_string(".");
  const size_t length = (size_t)(separator - path);
  if (length == 0) return copy_string("/");
  char *directory = malloc(length + 1);
  if (directory == NULL) return NULL;
  memcpy(directory, path, length);
  directory[length] = '\0';
  return directory;
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

static const char *const extensions[] = {".ts"};

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
  char *base = source[0] == '/' ? copy_string(source) : join_path(directory, source);
  free(directory);
  if (base == NULL) return NULL;
  char *resolved = regular_path(base);
  if (resolved == NULL) resolved = resolve_with_extensions(base);
  if (resolved == NULL) resolved = resolve_index_file(base);
  free(base);
  return resolved;
}

static int is_default_import(TSNode node) {
  if (!node_is(node, "identifier")) return 0;
  return node_is(ts_node_parent(node), "import_clause");
}

static TSNode import_local_name_node(TSNode node) {
  if (is_default_import(node)) return node;
  if (!node_is(node, "import_specifier")) return (TSNode){0};
  const TSNode alias = ts_node_child_by_field_name(node, "alias", 5);
  if (!ts_node_is_null(alias)) return alias;
  return ts_node_child_by_field_name(node, "name", 4);
}

static TSNode imported_name_node(TSNode node) {
  if (!node_is(node, "import_specifier")) return (TSNode){0};
  return ts_node_child_by_field_name(node, "name", 4);
}

static const char *implicit_imported_name(TSNode node) {
  return is_default_import(node) ? "default" : NULL;
}

static TSNode exported_reference_name_node(TSNode node) {
  if (!node_is(node, "export_specifier")) return (TSNode){0};
  const TSNode clause = ts_node_parent(node);
  const TSNode statement = ts_node_parent(clause);
  const TSNode source = ts_node_child_by_field_name(statement, "source", 6);
  if (!ts_node_is_null(source)) return (TSNode){0};
  return ts_node_child_by_field_name(node, "name", 4);
}

static TSNode exported_name_node(TSNode node) {
  if (!node_is(node, "export_specifier")) return (TSNode){0};
  const TSNode alias = ts_node_child_by_field_name(node, "alias", 5);
  if (!ts_node_is_null(alias)) return alias;
  return ts_node_child_by_field_name(node, "name", 4);
}

static int has_child_type(TSNode node, const char *type) {
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    if (node_is(ts_node_child(node, index), type)) return 1;
  }
  return 0;
}

static const char *implicit_export_name(TSNode input) {
  if (!node_is(input, "export_statement")) return NULL;
  return has_child_type(input, "default") ? "default" : NULL;
}

static int is_exported(TSNode input) { return node_is(input, "export_statement"); }

static int is_entrypoint_name(const char *name) { return strcmp(name, "main") == 0; }

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
  if (request->lookup(request->lookup_context, file->resolved_path, name, 0, result)) return 1;
  const SlImportFact *import = find_import(file, name);
  if (import == NULL || import->target_path == NULL) return 0;
  return request->lookup(request->lookup_context, import->target_path, import->imported_name, 1,
                         result);
}

const SlLanguagePack sl_typescript_pack = {
    "typescript",
    "could not parse TypeScript source",
    "//",
    extensions,
    sizeof(extensions) / sizeof(*extensions),
    tree_sitter_typescript,
    declaration_node,
    declaration_kind,
    name_node,
    function_node,
    is_function_node,
    binding_name_node,
    called_name_node,
    import_source_node,
    normalize_import_source,
    resolve_import,
    import_local_name_node,
    imported_name_node,
    implicit_imported_name,
    exported_reference_name_node,
    exported_name_node,
    implicit_export_name,
    is_exported,
    is_entrypoint_name,
    resolve_call,
};
