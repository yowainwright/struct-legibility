#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/.build/check"
tqs_binary="$repo_root/.build/struct-legibility"

bun run typecheck
"$repo_root/tests/e2e/test-build-args.sh"
cmake -S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target struct-legibility --parallel
ctest --test-dir "$build_dir" --output-on-failure
SL_BUILD_DIR="$build_dir" "$repo_root/scripts/build.sh" \
  "$repo_root/runtime/cli.ts" -o "$tqs_binary"
"$repo_root/tests/e2e/test-tqs.sh" "$tqs_binary"
SL_BUILD_DIR="$build_dir" "$repo_root/tests/e2e/test-config.sh"
SL_BUILD_DIR="$build_dir" "$repo_root/tests/e2e/test-sandbox.sh"
"$repo_root/scripts/benchmark.sh" "$tqs_binary"
