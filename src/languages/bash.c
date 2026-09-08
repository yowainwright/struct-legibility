#include "common.h"

const TSLanguage *tree_sitter_bash(void);

static TSNode called_name_node(TSNode node) {
  if (!sl_node_is(node, "command")) return (TSNode){0};
  const TSNode name = sl_name_node(node);
  const TSNode word = ts_node_named_child(name, 0);
  return sl_node_is(word, "word") ? word : (TSNode){0};
}

static int declares_function_export(TSNode node, const char *source) {
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_child(node, index);
    const char *text = source + ts_node_start_byte(child);
    const size_t length = ts_node_end_byte(child) - ts_node_start_byte(child);
    if (length < 2 || text[0] != '-' || text[1] == '-') continue;
    if (memchr(text + 1, 'f', length - 1) != NULL) return 1;
  }
  return 0;
}

static SlDeclarationKind declaration_kind(TSNode node, const char *source) {
  if (sl_node_is(node, "function_definition")) return SL_DECLARATION_FUNCTION;
  if (sl_node_is(node, "variable_assignment")) return SL_DECLARATION_CONSTANT;
  if (sl_node_is(node, "declaration_command") && !declares_function_export(node, source))
    return SL_DECLARATION_CONSTANT;
  const TSNode name = called_name_node(node);
  if (sl_text_is(name, source, "source") || sl_text_is(name, source, "."))
    return SL_DECLARATION_IMPORT;
  return SL_DECLARATION_NONE;
}

static int is_function_node(TSNode node) { return sl_node_is(node, "function_definition"); }

static TSNode binding_name_node(TSNode node) {
  return is_function_node(node) ? sl_name_node(node) : (TSNode){0};
}

static const char *const extensions[] = {".sh", ".bash"};

const SlLanguagePack sl_bash_pack = {
    .id = "bash",
    .parse_error_message = "could not parse Bash source",
    .line_comment_prefix = "#",
    .extensions = extensions,
    .extension_count = sizeof(extensions) / sizeof(*extensions),
    .tree_sitter_language = tree_sitter_bash,
    .declaration_node = sl_identity_node,
    .declaration_kind = declaration_kind,
    .name_node = sl_name_node,
    .function_node = sl_identity_node,
    .is_function_node = is_function_node,
    .binding_name_node = binding_name_node,
    .called_name_node = called_name_node,
    .import_source_node = sl_no_node,
    .normalize_import_source = sl_keep_source,
    .resolve_import = sl_no_import,
    .import_local_name_node = sl_no_node,
    .imported_name_node = sl_no_node,
    .implicit_imported_name = sl_no_text,
    .exported_reference_name_node = sl_no_node,
    .exported_name_node = sl_no_node,
    .implicit_export_name = sl_no_text,
    .is_exported = sl_no_export,
    .is_entrypoint_name = sl_is_main,
    .resolve_call = sl_resolve_local_call,
};
