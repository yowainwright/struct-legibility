#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$repo_root/.build"
temporary_dir="$(mktemp -d "$repo_root/.build/bootstrap.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT

export SL_TEST_ROOT="$temporary_dir/project with spaces"
export SL_TEST_LOG="$temporary_dir/commands"
mock_bin="$temporary_dir/bin"
mkdir -p "$SL_TEST_ROOT/scripts/hooks" "$mock_bin"
cp "$repo_root/scripts/bootstrap.sh" "$SL_TEST_ROOT/scripts/"
cp "$repo_root/scripts/hooks/"{pre-commit,post-merge} "$SL_TEST_ROOT/scripts/hooks/"

cat >"$temporary_dir/step" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
step="${0##*/}"
printf '%s\n' "$step" >>"$SL_TEST_LOG"
if [ "${SL_TEST_FAIL:-}" = "$step" ]; then exit 9; fi
SH
for step in lint check setup build; do
  cp "$temporary_dir/step" "$SL_TEST_ROOT/scripts/$step.sh"
done
cat >"$mock_bin/git" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
case "$*" in
  'rev-parse --show-toplevel') printf '%s\n' "$SL_TEST_ROOT" ;;
  '--no-pager diff --cached --check')
    printf 'staged-whitespace\n' >>"$SL_TEST_LOG"
    exit "${SL_TEST_WHITESPACE_STATUS:-0}"
    ;;
  'diff --name-only ORIG_HEAD HEAD -- CMakeLists.txt cmake/')
    printf '%s' "${SL_TEST_CHANGED:-}"
    exit "${SL_TEST_DIFF_STATUS:-0}"
    ;;
  *) exit 1 ;;
esac
SH
for tool in cmake ctest clang-format shellcheck; do
  printf '#!/usr/bin/env bash\nexit 0\n' >"$mock_bin/$tool"
done
chmod +x "$mock_bin/"* "$SL_TEST_ROOT/scripts/"*.sh "$SL_TEST_ROOT/scripts/hooks/"*
ln -s "$(command -v bash)" "$mock_bin/bash"
ln -s "$(command -v dirname)" "$mock_bin/dirname"

assert_log() {
  printf '%s\n' "$@" >"$temporary_dir/expected"
  cmp "$temporary_dir/expected" "$SL_TEST_LOG"
}

run_script() {
  : >"$SL_TEST_LOG"
  (cd "$temporary_dir" && PATH="$mock_bin" "$SL_TEST_ROOT/scripts/$1")
}

run_script bootstrap.sh
assert_log lint.sh check.sh setup.sh
for step in lint check setup; do
  if SL_TEST_FAIL="$step.sh" run_script bootstrap.sh; then
    printf 'bootstrap ignored failure in %s\n' "$step" >&2
    exit 1
  fi
  test "$(tail -n 1 "$SL_TEST_LOG")" = "$step.sh"
done

mv "$mock_bin/clang-format" "$temporary_dir/clang-format"
if run_script bootstrap.sh 2>/dev/null; then
  printf 'bootstrap accepted a missing tool\n' >&2
  exit 1
fi
test ! -s "$SL_TEST_LOG"
mv "$temporary_dir/clang-format" "$mock_bin/clang-format"

run_script hooks/pre-commit
assert_log staged-whitespace lint.sh
if SL_TEST_FAIL=lint.sh run_script hooks/pre-commit; then
  printf 'pre-commit ignored lint failure\n' >&2
  exit 1
fi
assert_log staged-whitespace lint.sh
SL_TEST_FAIL=check.sh run_script hooks/pre-commit
assert_log staged-whitespace lint.sh
if SL_TEST_WHITESPACE_STATUS=2 run_script hooks/pre-commit; then
  printf 'pre-commit ignored staged whitespace failure\n' >&2
  exit 1
fi
assert_log staged-whitespace

run_script hooks/post-merge
assert_log setup.sh
SL_TEST_CHANGED=CMakeLists.txt run_script hooks/post-merge
assert_log setup.sh build.sh
if SL_TEST_DIFF_STATUS=128 run_script hooks/post-merge; then
  printf 'post-merge ignored git diff failure\n' >&2
  exit 1
fi
assert_log setup.sh
