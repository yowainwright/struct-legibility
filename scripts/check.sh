#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${SL_BUILD_DIR:-$repo_root/.build/check}"
scriptc_binary="$repo_root/.build/struct-lint"

nub run typecheck
nub run test:unit
"$repo_root/tests/e2e/test-setup.sh"
"$repo_root/tests/e2e/test-build-args.sh"
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
SL_BUILD_DIR="$build_dir" "$repo_root/scripts/build.sh" \
  "$repo_root/runtime/cli.ts" -o "$scriptc_binary"
"$repo_root/tests/e2e/test-scriptc.sh" "$scriptc_binary"
"$repo_root/tests/e2e/test-options.sh" "$scriptc_binary"
"$repo_root/tests/e2e/test-discovery.sh" "$scriptc_binary"
SL_BUILD_DIR="$build_dir" "$repo_root/tests/e2e/test-config.sh"
SL_BUILD_DIR="$build_dir" "$repo_root/tests/e2e/test-sandbox.sh"
SL_BUILD_DIR="$build_dir" "$repo_root/tests/e2e/test-readme.sh" "$scriptc_binary"
