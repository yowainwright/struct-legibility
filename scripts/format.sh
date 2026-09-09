#!/usr/bin/env bash
set -euo pipefail

options=(-i)
if [ "${1:-}" = --check ]; then
  options=(--dry-run --Werror)
  shift
fi
if [ "$#" -ne 0 ]; then
  printf 'usage: scripts/format.sh [--check]\n' >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

exec clang-format --style=file:scripts/.clang-format "${options[@]}" \
  src/*.c src/*.h src/languages/*.c src/languages/*.h include/*.h tests/c/*.c
