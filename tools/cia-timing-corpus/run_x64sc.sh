#!/usr/bin/env bash
# Run CIA testprogs under x64sc using the VICE debugcart exit convention.
#
# Exit codes (debugcart write to $D7FF):
#   0   = PASS
#   255 = FAIL
#   1   = cycle limit hit (timeout / did not finish)
#   other = emulator/error
#
# Does NOT poll ICR mid-race. Autostart + wait for settled debugcart write only.
#
# Usage:
#   tools/cia-timing-corpus/run_x64sc.sh [priority|cia|lorenz-cia|all] [out.tsv]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CORPUS="$ROOT/external/cia-timing-corpus"
TP="$CORPUS/VICE-testprogs"
X64SC="${X64SC:-/Applications/vice-arm64-gtk3-3.10/bin/x64sc}"
if [[ ! -x "$X64SC" ]]; then
  if command -v x64sc >/dev/null 2>&1; then
    X64SC="$(command -v x64sc)"
  else
    echo "error: x64sc not found (set X64SC=...)" >&2
    exit 2
  fi
fi

if [[ ! -d "$TP" ]]; then
  echo "error: missing $TP — run tools/cia-timing-corpus/fetch.sh first" >&2
  exit 2
fi

SUITE="${1:-priority}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="${2:-$ROOT/md-files/corpus/cia-timing/results/x64sc-${SUITE}-${STAMP}.tsv}"
mkdir -p "$(dirname "$OUT")"

# Common VICE opts matching testbench/x64sc-hooks.sh exitcode path.
COMMON=(-default -console -warp -debugcart -jamaction 1 -VICIIfilter 0)

# List format (one per line, # comments ok):
#   relpath_from_VICE-testprogs|prg_name|timeout_cycles|extra_opts_space_sep|notes
# extra_opts examples: -ciamodel 0 | -ciamodel 1 | -pal | -ntsc
list_priority() {
  cat <<'EOF'
# Priority Phase-4 race / interrupt-delay set
general/Lorenz-2.15/src|cia1tb123.prg|20000000|-ciamodel 0|timer B write race CIA1 old
general/Lorenz-2.15/src|cia2tb123.prg|20000000|-ciamodel 0|timer B write race CIA2 old
general/Lorenz-2.15/src|cia1tb123.prg|20000000|-ciamodel 1|timer B write race CIA1 new
general/Lorenz-2.15/src|cia2tb123.prg|20000000|-ciamodel 1|timer B write race CIA2 new
general/Lorenz-2.15/src|icr01.prg|20000000|-ciamodel 0|ICR read/set race old
general/Lorenz-2.15/src|icr01new.prg|20000000|-ciamodel 1|ICR read/set race new
general/Lorenz-2.15/src|imr.prg|20000000|-ciamodel 0|IMR / interrupt mask old
general/Lorenz-2.15/src|flipos.prg|20000000|-ciamodel 0|flipos old
general/Lorenz-2.15/src|flipos.prg|20000000|-ciamodel 1|flipos new
general/Lorenz-2.15/src|oneshot.prg|20000000|-ciamodel 0|oneshot old
general/Lorenz-2.15/src|oneshot.prg|20000000|-ciamodel 1|oneshot new
CIA/irqdelay|irqdelay.prg|7000000|-ciamodel 0|1-cycle IRQ delay detect old
CIA/irqdelay|irqdelay-new.prg|7000000|-ciamodel 1|1-cycle IRQ delay detect new
CIA/irqdelay|irqdelay-oneshot.prg|7000000|-ciamodel 0|oneshot IRQ delay old
CIA/irqdelay|irqdelay-oneshot-new.prg|7000000|-ciamodel 1|oneshot IRQ delay new
CIA/irqdelay|irqdelay2.prg|9000000|-ciamodel 0|irqdelay2 old
CIA/irqdelay|irqdelay2-new.prg|9000000|-ciamodel 1|irqdelay2 new
CIA/irqdelay|irqdelay-cia1.prg|10000000||CIA1 delay
CIA/irqdelay|irqdelay-cia2.prg|10000000||CIA2 delay
CIA/irqdelay|irqdelay-cia1-oneshot.prg|10000000||CIA1 oneshot delay
CIA/irqdelay|irqdelay-cia2-oneshot.prg|10000000||CIA2 oneshot delay
CIA/irqdelay|irqdelay-cia1-4-old.prg|10000000|-ciamodel 0|CIA1-4 old
CIA/irqdelay|irqdelay-cia1-4-new.prg|10000000|-ciamodel 1|CIA1-4 new
CIA/irqdelay|irqdelay-cia1-oneshot-4-old.prg|10000000|-ciamodel 0|CIA1 oneshot-4 old
CIA/irqdelay|irqdelay-cia1-oneshot-4-new.prg|10000000|-ciamodel 1|CIA1 oneshot-4 new
CIA/irqdelay|irqdelay-cia2-4.prg|10000000||CIA2-4
CIA/irqdelay|irqdelay-cia2-oneshot-4.prg|10000000||CIA2 oneshot-4
CIA/dd0dtest|dd0dtest.prg|100000000|-ciamodel 0|CIA2 ICR race old
CIA/dd0dtest|dd0dtest.prg|100000000|-ciamodel 1|CIA2 ICR race new
CIA/reload0|reload0a.prg|20000000||reload0a
CIA/reload0|reload0b.prg|20000000||reload0b
EOF
}

list_lorenz_cia() {
  # Lorenz CIA rows from official c64-testlist.in (correct timeouts).
  local listin="$TP/testbench/c64-testlist.in"
  awk -F, '
    $0 ~ /^#/ { next }
    $0 ~ /^\.\.\/general\/Lorenz-2\.15\/src\// && $3 == "exitcode" {
      prg = $2
      # CIA-relevant names only
      if (prg !~ /^(cia|icr|imr|flipos|oneshot|irq|nmi|loadth|cnto2|cntdef)/) next
      if (prg ~ /-dtv/) next
      path = "general/Lorenz-2.15/src"
      to = $4
      opts = ""
      notes = ""
      for (i = 5; i <= NF; i++) {
        if ($i == "cia-old") opts = opts (opts?" ":"") "-ciamodel 0"
        else if ($i == "cia-new") opts = opts (opts?" ":"") "-ciamodel 1"
        else if ($i ~ /^comment:/) notes = notes $i " "
      }
      print path "|" prg "|" to "|" opts "|" notes
    }
  ' "$listin"
}

list_cia_dir() {
  # Auto-extract exitcode CIA entries from c64-testlist.in (no interactive).
  # Env CORE_ONLY=1 skips TOD + long sdr-icr matrix and expect:error rows.
  local listin="$TP/testbench/c64-testlist.in"
  local core_only="${CORE_ONLY:-0}"
  # shellcheck disable=SC2016
  awk -F, -v core_only="$core_only" '
    $0 ~ /^#/ { next }
    $0 ~ /^\.\.\/CIA\// && $3 == "exitcode" {
      path = $1
      sub(/^\.\.\//, "", path)
      sub(/\/$/, "", path)
      prg = $2
      to = $4
      opts = ""
      notes = ""
      expect_error = 0
      for (i = 5; i <= NF; i++) {
        if ($i == "cia-old") opts = opts (opts?" ":"") "-ciamodel 0"
        else if ($i == "cia-new") opts = opts (opts?" ":"") "-ciamodel 1"
        else if ($i == "vicii-pal") opts = opts (opts?" ":"") "-pal"
        else if ($i == "vicii-ntsc") opts = opts (opts?" ":"") "-ntsc"
        else if ($i ~ /^expect:error/) { notes = notes "expect:error "; expect_error = 1 }
        else if ($i ~ /^comment:/) notes = notes $i " "
      }
      if (core_only == "1") {
        if (path ~ /\/tod$|\/tod\// || path ~ /tod/) next
        if (path ~ /cia-sdr-icr/) next
        if (expect_error) next
        # also skip very long timeouts (>30s of cycles at 1MHz-ish warp still slow)
        if (to + 0 > 30000000 && path ~ /shiftregister/) next
      }
      print path "|" prg "|" to "|" opts "|" notes
    }
  ' "$listin"
}

case "$SUITE" in
  priority) LIST_FN=list_priority ;;
  lorenz-cia) LIST_FN=list_lorenz_cia ;;
  cia)
    LIST_FN=list_cia_dir
    ;;
  cia-core)
    CORE_ONLY=1
    export CORE_ONLY
    LIST_FN=list_cia_dir
    ;;
  all)
    LIST_FN=list_all
    list_all() { list_priority; list_lorenz_cia; list_cia_dir; }
    ;;
  *)
    echo "usage: $0 [priority|lorenz-cia|cia-core|cia|all] [out.tsv]" >&2
    exit 2
    ;;
esac

echo -e "status\texit\tcia_model\tpath\tprg\ttimeout\tnotes\tx64sc" > "$OUT"
META="$OUT.meta.txt"
{
  echo "suite=$SUITE"
  echo "x64sc=$X64SC"
  echo "x64sc_version=$($X64SC -version 2>&1 | head -3 | tr '\n' ' ')"
  echo "started_utc=$STAMP"
  echo "host=$(uname -a)"
  echo "common_opts=${COMMON[*]}"
} > "$META"

n_pass=0
n_fail=0
n_timeout=0
n_missing=0
n_other=0
n=0

while IFS='|' read -r relpath prg limit_cycles extra_opts notes; do
  [[ -z "${relpath:-}" || "$relpath" =~ ^# ]] && continue
  n=$((n + 1))
  prg_path="$TP/$relpath/$prg"
  if [[ ! -f "$prg_path" ]]; then
    printf 'MISSING\t-\t-\t%s\t%s\t%s\t%s\t%s\n' \
      "$relpath" "$prg" "$limit_cycles" "$notes" "$X64SC" >> "$OUT"
    echo "[$n] MISSING $relpath/$prg"
    n_missing=$((n_missing + 1))
    continue
  fi

  # shellcheck disable=SC2206
  extra=( $extra_opts )
  cia_model="default"
  for ((i=0; i<${#extra[@]}; i++)); do
    if [[ "${extra[i]}" == "-ciamodel" && $((i+1)) -lt ${#extra[@]} ]]; then
      cia_model="${extra[i+1]}"
    fi
  done

  set +e
  if ((${#extra[@]} > 0)); then
    "$X64SC" "${COMMON[@]}" "${extra[@]}" \
      -limitcycles "$limit_cycles" \
      "$prg_path" \
      >/dev/null 2>/dev/null
  else
    "$X64SC" "${COMMON[@]}" \
      -limitcycles "$limit_cycles" \
      "$prg_path" \
      >/dev/null 2>/dev/null
  fi
  ec=$?
  set -e

  case "$ec" in
    0)
      st=PASS
      n_pass=$((n_pass + 1))
      ;;
    255)
      st=FAIL
      n_fail=$((n_fail + 1))
      ;;
    1)
      st=TIMEOUT
      n_timeout=$((n_timeout + 1))
      ;;
    *)
      st=OTHER
      n_other=$((n_other + 1))
      ;;
  esac
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$st" "$ec" "$cia_model" "$relpath" "$prg" "$limit_cycles" "$notes" "$X64SC" >> "$OUT"
  echo "[$n] $st (exit=$ec cia=$cia_model) $relpath/$prg — $notes"
done < <("$LIST_FN")

{
  echo "finished_utc=$(date -u +%Y%m%dT%H%M%SZ)"
  echo "total=$n pass=$n_pass fail=$n_fail timeout=$n_timeout missing=$n_missing other=$n_other"
} | tee -a "$META"

echo "results: $OUT"
echo "meta:    $META"
echo "summary: total=$n pass=$n_pass fail=$n_fail timeout=$n_timeout missing=$n_missing other=$n_other"
if (( n_fail + n_timeout + n_missing > 0 )); then
  exit 1
fi
exit 0
