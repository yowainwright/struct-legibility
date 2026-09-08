#ifndef STRUCT_LINT_LANGUAGE_H
#define STRUCT_LINT_LANGUAGE_H

#include "struct_lint.h"

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
  const SlFileFact *file;
  const SlDeclarationFact *declaration;
} SlResolvedFunction;

typedef int (*SlSymbolLookup)(const void *context, const char *path, const char *name, int exported,
                              SlResolvedFunction *result);

typedef struct {
  const SlReport *project;
  const SlFileFact *caller_file;
  const char *called_name;
  const void *lookup_context;
  SlSymbolLookup lookup;
} SlCallResolutionRequest;

typedef struct {
  const char *id;
  const char *parse_error_message;
  const char *line_comment_prefix;
  const char *const *extensions;
  size_t extension_count;
  const TSLanguage *(*tree_sitter_language)(void);
  TSNode (*declaration_node)(TSNode input);
  SlDeclarationKind (*declaration_kind)(TSNode input, const char *source);
  TSNode (*name_node)(TSNode declaration);
  TSNode (*function_node)(TSNode declaration);
  int (*is_function_node)(TSNode node);
  TSNode (*binding_name_node)(TSNode node);
  TSNode (*called_name_node)(TSNode node);
  TSNode (*import_source_node)(TSNode declaration);
  void (*normalize_import_source)(char *source);
  char *(*resolve_import)(const char *path, const char *source);
  TSNode (*import_local_name_node)(TSNode node);
  TSNode (*imported_name_node)(TSNode node);
  const char *(*implicit_imported_name)(TSNode node);
  TSNode (*exported_reference_name_node)(TSNode node);
  TSNode (*exported_name_node)(TSNode node);
  const char *(*implicit_export_name)(TSNode input);
  int (*is_exported)(TSNode input, const char *source);
  int (*is_entrypoint_name)(const char *name);
  int (*resolve_call)(const SlCallResolutionRequest *request, SlResolvedFunction *result);
} SlLanguagePack;

extern const SlLanguagePack sl_typescript_pack;
extern const SlLanguagePack sl_tsx_pack;
extern const SlLanguagePack sl_javascript_pack;
extern const SlLanguagePack sl_go_pack;
extern const SlLanguagePack sl_python_pack;
extern const SlLanguagePack sl_bash_pack;

const SlLanguagePack *sl_language_for_path(const char *path);
int sl_language_resolve_call(const SlLanguagePack *pack, const SlCallResolutionRequest *request,
                             SlResolvedFunction *result);

#endif
