#ifndef STRUCT_LINT_FILES_H
#define STRUCT_LINT_FILES_H

#include "struct_lint.h"

typedef struct {
  char *path;
  char *resolved_path;
} SlFile;

typedef struct {
  SlFile *items;
  size_t count;
} SlFileList;

SlStatus sl_discover_files(const char *const *inputs, size_t input_count, int use_gitignore,
                           SlFileList *files);
void sl_file_list_free(SlFileList *files);

#endif
