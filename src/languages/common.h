#ifndef STRUCT_LINT_LANGUAGE_COMMON_H
#define STRUCT_LINT_LANGUAGE_COMMON_H

#include "language.h"

#include <string.h>

static inline int sl_node_is(TSNode node, const char *type) {
  return !ts_node_is_null(node) && strcmp(ts_node_type(node), type) == 0;
}

static inline TSNode sl_field(TSNode node, const char *name) {
  return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}

static inline int sl_field_is(TSNode parent, const char *name, TSNode child) {
  const uint32_t count = ts_node_child_count(parent);
  for (uint32_t index = 0; index < count; index++) {
    if (!ts_node_eq(ts_node_child(parent, index), child)) continue;
    const char *field = ts_node_field_name_for_child(parent, index);
    return field != NULL && strcmp(field, name) == 0;
  }
  return 0;
}

static inline TSNode sl_identity_node(TSNode node) { return node; }

static inline TSNode sl_no_source_node(TSNode node, const char *source) {
  (void)node;
  (void)source;
  return (TSNode){0};
}

static inline const char *sl_no_source_text(TSNode node, const char *source) {
  (void)node;
  (void)source;
  return NULL;
}

static inline TSNode sl_name_node(TSNode node) { return sl_field(node, "name"); }

static inline TSNode sl_no_node(TSNode node) {
  (void)node;
  return (TSNode){0};
}

static inline const char *sl_no_text(TSNode node) {
  (void)node;
  return NULL;
}

static inline void sl_keep_source(char *source) { (void)source; }

static inline char *sl_no_import(const char *path, const char *source, TSNode declaration) {
  (void)path;
  (void)source;
  (void)declaration;
  return NULL;
}

static inline int sl_no_export(TSNode node, const char *source) {
  (void)node;
  (void)source;
  return 0;
}

static inline int sl_is_main(const char *name) { return strcmp(name, "main") == 0; }

static inline int sl_text_is(TSNode node, const char *source, const char *text) {
  if (ts_node_is_null(node)) return 0;
  const size_t length = ts_node_end_byte(node) - ts_node_start_byte(node);
  return length == strlen(text) && memcmp(source + ts_node_start_byte(node), text, length) == 0;
}

static inline int sl_resolve_local_call(const SlCallResolutionRequest *request,
                                        SlResolvedFunction *result) {
  return request->lookup(request->lookup_context, request->caller_file->resolved_path,
                         request->called_name, 0, result);
}

#endif
