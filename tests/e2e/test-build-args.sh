#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

assert_rejected() {
  set +e
  "$@" >/dev/null 2>&1
  local status=$?
  set -e
  if [ "$status" -eq 2 ]; then return; fi
  printf 'invalid build arguments accepted: %s\n' "$*" >&2
  exit 1
}

check_script() {
  local script="$1"
  assert_rejected "$script" --unknown
  assert_rejected "$script" input.ts duplicate.ts -o output
  assert_rejected "$script" input.ts -o first -o second
  assert_rejected "$script" input.ts -o
  assert_rejected "$script" -o output
}

check_script "$repo_root/scripts/build.sh"
check_script "$repo_root/scripts/tqs-qjsc.sh"
