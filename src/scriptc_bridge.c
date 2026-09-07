#include "struct_lint.h"
#include "cli_info.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char **items;
  size_t count;
} PathList;

static char *copy_span(const uint8_t *bytes, size_t length) {
  char *copy = malloc(length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, bytes, length);
  copy[length] = '\0';
  return copy;
}

int32_t sl_scriptc_info(const uint8_t *bytes, size_t length) {
  if (bytes == NULL) return -1;
  char *option = copy_span(bytes, length);
  if (option == NULL) return 2;
  const int status = sl_cli_info(option);
  free(option);
  return status;
}

static void path_list_free(PathList *paths) {
  for (size_t index = 0; index < paths->count; index++)
    free(paths->items[index]);
  free(paths->items);
  *paths = (PathList){0};
}

static size_t path_count(const uint8_t *bytes, size_t length) {
  size_t count = 1;
  for (size_t index = 0; index < length; index++) {
    if (bytes[index] == '\0') count++;
  }
  return count;
}

static int append_path(PathList *paths, const uint8_t *bytes, size_t length) {
  if (length == 0) return 0;
  char *path = copy_span(bytes, length);
  if (path == NULL) return 0;
  paths->items[paths->count++] = path;
  return 1;
}

static int path_list_read(const uint8_t *bytes, size_t length, PathList *paths) {
  if (bytes == NULL || length == 0) return 0;
  paths->items = calloc(path_count(bytes, length), sizeof(*paths->items));
  if (paths->items == NULL) return 0;
  size_t start = 0;
  for (size_t index = 0; index <= length; index++) {
    const int at_end = index == length;
    if (!at_end && bytes[index] != '\0') continue;
    if (!append_path(paths, bytes + start, index - start)) {
      path_list_free(paths);
      return 0;
    }
    start = index + 1;
  }
  return 1;
}

static void write_json_byte(FILE *file, unsigned char byte) {
  if (byte == '"' || byte == '\\') fprintf(file, "\\%c", byte);
  else if (byte == '\b') fputs("\\b", file);
  else if (byte == '\f') fputs("\\f", file);
  else if (byte == '\n') fputs("\\n", file);
  else if (byte == '\r') fputs("\\r", file);
  else if (byte == '\t') fputs("\\t", file);
  else if (byte < 0x20) fprintf(file, "\\u%04x", byte);
  else fputc(byte, file);
}

static void write_json_string(FILE *file, const char *value) {
  fputc('"', file);
  for (const unsigned char *byte = (const unsigned char *)value; *byte != '\0'; byte++) {
    write_json_byte(file, *byte);
  }
  fputc('"', file);
}

static void write_string_array(FILE *file, char *const *items, size_t count) {
  fputc('[', file);
  for (size_t index = 0; index < count; index++) {
    if (index > 0) fputc(',', file);
    write_json_string(file, items[index]);
  }
  fputc(']', file);
}

static void write_diagnostic(FILE *file, const SlDiagnostic *diagnostic) {
  fputs("{\"path\":", file);
  write_json_string(file, diagnostic->path);
  fprintf(file, ",\"line\":%zu,\"column\":%zu,\"ruleId\":", diagnostic->line, diagnostic->column);
  write_json_string(file, diagnostic->rule_id);
  fputs(",\"message\":", file);
  write_json_string(file, diagnostic->message);
  fprintf(file, ",\"fixedError\":%s}", diagnostic->fixed_error ? "true" : "false");
}

static void write_diagnostics(FILE *file, const SlReport *report) {
  fputc('[', file);
  for (size_t index = 0; index < report->count; index++) {
    if (index > 0) fputc(',', file);
    write_diagnostic(file, &report->diagnostics[index]);
  }
  fputc(']', file);
}

static void write_declaration(FILE *file, const SlDeclarationFact *declaration) {
  fputs("{\"kind\":", file);
  write_json_string(file, declaration->kind);
  fputs(",\"name\":", file);
  write_json_string(file, declaration->name);
  fprintf(file, ",\"exported\":%s,\"entrypoint\":%s,\"exportNames\":",
          declaration->exported ? "true" : "false", declaration->entrypoint ? "true" : "false");
  write_string_array(file, declaration->export_names, declaration->export_name_count);
  fprintf(file, ",\"line\":%zu,\"column\":%zu,\"calls\":", declaration->line, declaration->column);
  write_string_array(file, declaration->calls, declaration->call_count);
  fputs(",\"suppressions\":", file);
  write_string_array(file, declaration->suppressions, declaration->suppression_count);
  fputc('}', file);
}

static void write_declarations(FILE *file, const SlFileFact *fact) {
  fputc('[', file);
  for (size_t index = 0; index < fact->declaration_count; index++) {
    if (index > 0) fputc(',', file);
    write_declaration(file, &fact->declarations[index]);
  }
  fputc(']', file);
}

static void write_nullable_string(FILE *file, const char *value) {
  if (value == NULL) {
    fputs("null", file);
    return;
  }
  write_json_string(file, value);
}

static void write_import(FILE *file, const SlImportFact *import) {
  fputs("{\"localName\":", file);
  write_json_string(file, import->local_name);
  fputs(",\"importedName\":", file);
  write_json_string(file, import->imported_name);
  fputs(",\"source\":", file);
  write_json_string(file, import->source);
  fputs(",\"targetPath\":", file);
  write_nullable_string(file, import->target_path);
  fputc('}', file);
}

static void write_imports(FILE *file, const SlFileFact *fact) {
  fputc('[', file);
  for (size_t index = 0; index < fact->import_count; index++) {
    if (index > 0) fputc(',', file);
    write_import(file, &fact->imports[index]);
  }
  fputc(']', file);
}

static void write_file(FILE *file, const SlFileFact *fact) {
  fputs("{\"path\":", file);
  write_json_string(file, fact->path);
  fputs(",\"declarations\":", file);
  write_declarations(file, fact);
  fputs(",\"imports\":", file);
  write_imports(file, fact);
  fputc('}', file);
}

static void write_files(FILE *file, const SlReport *report) {
  fputc('[', file);
  for (size_t index = 0; index < report->file_count; index++) {
    if (index > 0) fputc(',', file);
    write_file(file, &report->files[index]);
  }
  fputc(']', file);
}

static void write_call(FILE *file, const SlCallFact *call) {
  fputs("{\"callerPath\":", file);
  write_json_string(file, call->caller_path);
  fputs(",\"callerName\":", file);
  write_json_string(file, call->caller_name);
  fputs(",\"calleePath\":", file);
  write_json_string(file, call->callee_path);
  fputs(",\"calleeName\":", file);
  write_json_string(file, call->callee_name);
  fputc('}', file);
}

static void write_calls(FILE *file, const SlReport *report) {
  fputc('[', file);
  for (size_t index = 0; index < report->call_count; index++) {
    if (index > 0) fputc(',', file);
    write_call(file, &report->calls[index]);
  }
  fputc(']', file);
}

static void write_report(FILE *file, const SlReport *report) {
  fputs("{\"diagnostics\":", file);
  write_diagnostics(file, report);
  fputs(",\"project\":{\"files\":", file);
  write_files(file, report);
  fputs(",\"calls\":", file);
  write_calls(file, report);
  fputs("}}", file);
}

static SlStatus write_report_file(const uint8_t *bytes, size_t length, const SlReport *report) {
  if (bytes == NULL || length == 0) return SL_INVALID_ARGUMENT;
  char *path = copy_span(bytes, length);
  if (path == NULL) return SL_OUT_OF_MEMORY;
  FILE *file = fopen(path, "wb");
  free(path);
  if (file == NULL) return SL_IO_ERROR;
  write_report(file, report);
  const int write_failed = ferror(file);
  const int close_failed = fclose(file) != 0;
  const int failed = write_failed || close_failed;
  return failed ? SL_IO_ERROR : SL_OK;
}

uint32_t sl_scriptc_analyze(const uint8_t *path_bytes, size_t path_length, uint8_t use_gitignore,
                            uint8_t collect_facts, const uint8_t *output_bytes,
                            size_t output_length) {
  PathList paths = {0};
  if (!path_list_read(path_bytes, path_length, &paths)) return SL_INVALID_ARGUMENT;
  const SlRequest request = {(const char *const *)paths.items, paths.count, use_gitignore,
                             collect_facts};
  SlReport report = {0};
  SlStatus status = sl_analyze(&request, &report);
  if (status == SL_OK) status = write_report_file(output_bytes, output_length, &report);
  sl_report_free(&report);
  path_list_free(&paths);
  return (uint32_t)status;
}

uint32_t sl_scriptc_write(const uint8_t *bytes, size_t length, uint8_t use_stderr) {
  if (bytes == NULL && length > 0) return SL_INVALID_ARGUMENT;
  FILE *stream = use_stderr ? stderr : stdout;
  const int write_failed = fwrite(bytes, 1, length, stream) != length;
  const int flush_failed = fflush(stream) != 0;
  return write_failed || flush_failed ? SL_IO_ERROR : SL_OK;
}
