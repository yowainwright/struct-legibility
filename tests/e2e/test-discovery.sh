#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="$1"
temporary_dir="$(mktemp -d "$repo_root/.build/discovery.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT

mkdir -p "$temporary_dir/.git" "$temporary_dir/src/deep/nested" "$temporary_dir/src/blocked"
printf '%s\n' '/src/root-ignored.ts' 'src/**/generated*.ts' '**/cache.ts' \
  'src/blocked/' '!src/blocked/keep.ts' 'src/kept.ts' 'src/spaced.ts   ' >"$temporary_dir/.gitignore"
printf '%s\n' 'local.ts' '!kept.ts' >"$temporary_dir/src/.gitignore"

for path in root.ts src/main.ts src/kept.ts src/root-ignored.ts src/generated.ts \
  src/deep/generated.ts src/deep/nested/generated.ts src/cache.ts \
  src/deep/nested/cache.ts src/local.ts src/blocked/keep.ts src/spaced.ts; do
  cp "$repo_root/tests/fixtures/typescript/section-order.ts" "$temporary_dir/$path"
done

assert_paths() {
  local expected="$1"
  shift
  local output status=0 actual
  output="$("$binary" --profile ci --format json "$@")" || status=$?
  test "$status" -eq 1
  actual="$(node -e '
    const fs = require("node:fs");
    const path = require("node:path");
    const report = JSON.parse(fs.readFileSync(0, "utf8"));
    const paths = report.diagnostics.map((item) => path.relative(process.argv[1], item.path));
    process.stdout.write(paths.sort().join("\n"));
  ' "$temporary_dir" <<<"$output")"
  if [ "$actual" = "$expected" ]; then return; fi
  printf 'unexpected discovered files\nexpected: %s\nactual: %s\n' "$expected" "$actual" >&2
  exit 1
}

expected=$'src/kept.ts\nsrc/main.ts'
assert_paths "$expected" "$temporary_dir/src"
assert_paths "$expected" "$temporary_dir/src" "$temporary_dir/src/./main.ts" \
  "$temporary_dir/src/../src/kept.ts"
assert_paths "$expected" "$temporary_dir/src/blocked" "$temporary_dir/src"
assert_paths $'root.ts\nsrc/kept.ts\nsrc/main.ts' "$temporary_dir"
(cd "$temporary_dir" && assert_paths "$expected" src)
(cd "$temporary_dir/src" && assert_paths "$expected" .)

status=0
output="$("$binary" --profile ci --format json --no-ignore "$temporary_dir")" || status=$?
test "$status" -eq 1
node -e 'const r = JSON.parse(require("node:fs").readFileSync(0, "utf8"));
  require("node:assert/strict").equal(r.diagnostics.length, 12);' <<<"$output"
