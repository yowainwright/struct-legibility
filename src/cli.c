#include "struct_lint.h"
#include "cli_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { PROFILE_UNSET, PROFILE_LOCAL, PROFILE_CI } Profile;
typedef enum { FORMAT_HUMAN, FORMAT_JSON } OutputFormat;
typedef enum { OPTION_NONE, OPTION_PROFILE, OPTION_FORMAT, OPTION_NO_IGNORE } OptionKind;

typedef struct {
  Profile profile;
  OutputFormat format;
  int first_path;
  int use_gitignore;
} CliOptions;

typedef struct {
  size_t errors;
  size_t warnings;
} DiagnosticCounts;

static int parse_profile(const char *value, Profile *profile) {
  if (strcmp(value, "local") == 0) return *profile = PROFILE_LOCAL, 1;
  if (strcmp(value, "ci") == 0) return *profile = PROFILE_CI, 1;
  return 0;
}

static int parse_format(const char *value, OutputFormat *format) {
  if (strcmp(value, "human") == 0) return *format = FORMAT_HUMAN, 1;
  if (strcmp(value, "json") == 0) return *format = FORMAT_JSON, 1;
  return 0;
}

static int profile_from_environment(Profile *profile) {
  const char *value = getenv("STRUCT_LINT_PROFILE");
  if (value == NULL || value[0] == '\0') return *profile = PROFILE_LOCAL, 1;
  return parse_profile(value, profile);
}

static OptionKind option_kind(const char *value) {
  if (strcmp(value, "--profile") == 0) return OPTION_PROFILE;
  if (strcmp(value, "--format") == 0) return OPTION_FORMAT;
  if (strcmp(value, "--no-ignore") == 0) return OPTION_NO_IGNORE;
  return OPTION_NONE;
}

static int parse_option_value(OptionKind kind, const char *value, CliOptions *options) {
  if (kind == OPTION_PROFILE) return parse_profile(value, &options->profile);
  if (kind == OPTION_FORMAT) return parse_format(value, &options->format);
  return 0;
}

static int apply_option(int argc, char **argv, int *index, CliOptions *options) {
  const OptionKind kind = option_kind(argv[*index]);
  if (kind == OPTION_NONE) return argv[*index][0] == '-' ? 0 : -1;
  if (kind == OPTION_NO_IGNORE) {
    options->use_gitignore = 0;
    (*index)++;
    return 1;
  }
  if (*index + 1 >= argc) return 0;
  if (!parse_option_value(kind, argv[*index + 1], options)) return 0;
  *index += 2;
  return 1;
}

static int parse_options(int argc, char **argv, CliOptions *options) {
  const CliOptions defaults = {.format = FORMAT_HUMAN, .use_gitignore = 1};
  *options = defaults;
  int index = 1;
  while (index < argc) {
    const int result = apply_option(argc, argv, &index, options);
    if (result < 0) break;
    if (result == 0) return 0;
  }
  options->first_path = index;
  if (options->profile == PROFILE_UNSET) return profile_from_environment(&options->profile);
  return 1;
}

static int print_usage(void) {
  const char *usage = "usage: struct-lint [options] [path...]\n"
                      "options: --profile local|ci, --format human|json, --no-ignore\n";
  fputs(usage, stderr);
  return 2;
}

static int diagnostic_is_error(const SlDiagnostic *diagnostic, Profile profile) {
  return diagnostic->fixed_error || profile == PROFILE_CI;
}

static const char *severity_name(const SlDiagnostic *diagnostic, Profile profile) {
  return diagnostic_is_error(diagnostic, profile) ? "error" : "warning";
}

static void print_human_diagnostic(const SlDiagnostic *diagnostic, Profile profile) {
  const char *severity = severity_name(diagnostic, profile);
  fprintf(stderr, "%s:%zu:%zu: %s[%s] %s\n", diagnostic->path, diagnostic->line, diagnostic->column,
          severity, diagnostic->rule_id, diagnostic->message);
}

static int write_json_byte(unsigned char byte) {
  if (byte == '"' || byte == '\\') return printf("\\%c", byte);
  if (byte == '\b') return fputs("\\b", stdout);
  if (byte == '\f') return fputs("\\f", stdout);
  if (byte == '\n') return fputs("\\n", stdout);
  if (byte == '\r') return fputs("\\r", stdout);
  if (byte == '\t') return fputs("\\t", stdout);
  if (byte < 0x20) return printf("\\u%04x", byte);
  return fputc(byte, stdout);
}

static void write_json_string(const char *value) {
  fputc('"', stdout);
  for (const unsigned char *byte = (const unsigned char *)value; *byte != '\0'; byte++) {
    write_json_byte(*byte);
  }
  fputc('"', stdout);
}

static void print_json_diagnostic(const SlDiagnostic *diagnostic, Profile profile) {
  fputs("{\"ruleId\":", stdout);
  write_json_string(diagnostic->rule_id);
  fputs(",\"severity\":", stdout);
  write_json_string(severity_name(diagnostic, profile));
  fputs(",\"path\":", stdout);
  write_json_string(diagnostic->path);
  printf(",\"line\":%zu,\"column\":%zu,\"message\":", diagnostic->line, diagnostic->column);
  write_json_string(diagnostic->message);
  fputs(",\"related\":null}", stdout);
}

static DiagnosticCounts count_diagnostics(const SlReport *report, Profile profile) {
  DiagnosticCounts counts = {0};
  for (size_t index = 0; index < report->count; index++) {
    if (diagnostic_is_error(&report->diagnostics[index], profile)) counts.errors++;
    else counts.warnings++;
  }
  return counts;
}

static void print_json_report(const SlReport *report, Profile profile) {
  fputs("{\"diagnostics\":[", stdout);
  for (size_t index = 0; index < report->count; index++) {
    if (index > 0) fputc(',', stdout);
    print_json_diagnostic(&report->diagnostics[index], profile);
  }
  const DiagnosticCounts counts = count_diagnostics(report, profile);
  printf("],\"summary\":{\"errors\":%zu,\"warnings\":%zu}}\n", counts.errors, counts.warnings);
}

static void print_human_report(const SlReport *report, Profile profile) {
  for (size_t index = 0; index < report->count; index++) {
    print_human_diagnostic(&report->diagnostics[index], profile);
  }
}

static void print_report(const SlReport *report, const CliOptions *options) {
  if (options->format == FORMAT_JSON) {
    print_json_report(report, options->profile);
    return;
  }
  print_human_report(report, options->profile);
}

static int report_exit_code(const SlReport *report, Profile profile) {
  const DiagnosticCounts counts = count_diagnostics(report, profile);
  return counts.errors > 0 ? 1 : 0;
}

static int execute_request(const SlRequest *request, const CliOptions *options) {
  SlReport report = {0};
  const SlStatus status = sl_analyze(request, &report);
  if (status != SL_OK) {
    sl_report_free(&report);
    return fprintf(stderr, "struct-lint: analysis failed (%d)\n", status), 2;
  }
  print_report(&report, options);
  const int exit_code = report_exit_code(&report, options->profile);
  sl_report_free(&report);
  return exit_code;
}

int main(int argc, char **argv) {
  const int info = argc > 1 ? sl_cli_info(argv[1]) : -1;
  if (info >= 0) return info;
  CliOptions options;
  if (!parse_options(argc, argv, &options)) return print_usage();
  const char *const default_paths[] = {"."};
  const char *const *paths = default_paths;
  size_t path_count = 1;
  if (options.first_path < argc) {
    paths = (const char *const *)&argv[options.first_path];
    path_count = (size_t)(argc - options.first_path);
  }
  const int use_gitignore = options.use_gitignore;
  const int collect_facts = 0;
  const SlRequest request = {paths, path_count, use_gitignore, collect_facts};
  return execute_request(&request, &options);
}
