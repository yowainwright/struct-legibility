#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/struct-legibility-static.XXXXXX")"

cleanup() {
  rm -rf "$temporary_dir"
}

trap cleanup EXIT

assert_rejected() {
  local entry="$1"
  local name="$2"
  local output="$temporary_dir/$name"
  set +e
  local diagnostic
  diagnostic="$(SL_BUILD_DIR="${SL_BUILD_DIR:-${TMPDIR:-/tmp}/struct-legibility-build}" \
    "$repo_root/scripts/build.sh" "$entry" -o "$output" 2>&1)"
  local status=$?
  set -e
  if [ "$status" -ne 0 ] && [[ "$diagnostic" == *"error SC"* ]] && [ ! -e "$output" ]; then
    return
  fi
  printf 'static build accepted %s\nstatus: %s\noutput: %s\n' "$name" "$status" "$diagnostic" >&2
  exit 1
}

assert_rejected "$repo_root/tests/fixtures/config/forbidden-eval.ts" eval
assert_rejected "$repo_root/tests/fixtures/config/forbidden-module.ts" module
