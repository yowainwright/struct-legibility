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

fixture="$repo_root/tests/fixtures/typescript/custom-rule.ts"
set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e
expected="$fixture:1:1: error[entrypoint-name] exported entrypoint launch must be named main"
if [ "$status" -eq 1 ] && [ "$diagnostic" = "$expected" ]; then exit 0; fi
printf 'custom config failed\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
exit 1
