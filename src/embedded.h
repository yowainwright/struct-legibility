#ifndef STRUCT_LINT_EMBEDDED_H
#define STRUCT_LINT_EMBEDDED_H

#include "language.h"

typedef struct {
  TSRange range;
  const SlLanguagePack *pack;
} SlScriptRegion;

typedef struct {
  SlScriptRegion *items;
  uint32_t count;
} SlScriptRegions;

SlStatus sl_script_regions(TSNode root, const char *source, const SlLanguagePack *host,
                           SlScriptRegions *regions);

#endif
