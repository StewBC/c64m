#!/usr/bin/env bash
# Run the priority (or named) CIA suite under c64m debugcart runner.
# Does not poll ICR mid-race.
#
# Usage:
#   tools/cia-timing-corpus/run_c64m.sh [priority|lorenz-cia] [out.tsv]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CORPUS="$ROOT/external/cia-timing-corpus"
TP="$CORPUS/VICE-testprogs"
RUNNER="${RUN_C64M:-$ROOT/build/run_c64m_cia_corpus}"
SUITE="${1:-priority}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="${2:-$ROOT/md-files/corpus/cia-timing/results/c64m-${SUITE}-${STAMP}.tsv}"

if [[ ! -x "$RUNNER" ]]; then
  echo "error: runner not found at $RUNNER — build run_c64m_cia_corpus first" >&2
  exit 2
fi
if [[ ! -d "$TP" ]]; then
  echo "error: missing $TP — run tools/cia-timing-corpus/fetch.sh first" >&2
  exit 2
fi

mkdir -p "$(dirname "$OUT")"
echo -e "status\texit\tcia_model\tpath\tprg\ttimeout\tnotes\trunner" > "$OUT"
META="$OUT.meta.txt"
{
  echo "suite=$SUITE"
  echo "runner=$RUNNER"
  echo "started_utc=$STAMP"
  echo "host=$(uname -a)"
} > "$META"

# Same priority list as run_x64sc.sh (kept in sync intentionally).
list_priority() {
  cat <<'EOF'
general/Lorenz-2.15/src|cia1tb123.prg|20000000|0|timer B write race CIA1 old
general/Lorenz-2.15/src|cia2tb123.prg|20000000|0|timer B write race CIA2 old
general/Lorenz-2.15/src|cia1tb123.prg|20000000|1|timer B write race CIA1 new
general/Lorenz-2.15/src|cia2tb123.prg|20000000|1|timer B write race CIA2 new
general/Lorenz-2.15/src|icr01.prg|20000000|0|ICR read/set race old
general/Lorenz-2.15/src|icr01new.prg|20000000|1|ICR read/set race new
general/Lorenz-2.15/src|imr.prg|20000000|0|IMR / interrupt mask old
general/Lorenz-2.15/src|flipos.prg|20000000|0|flipos old
general/Lorenz-2.15/src|flipos.prg|20000000|1|flipos new
general/Lorenz-2.15/src|oneshot.prg|20000000|0|oneshot old
general/Lorenz-2.15/src|oneshot.prg|20000000|1|oneshot new
CIA/irqdelay|irqdelay.prg|7000000|0|1-cycle IRQ delay detect old
CIA/irqdelay|irqdelay-new.prg|7000000|1|1-cycle IRQ delay detect new
CIA/irqdelay|irqdelay-oneshot.prg|7000000|0|oneshot IRQ delay old
CIA/irqdelay|irqdelay-oneshot-new.prg|7000000|1|oneshot IRQ delay new
CIA/irqdelay|irqdelay2.prg|9000000|0|irqdelay2 old
CIA/irqdelay|irqdelay2-new.prg|9000000|1|irqdelay2 new
CIA/irqdelay|irqdelay-cia1.prg|10000000|default|CIA1 delay
CIA/irqdelay|irqdelay-cia2.prg|10000000|default|CIA2 delay
CIA/irqdelay|irqdelay-cia1-oneshot.prg|10000000|default|CIA1 oneshot delay
CIA/irqdelay|irqdelay-cia2-oneshot.prg|10000000|default|CIA2 oneshot delay
CIA/irqdelay|irqdelay-cia1-4-old.prg|10000000|0|CIA1-4 old
CIA/irqdelay|irqdelay-cia1-4-new.prg|10000000|1|CIA1-4 new
CIA/irqdelay|irqdelay-cia1-oneshot-4-old.prg|10000000|0|CIA1 oneshot-4 old
CIA/irqdelay|irqdelay-cia1-oneshot-4-new.prg|10000000|1|CIA1 oneshot-4 new
CIA/irqdelay|irqdelay-cia2-4.prg|10000000|default|CIA2-4
CIA/irqdelay|irqdelay-cia2-oneshot-4.prg|10000000|default|CIA2 oneshot-4
CIA/dd0dtest|dd0dtest.prg|100000000|0|CIA2 ICR race old
CIA/dd0dtest|dd0dtest.prg|100000000|1|CIA2 ICR race new
CIA/reload0|reload0a.prg|20000000|default|reload0a
CIA/reload0|reload0b.prg|20000000|default|reload0b
EOF
}

list_lorenz_short() {
  # Quick Lorenz subset (excludes multi-minute ta/tb scans).
  cat <<'EOF'
general/Lorenz-2.15/src|cia1tb123.prg|20000000|0|cia1tb123 old
general/Lorenz-2.15/src|cia2tb123.prg|20000000|0|cia2tb123 old
general/Lorenz-2.15/src|icr01.prg|20000000|0|icr01 old
general/Lorenz-2.15/src|imr.prg|20000000|0|imr old
general/Lorenz-2.15/src|flipos.prg|20000000|0|flipos old
general/Lorenz-2.15/src|oneshot.prg|20000000|0|oneshot old
general/Lorenz-2.15/src|cia1tb123.prg|20000000|1|cia1tb123 new
general/Lorenz-2.15/src|cia2tb123.prg|20000000|1|cia2tb123 new
general/Lorenz-2.15/src|icr01new.prg|20000000|1|icr01 new
general/Lorenz-2.15/src|flipos.prg|20000000|1|flipos new
general/Lorenz-2.15/src|oneshot.prg|20000000|1|oneshot new
EOF
}

case "$SUITE" in
  priority) LIST_FN=list_priority ;;
  lorenz-short) LIST_FN=list_lorenz_short ;;
  *)
    echo "usage: $0 [priority|lorenz-short] [out.tsv]" >&2
    exit 2
    ;;
esac

n_pass=0
n_fail=0
n_timeout=0
n_missing=0
n_other=0
n=0

while IFS='|' read -r relpath prg limit_cycles cia_model notes; do
  [[ -z "${relpath:-}" || "$relpath" =~ ^# ]] && continue
  n=$((n + 1))
  prg_path="$TP/$relpath/$prg"
  if [[ ! -f "$prg_path" ]]; then
    printf 'MISSING\t-\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$cia_model" "$relpath" "$prg" "$limit_cycles" "$notes" "$RUNNER" >> "$OUT"
    echo "[$n] MISSING $relpath/$prg"
    n_missing=$((n_missing + 1))
    continue
  fi

  # Note: c64m does not yet switch CIA chip models; cia_model is recorded for
  # comparison with VICE baselines only.
  set +e
  "$RUNNER" --prg "$prg_path" --limit "$limit_cycles" --pal
  ec=$?
  set -e

  case "$ec" in
    0) st=PASS; n_pass=$((n_pass + 1)) ;;
    255) st=FAIL; n_fail=$((n_fail + 1)) ;;
    1) st=TIMEOUT; n_timeout=$((n_timeout + 1)) ;;
    *) st=OTHER; n_other=$((n_other + 1)) ;;
  esac
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$st" "$ec" "$cia_model" "$relpath" "$prg" "$limit_cycles" "$notes" "$RUNNER" >> "$OUT"
  echo "[$n] $st (exit=$ec cia=$cia_model) $relpath/$prg — $notes"
done < <("$LIST_FN")

{
  echo "finished_utc=$(date -u +%Y%m%dT%H%M%SZ)"
  echo "total=$n pass=$n_pass fail=$n_fail timeout=$n_timeout missing=$n_missing other=$n_other"
  echo "note=c64m does not select 6526 vs 8521 yet; cia_model column is documentary"
} | tee -a "$META"

echo "results: $OUT"
echo "summary: total=$n pass=$n_pass fail=$n_fail timeout=$n_timeout missing=$n_missing other=$n_other"
# Always exit 0 for the suite driver so agents can collect a full matrix;
# individual row status is in the TSV.
exit 0
