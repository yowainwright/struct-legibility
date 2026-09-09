#ifndef STRUCT_LINT_NODES_H
#define STRUCT_LINT_NODES_H

#include <tree_sitter/api.h>

/* A cursor visits siblings once; indexed child access rescans preceding siblings. */
static inline TSNode sl_next_named_child(TSTreeCursor *cursor, int *started) {
  int found =
      *started ? ts_tree_cursor_goto_next_sibling(cursor) : ts_tree_cursor_goto_first_child(cursor);
  *started = 1;
  while (found) {
    const TSNode node = ts_tree_cursor_current_node(cursor);
    if (ts_node_is_named(node)) return node;
    found = ts_tree_cursor_goto_next_sibling(cursor);
  }
  return (TSNode){0};
}

#endif
