#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary_input="${1:-$repo_root/.build/struct-legibility}"
binary="$(cd "$(dirname "$binary_input")" && pwd)/$(basename "$binary_input")"
fixture="$repo_root/tests/fixtures/typescript/section-order.ts"

assert_result() {
  local expected_status="$1"
  local expected_output="$2"
  shift 2
  set +e
  local output
  output="$($binary "$@" 2>&1)"
  local status=$?
  set -e
  if [ "$status" -eq "$expected_status" ] && [ "$output" = "$expected_output" ]; then return; fi
  printf 'unexpected scriptc result\nstatus: %s\noutput: %s\n' "$status" "$output" >&2
  exit 1
}

expected="$fixture:2:1: error[section-order] imports must appear before public types"
assert_result 1 "$expected" --profile ci "$fixture"

expected="$fixture:2:1: warning[section-order] imports must appear before public types"
assert_result 0 "$expected" --profile local "$fixture"

expected="{\"diagnostics\":[{\"ruleId\":\"section-order\",\"severity\":\"error\",\"path\":\"$fixture\",\"line\":2,\"column\":1,\"message\":\"imports must appear before public types\",\"related\":null}],\"summary\":{\"errors\":1,\"warnings\":0}}"
assert_result 1 "$expected" --profile ci --format json "$fixture"

expected="usage: struct-legibility [options] [path...]"
assert_result 2 "$expected" --profile missing "$fixture"

set +e
output="$(STRUCT_LEGIBILITY_PROFILE=missing "$binary" "$fixture" 2>&1)"
status=$?
set -e
if [ "$status" -ne 2 ] || [ "$output" != "$expected" ]; then
  printf 'unknown environment profile was accepted\nstatus: %s\noutput: %s\n' \
    "$status" "$output" >&2
  exit 1
fi

project="$repo_root/tests/fixtures/typescript/project"
set +e
output="$(cd "$project" && "$binary" --profile ci 2>&1)"
status=$?
set -e
expected="./src/main.ts:2:1: error[section-order] imports must appear before public types"
if [ "$status" -eq 1 ] && [ "$output" = "$expected" ]; then exit 0; fi
printf 'current directory default was not applied\n%s\n' "$output" >&2
exit 1
