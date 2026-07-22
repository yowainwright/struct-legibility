#!/usr/bin/env bash
set -euo pipefail

output_file=""
input_file=""
temporary_dir=""

set_input_file() {
  local value="$1"
  if [ -n "$input_file" ] || [ -z "$value" ]; then return 1; fi
  case "$value" in -*) return 1 ;; esac
  input_file="$value"
}

set_output_file() {
  local value="$1"
  if [ -n "$output_file" ] || [ -z "$value" ]; then return 1; fi
  case "$value" in -*) return 1 ;; esac
  output_file="$value"
}

cleanup() {
  if [ -n "$temporary_dir" ]; then rm -rf "$temporary_dir"; fi
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      -o)
        if [ "$#" -lt 2 ]; then return 1; fi
        set_output_file "$2" || return 1
        shift 2
        ;;
      -*) return 1 ;;
      *)
        set_input_file "$1" || return 1
        shift
        ;;
    esac
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
  local root
  root="$(repo_root)"
  awk -f "$root/scripts/patch-qjsc.awk" "$generated" > "$patched"
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
