#!/usr/bin/env bash
set -euo pipefail

output_file=""
input_file=""
temporary_dir=""

cleanup() {
  if [ -n "$temporary_dir" ]; then rm -rf "$temporary_dir"; fi
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      -o)
        shift
        output_file="${1:-}"
        ;;
      *)
        input_file="$1"
        ;;
    esac
    shift
  done
  if [ -z "$output_file" ] || [ -z "$input_file" ]; then return 1; fi
}

repo_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

resolve_tqs_root() {
  local root
  root="$(repo_root)"
  if [ -n "${TQS_ROOT:-}" ]; then printf '%s\n' "$TQS_ROOT"; return; fi
  if [ -d "$root/../tqs/deps/quickjs-ng" ]; then printf '%s\n' "$root/../tqs"; return; fi
  return 1
}

patch_generated_c() {
  local generated="$1"
  local patched="$2"
  awk '
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
  ' "$generated" > "$patched"
}

build_qjsc() {
  local root="$1"
  local tqs_root="$2"
  local build_dir
  build_dir="${SL_BUILD_DIR:-$root/.build/native}"
  cmake -S "$root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSL_QUICKJS_SOURCE= \
    -DSL_TQS_ROOT="$tqs_root" >&2
  cmake --build "$build_dir" --target struct-legibility-qjsc --parallel >&2
  printf '%s\n' "$build_dir/struct-legibility-qjsc"
}

resolve_qjsc() {
  local root="$1"
  local tqs_root="$2"
  if [ -n "${TQS_QJSC_BIN:-}" ]; then printf '%s\n' "$TQS_QJSC_BIN"; return; fi
  if [ -x "$tqs_root/bin/qjsc" ]; then printf '%s\n' "$tqs_root/bin/qjsc"; return; fi
  build_qjsc "$root" "$tqs_root"
}

build_binary() {
  local root="$1"
  local tqs_root="$2"
  local generated="$3"
  local build_dir stable_source
  build_dir="${SL_BUILD_DIR:-$root/.build/native}"
  stable_source="$build_dir/generated/program.c"
  mkdir -p "$(dirname "$stable_source")"
  cp "$generated" "$stable_source"
  cmake -S "$root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSL_QUICKJS_SOURCE="$stable_source" \
    -DSL_TQS_ROOT="$tqs_root"
  cmake --build "$build_dir" --target struct-legibility-tqs --parallel
  cp "$build_dir/struct-legibility-tqs" "$output_file"
  chmod 755 "$output_file"
}

main() {
  parse_args "$@" || exit 2
  local root tqs_root qjsc
  root="$(repo_root)"
  tqs_root="$(resolve_tqs_root)"
  qjsc="$(resolve_qjsc "$root" "$tqs_root")"
  if [ ! -x "$qjsc" ]; then echo "tqs qjsc not found: $qjsc" >&2; exit 2; fi
  temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/struct-legibility-qjsc.XXXXXX")"
  trap cleanup EXIT
  "$qjsc" -e -o "$temporary_dir/program.c" "$input_file"
  patch_generated_c "$temporary_dir/program.c" "$temporary_dir/program-patched.c"
  build_binary "$root" "$tqs_root" "$temporary_dir/program-patched.c"
}

main "$@"
