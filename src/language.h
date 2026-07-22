#ifndef STRUCT_LEGIBILITY_LANGUAGE_H
#define STRUCT_LEGIBILITY_LANGUAGE_H

#include <stddef.h>
#include <tree_sitter/api.h>

typedef enum {
  SL_DECLARATION_NONE = -1,
  SL_DECLARATION_IMPORT = 0,
  SL_DECLARATION_TYPE = 1,
  SL_DECLARATION_CONSTANT = 2,
  SL_DECLARATION_FUNCTION = 3
} SlDeclarationKind;

typedef struct {
  const char *id;
  const char *parse_error_message;
  const char *const *extensions;
  size_t extension_count;
  const TSLanguage *(*tree_sitter_language)(void);
  TSNode (*declaration_node)(TSNode input);
  SlDeclarationKind (*declaration_kind)(TSNode input);
  TSNode (*name_node)(TSNode declaration);
  TSNode (*called_name_node)(TSNode node);
  int (*is_exported)(TSNode input);
} SlLanguagePack;

extern const SlLanguagePack sl_typescript_pack;

const SlLanguagePack *sl_language_for_path(const char *path);

#endif
