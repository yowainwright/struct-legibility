#ifndef STRUCT_LEGIBILITY_FILES_H
#define STRUCT_LEGIBILITY_FILES_H

#include "struct_legibility.h"

typedef struct {
  char **paths;
  size_t count;
} SlFileList;

SlStatus sl_discover_files(const char *const *inputs, size_t input_count, int use_gitignore,
                           SlFileList *files);
void sl_file_list_free(SlFileList *files);

#endif
