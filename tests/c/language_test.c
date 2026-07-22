#include "language.h"

static int invalid_resolver(const SlCallResolutionRequest *request, SlResolvedFunction *result) {
  (void)request;
  *result = (SlResolvedFunction){0};
  return 1;
}

int main(void) {
  const SlLanguagePack pack = {.resolve_call = invalid_resolver};
  const SlCallResolutionRequest request = {0};
  SlResolvedFunction result = {0};
  return sl_language_resolve_call(&pack, &request, &result) ? 1 : 0;
}
