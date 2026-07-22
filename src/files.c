#include "files.h"
#include "language.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  char *pattern;
  char *base;
  int negated;
  int directory_only;
  int basename_only;
} IgnoreRule;

typedef struct {
  IgnoreRule *rules;
  size_t count;
} IgnoreList;

typedef struct {
  IgnoreList ignores;
  int use_gitignore;
} Discovery;

static char *copy_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy == NULL) return NULL;
  memcpy(copy, value, length);
  return copy;
}

static int is_source_file(const char *path) { return sl_language_for_path(path) != NULL; }

static int is_ignored_name(const char *name, int use_gitignore) {
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
  if (strcmp(name, ".git") == 0) return 1;
  if (!use_gitignore) return 0;
  if (name[0] == '.') return 1;
  if (strcmp(name, "node_modules") == 0) return 1;
  return strcmp(name, "vendor") == 0;
}

static char *join_path(const char *directory, const char *name) {
  const size_t directory_length = strlen(directory);
  const size_t name_length = strlen(name);
  char *path = malloc(directory_length + name_length + 2);
  if (path == NULL) return NULL;
  memcpy(path, directory, directory_length);
  path[directory_length] = '/';
  memcpy(path + directory_length + 1, name, name_length + 1);
  return path;
}

static char *join_relative(const char *directory, const char *name) {
  if (directory[0] == '\0') return copy_string(name);
  return join_path(directory, name);
}

static SlStatus append_file(SlFileList *files, const char *path) {
  char *copy = copy_string(path);
  if (copy == NULL) return SL_OUT_OF_MEMORY;
  const size_t size = (files->count + 1) * sizeof(*files->paths);
  char **paths = realloc(files->paths, size);
  if (paths == NULL) return free(copy), SL_OUT_OF_MEMORY;
  files->paths = paths;
  files->paths[files->count++] = copy;
  return SL_OK;
}

static void trim_line(char *line) {
  size_t length = strlen(line);
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    line[--length] = '\0';
  }
}

static void ignore_rule_free(IgnoreRule *rule) {
  free(rule->pattern);
  free(rule->base);
  *rule = (IgnoreRule){0};
}

static SlStatus append_ignore(IgnoreList *ignores, IgnoreRule rule) {
  const size_t size = (ignores->count + 1) * sizeof(*ignores->rules);
  IgnoreRule *rules = realloc(ignores->rules, size);
  if (rules == NULL) return ignore_rule_free(&rule), SL_OUT_OF_MEMORY;
  ignores->rules = rules;
  ignores->rules[ignores->count++] = rule;
  return SL_OK;
}

static IgnoreRule create_ignore_rule(const char *pattern, const char *base, int negated,
                                     int directory_only, int basename_only) {
  return (IgnoreRule){.pattern = copy_string(pattern),
                      .base = copy_string(base),
                      .negated = negated,
                      .directory_only = directory_only,
                      .basename_only = basename_only};
}

static SlStatus parse_ignore_line(char *line, const char *base, IgnoreList *ignores) {
  trim_line(line);
  if (line[0] == '\0' || line[0] == '#') return SL_OK;
  const int negated = line[0] == '!';
  char *pattern = line + negated;
  const int anchored = pattern[0] == '/';
  if (anchored) pattern++;
  size_t length = strlen(pattern);
  const int directory_only = length > 0 && pattern[length - 1] == '/';
  if (directory_only) pattern[--length] = '\0';
  if (length == 0) return SL_OK;
  const int basename_only = !anchored && strchr(pattern, '/') == NULL;
  IgnoreRule rule = create_ignore_rule(pattern, base, negated, directory_only, basename_only);
  if (rule.pattern == NULL || rule.base == NULL) return ignore_rule_free(&rule), SL_OUT_OF_MEMORY;
  return append_ignore(ignores, rule);
}

static SlStatus load_ignores(const char *directory, const char *relative, IgnoreList *ignores) {
  char *path = join_path(directory, ".gitignore");
  if (path == NULL) return SL_OUT_OF_MEMORY;
  FILE *file = fopen(path, "rb");
  free(path);
  if (file == NULL && errno == ENOENT) return SL_OK;
  if (file == NULL) return SL_IO_ERROR;
  char *line = NULL;
  size_t capacity = 0;
  SlStatus status = SL_OK;
  while (status == SL_OK && getline(&line, &capacity, file) >= 0) {
    status = parse_ignore_line(line, relative, ignores);
  }
  if (status == SL_OK && ferror(file)) status = SL_IO_ERROR;
  free(line);
  fclose(file);
  return status;
}

static void ignore_list_free(IgnoreList *ignores) {
  for (size_t index = 0; index < ignores->count; index++)
    ignore_rule_free(&ignores->rules[index]);
  free(ignores->rules);
  *ignores = (IgnoreList){0};
}

static void ignore_list_truncate(IgnoreList *ignores, size_t count) {
  while (ignores->count > count)
    ignore_rule_free(&ignores->rules[--ignores->count]);
}

static const char *scoped_relative(const IgnoreRule *rule, const char *relative) {
  if (rule->base[0] == '\0') return relative;
  const size_t length = strlen(rule->base);
  if (strncmp(relative, rule->base, length) != 0) return NULL;
  if (relative[length] != '/') return NULL;
  return relative + length + 1;
}

static int rule_matches(const IgnoreRule *rule, const char *relative, const char *name,
                        int is_directory) {
  if (rule->directory_only && !is_directory) return 0;
  const char *scoped = scoped_relative(rule, relative);
  if (scoped == NULL) return 0;
  const char *candidate = rule->basename_only ? name : scoped;
  const int flags = rule->basename_only ? 0 : FNM_PATHNAME;
  return fnmatch(rule->pattern, candidate, flags) == 0;
}

static int path_is_ignored(const Discovery *discovery, const char *relative, const char *name,
                           int is_directory) {
  int ignored = 0;
  for (size_t index = 0; index < discovery->ignores.count; index++) {
    const IgnoreRule *rule = &discovery->ignores.rules[index];
    if (rule_matches(rule, relative, name, is_directory)) ignored = !rule->negated;
  }
  return ignored;
}

static SlStatus discover_directory(const char *path, const char *relative, Discovery *discovery,
                                   SlFileList *files);

static SlStatus discover_child(const char *path, const char *relative, const char *name,
                               const struct stat *details, Discovery *discovery,
                               SlFileList *files) {
  const int is_directory = S_ISDIR(details->st_mode);
  const int ignored =
      discovery->use_gitignore && path_is_ignored(discovery, relative, name, is_directory);
  if (ignored) return SL_OK;
  if (is_directory) return discover_directory(path, relative, discovery, files);
  if (S_ISREG(details->st_mode) && is_source_file(path)) return append_file(files, path);
  return SL_OK;
}

static SlStatus discover_entry(const char *directory, const char *relative,
                               const struct dirent *entry, Discovery *discovery,
                               SlFileList *files) {
  if (is_ignored_name(entry->d_name, discovery->use_gitignore)) return SL_OK;
  char *path = join_path(directory, entry->d_name);
  char *child_relative = join_relative(relative, entry->d_name);
  if (path == NULL || child_relative == NULL)
    return free(path), free(child_relative), SL_OUT_OF_MEMORY;
  struct stat details;
  if (lstat(path, &details) != 0) return free(path), free(child_relative), SL_IO_ERROR;
  const SlStatus status =
      discover_child(path, child_relative, entry->d_name, &details, discovery, files);
  free(path);
  free(child_relative);
  return status;
}

static SlStatus open_discovery_directory(const char *path, const char *relative,
                                         Discovery *discovery, size_t *inherited_count,
                                         DIR **directory) {
  *inherited_count = discovery->ignores.count;
  const SlStatus status =
      discovery->use_gitignore ? load_ignores(path, relative, &discovery->ignores) : SL_OK;
  if (status != SL_OK) return status;
  *directory = opendir(path);
  if (*directory != NULL) return SL_OK;
  ignore_list_truncate(&discovery->ignores, *inherited_count);
  return SL_IO_ERROR;
}

static SlStatus discover_directory(const char *path, const char *relative, Discovery *discovery,
                                   SlFileList *files) {
  size_t inherited_count = 0;
  DIR *directory = NULL;
  SlStatus status =
      open_discovery_directory(path, relative, discovery, &inherited_count, &directory);
  if (status != SL_OK) return status;
  struct dirent *entry = NULL;
  errno = 0;
  while (status == SL_OK && (entry = readdir(directory)) != NULL) {
    status = discover_entry(path, relative, entry, discovery, files);
  }
  if (status == SL_OK && errno != 0) status = SL_IO_ERROR;
  closedir(directory);
  ignore_list_truncate(&discovery->ignores, inherited_count);
  return status;
}

static SlStatus discover_input(const char *path, int use_gitignore, SlFileList *files) {
  struct stat details;
  if (lstat(path, &details) != 0) return SL_IO_ERROR;
  if (S_ISREG(details.st_mode) && is_source_file(path)) return append_file(files, path);
  if (!S_ISDIR(details.st_mode)) return SL_OK;
  Discovery discovery = {.use_gitignore = use_gitignore};
  const SlStatus status = discover_directory(path, "", &discovery, files);
  ignore_list_free(&discovery.ignores);
  return status;
}

static int compare_paths(const void *left, const void *right) {
  const char *const *left_path = left;
  const char *const *right_path = right;
  return strcmp(*left_path, *right_path);
}

SlStatus sl_discover_files(const char *const *inputs, size_t input_count, int use_gitignore,
                           SlFileList *files) {
  if (inputs == NULL || input_count == 0 || files == NULL) return SL_INVALID_ARGUMENT;
  *files = (SlFileList){0};
  for (size_t index = 0; index < input_count; index++) {
    const SlStatus status = discover_input(inputs[index], use_gitignore, files);
    if (status != SL_OK) return sl_file_list_free(files), status;
  }
  if (files->count == 0) return sl_file_list_free(files), SL_INVALID_ARGUMENT;
  qsort(files->paths, files->count, sizeof(*files->paths), compare_paths);
  return SL_OK;
}

void sl_file_list_free(SlFileList *files) {
  if (files == NULL) return;
  for (size_t index = 0; index < files->count; index++)
    free(files->paths[index]);
  free(files->paths);
  *files = (SlFileList){0};
}
