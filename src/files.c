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
  int excluded;
} Discovery;

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

static void file_free(SlFile *file) {
  free(file->path);
  free(file->resolved_path);
}

static SlStatus append_file(SlFileList *files, const char *path) {
  SlFile file = {.resolved_path = realpath(path, NULL)};
  if (file.resolved_path == NULL) return errno == ENOMEM ? SL_OUT_OF_MEMORY : SL_IO_ERROR;
  file.path = strdup(path);
  if (file.path == NULL) return file_free(&file), SL_OUT_OF_MEMORY;
  const size_t size = (files->count + 1) * sizeof(*files->items);
  SlFile *items = realloc(files->items, size);
  if (items == NULL) return file_free(&file), SL_OUT_OF_MEMORY;
  files->items = items;
  files->items[files->count++] = file;
  return SL_OK;
}

static void trim_line(char *line) {
  size_t length = strlen(line);
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    line[--length] = '\0';
  }
  while (length > 0 && line[length - 1] == ' ') {
    size_t start = length - 1;
    while (start > 0 && line[start - 1] == '\\')
      start--;
    if ((length - 1 - start) % 2 != 0) break;
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
  return (IgnoreRule){.pattern = strdup(pattern),
                      .base = strdup(base),
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

static char *scoped_relative(const IgnoreRule *rule, char *relative) {
  if (strcmp(rule->base, "/") == 0) return relative + 1;
  const size_t length = strlen(rule->base);
  if (strncmp(relative, rule->base, length) != 0) return NULL;
  if (relative[length] != '/') return NULL;
  return relative + length + 1;
}

// Keep component matching in fnmatch; only ** spans directories.
static int match_path(char *pattern, char *path) {
  char *separator = strchr(pattern, '/');
  if (separator == NULL) return fnmatch(pattern, path, FNM_PATHNAME) == 0;
  char *next = strchr(path, '/');
  const int recursive = separator == pattern + 2 && strncmp(pattern, "**", 2) == 0;
  if (recursive) {
    const int zero_directories = match_path(separator + 1, path);
    return zero_directories || (next != NULL && match_path(pattern, next + 1));
  }
  if (next == NULL) return 0;
  *separator = *next = '\0';
  const int matched = fnmatch(pattern, path, 0) == 0;
  *separator = *next = '/';
  return matched && match_path(separator + 1, next + 1);
}

static int rule_matches(const IgnoreRule *rule, char *relative, const char *name,
                        int is_directory) {
  if (rule->directory_only && !is_directory) return 0;
  char *scoped = scoped_relative(rule, relative);
  if (scoped == NULL) return 0;
  if (rule->basename_only) return fnmatch(rule->pattern, name, 0) == 0;
  return match_path(rule->pattern, scoped);
}

static int path_is_ignored(const Discovery *discovery, char *relative, const char *name,
                           int is_directory) {
  int ignored = 0;
  for (size_t index = 0; index < discovery->ignores.count; index++) {
    const IgnoreRule *rule = &discovery->ignores.rules[index];
    if (rule_matches(rule, relative, name, is_directory)) ignored = !rule->negated;
  }
  return ignored;
}

static SlStatus discover_directory(const char *path, char *relative, Discovery *discovery,
                                   SlFileList *files);

static SlStatus discover_child(const char *path, char *relative, const char *name,
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
  char *child_relative = join_path(relative, entry->d_name);
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

static SlStatus read_directory(DIR *directory, const char *path, const char *relative,
                               Discovery *discovery, SlFileList *files) {
  while (1) {
    errno = 0;
    const struct dirent *entry = readdir(directory);
    if (entry == NULL) return errno == 0 ? SL_OK : SL_IO_ERROR;
    const SlStatus status = discover_entry(path, relative, entry, discovery, files);
    if (status != SL_OK) return status;
  }
}

static SlStatus discover_directory(const char *path, char *relative, Discovery *discovery,
                                   SlFileList *files) {
  const size_t inherited_count = discovery->ignores.count;
  SlStatus status =
      discovery->use_gitignore ? load_ignores(path, relative, &discovery->ignores) : SL_OK;
  if (status != SL_OK) return status;
  DIR *directory = opendir(path);
  if (directory == NULL)
    return ignore_list_truncate(&discovery->ignores, inherited_count), SL_IO_ERROR;
  status = read_directory(directory, path, relative, discovery, files);
  closedir(directory);
  ignore_list_truncate(&discovery->ignores, inherited_count);
  return status;
}

static char *parent_directory(const char *path) {
  char *parent = strdup(path);
  if (parent == NULL) return NULL;
  char *separator = strrchr(parent, '/');
  if (separator == parent) separator++;
  *separator = '\0';
  return parent;
}

static SlStatus load_parent_ignores(char *path, Discovery *discovery) {
  if (strcmp(path, "/") == 0) return SL_OK;
  char *marker = join_path(path, ".git");
  if (marker == NULL) return SL_OUT_OF_MEMORY;
  struct stat details;
  const int repository_root = lstat(marker, &details) == 0;
  free(marker);
  if (repository_root) return SL_OK;
  const char *name = strrchr(path, '/') + 1;
  char *parent = parent_directory(path);
  if (parent == NULL) return SL_OUT_OF_MEMORY;
  SlStatus status = load_parent_ignores(parent, discovery);
  if (status == SL_OK && !discovery->excluded)
    status = load_ignores(parent, parent, &discovery->ignores);
  if (status == SL_OK && !discovery->excluded)
    discovery->excluded = path_is_ignored(discovery, path, name, 1);
  free(parent);
  return status;
}

static SlStatus discover_input(const char *path, int use_gitignore, SlFileList *files) {
  struct stat details;
  if (lstat(path, &details) != 0) return SL_IO_ERROR;
  if (S_ISREG(details.st_mode) && is_source_file(path)) return append_file(files, path);
  if (!S_ISDIR(details.st_mode)) return SL_OK;
  char *resolved = realpath(path, NULL);
  if (resolved == NULL) return SL_IO_ERROR;
  Discovery discovery = {.use_gitignore = use_gitignore};
  SlStatus status = use_gitignore ? load_parent_ignores(resolved, &discovery) : SL_OK;
  if (status == SL_OK && !discovery.excluded)
    status = discover_directory(path, resolved, &discovery, files);
  free(resolved);
  ignore_list_free(&discovery.ignores);
  return status;
}

static int compare_paths(const void *left, const void *right) {
  const SlFile *left_file = left;
  const SlFile *right_file = right;
  return strcmp(left_file->path, right_file->path);
}

static int compare_resolved_paths(const void *left, const void *right) {
  const SlFile *left_file = left;
  const SlFile *right_file = right;
  const int order = strcmp(left_file->resolved_path, right_file->resolved_path);
  return order == 0 ? compare_paths(left, right) : order;
}

static void deduplicate_files(SlFileList *files) {
  qsort(files->items, files->count, sizeof(*files->items), compare_resolved_paths);
  size_t count = 0;
  for (size_t index = 0; index < files->count; index++) {
    SlFile *file = &files->items[index];
    if (count > 0 && strcmp(file->resolved_path, files->items[count - 1].resolved_path) == 0) {
      file_free(file);
      continue;
    }
    files->items[count++] = *file;
  }
  files->count = count;
  qsort(files->items, files->count, sizeof(*files->items), compare_paths);
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
  deduplicate_files(files);
  return SL_OK;
}

void sl_file_list_free(SlFileList *files) {
  if (files == NULL) return;
  for (size_t index = 0; index < files->count; index++)
    file_free(&files->items[index]);
  free(files->items);
  *files = (SlFileList){0};
}
