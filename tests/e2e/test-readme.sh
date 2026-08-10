#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
default_binary="${1:-$repo_root/.build/struct-legibility}"
fixture_dir="$repo_root/tests/fixtures/readme"
temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/struct-legibility-readme.XXXXXX")"

cleanup() {
  rm -rf "$temporary_dir"
}

trap cleanup EXIT

assert_clean_source() {
  local source="$fixture_dir/main.ts"
  set +e
  local output
  output="$("$default_binary" --profile ci "$source" 2>&1)"
  local exit_code=$?
  set -e
  if [ "$exit_code" -eq 0 ] && [ -z "$output" ]; then return; fi
  printf 'README source failed\nstatus: %s\noutput: %s\n' "$exit_code" "$output" >&2
  exit 1
}

assert_custom_rule() {
  local binary="$1"
  local source="$fixture_dir/launch.ts"
  local expected="$source:1:1: error[entrypoint-name] exported function launch must be named main"
  set +e
  local output
  output="$("$binary" --profile ci "$source" 2>&1)"
  local exit_code=$?
  set -e
  if [ "$exit_code" -eq 1 ] && [ "$output" = "$expected" ]; then return; fi
  printf 'README custom rule failed\nstatus: %s\noutput: %s\n' "$exit_code" "$output" >&2
  exit 1
}

assert_clean_source

custom_binary="$temporary_dir/struct-legibility-custom"
config="$fixture_dir/struct-legibility.config.ts"
SL_BUILD_DIR="${SL_BUILD_DIR:-$repo_root/.build/check}" \
  "$repo_root/scripts/build.sh" "$config" -o "$custom_binary"
assert_custom_rule "$custom_binary"
