#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$repo_root/.build"
temporary_dir="$(mktemp -d "$repo_root/.build/setup.XXXXXX")"

cleanup() {
  rm -rf "$temporary_dir"
}

trap cleanup EXIT

assert_hook() {
  local name="$1"
  local installed="$temporary_dir/.git/hooks/$name"
  test -x "$installed"
  cmp "$repo_root/scripts/hooks/$name" "$installed"
}

git -C "$temporary_dir" init --quiet
printf '#!/usr/bin/env bash\n# struct-legibility-managed-hook\nexit 0\n' \
  >"$temporary_dir/.git/hooks/post-merge"
"$repo_root/scripts/setup.sh" "$temporary_dir"
test ! -e "$temporary_dir/.git/hooks/post-merge"

assert_hook pre-commit
assert_hook commit-msg

sed 's/struct-lint-managed-hook/struct-legibility-managed-hook/' \
  "$repo_root/scripts/hooks/pre-commit" >"$temporary_dir/.git/hooks/pre-commit"
"$repo_root/scripts/setup.sh" "$temporary_dir"
assert_hook pre-commit

message_file="$temporary_dir/COMMIT_EDITMSG"
printf 'fix: valid message\n' >"$message_file"
"$temporary_dir/.git/hooks/commit-msg" "$message_file"

printf 'invalid message\n' >"$message_file"
if "$temporary_dir/.git/hooks/commit-msg" "$message_file" 2>/dev/null; then
  printf 'commit-msg accepted an invalid message\n' >&2
  exit 1
fi

printf '#!/usr/bin/env sh\nexit 0\n' >"$temporary_dir/.git/hooks/commit-msg"
cp "$temporary_dir/.git/hooks/commit-msg" "$temporary_dir/.git/hooks/post-merge"
"$repo_root/scripts/setup.sh" "$temporary_dir"
cmp "$temporary_dir/.git/hooks/commit-msg" "$temporary_dir/.git/hooks/post-merge"

expected='#!/usr/bin/env sh'
actual="$(sed -n '1p' "$temporary_dir/.git/hooks/commit-msg")"
test "$actual" = "$expected"
