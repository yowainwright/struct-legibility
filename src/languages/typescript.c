#include "language.h"

#include <string.h>

const TSLanguage *tree_sitter_typescript(void);

static int node_is(TSNode node, const char *type) { return strcmp(ts_node_type(node), type) == 0; }

static TSNode declaration_node(TSNode node) {
  if (!node_is(node, "export_statement")) return node;
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    TSNode child = ts_node_named_child(node, index);
    if (!node_is(child, "string")) return child;
  }
  return node;
}

static SlDeclarationKind declaration_kind(TSNode input) {
  const TSNode node = declaration_node(input);
  if (node_is(node, "import_statement")) return SL_DECLARATION_IMPORT;
  if (node_is(node, "type_alias_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "interface_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "enum_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "class_declaration")) return SL_DECLARATION_TYPE;
  if (node_is(node, "lexical_declaration")) return SL_DECLARATION_CONSTANT;
  if (node_is(node, "function_declaration")) return SL_DECLARATION_FUNCTION;
  return SL_DECLARATION_NONE;
}

static TSNode lexical_name_node(TSNode node) {
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (node_is(child, "variable_declarator")) {
      return ts_node_child_by_field_name(child, "name", 4);
    }
  }
  return (TSNode){0};
}

static TSNode name_node(TSNode node) {
  if (node_is(node, "lexical_declaration")) return lexical_name_node(node);
  return ts_node_child_by_field_name(node, "name", 4);
}

static TSNode called_name_node(TSNode node) {
  if (!node_is(node, "call_expression")) return (TSNode){0};
  const TSNode callee = ts_node_child_by_field_name(node, "function", 8);
  if (node_is(callee, "identifier")) return callee;
  return (TSNode){0};
}

static int is_exported(TSNode input) { return node_is(input, "export_statement"); }

static const char *const extensions[] = {".ts"};

const SlLanguagePack sl_typescript_pack = {
    "typescript",
    "could not parse TypeScript source",
    extensions,
    sizeof(extensions) / sizeof(*extensions),
    tree_sitter_typescript,
    declaration_node,
    declaration_kind,
    name_node,
    called_name_node,
    is_exported,
};
