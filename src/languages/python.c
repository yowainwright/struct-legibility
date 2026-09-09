#include "common.h"

const TSLanguage *tree_sitter_python(void);

static TSNode declaration_node(TSNode node) {
  if (sl_node_is(node, "decorated_definition")) return sl_field(node, "definition");
  if (sl_node_is(node, "expression_statement") && ts_node_named_child_count(node) == 1)
    return ts_node_named_child(node, 0);
  return node;
}

static SlDeclarationKind declaration_kind(TSNode input, const char *source) {
  (void)source;
  const TSNode node = declaration_node(input);
  if (sl_node_is(node, "import_statement") || sl_node_is(node, "import_from_statement"))
    return SL_DECLARATION_IMPORT;
  if (sl_node_is(node, "class_definition") || sl_node_is(node, "type_alias_statement"))
    return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "assignment")) return SL_DECLARATION_CONSTANT;
  if (sl_node_is(node, "function_definition")) return SL_DECLARATION_FUNCTION;
  return SL_DECLARATION_NONE;
}

static TSNode name_node(TSNode node) {
  if (sl_node_is(node, "assignment")) return sl_field(node, "left");
  return sl_name_node(node);
}

static int is_function_node(TSNode node) {
  return sl_node_is(node, "function_definition") || sl_node_is(node, "lambda") ||
         sl_node_is(node, "class_definition");
}

static int binding_owner(TSNode parent, TSNode child) {
  if (sl_node_is(parent, "parameters")) return 1;
  if (sl_node_is(parent, "typed_parameter")) return !sl_field_is(parent, "type", child);
  if (sl_node_is(parent, "default_parameter") || sl_node_is(parent, "typed_default_parameter"))
    return sl_field_is(parent, "name", child);
  if (sl_node_is(parent, "named_expression")) return sl_field_is(parent, "name", child);
  if (sl_node_is(parent, "as_pattern") || sl_node_is(parent, "except_clause"))
    return sl_field_is(parent, "alias", child);
  const int assignment = sl_node_is(parent, "assignment") ||
                         sl_node_is(parent, "augmented_assignment") ||
                         sl_node_is(parent, "for_statement") || sl_node_is(parent, "for_in_clause");
  return assignment && sl_field_is(parent, "left", child);
}

static int is_pattern(TSNode node) {
  return sl_node_is(node, "pattern_list") || sl_node_is(node, "tuple_pattern") ||
         sl_node_is(node, "list_pattern") || sl_node_is(node, "list_splat_pattern") ||
         sl_node_is(node, "dictionary_splat_pattern") || sl_node_is(node, "as_pattern_target");
}

static TSNode import_binding(TSNode node) {
  if (sl_node_is(node, "aliased_import")) return sl_field(node, "alias");
  if (!sl_node_is(node, "dotted_name")) return (TSNode){0};
  const TSNode parent = ts_node_parent(node);
  if (sl_node_is(parent, "import_statement")) return ts_node_named_child(node, 0);
  if (!sl_node_is(parent, "import_from_statement")) return (TSNode){0};
  if (sl_field_is(parent, "module_name", node)) return (TSNode){0};
  return ts_node_named_child(node, 0);
}

static TSNode binding_name_node(TSNode node) {
  if (is_function_node(node)) return sl_name_node(node);
  const TSNode imported = import_binding(node);
  if (!ts_node_is_null(imported)) return imported;
  if (!sl_node_is(node, "identifier")) return (TSNode){0};
  TSNode child = node;
  for (TSNode parent = ts_node_parent(child); !ts_node_is_null(parent);
       child = parent, parent = ts_node_parent(parent)) {
    if (binding_owner(parent, child)) return node;
    if (!is_pattern(parent)) break;
  }
  return (TSNode){0};
}

static int is_body_call(TSNode node) {
  for (TSNode parent = ts_node_parent(node); !ts_node_is_null(parent);
       parent = ts_node_parent(parent)) {
    if (!sl_node_is(parent, "function_definition")) continue;
    const TSNode body = sl_field(parent, "body");
    return ts_node_start_byte(node) >= ts_node_start_byte(body);
  }
  return 0;
}

static TSNode called_name_node(TSNode node) {
  if (!sl_node_is(node, "call") || !is_body_call(node)) return (TSNode){0};
  const TSNode function = sl_field(node, "function");
  return sl_node_is(function, "identifier") ? function : (TSNode){0};
}

static int is_exported(TSNode input, const char *source) {
  const TSNode node = declaration_node(input);
  if (!sl_node_is(node, "function_definition")) return 0;
  const TSNode name = sl_name_node(node);
  return source[ts_node_start_byte(name)] != '_';
}

static const char *const extensions[] = {".py", ".pyi"};

const SlLanguagePack sl_python_pack = {
    .id = "python",
    .parse_error_message = "could not parse Python source",
    .line_comment_prefix = "#",
    .extensions = extensions,
    .extension_count = sizeof(extensions) / sizeof(*extensions),
    .tree_sitter_language = tree_sitter_python,
    .declaration_node = declaration_node,
    .declaration_kind = declaration_kind,
    .name_node = name_node,
    .function_node = sl_identity_node,
    .is_function_node = is_function_node,
    .binding_name_node = binding_name_node,
    .called_name_node = called_name_node,
    .import_source_node = sl_no_source_node,
    .normalize_import_source = sl_keep_source,
    .resolve_import = sl_no_import,
    .import_local_name_node = sl_no_node,
    .imported_name_node = sl_no_node,
    .implicit_imported_name = sl_no_text,
    .exported_reference_name_node = sl_no_source_node,
    .exported_name_node = sl_no_source_node,
    .implicit_export_name = sl_no_source_text,
    .is_exported = is_exported,
    .is_entrypoint_name = sl_is_main,
    .resolve_call = sl_resolve_local_call,
};
