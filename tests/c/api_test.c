#include "struct_legibility.h"

static int expect_invalid(const char *const *paths, size_t count, int use_gitignore,
                          int collect_facts) {
  const SlRequest request = {paths, count, use_gitignore, collect_facts};
  SlReport report = {0};
  const SlStatus status = sl_analyze(&request, &report);
  sl_report_free(&report);
  return status == SL_INVALID_ARGUMENT;
}

int main(void) {
  const char *const null_path[] = {NULL};
  const char *const empty_path[] = {""};
  const char *const valid_path[] = {"."};
  const SlRequest valid_request = {valid_path, 1, 1, 0};
  SlReport report = {0};
  if (sl_analyze(NULL, &report) != SL_INVALID_ARGUMENT) return 1;
  if (sl_analyze(&valid_request, NULL) != SL_INVALID_ARGUMENT) return 1;
  if (!expect_invalid(NULL, 1, 1, 0)) return 1;
  if (!expect_invalid(null_path, 1, 1, 0)) return 1;
  if (!expect_invalid(empty_path, 1, 1, 0)) return 1;
  if (!expect_invalid(valid_path, 1, 2, 0)) return 1;
  if (!expect_invalid(valid_path, 1, 1, -1)) return 1;
  return 0;
}
