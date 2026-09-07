#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
archive="$1"
temporary_dir="$(mktemp -d "$repo_root/.build/release.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT

tar -xzf "$archive" -C "$temporary_dir"
cmp "$repo_root/LICENSE" "$temporary_dir/LICENSE"
for notice in "$repo_root"/LICENSES/*; do
  cmp "$notice" "$temporary_dir/LICENSES/$(basename "$notice")"
done

binary="$temporary_dir/struct-lint"
PATH=/nonexistent "$binary" --profile ci "$repo_root/tests/fixtures/readme/main.ts"
"$repo_root/tests/e2e/test-options.sh" "$binary"
"$repo_root/tests/e2e/test-scriptc.sh" "$binary"
"$repo_root/tests/e2e/test-discovery.sh" "$binary"
