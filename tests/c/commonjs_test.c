#include "struct_lint.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *imports;
  const char *caller;
  const char *exports;
  size_t calls;
} Case;

static const Case cases[] = {
    {"const { nested: { helper } } = require('./helper.cjs');",
     "function main() { return helper(); }", "exports.helper = helper;", 0},
    {"const { ...helper } = require('./helper.cjs');", "function main() { return helper(); }",
     "exports.helper = helper;", 0},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "const exports = {}; exports.helper = helper;", 0},
    {"const local = require('./helper.cjs');", "function main() { return local(); }",
     "const module = {}; module.exports = helper;", 0},
    {"const local = require('./helper.cjs');", "function main() { return local(); }",
     "module.exports = helper;", 1},
    {"const { helper } = require('./helper.cjs');", "function main() { return helper(); }",
     "module.exports = { helper };", 1},
    {"const { publicName: local } = require('./helper.cjs');",
     "function main() { return local(); }", "module.exports = { publicName: helper };", 1},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "exports.helper = helper;", 1},
    {"const api = require('./helper.cjs');",
     "function main() { return api /* comment */ . helper(); }", "module.exports.helper = helper;",
     1},
    {"const value = 1, api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "exports.helper = helper;", 1},
    {"const api = require('./helper.cjs');", "function main(api) { return api.helper(); }",
     "exports.helper = helper;", 0},
    {"const api = require('./helper.cjs');",
     "function main() { const api = {}; return api.helper(); }", "exports.helper = helper;", 0},
    {"const require = custom; const api = require('./helper.cjs');",
     "function main() { return api.helper(); }", "exports.helper = helper;", 0},
    {"const api = require(path);", "function main() { return api.helper(); }",
     "exports.helper = helper;", 0},
    {"const api = require('./helper.cjs');", "function main() { return api['helper'](); }",
     "exports.helper = helper;", 0},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "if (flag) exports.helper = helper;", 0},
    {"import * as api from './helper.cjs';", "function main() { return api.helper(); }",
     "exports.helper = helper;", 1},
    {"import { helper } from './helper.cjs';", "function main() { return helper(); }",
     "exports.helper = helper;", 1},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "exports.nested = { helper };", 0},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "exports.helper = helper; exports.helper = 0;", 0},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "module.exports = { helper }; module.exports = {};", 0},
    {"const api = require('./helper.cjs');", "function main() { return api.helper(); }",
     "exports.helper = helper; module.exports = {};", 0},
    {"import { require } from 'custom'; const api = require('./helper.cjs');",
     "function main() { return api.helper(); }", "exports.helper = helper;", 0},
    {"const api = require('./helper');", "function main() { return api.helper(); }",
     "exports.helper = helper;", 0},
};

static int write_source(const char *path, const char *first, const char *second) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return 0;
  const int written = fprintf(file, "%s\n%s\n", first, second) >= 0;
  return fclose(file) == 0 && written;
}

static int check_case(const Case *test, const char *callee) {
  if (!write_source("main.cjs", test->imports, test->caller)) return 0;
  if (!write_source("helper.cjs", test->exports, "function helper() {}")) return 0;
  const char *const paths[] = {"main.cjs", "helper.cjs"};
  const SlRequest request = {paths, 2, 0, 1};
  SlReport report = {0};
  const SlStatus status = sl_analyze(&request, &report);
  int passed = status == SL_OK && report.count == 0 && report.call_count == test->calls;
  if (passed && test->calls > 0)
    passed = strcmp(report.calls[0].callee_name, callee) == 0 &&
             strcmp(report.calls[0].callee_path, "helper.cjs") == 0;
  if (!passed)
    fprintf(stderr, "%s\n%s\n%s\nstatus %d, diagnostics %zu, calls %zu\n", test->imports,
            test->caller, test->exports, status, report.count, report.call_count);
  sl_report_free(&report);
  return passed;
}

int main(void) {
  for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); index++) {
    if (!check_case(&cases[index], "helper")) return 1;
  }
  const Case direct = {"const api = require('./helper.cjs');",
                       "function main() { return api.work(); }", "exports.work = function() {};",
                       1};
  const Case default_export = {"const local = require('./helper.cjs');",
                               "function main() { return local(); }", "module.exports = () => 1;",
                               1};
  return check_case(&direct, "exports.work") && check_case(&default_export, "module.exports") ? 0
                                                                                              : 1;
}
