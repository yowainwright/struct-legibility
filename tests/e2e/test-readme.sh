#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary_input="${1:-$repo_root/.build/struct-lint}"
binary="$(cd "$(dirname "$binary_input")" && pwd)/$(basename "$binary_input")"
mkdir -p "$repo_root/.build"
temporary_dir="$(mktemp -d "$repo_root/.build/readme.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT

extract_example() {
  awk -v wanted="$1" '
    /^```ts$/ { block++; active = block == wanted; next }
    /^```$/ { active = 0 }
    active { print }
  ' "$repo_root/README.md" >"$temporary_dir/main.ts"
}

extract_example 1
cmp "$repo_root/tests/fixtures/typescript/function-order.ts" "$temporary_dir/main.ts"
status=0
output="$(cd "$temporary_dir" && "$binary" --profile ci main.ts 2>&1)" || status=$?
test "$status" -eq 1
expected='main.ts:1:1: error[function-order] helper must appear below caller main'
test "$output" = "$expected"

extract_example 2
cmp "$repo_root/tests/fixtures/readme/main.ts" "$temporary_dir/main.ts"
output="$(cd "$temporary_dir" && PATH=/nonexistent "$binary" --profile ci main.ts 2>&1)"
test -z "$output"
