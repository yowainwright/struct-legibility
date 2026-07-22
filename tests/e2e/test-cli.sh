#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary_input="${1:-$repo_root/build/struct-legibility}"
binary="$(cd "$(dirname "$binary_input")" && pwd)/$(basename "$binary_input")"

assert_result() {
  local profile="$1"
  local expected_status="$2"
  local expected_output="$3"
  shift 3
  set +e
  local output
  output="$($binary --profile "$profile" "$@" 2>&1)"
  local status=$?
  set -e
  if [ "$status" -eq "$expected_status" ] && [ "$output" = "$expected_output" ]; then return; fi
  printf 'unexpected result\nexpected status: %s\nactual status:   %s\nexpected: %s\nactual:   %s\n' \
    "$expected_status" "$status" "$expected_output" "$output" >&2
  exit 1
}

fixture="$repo_root/tests/fixtures/typescript/section-order.ts"
expected="$fixture:2:1: error[section-order] imports must appear before public types"
assert_result ci 1 "$expected" "$fixture"

expected="$fixture:2:1: warning[section-order] imports must appear before public types"
assert_result local 0 "$expected" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/recursive-cycle.ts"
assert_result ci 0 "" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/function-order.ts"
expected="$fixture:1:1: error[function-order] helper must appear below caller main"
assert_result ci 1 "$expected" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/export-order.ts"
expected="$fixture:1:1: error[function-order] helper must appear below exported function main"
assert_result ci 1 "$expected" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/multiple-errors.ts"
expected="$fixture:1:1: error[function-order] helper must appear below caller main
$fixture:5:1: error[section-order] public types must appear before functions"
assert_result ci 1 "$expected" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/section-order.ts"
expected="$fixture:2:1: error[section-order] imports must appear before public types"
set +e
output="$(STRUCT_LEGIBILITY_PROFILE=ci "$binary" "$fixture" 2>&1)"
status=$?
set -e
if [ "$status" -ne 1 ] || [ "$output" != "$expected" ]; then
  printf 'environment profile was not applied\n%s\n' "$output" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/parse-error.ts"
expected="$fixture:1:1: error[parse-error] could not parse TypeScript source"
assert_result local 1 "$expected" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/section-order.ts"
expected="{\"diagnostics\":[{\"ruleId\":\"section-order\",\"severity\":\"error\",\"path\":\"$fixture\",\"line\":2,\"column\":1,\"message\":\"imports must appear before public types\",\"related\":null}],\"summary\":{\"errors\":1,\"warnings\":0}}"
assert_result ci 1 "$expected" --format json "$fixture"

project="$repo_root/tests/fixtures/typescript/project"
fixture="$project/src/main.ts"
expected="$fixture:2:1: error[section-order] imports must appear before public types"
assert_result ci 1 "$expected" "$project"

set +e
output="$(cd "$project" && "$binary" --profile ci 2>&1)"
status=$?
set -e
expected="./src/main.ts:2:1: error[section-order] imports must appear before public types"
if [ "$status" -ne 1 ] || [ "$output" != "$expected" ]; then
  printf 'current directory default was not applied\n%s\n' "$output" >&2
  exit 1
fi

generated="$project/generated/ignored.ts"
hidden="$project/.hidden/ignored.ts"
dependency="$project/node_modules/ignored.ts"
expected="$hidden:2:1: error[section-order] imports must appear before public types
$generated:2:1: error[section-order] imports must appear before public types
$dependency:2:1: error[section-order] imports must appear before public types
$fixture:2:1: error[section-order] imports must appear before public types"
assert_result ci 1 "$expected" --no-ignore "$project"

fixture="$repo_root/tests/fixtures/typescript/suppressed-function.ts"
assert_result ci 0 "" "$fixture"

fixture="$repo_root/tests/fixtures/typescript/suppressed-section.ts"
assert_result ci 0 "" "$fixture"
