#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 0 ]; then
  printf 'usage: scripts/bootstrap.sh\n' >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

for tool in git cmake ctest clang-format shellcheck; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Missing required tool: %s. See .github/CONTRIBUTING.md.\n' "$tool" >&2
    exit 1
  fi
done

./scripts/lint.sh
./scripts/check.sh
./scripts/setup.sh
printf 'Bootstrap complete. Binary: %s/struct-lint\n' "${SL_BUILD_DIR:-$repo_root/.build}"
