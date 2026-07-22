#include "files.h"
#include "language.h"
#include "struct_legibility.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include <unistd.h>

typedef struct {
  char *bytes;
  size_t length;
} Source;

typedef struct {
  char *name;
  TSNode node;
  char **calls;
  size_t call_count;
  int exported;
  int suppress_function_order;
} FunctionFact;

typedef struct {
  FunctionFact *items;
  size_t count;
} FunctionList;

typedef struct {
  const char *name;
  FunctionFact *function;
} NameSlot;

typedef struct {
  NameSlot *slots;
  size_t capacity;
} NameIndex;

typedef struct {
  size_t start;
  size_t end;
} TextRange;

typedef struct {
  FunctionList *functions;
  const NameIndex *names;
  size_t *indices;
  size_t *lowlinks;
  size_t *components;
  size_t *stack;
  unsigned char *on_stack;
  size_t stack_count;
  size_t next_index;
  size_t next_component;
} ComponentSearch;

typedef struct {
  const SlRequest *request;
  const SlFileList *files;
  SlReport *reports;
  size_t index;
  size_t count;
  SlStatus status;
} AnalysisWorker;

typedef struct {
  AnalysisWorker *workers;
  pthread_t *threads;
  SlReport *reports;
  size_t worker_count;
  size_t file_count;
} AnalysisPool;

static char *copy_string(const char *value) {
  const size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (copy == NULL) return NULL;
  memcpy(copy, value, length);
  return copy;
}

static SlStatus read_source(const char *path, Source *source) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return SL_IO_ERROR;
  if (fseek(file, 0, SEEK_END) != 0) return fclose(file), SL_IO_ERROR;
  const long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) return fclose(file), SL_IO_ERROR;
  source->bytes = malloc((size_t)size + 1);
  if (source->bytes == NULL) return fclose(file), SL_OUT_OF_MEMORY;
  source->length = fread(source->bytes, 1, (size_t)size, file);
  const int read_failed = ferror(file);
  fclose(file);
  if (read_failed) return free(source->bytes), SL_IO_ERROR;
  source->bytes[source->length] = '\0';
  return SL_OK;
}

static int node_is(TSNode node, const char *type) { return strcmp(ts_node_type(node), type) == 0; }

static const char *section_name(SlDeclarationKind section) {
  if (section == SL_DECLARATION_IMPORT) return "imports";
  if (section == SL_DECLARATION_TYPE) return "public types";
  if (section == SL_DECLARATION_CONSTANT) return "constants";
  if (section == SL_DECLARATION_FUNCTION) return "functions";
  return "declarations";
}

static SlStatus append_diagnostic(SlReport *report, SlDiagnostic diagnostic) {
  const size_t size = (report->count + 1) * sizeof(*report->diagnostics);
  SlDiagnostic *items = realloc(report->diagnostics, size);
  if (items == NULL) {
    free(diagnostic.path);
    free(diagnostic.message);
    return SL_OUT_OF_MEMORY;
  }
  report->diagnostics = items;
  report->diagnostics[report->count] = diagnostic;
  report->count++;
  return SL_OK;
}

static char *node_text(const Source *source, TSNode node) {
  const uint32_t start = ts_node_start_byte(node);
  const uint32_t end = ts_node_end_byte(node);
  const size_t length = end - start;
  char *text = malloc(length + 1);
  if (text == NULL) return NULL;
  memcpy(text, source->bytes + start, length);
  text[length] = '\0';
  return text;
}

static size_t line_start(const Source *source, size_t offset) {
  while (offset > 0 && source->bytes[offset - 1] != '\n')
    offset--;
  return offset;
}

static TextRange previous_line(const Source *source, size_t before) {
  size_t end = before;
  if (end > 0 && source->bytes[end - 1] == '\n') end--;
  if (end > 0 && source->bytes[end - 1] == '\r') end--;
  const size_t start = line_start(source, end);
  return (TextRange){start, end};
}

static TextRange trim_range(const Source *source, TextRange range) {
  while (range.start < range.end &&
         (source->bytes[range.start] == ' ' || source->bytes[range.start] == '\t')) {
    range.start++;
  }
  while (range.end > range.start &&
         (source->bytes[range.end - 1] == ' ' || source->bytes[range.end - 1] == '\t')) {
    range.end--;
  }
  return range;
}

static int previous_content_line(const Source *source, size_t declaration_start,
                                 TextRange *content) {
  size_t cursor = line_start(source, declaration_start);
  while (cursor > 0) {
    const TextRange line = previous_line(source, cursor);
    const TextRange trimmed = trim_range(source, line);
    if (trimmed.start < trimmed.end) return *content = trimmed, 1;
    cursor = line.start;
  }
  return 0;
}

static int suppression_line_matches(const Source *source, TextRange line, const char *rule_id) {
  const char *prefix = "// struct-legibility-disable-next ";
  const size_t prefix_length = strlen(prefix);
  const size_t rule_length = strlen(rule_id);
  const size_t required_length = prefix_length + rule_length + 4;
  const size_t line_length = line.end - line.start;
  if (line_length <= required_length) return 0;
  const char *text = source->bytes + line.start;
  if (memcmp(text, prefix, prefix_length) != 0) return 0;
  if (memcmp(text + prefix_length, rule_id, rule_length) != 0) return 0;
  return memcmp(text + prefix_length + rule_length, " -- ", 4) == 0;
}

static int declaration_suppresses(const Source *source, TSNode node, const char *rule_id) {
  TextRange line = {0};
  if (!previous_content_line(source, ts_node_start_byte(node), &line)) return 0;
  return suppression_line_matches(source, line, rule_id);
}

static SlStatus append_call(FunctionFact *function, char *name) {
  if (name == NULL) return SL_OUT_OF_MEMORY;
  const size_t size = (function->call_count + 1) * sizeof(*function->calls);
  char **calls = realloc(function->calls, size);
  if (calls == NULL) return free(name), SL_OUT_OF_MEMORY;
  function->calls = calls;
  function->calls[function->call_count++] = name;
  return SL_OK;
}

static SlStatus collect_calls(TSNode node, const Source *source, const SlLanguagePack *pack,
                              FunctionFact *function) {
  const TSNode called_name = pack->called_name_node(node);
  if (!ts_node_is_null(called_name)) {
    const SlStatus status = append_call(function, node_text(source, called_name));
    if (status != SL_OK) return status;
  }
  const uint32_t count = ts_node_named_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    const SlStatus status = collect_calls(ts_node_named_child(node, index), source, pack, function);
    if (status != SL_OK) return status;
  }
  return SL_OK;
}

static const char *fact_kind(SlDeclarationKind kind) {
  if (kind == SL_DECLARATION_IMPORT) return "import";
  if (kind == SL_DECLARATION_TYPE) return "type";
  if (kind == SL_DECLARATION_CONSTANT) return "constant";
  if (kind == SL_DECLARATION_FUNCTION) return "function";
  return NULL;
}

static void declaration_fact_free(SlDeclarationFact *fact) {
  free(fact->name);
  for (size_t index = 0; index < fact->call_count; index++)
    free(fact->calls[index]);
  free(fact->calls);
  *fact = (SlDeclarationFact){0};
}

static SlStatus collect_fact_calls(TSNode node, const Source *source, const SlLanguagePack *pack,
                                   SlDeclarationFact *fact) {
  if (strcmp(fact->kind, "function") != 0) return SL_OK;
  FunctionFact function = {0};
  const SlStatus status = collect_calls(node, source, pack, &function);
  fact->calls = function.calls;
  fact->call_count = function.call_count;
  return status;
}

static SlStatus append_declaration(SlFileFact *file, SlDeclarationFact fact) {
  const size_t size = (file->declaration_count + 1) * sizeof(*file->declarations);
  SlDeclarationFact *declarations = realloc(file->declarations, size);
  if (declarations == NULL) return SL_OUT_OF_MEMORY;
  file->declarations = declarations;
  file->declarations[file->declaration_count++] = fact;
  return SL_OK;
}

static SlStatus collect_declaration_fact(TSNode input, const Source *source,
                                         const SlLanguagePack *pack, SlFileFact *file) {
  const SlDeclarationKind declaration_kind = pack->declaration_kind(input);
  const char *kind = fact_kind(declaration_kind);
  if (kind == NULL) return SL_OK;
  const TSNode node = pack->declaration_node(input);
  const TSNode name_node = pack->name_node(node);
  const TSPoint point = ts_node_start_point(input);
  char *name = ts_node_is_null(name_node) ? copy_string("") : node_text(source, name_node);
  SlDeclarationFact fact = {kind, name, pack->is_exported(input), point.row + 1, point.column + 1,
                            NULL, 0};
  if (fact.name == NULL) return SL_OUT_OF_MEMORY;
  const SlStatus call_status = collect_fact_calls(node, source, pack, &fact);
  if (call_status != SL_OK) return declaration_fact_free(&fact), call_status;
  const SlStatus status = append_declaration(file, fact);
  if (status != SL_OK) declaration_fact_free(&fact);
  return status;
}

static void file_fact_free(SlFileFact *file) {
  free(file->path);
  for (size_t index = 0; index < file->declaration_count; index++) {
    declaration_fact_free(&file->declarations[index]);
  }
  free(file->declarations);
  *file = (SlFileFact){0};
}

static SlStatus append_file_fact(SlReport *report, SlFileFact file) {
  const size_t size = (report->file_count + 1) * sizeof(*report->files);
  SlFileFact *files = realloc(report->files, size);
  if (files == NULL) return SL_OUT_OF_MEMORY;
  report->files = files;
  report->files[report->file_count++] = file;
  return SL_OK;
}

static SlStatus collect_file_fact(const char *path, TSNode root, const Source *source,
                                  const SlLanguagePack *pack, SlReport *report) {
  SlFileFact file = {.path = copy_string(path)};
  if (file.path == NULL) return SL_OUT_OF_MEMORY;
  const uint32_t count = ts_node_named_child_count(root);
  for (uint32_t index = 0; index < count; index++) {
    const SlStatus status =
        collect_declaration_fact(ts_node_named_child(root, index), source, pack, &file);
    if (status != SL_OK) return file_fact_free(&file), status;
  }
  const SlStatus status = append_file_fact(report, file);
  if (status != SL_OK) file_fact_free(&file);
  return status;
}

static SlStatus append_function(FunctionList *functions, FunctionFact function) {
  const size_t size = (functions->count + 1) * sizeof(*functions->items);
  FunctionFact *items = realloc(functions->items, size);
  if (items == NULL) return SL_OUT_OF_MEMORY;
  functions->items = items;
  functions->items[functions->count++] = function;
  return SL_OK;
}

static void function_free(FunctionFact *function) {
  free(function->name);
  for (size_t index = 0; index < function->call_count; index++)
    free(function->calls[index]);
  free(function->calls);
  *function = (FunctionFact){0};
}

static SlStatus collect_function(TSNode input, const Source *source, const SlLanguagePack *pack,
                                 FunctionList *functions) {
  if (pack->declaration_kind(input) != SL_DECLARATION_FUNCTION) return SL_OK;
  const TSNode node = pack->declaration_node(input);
  const TSNode name_node = pack->name_node(node);
  const int exported = pack->is_exported(input);
  const int suppressed = declaration_suppresses(source, input, "function-order");
  FunctionFact function = {node_text(source, name_node), node, NULL, 0, exported, suppressed};
  if (function.name == NULL) return SL_OUT_OF_MEMORY;
  const SlStatus call_status = collect_calls(node, source, pack, &function);
  if (call_status != SL_OK) return function_free(&function), call_status;
  const SlStatus status = append_function(functions, function);
  if (status != SL_OK) function_free(&function);
  return status;
}

static SlStatus collect_functions(TSNode root, const Source *source, const SlLanguagePack *pack,
                                  FunctionList *functions) {
  const uint32_t count = ts_node_named_child_count(root);
  for (uint32_t index = 0; index < count; index++) {
    const SlStatus status =
        collect_function(ts_node_named_child(root, index), source, pack, functions);
    if (status != SL_OK) return status;
  }
  return SL_OK;
}

static void function_list_free(FunctionList *functions) {
  for (size_t index = 0; index < functions->count; index++)
    function_free(&functions->items[index]);
  free(functions->items);
  *functions = (FunctionList){0};
}

static size_t hash_name(const char *name) {
  size_t hash = 1469598103934665603ULL;
  for (const unsigned char *byte = (const unsigned char *)name; *byte != '\0'; byte++) {
    hash = (hash ^ *byte) * 1099511628211ULL;
  }
  return hash;
}

static size_t index_capacity(size_t count) {
  size_t capacity = 8;
  while (capacity < count * 2)
    capacity *= 2;
  return capacity;
}

static void name_index_insert(NameIndex *index, FunctionFact *function) {
  size_t slot = hash_name(function->name) & (index->capacity - 1);
  while (index->slots[slot].name != NULL)
    slot = (slot + 1) & (index->capacity - 1);
  index->slots[slot] = (NameSlot){function->name, function};
}

static SlStatus name_index_build(FunctionList *functions, NameIndex *index) {
  index->capacity = index_capacity(functions->count);
  index->slots = calloc(index->capacity, sizeof(*index->slots));
  if (index->slots == NULL) return SL_OUT_OF_MEMORY;
  for (size_t item = 0; item < functions->count; item++) {
    name_index_insert(index, &functions->items[item]);
  }
  return SL_OK;
}

static FunctionFact *name_index_find(const NameIndex *index, const char *name) {
  size_t slot = hash_name(name) & (index->capacity - 1);
  while (index->slots[slot].name != NULL) {
    if (strcmp(index->slots[slot].name, name) == 0) return index->slots[slot].function;
    slot = (slot + 1) & (index->capacity - 1);
  }
  return NULL;
}

static void component_search_free(ComponentSearch *search) {
  free(search->indices);
  free(search->lowlinks);
  free(search->components);
  free(search->stack);
  free(search->on_stack);
  *search = (ComponentSearch){0};
}

static int component_search_alloc(ComponentSearch *search, size_t count) {
  search->indices = malloc(count * sizeof(*search->indices));
  search->lowlinks = malloc(count * sizeof(*search->lowlinks));
  search->components = malloc(count * sizeof(*search->components));
  search->stack = malloc(count * sizeof(*search->stack));
  search->on_stack = calloc(count, sizeof(*search->on_stack));
  return search->indices != NULL && search->lowlinks != NULL && search->components != NULL &&
         search->stack != NULL && search->on_stack != NULL;
}

static SlStatus component_search_init(ComponentSearch *search, FunctionList *functions,
                                      const NameIndex *names) {
  *search = (ComponentSearch){.functions = functions, .names = names};
  if (functions->count == 0) return SL_OK;
  if (!component_search_alloc(search, functions->count)) {
    return component_search_free(search), SL_OUT_OF_MEMORY;
  }
  for (size_t index = 0; index < functions->count; index++) {
    search->indices[index] = SIZE_MAX;
    search->components[index] = SIZE_MAX;
  }
  return SL_OK;
}

static size_t function_index(const ComponentSearch *search, const FunctionFact *function) {
  return (size_t)(function - search->functions->items);
}

static void component_visit(ComponentSearch *search, size_t current);

static void lower_lowlink(ComponentSearch *search, size_t current, size_t candidate) {
  if (candidate < search->lowlinks[current]) search->lowlinks[current] = candidate;
}

static void component_visit_edge(ComponentSearch *search, size_t current, const char *call) {
  FunctionFact *target = name_index_find(search->names, call);
  if (target == NULL) return;
  const size_t next = function_index(search, target);
  if (search->indices[next] == SIZE_MAX) {
    component_visit(search, next);
    lower_lowlink(search, current, search->lowlinks[next]);
    return;
  }
  if (search->on_stack[next]) lower_lowlink(search, current, search->indices[next]);
}

static void component_pop(ComponentSearch *search, size_t root) {
  size_t member = SIZE_MAX;
  while (member != root) {
    member = search->stack[--search->stack_count];
    search->on_stack[member] = 0;
    search->components[member] = search->next_component;
  }
  search->next_component++;
}

static void component_visit(ComponentSearch *search, size_t current) {
  search->indices[current] = search->next_index;
  search->lowlinks[current] = search->next_index++;
  search->stack[search->stack_count++] = current;
  search->on_stack[current] = 1;
  FunctionFact *function = &search->functions->items[current];
  for (size_t call = 0; call < function->call_count; call++) {
    component_visit_edge(search, current, function->calls[call]);
  }
  if (search->lowlinks[current] == search->indices[current]) component_pop(search, current);
}

static SlStatus assign_components(FunctionList *functions, const NameIndex *names,
                                  ComponentSearch *search) {
  const SlStatus status = component_search_init(search, functions, names);
  if (status != SL_OK) return status;
  for (size_t index = 0; index < functions->count; index++) {
    if (search->indices[index] == SIZE_MAX) component_visit(search, index);
  }
  return SL_OK;
}

static SlStatus add_function_diagnostic(const char *path, const FunctionFact *target,
                                        const FunctionFact *caller, SlReport *report) {
  char message[192];
  snprintf(message, sizeof(message), "%s must appear below caller %s", target->name, caller->name);
  const TSPoint point = ts_node_start_point(target->node);
  SlDiagnostic diagnostic = {copy_string(path), point.row + 1,        point.column + 1,
                             "function-order",  copy_string(message), 0};
  if (diagnostic.path != NULL && diagnostic.message != NULL) {
    return append_diagnostic(report, diagnostic);
  }
  free(diagnostic.path);
  free(diagnostic.message);
  return SL_OUT_OF_MEMORY;
}

static SlStatus analyze_function_calls(const char *path, FunctionFact *caller,
                                       const ComponentSearch *search, SlReport *report) {
  for (size_t call = 0; call < caller->call_count; call++) {
    FunctionFact *target = name_index_find(search->names, caller->calls[call]);
    if (target == NULL || ts_node_start_byte(target->node) >= ts_node_start_byte(caller->node))
      continue;
    if (target->suppress_function_order) continue;
    const size_t caller_index = function_index(search, caller);
    const size_t target_index = function_index(search, target);
    if (search->components[caller_index] == search->components[target_index]) continue;
    return add_function_diagnostic(path, target, caller, report);
  }
  return SL_OK;
}

static SlStatus add_export_diagnostic(const char *path, const FunctionFact *internal,
                                      const FunctionFact *exported, SlReport *report) {
  char message[192];
  snprintf(message, sizeof(message), "%s must appear below exported function %s", internal->name,
           exported->name);
  const TSPoint point = ts_node_start_point(internal->node);
  SlDiagnostic diagnostic = {copy_string(path), point.row + 1,        point.column + 1,
                             "function-order",  copy_string(message), 0};
  if (diagnostic.path != NULL && diagnostic.message != NULL) {
    return append_diagnostic(report, diagnostic);
  }
  free(diagnostic.path);
  free(diagnostic.message);
  return SL_OUT_OF_MEMORY;
}

static SlStatus analyze_export_order(const char *path, const FunctionList *functions,
                                     const ComponentSearch *search, SlReport *report) {
  const FunctionFact *next_export = NULL;
  for (size_t index = functions->count; index-- > 0;) {
    const FunctionFact *function = &functions->items[index];
    if (function->suppress_function_order) continue;
    if (function->exported) {
      next_export = function;
      continue;
    }
    if (next_export == NULL) continue;
    const size_t current = search->components[function_index(search, function)];
    const size_t exported = search->components[function_index(search, next_export)];
    if (current == exported) continue;
    return add_export_diagnostic(path, function, next_export, report);
  }
  return SL_OK;
}

static SlStatus diagnose_function_order(const char *path, FunctionList *functions,
                                        const ComponentSearch *search, SlReport *report) {
  const size_t initial_diagnostics = report->count;
  for (size_t item = 0; item < functions->count; item++) {
    const SlStatus status = analyze_function_calls(path, &functions->items[item], search, report);
    if (status != SL_OK) return status;
  }
  if (report->count != initial_diagnostics) return SL_OK;
  return analyze_export_order(path, functions, search, report);
}

static SlStatus analyze_function_order(const char *path, TSNode root, const Source *source,
                                       const SlLanguagePack *pack, SlReport *report) {
  FunctionList functions = {0};
  const SlStatus collect_status = collect_functions(root, source, pack, &functions);
  if (collect_status != SL_OK) return function_list_free(&functions), collect_status;
  if (functions.count < 2) return function_list_free(&functions), SL_OK;
  NameIndex index = {0};
  SlStatus status = name_index_build(&functions, &index);
  ComponentSearch search = {0};
  if (status == SL_OK) status = assign_components(&functions, &index, &search);
  if (status == SL_OK) status = diagnose_function_order(path, &functions, &search, report);
  component_search_free(&search);
  free(index.slots);
  function_list_free(&functions);
  return status;
}

static SlStatus add_order_diagnostic(const char *path, TSNode node, SlDeclarationKind current,
                                     SlDeclarationKind previous, SlReport *report) {
  char message[128];
  snprintf(message, sizeof(message), "%s must appear before %s", section_name(current),
           section_name(previous));
  const TSPoint point = ts_node_start_point(node);
  SlDiagnostic diagnostic = {copy_string(path), point.row + 1,        point.column + 1,
                             "section-order",   copy_string(message), 0};
  if (diagnostic.path != NULL && diagnostic.message != NULL) {
    return append_diagnostic(report, diagnostic);
  }
  free(diagnostic.path);
  free(diagnostic.message);
  return SL_OUT_OF_MEMORY;
}

static SlStatus analyze_root(const char *path, TSNode root, const Source *source,
                             const SlLanguagePack *pack, SlReport *report) {
  SlDeclarationKind highest = SL_DECLARATION_NONE;
  const uint32_t count = ts_node_named_child_count(root);
  for (uint32_t index = 0; index < count; index++) {
    const TSNode node = ts_node_named_child(root, index);
    const SlDeclarationKind section = pack->declaration_kind(node);
    if (section == SL_DECLARATION_NONE) continue;
    const int out_of_order = section < highest;
    const int suppressed = declaration_suppresses(source, node, "section-order");
    if (out_of_order && !suppressed) {
      const SlStatus status = add_order_diagnostic(path, node, section, highest, report);
      if (status != SL_OK) return status;
    }
    if (section > highest) highest = section;
  }
  return analyze_function_order(path, root, source, pack, report);
}

static int compare_diagnostics(const void *left_value, const void *right_value) {
  const SlDiagnostic *left = left_value;
  const SlDiagnostic *right = right_value;
  const int path_order = strcmp(left->path, right->path);
  if (path_order != 0) return path_order;
  if (left->line != right->line) return left->line < right->line ? -1 : 1;
  if (left->column != right->column) return left->column < right->column ? -1 : 1;
  return strcmp(left->rule_id, right->rule_id);
}

static void sort_diagnostics(SlReport *report) {
  if (report->count < 2) return;
  qsort(report->diagnostics, report->count, sizeof(*report->diagnostics), compare_diagnostics);
}

static int find_error_node(TSNode node, TSNode *error) {
  if (node_is(node, "ERROR") || ts_node_is_missing(node)) return *error = node, 1;
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t index = 0; index < count; index++) {
    if (find_error_node(ts_node_child(node, index), error)) return 1;
  }
  return 0;
}

static SlStatus add_parse_diagnostic(const char *path, TSNode root, const SlLanguagePack *pack,
                                     SlReport *report) {
  TSNode error = root;
  find_error_node(root, &error);
  const TSPoint point = ts_node_start_point(error);
  SlDiagnostic diagnostic = {copy_string(path),
                             point.row + 1,
                             point.column + 1,
                             "parse-error",
                             copy_string(pack->parse_error_message),
                             1};
  if (diagnostic.path != NULL && diagnostic.message != NULL) {
    return append_diagnostic(report, diagnostic);
  }
  free(diagnostic.path);
  free(diagnostic.message);
  return SL_OUT_OF_MEMORY;
}

static SlStatus collect_requested_facts(const char *path, TSNode root, const Source *source,
                                        const SlLanguagePack *pack, int collect_facts,
                                        SlReport *report) {
  if (!collect_facts) return SL_OK;
  return collect_file_fact(path, root, source, pack, report);
}

static SlStatus analyze_parsed_file(const char *path, TSNode root, const Source *source,
                                    const SlLanguagePack *pack, int collect_facts,
                                    SlReport *report) {
  if (ts_node_has_error(root)) return add_parse_diagnostic(path, root, pack, report);
  const SlStatus fact_status =
      collect_requested_facts(path, root, source, pack, collect_facts, report);
  if (fact_status != SL_OK) return fact_status;
  return analyze_root(path, root, source, pack, report);
}

static SlStatus analyze_file(const char *path, TSParser *parser, int collect_facts,
                             SlReport *report) {
  const SlLanguagePack *pack = sl_language_for_path(path);
  if (pack == NULL) return SL_INVALID_ARGUMENT;
  const TSLanguage *language = pack->tree_sitter_language();
  const int language_changed = ts_parser_language(parser) != language;
  if (language_changed && !ts_parser_set_language(parser, language)) return SL_INVALID_ARGUMENT;
  Source source = {0};
  const SlStatus read_status = read_source(path, &source);
  if (read_status != SL_OK) return read_status;
  TSTree *tree = ts_parser_parse_string(parser, NULL, source.bytes, (uint32_t)source.length);
  if (tree == NULL) return free(source.bytes), SL_OUT_OF_MEMORY;
  const TSNode root = ts_tree_root_node(tree);
  const SlStatus status = analyze_parsed_file(path, root, &source, pack, collect_facts, report);
  ts_tree_delete(tree);
  free(source.bytes);
  return status;
}

static int request_is_valid(const SlRequest *request, const SlReport *report) {
  if (request == NULL || report == NULL) return 0;
  if (request->paths == NULL) return 0;
  return request->path_count > 0;
}

static SlStatus create_parser(TSParser **parser) {
  *parser = ts_parser_new();
  if (*parser == NULL) return SL_OUT_OF_MEMORY;
  return SL_OK;
}

static void *run_analysis_worker(void *value) {
  AnalysisWorker *worker = value;
  TSParser *parser = NULL;
  worker->status = create_parser(&parser);
  for (size_t index = worker->index; index < worker->files->count && worker->status == SL_OK;
       index += worker->count) {
    worker->status = analyze_file(worker->files->paths[index], parser,
                                  worker->request->collect_facts, &worker->reports[index]);
  }
  ts_parser_delete(parser);
  return NULL;
}

static size_t analysis_worker_count(size_t file_count) {
  const long processor_count = sysconf(_SC_NPROCESSORS_ONLN);
  size_t count = processor_count > 0 ? (size_t)processor_count : 1;
  if (count > 8) count = 8;
  if (count > file_count) count = file_count;
  return count;
}

static void analysis_pool_free(AnalysisPool *pool) {
  for (size_t index = 0; index < pool->file_count; index++)
    sl_report_free(&pool->reports[index]);
  free(pool->reports);
  free(pool->threads);
  free(pool->workers);
  *pool = (AnalysisPool){0};
}

static SlStatus analysis_pool_init(const SlRequest *request, const SlFileList *files,
                                   AnalysisPool *pool) {
  pool->worker_count = analysis_worker_count(files->count);
  pool->file_count = files->count;
  pool->reports = calloc(files->count, sizeof(*pool->reports));
  pool->workers = calloc(pool->worker_count, sizeof(*pool->workers));
  pool->threads = calloc(pool->worker_count, sizeof(*pool->threads));
  if (pool->reports == NULL || pool->workers == NULL || pool->threads == NULL)
    return analysis_pool_free(pool), SL_OUT_OF_MEMORY;
  for (size_t index = 0; index < pool->worker_count; index++) {
    pool->workers[index] =
        (AnalysisWorker){request, files, pool->reports, index, pool->worker_count, SL_OK};
  }
  return SL_OK;
}

static SlStatus spawn_analysis_workers(AnalysisPool *pool, size_t *created) {
  *created = 0;
  for (size_t index = 1; index < pool->worker_count; index++) {
    const int result =
        pthread_create(&pool->threads[*created], NULL, run_analysis_worker, &pool->workers[index]);
    if (result != 0) return SL_OUT_OF_MEMORY;
    (*created)++;
  }
  return SL_OK;
}

static SlStatus join_analysis_workers(AnalysisPool *pool, size_t created) {
  SlStatus status = SL_OK;
  for (size_t index = 0; index < created; index++) {
    if (pthread_join(pool->threads[index], NULL) != 0) status = SL_IO_ERROR;
  }
  return status;
}

static SlStatus analysis_worker_status(const AnalysisPool *pool) {
  for (size_t index = 0; index < pool->worker_count; index++) {
    if (pool->workers[index].status != SL_OK) return pool->workers[index].status;
  }
  return SL_OK;
}

static SlStatus run_analysis_workers(AnalysisPool *pool) {
  size_t created = 0;
  const SlStatus spawn_status = spawn_analysis_workers(pool, &created);
  run_analysis_worker(&pool->workers[0]);
  const SlStatus join_status = join_analysis_workers(pool, created);
  if (spawn_status != SL_OK) return spawn_status;
  if (join_status != SL_OK) return join_status;
  return analysis_worker_status(pool);
}

static SlStatus move_diagnostics(SlReport *destination, SlReport *source) {
  if (source->count == 0) return SL_OK;
  const size_t count = destination->count + source->count;
  SlDiagnostic *items = realloc(destination->diagnostics, count * sizeof(*items));
  if (items == NULL) return SL_OUT_OF_MEMORY;
  memcpy(items + destination->count, source->diagnostics,
         source->count * sizeof(*source->diagnostics));
  free(source->diagnostics);
  destination->diagnostics = items;
  destination->count = count;
  source->diagnostics = NULL;
  source->count = 0;
  return SL_OK;
}

static SlStatus move_file_facts(SlReport *destination, SlReport *source) {
  if (source->file_count == 0) return SL_OK;
  const size_t count = destination->file_count + source->file_count;
  SlFileFact *items = realloc(destination->files, count * sizeof(*items));
  if (items == NULL) return SL_OUT_OF_MEMORY;
  memcpy(items + destination->file_count, source->files,
         source->file_count * sizeof(*source->files));
  free(source->files);
  destination->files = items;
  destination->file_count = count;
  source->files = NULL;
  source->file_count = 0;
  return SL_OK;
}

static SlStatus merge_analysis_reports(AnalysisPool *pool, SlReport *report) {
  for (size_t index = 0; index < pool->file_count; index++) {
    SlStatus status = move_diagnostics(report, &pool->reports[index]);
    if (status == SL_OK) status = move_file_facts(report, &pool->reports[index]);
    if (status != SL_OK) return status;
  }
  return SL_OK;
}

static SlStatus analyze_files(const SlRequest *request, const SlFileList *files, SlReport *report) {
  AnalysisPool pool = {0};
  SlStatus status = analysis_pool_init(request, files, &pool);
  if (status == SL_OK) status = run_analysis_workers(&pool);
  if (status == SL_OK) status = merge_analysis_reports(&pool, report);
  analysis_pool_free(&pool);
  return status;
}

SlStatus sl_analyze(const SlRequest *request, SlReport *report) {
  if (!request_is_valid(request, report)) return SL_INVALID_ARGUMENT;
  *report = (SlReport){0};
  SlFileList files = {0};
  const SlStatus discovery_status =
      sl_discover_files(request->paths, request->path_count, request->use_gitignore, &files);
  if (discovery_status != SL_OK) return discovery_status;
  SlStatus status = analyze_files(request, &files, report);
  if (status == SL_OK) sort_diagnostics(report);
  sl_file_list_free(&files);
  if (status != SL_OK) sl_report_free(report);
  return status;
}

void sl_report_free(SlReport *report) {
  if (report == NULL) return;
  for (size_t index = 0; index < report->count; index++) {
    free(report->diagnostics[index].path);
    free(report->diagnostics[index].message);
  }
  for (size_t index = 0; index < report->file_count; index++) {
    file_fact_free(&report->files[index]);
  }
  free(report->diagnostics);
  free(report->files);
  *report = (SlReport){0};
}
