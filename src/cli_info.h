#ifndef STRUCT_LINT_CLI_INFO_H
#define STRUCT_LINT_CLI_INFO_H

#include <stdio.h>
#include <string.h>

static int sl_cli_info(const char *option) {
  const char *text = NULL;
  if (strcmp(option, "--version") == 0) text = "struct-lint " SL_VERSION "\n";
  if (strcmp(option, "--help") == 0)
    text = "usage: struct-lint [options] [path...]\n"
           "  --profile <name>     Select a compiled profile (default: local)\n"
           "  --format human|json Select diagnostic output (default: human)\n"
           "  --no-ignore         Include ignored paths\n"
           "  --help              Show help\n"
           "  --version           Show version\n";
  if (text == NULL) return -1;
  const int failed = fputs(text, stdout) == EOF;
  return fflush(stdout) != 0 || failed ? 2 : 0;
}

#endif
