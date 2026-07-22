#ifndef STRUCT_LEGIBILITY_H
#define STRUCT_LEGIBILITY_H

#include <stddef.h>

typedef struct {
  char *path;
  size_t line;
  size_t column;
  const char *rule_id;
  char *message;
  int fixed_error;
} SlDiagnostic;

typedef struct {
  const char *kind;
  char *name;
  int exported;
  size_t line;
  size_t column;
  char **calls;
  size_t call_count;
} SlDeclarationFact;

typedef struct {
  char *path;
  SlDeclarationFact *declarations;
  size_t declaration_count;
} SlFileFact;

typedef struct {
  SlDiagnostic *diagnostics;
  size_t count;
  SlFileFact *files;
  size_t file_count;
} SlReport;

typedef struct {
  const char *const *paths;
  size_t path_count;
  int use_gitignore;
  int collect_facts;
} SlRequest;

typedef enum { SL_OK = 0, SL_INVALID_ARGUMENT = 1, SL_IO_ERROR = 2, SL_OUT_OF_MEMORY = 3 } SlStatus;

SlStatus sl_analyze(const SlRequest *request, SlReport *report);
void sl_report_free(SlReport *report);

#endif
