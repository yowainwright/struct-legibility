#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:-${TMPDIR:-/tmp}/struct-legibility-sandbox}"
entry="$repo_root/tests/fixtures/config/forbidden-eval.ts"
fixture="$repo_root/tests/fixtures/typescript/recursive-cycle.ts"

SL_BUILD_DIR="${SL_BUILD_DIR:-${TMPDIR:-/tmp}/struct-legibility-build}" \
  "$repo_root/scripts/build.sh" "$entry" -o "$output"

set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e

if [ "$status" -ne 2 ] || [[ "$diagnostic" != struct-legibility:* ]]; then
  printf 'dynamic evaluation was available\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
  exit 1
fi

output="$output-module"
entry="$repo_root/tests/fixtures/config/forbidden-module.ts"
SL_BUILD_DIR="${SL_BUILD_DIR:-${TMPDIR:-/tmp}/struct-legibility-build}" \
  "$repo_root/scripts/build.sh" "$entry" -o "$output"

set +e
diagnostic="$($output "$fixture" 2>&1)"
status=$?
set -e

if [ "$status" -eq 2 ]; then exit 0; fi
printf 'runtime module loading was available\nstatus: %s\noutput: %s\n' "$status" "$diagnostic" >&2
exit 1
