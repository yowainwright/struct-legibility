BEGIN {
  in_context = 0
  registered = 0
  exit_code = 0
  context_replaced = 0
  loader_removed = 0
  evaluation_replaced = 0
  loop_guarded = 0
}

/^#include "quickjs-libc.h"$/ {
  print
  print "#include \"quickjs_bridge.h\""
  next
}

/^  JSContext \*ctx = JS_NewContext\(rt\);$/ {
  print "  JSContext *ctx = sl_quickjs_new_context(rt);"
  context_replaced = 1
  next
}

/^  JS_SetModuleLoaderFunc2/ {
  loader_removed = 1
  next
}

/^    extern JSModuleDef \*js_init_module_(std|os)/ { next }
/^    js_init_module_(std|os)/ { next }

/^  js_std_eval_binary\(ctx, .* 0\);$/ {
  sub("js_std_eval_binary", "r = sl_quickjs_eval_binary")
  print
  evaluation_replaced = 1
  next
}

/^  r = js_std_loop\(ctx\);$/ {
  print "  if (r == 0)"
  print "    r = js_std_loop(ctx);"
  loop_guarded = 1
  next
}

/^static JSContext \*JS_NewCustomContext\(JSRuntime \*rt\)$/ { in_context = 1 }

in_context && /^  return ctx;$/ {
  print "  sl_quickjs_register(ctx);"
  registered = 1
}

in_context && /^}$/ { in_context = 0 }

/^  return r;$/ {
  print "  if (r != 0)"
  print "    r = 2;"
  print "  else"
  print "    r = sl_quickjs_exit_code();"
  exit_code = 1
}

{ print }

END {
  if (!registered || !exit_code || !context_replaced || !loader_removed) exit 1
  if (!evaluation_replaced || !loop_guarded) exit 1
}
