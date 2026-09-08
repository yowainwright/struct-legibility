#include "../embedded.h"
#include "../nodes.h"
#include "common.h"

#include <stdlib.h>

const TSLanguage *tree_sitter_vue(void);
const TSLanguage *tree_sitter_svelte(void);
const TSLanguage *tree_sitter_astro(void);
const TSLanguage *tree_sitter_mdx(void);

static TSNode child_of_type(TSNode node, const char *type) {
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode child = ts_node_named_child(node, index);
    if (sl_node_is(child, type)) return child;
  }
  return (TSNode){0};
}

static TSNode script_attribute(TSNode node, const char *source, const char *name) {
  const TSNode tag = ts_node_named_child(node, 0);
  const uint32_t count = ts_node_named_child_count(tag);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode attribute = ts_node_named_child(tag, index);
    if (!sl_node_is(attribute, "attribute")) continue;
    if (sl_text_is(ts_node_named_child(attribute, 0), source, name)) return attribute;
  }
  return (TSNode){0};
}

static int attribute_is(TSNode attribute, const char *source, const char *value) {
  if (ts_node_is_null(attribute)) return 0;
  TSNode text = ts_node_named_child(attribute, 1);
  if (sl_node_is(text, "quoted_attribute_value")) text = ts_node_named_child(text, 0);
  return sl_text_is(text, source, value);
}

static const SlLanguagePack *script_language(TSNode node, const char *source,
                                             const SlLanguagePack *host) {
  if (!ts_node_is_null(script_attribute(node, source, "src"))) return NULL;
  const TSNode type = script_attribute(node, source, "type");
  const int code = ts_node_is_null(type) || attribute_is(type, source, "module") ||
                   attribute_is(type, source, "text/javascript") ||
                   attribute_is(type, source, "application/javascript");
  if (!code) return NULL;
  const TSNode lang = script_attribute(node, source, "lang");
  if (attribute_is(lang, source, "tsx")) return &sl_tsx_pack;
  if (attribute_is(lang, source, "ts") || attribute_is(lang, source, "typescript"))
    return &sl_typescript_pack;
  if (!ts_node_is_null(lang) && !attribute_is(lang, source, "js") &&
      !attribute_is(lang, source, "javascript") && !attribute_is(lang, source, "jsx"))
    return NULL;
  return host == &sl_astro_pack ? &sl_typescript_pack : &sl_javascript_pack;
}

static SlStatus append_region(TSNode node, const SlLanguagePack *pack, SlScriptRegions *regions) {
  if (pack == NULL || ts_node_is_null(node)) return SL_OK;
  if (ts_node_start_byte(node) == ts_node_end_byte(node)) return SL_OK;
  SlScriptRegion *items = realloc(regions->items, (regions->count + 1) * sizeof(*items));
  if (items == NULL) return SL_OUT_OF_MEMORY;
  regions->items = items;
  const TSRange range = {.start_point = ts_node_start_point(node),
                         .end_point = ts_node_end_point(node),
                         .start_byte = ts_node_start_byte(node),
                         .end_byte = ts_node_end_byte(node)};
  regions->items[regions->count++] = (SlScriptRegion){range, pack};
  return SL_OK;
}

static int descend_into(TSNode node, const SlLanguagePack *host) {
  if (ts_node_is_null(ts_node_parent(node))) return 1;
  if (host == &sl_mdx_pack) return sl_node_is(node, "section");
  return host == &sl_astro_pack && sl_node_is(node, "element");
}

SlStatus sl_script_regions(TSNode node, const char *source, const SlLanguagePack *host,
                           SlScriptRegions *regions) {
  if (sl_node_is(node, "script_element"))
    return append_region(child_of_type(node, "raw_text"), script_language(node, source, host),
                         regions);
  if (host == &sl_astro_pack && sl_node_is(node, "frontmatter"))
    return append_region(child_of_type(node, "frontmatter_js_block"), &sl_typescript_pack, regions);
  if (host == &sl_mdx_pack &&
      (sl_node_is(node, "import_statement") || sl_node_is(node, "export_statement")))
    return append_region(node, &sl_javascript_pack, regions);
  if (!descend_into(node, host)) return SL_OK;
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  int started = 0;
  TSNode child;
  SlStatus status = SL_OK;
  while (status == SL_OK && !ts_node_is_null(child = sl_next_named_child(&cursor, &started)))
    status = sl_script_regions(child, source, host, regions);
  ts_tree_cursor_delete(&cursor);
  return status;
}

#define EMBEDDED_PACK(grammar, label)                                                              \
  {.id = #grammar,                                                                                 \
   .parse_error_message = "could not parse " label " source",                                      \
   .extensions = grammar##_extensions,                                                             \
   .extension_count = 1,                                                                           \
   .embedded = 1,                                                                                  \
   .tree_sitter_language = tree_sitter_##grammar,                                                  \
   .resolve_call = sl_script_resolve_call}

static const char *const vue_extensions[] = {".vue"};
static const char *const svelte_extensions[] = {".svelte"};
static const char *const astro_extensions[] = {".astro"};
static const char *const mdx_extensions[] = {".mdx"};

const SlLanguagePack sl_vue_pack = EMBEDDED_PACK(vue, "Vue");
const SlLanguagePack sl_svelte_pack = EMBEDDED_PACK(svelte, "Svelte");
const SlLanguagePack sl_astro_pack = EMBEDDED_PACK(astro, "Astro");
const SlLanguagePack sl_mdx_pack = EMBEDDED_PACK(mdx, "MDX");
