#include "struct_lint.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *path;
  const char *source;
  const char *rule;
  size_t line;
  size_t files;
  size_t calls;
} Case;

static const Case cases[] = {
    {"test.vue",
     "// struct-lint-disable-next function-order -- outside code\n"
     "<script>function helper() {}\nfunction main() { return helper(); }\n</script>\n",
     "function-order", 2, 1, 1},
    {"test.mdx",
     "// struct-lint-disable-next function-order -- prose\n\n"
     "export function helper() {}\n\nexport function main() { return helper(); }\n",
     "function-order", 3, 1, 1},
    {"test.vue",
     "<template><p>function fake() {}</p></template>\n<script setup lang='ts'>\n"
     "function helper(): number { return 1; }\nfunction main() { return helper(); }\n</script>\n",
     "function-order", 3, 1, 1},
    {"test.svelte",
     "<script lang=ts>\nfunction main() { return helper(); }\n"
     "function helper(): number { return 1; }\n</script>\n<p>{main()}</p>\n",
     NULL, 0, 1, 1},
    {"test.astro",
     "---\nfunction helper(): number { return 1; }\n"
     "function main() { return helper(); }\n---\n<div>{main()}</div>\n",
     "function-order", 2, 1, 1},
    {"test.mdx",
     "# Title\n\nexport function helper() { return 1; }\n\n"
     "## Prose\n\nexport function main() { return helper(); }\n",
     "function-order", 3, 1, 1},
    {"test.vue",
     "<script>\nfunction helper() {}\n</script>\n"
     "<script setup>\nfunction main() { return helper(); }\n</script>\n",
     NULL, 0, 2, 0},
    {"test.svelte",
     "<script context='module'>\nfunction main() { return helper(); }\n"
     "function helper() {}\n</script>\n<script>\nfunction main() { return helper(); }\n"
     "function helper() {}\n</script>\n",
     NULL, 0, 2, 2},
    {"test.astro",
     "---\nfunction helper() {}\n---\n<section><script>\n"
     "function main() { return helper(); }\n</script></section>\n",
     NULL, 0, 2, 0},
    {"test.vue",
     "<script src='./external.js'></script>\n<script type='application/json'>"
     "{\"example\":\"function main(){}\"}</script>\n<script lang='coffee'>not "
     "javascript</script>\n",
     NULL, 0, 0, 0},
    {"test.vue",
     "<template><script>function nope(</script></template>\n"
     "<style>.x { color: red; }</style>\n",
     NULL, 0, 0, 0},
    {"test.mdx",
     "# Title\n\n```js\nexport function broken(\n```\n\n"
     "Ordinary prose.\n\n<div>text</div>\n",
     NULL, 0, 0, 0},
    {"test.vue", "<script>\nfunction broken(\n</script>\n", "parse-error", 2, 0, 0},
    {"test.svelte", "<script>\nconst typed: number = 1;\n</script>\n", "parse-error", 2, 0, 0},
    {"test.vue",
     "<script>\n// struct-lint-disable-next function-order -- fixture\n"
     "function helper() {}\nfunction main() { return helper(); }\n</script>\n",
     NULL, 0, 1, 1},
    {"test.vue",
     "<script setup>\nimport { helper } from './helper.js';\n"
     "function main() { return helper(); }\n</script>\n",
     NULL, 0, 1, 1},
    {"test.mdx",
     "import { helper } from './helper.js'\n\n# Heading\n\n"
     "export function main() { return helper(); }\n",
     NULL, 0, 1, 1},
};

static int write_source(const char *path, const char *source) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return 0;
  const int written = fputs(source, file) >= 0;
  return fclose(file) == 0 && written;
}

static int check_case(const Case *test) {
  if (!write_source(test->path, test->source)) return 0;
  const char *const paths[] = {test->path, "helper.js"};
  const SlRequest request = {paths, 2, 0, 1};
  SlReport report = {0};
  const SlStatus status = sl_analyze(&request, &report);
  int passed =
      status == SL_OK && report.file_count == test->files + 1 && report.call_count == test->calls;
  if (passed && test->calls == 2) {
    const SlCallFact *first = &report.calls[0], *second = &report.calls[1];
    passed = first->caller_line == 2 && first->callee_line == 3 && second->caller_line == 6 &&
             second->callee_line == 7 && first->caller_column == 1 && second->callee_column == 1;
  }
  if (test->rule == NULL) passed = passed && report.count == 0;
  else
    passed = passed && report.count == 1 && report.diagnostics[0].line == test->line &&
             strcmp(report.diagnostics[0].rule_id, test->rule) == 0;
  if (!passed) {
    fprintf(stderr, "%s: status %d, files %zu, calls %zu, diagnostics %zu\n%s", test->path, status,
            report.file_count, report.call_count, report.count, test->source);
    for (size_t index = 0; index < report.count; index++)
      fprintf(stderr, "line %zu: %s\n", report.diagnostics[index].line,
              report.diagnostics[index].rule_id);
  }
  sl_report_free(&report);
  return passed;
}

int main(void) {
  if (!write_source("helper.js", "export function helper() {}\n")) return 1;
  for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); index++) {
    if (!check_case(&cases[index])) return 1;
  }
  return 0;
}
