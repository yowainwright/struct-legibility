#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="$1"
temporary_dir="$(mktemp -d "$repo_root/.build/embedded-cli.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT

write_fixture() {
  local extension="$1"
  case "$extension" in
    vue|svelte) printf '<script>\n' ;;
    astro) printf '%s\n' '---' ;;
  esac
  printf '%s\n\n' 'export function helper() { return 1; }' \
    'export function main() { return helper(); }'
  case "$extension" in
    vue|svelte) printf '</script>\n' ;;
    astro) printf '%s\n' '---' ;;
  esac
}

# More than eight inputs exercises parser reuse across host and script grammars.
for index in 0 1 2 3; do
  for extension in vue svelte astro mdx ts; do
    write_fixture "$extension" >"$temporary_dir/$index.$extension"
  done
done
status=0
output="$(PATH=/nonexistent "$binary" --profile ci --no-ignore "$temporary_dir" 2>&1)" || status=$?
test "$status" -eq 1
test "$(printf '%s\n' "$output" | wc -l | tr -d ' ')" -eq 20
test "$(printf '%s\n' "$output" | grep -c 'error\[function-order\]')" -eq 20
