#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$repo_root/.build"
temporary_dir="$(mktemp -d "$repo_root/.build/setup.XXXXXX")"

cleanup() {
  rm -rf "$temporary_dir"
}

trap cleanup EXIT

while IFS= read -r variable; do unset "$variable"; done < <(git rev-parse --local-env-vars)
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null CI=false

assert_hook() {
  local name="$1"
  local installed="$temporary_dir/.git/hooks/$name"
  test -x "$installed"
  cmp "$repo_root/scripts/hooks/$name" "$installed"
}

for ci in true 1; do
  CI="$ci" "$repo_root/scripts/setup.sh" "$temporary_dir/missing-repository"
done
git -C "$temporary_dir" init --quiet
printf '#!/usr/bin/env bash\n# struct-legibility-managed-hook\nexit 0\n' \
  >"$temporary_dir/.git/hooks/post-merge"
"$repo_root/scripts/setup.sh" "$temporary_dir"
assert_hook pre-commit
assert_hook commit-msg
assert_hook post-merge

"$repo_root/scripts/setup.sh" "$temporary_dir"
assert_hook pre-commit
assert_hook commit-msg
assert_hook post-merge

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

mv "$temporary_dir/.git/hooks/pre-commit" "$temporary_dir/original-hook"
ln -s missing-hook "$temporary_dir/.git/hooks/pre-commit"
"$repo_root/scripts/setup.sh" "$temporary_dir"
test "$(readlink "$temporary_dir/.git/hooks/pre-commit")" = missing-hook

git -C "$temporary_dir" config core.hooksPath custom-hooks
if "$repo_root/scripts/setup.sh" "$temporary_dir" 2>/dev/null; then
  printf 'setup ignored core.hooksPath\n' >&2
  exit 1
fi
test "$(git -C "$temporary_dir" config --get core.hooksPath)" = custom-hooks
test ! -e "$temporary_dir/custom-hooks"
