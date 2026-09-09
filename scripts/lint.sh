#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

./scripts/format.sh --check
shellcheck scripts/*.sh scripts/hooks/* tests/e2e/*.sh
