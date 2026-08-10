#!/usr/bin/env bash
set -euo pipefail

entry=""
output=""

set_entry() {
  local value="$1"
  if [ -n "$entry" ] || [ -z "$value" ]; then return 1; fi
  case "$value" in -*) return 1 ;; esac
  entry="$value"
}

set_output() {
  local value="$1"
  if [ -n "$output" ] || [ -z "$value" ]; then return 1; fi
  case "$value" in -*) return 1 ;; esac
  output="$value"
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      -o)
        if [ "$#" -lt 2 ]; then return 1; fi
        set_output "$2" || return 1
        shift 2
        ;;
      -*) return 1 ;;
      *)
        set_entry "$1" || return 1
        shift
        ;;
    esac
  done
  if [ -z "$entry" ] || [ -z "$output" ]; then return 1; fi
}

repo_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

build_native_bridge() {
  local root="$1"
  local build_dir
  build_dir="${SL_BUILD_DIR:-$root/.build/native}"
  cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release >&2
  cmake --build "$build_dir" --target struct_legibility_scriptc --parallel >&2
  printf '%s\n' "$build_dir/struct-legibility-ffi.json"
}

main() {
  parse_args "$@" || exit 2
  local root ffi_manifest scriptc
  root="$(repo_root)"
  ffi_manifest="$(build_native_bridge "$root")"
  scriptc="$root/node_modules/.bin/scriptc"
  if [ ! -x "$scriptc" ]; then
    echo "scriptc not installed: $scriptc" >&2
    exit 2
  fi
  "$scriptc" build "$entry" --ffi "$ffi_manifest" --no-keep-c -o "$output"
}

main "$@"
