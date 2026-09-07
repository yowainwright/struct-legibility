#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE

#include <stdlib.h>

static size_t remaining_allocations;

static void *failing_calloc(size_t count, size_t size) {
  if (remaining_allocations == 0) return NULL;
  remaining_allocations--;
  return calloc(count, size);
}

// Inject failures into the analyzer without changing the production allocator.
#define calloc failing_calloc
#include "../../src/analyzer.c"
#undef calloc

int main(int argc, char **argv) {
  if (argc != 3) return 1;
  const SlRequest request = {(const char *const *)(argv + 1), 2, 0, 0};
  for (size_t allocation = 0; allocation < 3; allocation++) {
    remaining_allocations = allocation;
    SlReport report = {0};
    const SlStatus status = sl_analyze(&request, &report);
    sl_report_free(&report);
    if (status != SL_OUT_OF_MEMORY) return 1;
  }
  return 0;
}
