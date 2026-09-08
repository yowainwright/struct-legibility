#include "common.h"

#include <locale.h>
#include <stdint.h>
#include <wctype.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

const TSLanguage *tree_sitter_go(void);

static SlDeclarationKind declaration_kind(TSNode node, const char *source) {
  (void)source;
  if (sl_node_is(node, "import_declaration")) return SL_DECLARATION_IMPORT;
  if (sl_node_is(node, "type_declaration")) return SL_DECLARATION_TYPE;
  if (sl_node_is(node, "const_declaration")) return SL_DECLARATION_CONSTANT;
  if (sl_node_is(node, "var_declaration")) return SL_DECLARATION_CONSTANT;
  if (sl_node_is(node, "function_declaration")) return SL_DECLARATION_FUNCTION;
  return SL_DECLARATION_NONE;
}

static int is_function_node(TSNode node) {
  return sl_node_is(node, "function_declaration") || sl_node_is(node, "method_declaration") ||
         sl_node_is(node, "func_literal");
}

static int binding_owner(TSNode parent, TSNode child) {
  if (sl_node_is(parent, "short_var_declaration") || sl_node_is(parent, "range_clause"))
    return sl_field_is(parent, "left", child);
  const int named_binding = sl_node_is(parent, "parameter_declaration") ||
                            sl_node_is(parent, "variadic_parameter_declaration") ||
                            sl_node_is(parent, "var_spec") || sl_node_is(parent, "const_spec") ||
                            sl_node_is(parent, "type_spec");
  return named_binding && sl_field_is(parent, "name", child);
}

static TSNode binding_name_node(TSNode node) {
  if (!sl_node_is(node, "identifier") && !sl_node_is(node, "type_identifier")) return (TSNode){0};
  TSNode child = node;
  TSNode parent = ts_node_parent(child);
  if (sl_node_is(parent, "expression_list")) {
    child = parent;
    parent = ts_node_parent(child);
  }
  return binding_owner(parent, child) ? node : (TSNode){0};
}

static TSNode called_name_node(TSNode node) {
  if (!sl_node_is(node, "call_expression")) return (TSNode){0};
  const TSNode function = sl_field(node, "function");
  return sl_node_is(function, "identifier") ? function : (TSNode){0};
}

static size_t codepoint_width(unsigned char lead) {
  if (lead < 0xe0) return 2;
  if (lead < 0xf0) return 3;
  return 4;
}

static uint32_t initial_codepoint(const unsigned char *text) {
  if (text[0] < 0x80) return text[0];
  const size_t count = codepoint_width(text[0]);
  uint32_t point = text[0] & (0x7f >> count);
  for (size_t index = 1; index < count; index++)
    point = (point << 6) | (text[index] & 0x3f);
  return point;
}

static int unicode_uppercase(uint32_t point) {
  const char *const names[] = {"C.UTF-8", "en_US.UTF-8", "UTF-8"};
  for (size_t index = 0; index < sizeof(names) / sizeof(*names); index++) {
    locale_t locale = newlocale(LC_CTYPE_MASK, names[index], (locale_t)0);
    if (locale == (locale_t)0) continue;
    const int uppercase = iswupper_l((wint_t)point, locale);
    freelocale(locale);
    return uppercase != 0;
  }
  return 0;
}

static int is_exported(TSNode node, const char *source) {
  if (!sl_node_is(node, "function_declaration")) return 0;
  const TSNode name = sl_name_node(node);
  const unsigned char *text = (const unsigned char *)source + ts_node_start_byte(name);
  if (text[0] < 0x80) return text[0] >= 'A' && text[0] <= 'Z';
  return unicode_uppercase(initial_codepoint(text));
}

static int is_entrypoint_name(const char *name) {
  return sl_is_main(name) || strcmp(name, "init") == 0;
}

static const char *const extensions[] = {".go"};

const SlLanguagePack sl_go_pack = {
    .id = "go",
    .parse_error_message = "could not parse Go source",
    .line_comment_prefix = "//",
    .extensions = extensions,
    .extension_count = sizeof(extensions) / sizeof(*extensions),
    .tree_sitter_language = tree_sitter_go,
    .declaration_node = sl_identity_node,
    .declaration_kind = declaration_kind,
    .name_node = sl_name_node,
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
    .is_entrypoint_name = is_entrypoint_name,
    .resolve_call = sl_resolve_local_call,
};
