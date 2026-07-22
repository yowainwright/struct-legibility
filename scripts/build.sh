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

resolve_tqs_root() {
  local root
  root="$(repo_root)"
  if [ -n "${TQS_ROOT:-}" ]; then printf '%s\n' "$TQS_ROOT"; return; fi
  if [ -d "$root/node_modules/@yowainwright/tqs/src" ]; then
    printf '%s\n' "$root/node_modules/@yowainwright/tqs"
    return
  fi
  if [ -d "$root/../tqs/dist" ]; then printf '%s\n' "$root/../tqs"; return; fi
  return 1
}

resolve_tqs_cli() {
  local tqs_root="$1"
  local root="$2"
  if [ -f "$tqs_root/dist/cli/index.js" ]; then
    printf '%s\n' "$tqs_root/dist/cli/index.js"
    return
  fi
  local output_dir="$root/.build/tqs-cli"
  local output="$output_dir/index.js"
  mkdir -p "$output_dir"
  bun build "$tqs_root/src/cli/index.ts" \
    --outfile "$output" \
    --target node \
    --minify \
    --define '__VERSION__="0.0.5"' >&2
  printf '%s\n' "$output"
}

main() {
  parse_args "$@" || exit 2
  local root tqs_root tqs_cli backend
  root="$(repo_root)"
  tqs_root="$(resolve_tqs_root)"
  tqs_cli="$(resolve_tqs_cli "$tqs_root" "$root")"
  backend="$root/scripts/tqs-qjsc.sh"
  TQS_QJSC="$backend" TQS_ROOT="$tqs_root" \
    bun "$tqs_cli" "$entry" -o "$output"
}

main "$@"
