#ifndef STRUCT_LEGIBILITY_QUICKJS_BRIDGE_H
#define STRUCT_LEGIBILITY_QUICKJS_BRIDGE_H

#include "quickjs.h"

JSContext *sl_quickjs_new_context(JSRuntime *runtime);
int sl_quickjs_eval_binary(JSContext *context, const uint8_t *bytes, size_t length, int load_only);
void sl_quickjs_register(JSContext *context);
int sl_quickjs_exit_code(void);

#endif
