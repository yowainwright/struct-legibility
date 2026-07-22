#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *bytes;
  size_t length;
} Source;

static int read_source(const char *path, Source *source) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0) return fclose(file), 0;
  const long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) return fclose(file), 0;
  source->bytes = malloc((size_t)length);
  if (source->bytes == NULL) return fclose(file), 0;
  source->length = fread(source->bytes, 1, (size_t)length, file);
  const int complete = source->length == (size_t)length;
  fclose(file);
  if (complete) return 1;
  free(source->bytes);
  *source = (Source){0};
  return 0;
}

static int write_source(const char *path, const Source *source) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return 0;
  const size_t written = fwrite(source->bytes, 1, source->length, file);
  int complete = written == source->length;
  if (fclose(file) != 0) complete = 0;
  return complete;
}

static int generate_files(const char *directory, const Source *source) {
  const size_t capacity = strlen(directory) + 32;
  char *path = malloc(capacity);
  if (path == NULL) return 0;
  for (size_t index = 0; index < 10000; index++) {
    snprintf(path, capacity, "%s/file-%zu.ts", directory, index);
    if (!write_source(path, source)) return free(path), 0;
  }
  free(path);
  return 1;
}

int main(int argument_count, char **arguments) {
  if (argument_count != 3) return 2;
  Source source = {0};
  if (!read_source(arguments[1], &source)) return 1;
  const int generated = generate_files(arguments[2], &source);
  free(source.bytes);
  return generated ? 0 : 1;
}
