#include "language.h"

#include <string.h>

static int path_has_extension(const char *path, const char *extension) {
  const size_t path_length = strlen(path);
  const size_t extension_length = strlen(extension);
  if (path_length < extension_length) return 0;
  return strcmp(path + path_length - extension_length, extension) == 0;
}

static int pack_accepts_path(const SlLanguagePack *pack, const char *path) {
  for (size_t index = 0; index < pack->extension_count; index++) {
    if (path_has_extension(path, pack->extensions[index])) return 1;
  }
  return 0;
}

const SlLanguagePack *sl_language_for_path(const char *path) {
  const SlLanguagePack *packs[] = {&sl_typescript_pack};
  const size_t count = sizeof(packs) / sizeof(*packs);
  for (size_t index = 0; index < count; index++) {
    if (pack_accepts_path(packs[index], path)) return packs[index];
  }
  return NULL;
}
