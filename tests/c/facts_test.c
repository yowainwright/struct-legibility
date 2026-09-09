#include "struct_lint.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *path;
  const char *callee_name;
  const char *callee_path;
} GraphCase;

static const GraphCase graph_cases[] = {
    {"graph", "helper", "graph/helper.ts"},
    {"graph-default", "helper", "graph-default/helper.ts"},
    {"graph-export-alias", "helper", "graph-export-alias/helper.ts"},
    {"graph-default-anonymous", "default", "graph-default-anonymous/helper.ts"},
    {"graph-directory", "helper", "graph-directory/helper/index.ts"},
    {"graph-unrelated", NULL, NULL},
    {"graph-shadow", NULL, NULL},
    {"graph-destructure", NULL, NULL},
    {"nested-calls.ts", NULL, NULL},
};

static SlStatus analyze(const char *path, int collect_facts, SlReport *report) {
  const char *const paths[] = {path, path};
  const SlRequest request = {paths, 2, 1, collect_facts};
  return sl_analyze(&request, report);
}

static int graph_matches(const SlReport *report, const GraphCase *test) {
  if (test->callee_name == NULL) return report->call_count == 0;
  if (report->call_count != 1) return 0;
  const SlCallFact *call = &report->calls[0];
  const char *caller_file = strrchr(call->caller_path, '/');
  return caller_file != NULL && strcmp(caller_file, "/main.ts") == 0 &&
         strcmp(call->caller_name, "crossFileMain") == 0 &&
         strcmp(call->callee_name, test->callee_name) == 0 &&
         strcmp(call->callee_path, test->callee_path) == 0;
}

static int check_graph(const GraphCase *test) {
  SlReport report = {0};
  const SlStatus status = analyze(test->path, 1, &report);
  const int passed = status == SL_OK && graph_matches(&report, test);
  sl_report_free(&report);
  if (!passed) fprintf(stderr, "incorrect call graph: %s\n", test->path);
  return passed;
}

static int has_name(char *const *names, size_t count, const char *expected) {
  for (size_t index = 0; index < count; index++) {
    if (strcmp(names[index], expected) == 0) return 1;
  }
  return 0;
}

static const SlDeclarationFact *exported_launch(const SlReport *report) {
  if (report->file_count != 1) return NULL;
  const SlFileFact *file = &report->files[0];
  for (size_t index = 0; index < file->declaration_count; index++) {
    const SlDeclarationFact *declaration = &file->declarations[index];
    const int is_launch = strcmp(declaration->name, "launch") == 0;
    const int is_function = strcmp(declaration->kind, "function") == 0;
    if (is_launch && is_function && declaration->exported) return declaration;
  }
  return NULL;
}

static int declaration_matches(const SlReport *report, int suppressed) {
  const SlDeclarationFact *declaration = exported_launch(report);
  if (declaration == NULL) return 0;
  if (!has_name(declaration->export_names, declaration->export_name_count, "launch")) return 0;
  const int has_suppression =
      has_name(declaration->suppressions, declaration->suppression_count, "entrypoint-name");
  return has_suppression == suppressed;
}

static int check_declaration(const char *path, int suppressed) {
  SlReport report = {0};
  const SlStatus status = analyze(path, 1, &report);
  const int passed = status == SL_OK && declaration_matches(&report, suppressed);
  sl_report_free(&report);
  if (!passed) fprintf(stderr, "incorrect declaration facts: %s\n", path);
  return passed;
}

static int check_facts_disabled(void) {
  SlReport report = {0};
  const SlStatus status = analyze("graph", 0, &report);
  const int passed = status == SL_OK && report.file_count == 0 && report.call_count == 0;
  sl_report_free(&report);
  if (!passed) fputs("facts collected when disabled\n", stderr);
  return passed;
}

int main(void) {
  for (size_t index = 0; index < sizeof(graph_cases) / sizeof(*graph_cases); index++) {
    if (!check_graph(&graph_cases[index])) return 1;
  }
  if (!check_declaration("custom-rule.ts", 0)) return 1;
  if (!check_declaration("arrow-custom-rule.ts", 0)) return 1;
  if (!check_declaration("re-export.ts", 0)) return 1;
  if (!check_declaration("custom-suppressed.ts", 1)) return 1;
  return check_facts_disabled() ? 0 : 1;
}
