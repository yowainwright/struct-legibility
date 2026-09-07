#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
default_root="$(cd "$script_dir/.." && pwd)"
repo_root="$(cd "${1:-$default_root}" && pwd)"
source_dir="$script_dir/hooks"

git_common_dir() {
  local directory
  directory="$(git -C "$repo_root" rev-parse --git-common-dir)"
  if [[ "$directory" = /* ]]; then
    printf '%s\n' "$directory"
    return
  fi
  printf '%s/%s\n' "$repo_root" "$directory"
}

install_hook() {
  local name="$1"
  local source="$source_dir/$name"
  local destination="$hooks_dir/$name"
  if [ -e "$destination" ] && ! grep -Eq '^# struct-(lint|legibility)-managed-hook$' "$destination"; then
    printf 'Skipping unmanaged hook: %s\n' "$name"
    return
  fi
  if [ -e "$destination" ] && cmp -s "$source" "$destination"; then
    chmod 0755 "$destination"
    printf 'Git hook already current: %s\n' "$name"
    return
  fi
  install -m 0755 "$source" "$destination"
  printf 'Installed git hook: %s\n' "$name"
}

custom_hooks_path="$(git -C "$repo_root" config --get core.hooksPath || true)"
if [ -n "$custom_hooks_path" ]; then
  printf 'core.hooksPath is already set to %s\n' "$custom_hooks_path" >&2
  exit 1
fi

hooks_dir="$(git_common_dir)/hooks"
mkdir -p "$hooks_dir"

install_hook pre-commit
install_hook commit-msg
install_hook post-merge
