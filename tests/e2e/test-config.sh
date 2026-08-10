#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:-${TMPDIR:-/tmp}/struct-legibility-custom}"
entry="$repo_root/tests/fixtures/config/custom.ts"
fixture="$repo_root/tests/fixtures/typescript/section-order.ts"

SL_BUILD_DIR="${SL_BUILD_DIR:-${TMPDIR:-/tmp}/struct-legibility-build}" \
  "$repo_root/scripts/build.sh" "$entry" -o "$output"

set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e

expected="$fixture:2:1: error[section-order] imports must appear before public types"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'custom config failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/overrides/section-order.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:2:1: warning[section-order] imports must appear before public types"
if [ "$status" -ne 0 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'ordered override failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

set +e
diagnostic="$($output --profile ci "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:2:1: error[section-order] imports must appear before public types"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'sparse override profile failed\nstatus: %s\noutput: %s\n' \
    "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/function-order.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:1:1: warning[function-order] helper must appear below caller main"
if [ "$status" -ne 0 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'sparse rule severity failed\nstatus: %s\noutput: %s\n' \
    "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/custom-rule.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:1:1: error[entrypoint-name] exported entrypoint launch must be named main"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'custom config failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/arrow-custom-rule.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:1:1: error[entrypoint-name] exported entrypoint launch must be named main"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'arrow function facts failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/re-export.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:1:1: error[entrypoint-name] exported entrypoint launch must be named main"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 're-export facts failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph"
main="$fixture/main.ts"
helper="$fixture/helper.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$main:1:1: error[call-edge] crossFileMain calls helper in $helper"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'import-aware call graph failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-unrelated"
if ! "$output" "$fixture" >/dev/null 2>&1; then
  printf 'unimported call was linked across files\n' >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-shadow"
if ! "$output" "$fixture" >/dev/null 2>&1; then
  printf 'shadowed import was linked across files\n' >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-destructure"
if ! "$output" "$fixture" >/dev/null 2>&1; then
  printf 'destructured shadow was linked across files\n' >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-default"
main="$fixture/main.ts"
helper="$fixture/helper.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$main:1:1: error[call-edge] crossFileMain calls helper in $helper"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'default import call graph failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-export-alias"
main="$fixture/main.ts"
helper="$fixture/helper.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$main:1:1: error[call-edge] crossFileMain calls helper in $helper"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'export alias call graph failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-default-anonymous"
main="$fixture/main.ts"
helper="$fixture/helper.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$main:1:1: error[call-edge] crossFileMain calls default in $helper"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'anonymous default call graph failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/graph-directory"
main="$fixture/main.ts"
helper="$fixture/helper/index.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$main:1:1: error[call-edge] crossFileMain calls helper in $helper"
if [ "$status" -ne 1 ] || [ "$diagnostic" != "$expected" ]; then
  printf 'directory import call graph failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/nested-calls.ts"
if ! "$output" "$fixture" >/dev/null 2>&1; then
  printf 'nested call was attributed to its outer function\n' >&2
  exit 1
fi

fixture="$repo_root/tests/fixtures/typescript/custom-suppressed.ts"
if "$output" "$fixture" >/dev/null 2>&1; then exit 0; fi
printf 'custom rule suppression failed\n' >&2
exit 1
