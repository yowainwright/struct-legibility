#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${1:-$repo_root/.build/struct-lint}"
temporary_dir=""

cleanup() {
  if [ -n "$temporary_dir" ]; then rm -rf "$temporary_dir"; fi
}

make_corpus() {
  local corpus="$1"
  local seed="$repo_root/tests/fixtures/performance/typescript-100-lines.ts"
  local generator="$temporary_dir/generate-corpus"
  mkdir -p "$corpus/.git"
  cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$repo_root/tests/performance/generate-corpus.c" -o "$generator"
  "$generator" "$seed" "$corpus"
}

measure_darwin() {
  local corpus="$1"
  local metrics="$2"
  /usr/bin/time -lp "$binary" --profile ci "$corpus" 2>"$metrics"
}

measure_linux() {
  local corpus="$1"
  local metrics="$2"
  /usr/bin/time -f 'real %e\nrss_kib %M' -o "$metrics" "$binary" --profile ci "$corpus"
}

metric_value() {
  local name="$1"
  local metrics="$2"
  awk -v name="$name" '$1 == name { print $2; exit }' "$metrics"
}

darwin_rss_bytes() {
  local metrics="$1"
  awk '/maximum resident set size/ { if ($1 ~ /^[0-9]+$/) print $1; else print $NF; exit }' \
    "$metrics"
}

assert_limit() {
  local actual="$1"
  local maximum="$2"
  local label="$3"
  awk -v actual="$actual" -v maximum="$maximum" -v label="$label" \
    'BEGIN { if (actual > maximum) { printf "%s exceeded: %s > %s\n", label, actual, maximum; exit 1 } }'
}

require_metric() {
  local value="$1"
  local label="$2"
  if [ -n "$value" ]; then return; fi
  printf 'benchmark metric unavailable: %s\n' "$label" >&2
  exit 2
}

measure() {
  local corpus="$1"
  local metrics="$2"
  if [ "$(uname -s)" = "Darwin" ]; then
    measure_darwin "$corpus" "$metrics"
    return
  fi
  measure_linux "$corpus" "$metrics"
}

read_rss_kib() {
  local metrics="$1"
  if [ "$(uname -s)" != "Darwin" ]; then
    metric_value rss_kib "$metrics"
    return
  fi
  local bytes
  bytes="$(darwin_rss_bytes "$metrics")"
  awk -v bytes="$bytes" 'BEGIN { print int((bytes + 1023) / 1024) }'
}

verify_metrics() {
  local seconds="$1"
  local rss_kib="$2"
  local bytes="$3"
  require_metric "$seconds" runtime_seconds
  require_metric "$rss_kib" peak_rss_kib
  require_metric "$bytes" binary_bytes
  assert_limit "$seconds" "${SL_BENCHMARK_SECONDS:-2}" runtime_seconds
  assert_limit "$rss_kib" "${SL_BENCHMARK_RSS_KIB:-65536}" peak_rss_kib
  assert_limit "$bytes" "${SL_BENCHMARK_BYTES:-6291456}" binary_bytes
}

main() {
  if [ ! -x "$binary" ]; then
    printf 'binary not executable: %s\n' "$binary" >&2
    exit 2
  fi
  mkdir -p "$repo_root/.build"
  temporary_dir="$(mktemp -d "$repo_root/.build/benchmark.XXXXXX")"
  trap cleanup EXIT
  local corpus="$temporary_dir/corpus"
  local metrics="$temporary_dir/metrics"
  make_corpus "$corpus"
  measure "$corpus" "$metrics" || {
    cat "$metrics" >&2
    exit 1
  }
  local seconds rss_kib bytes
  seconds="$(metric_value real "$metrics")"
  rss_kib="$(read_rss_kib "$metrics")"
  bytes="$(wc -c <"$binary" | tr -d ' ')"
  verify_metrics "$seconds" "$rss_kib" "$bytes"
  printf 'benchmark: %ss, %s KiB RSS, %s bytes\n' "$seconds" "$rss_kib" "$bytes"
}

main "$@"
