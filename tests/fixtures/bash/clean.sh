#!/usr/bin/env bash
source ./settings.sh
LIMIT=1
main() { helper; }
helper() { printf '%s\n' "$LIMIT"; }
main
