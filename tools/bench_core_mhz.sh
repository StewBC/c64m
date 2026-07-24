#!/usr/bin/env bash
# Tier-0 hygiene: pure-core MHz recipes from agents/perf-baseline-turbo2.md
# Usage: from repo root, ./tools/bench_core_mhz.sh [cycles]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CYCLES="${1:-20000000}"
BIN="${BIN:-./build/profile_c64_hotloop}"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — build profile_c64_hotloop first" >&2
  exit 1
fi

run2() {
  local label="$1"; shift
  local a b ma mb avg
  a="$("$BIN" "$CYCLES" "$@" 2>/dev/null | tail -1)"
  b="$("$BIN" "$CYCLES" "$@" 2>/dev/null | tail -1)"
  ma=$(echo "$a" | sed -n 's/.*mhz=\([0-9.]*\).*/\1/p')
  mb=$(echo "$b" | sed -n 's/.*mhz=\([0-9.]*\).*/\1/p')
  avg=$(python3 -c "print('%.3f' % ((float('$ma')+float('$mb'))/2.0))")
  printf "%-28s avg_mhz=%s  (%s / %s)\n" "$label" "$avg" "$ma" "$mb"
}

echo "host=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)  cycles=$CYCLES"
run2 "host video=on"
run2 "host video=off" no-video
run2 "drive8 video=on" 1541-one
run2 "drive8+9 video=on" 1541
run2 "drive8+9+media video=on" 1541 media
