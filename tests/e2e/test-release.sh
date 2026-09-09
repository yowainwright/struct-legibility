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
for fixture in readme/main.ts go/clean.go python/clean.py bash/clean.sh; do
  PATH=/nonexistent "$binary" --profile ci "$repo_root/tests/fixtures/$fixture"
done
"$repo_root/tests/e2e/test-options.sh" "$binary"
"$repo_root/tests/e2e/test-cli.sh" "$binary"
"$repo_root/tests/e2e/test-discovery.sh" "$binary"
"$repo_root/tests/e2e/test-script-cli.sh" "$binary"
"$repo_root/tests/e2e/test-embedded-cli.sh" "$binary"
