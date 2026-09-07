#include "language.h"

#include <string.h>

static int check_extensions(void) {
  const char *const paths[] = {"main.ts", "main.go", "main.py", "main.pyi", "main.sh", "main.bash"};
  const char *const languages[] = {"typescript", "go", "python", "python", "bash", "bash"};
  for (size_t index = 0; index < sizeof(paths) / sizeof(*paths); index++) {
    const SlLanguagePack *pack = sl_language_for_path(paths[index]);
    if (pack == NULL || strcmp(pack->id, languages[index]) != 0) return 0;
  }
  return sl_language_for_path("main.txt") == NULL;
}

static int invalid_resolver(const SlCallResolutionRequest *request, SlResolvedFunction *result) {
  (void)request;
  *result = (SlResolvedFunction){0};
  return 1;
}

int main(void) {
  if (!check_extensions()) return 1;
  const SlLanguagePack pack = {.resolve_call = invalid_resolver};
  const SlCallResolutionRequest request = {0};
  SlResolvedFunction result = {0};
  return sl_language_resolve_call(&pack, &request, &result) ? 1 : 0;
}
