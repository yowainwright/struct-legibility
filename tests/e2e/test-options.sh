#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="$1"
fixture="$repo_root/tests/fixtures/typescript/section-order.ts"
version="$(node -p "require('$repo_root/package.json').version")"

test "$(STRUCT_LINT_PROFILE=missing "$binary" --version)" = "struct-lint $version"
help="$(STRUCT_LINT_PROFILE=missing "$binary" --help)"
[[ "$help" == 'usage: struct-lint '* ]]
for option in --profile --format --no-ignore --help --version; do
  [[ "$help" == *"$option"* ]]
done

status=0
output="$(STRUCT_LINT_PROFILE=missing "$binary" --profile ci "$fixture" 2>&1)" || status=$?
test "$status" -eq 1
test "$output" = "$fixture:2:1: error[section-order] imports must appear before public types"

status=0
STRUCT_LINT_PROFILE=missing "$binary" "$fixture" >/dev/null 2>&1 || status=$?
test "$status" -eq 2
