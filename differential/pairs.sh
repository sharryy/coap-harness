#!/usr/bin/env bash
# Run recording monitors on Paul's version pairs and cross-check each pair.
# usage: pairs.sh <EXP>...   (env: SERVER_PORT, MAX_TIME, HARD_CAP, EXTRA)
# KLEE_ONLY=1 skips the checker, CHECK_ONLY=1 skips KLEE, JOBS=N runs N (EXP,pair) jobs at once, each on its own port from PORT up.
set -uo pipefail
cd "$(dirname "$0")"
DIFF_DIR="$PWD"
ROOT="$(cd .. && pwd)"
LC="$ROOT/libcoap"
CHECKER="$ROOT/tools/smt-cross-checker"
PORT="${SERVER_PORT:-5683}"
MAX_TIME="${MAX_TIME:-600s}"
HARD_CAP="${HARD_CAP:-780}"
JOBS="${JOBS:-1}"
# DFS: finish each path before switching, so forked states never interleave on the shared real sockets
EXTRA="${EXTRA:---search=dfs}"

# tag : FreeCoAP bitcode (with its facilitator) : libcoap bitcode
PAIRS="new:freecoap-18ee7d4-435-linked.bc:libcoap-linked.bc old:freecoap-ffc87fd-431-linked.bc:libcoap-431-linked.bc"

info_line () {
  local d="$1" q p r
  q=$(grep -oE 'total queries = [0-9]+' "$d/info" 2>/dev/null | grep -oE '[0-9]+$' | head -1)
  p=$(grep -oE 'explored paths = [0-9]+' "$d/info" 2>/dev/null | grep -oE '[0-9]+$' | head -1)
  r=$(find "$d" -maxdepth 1 -name 'test*.resp' 2>/dev/null | wc -l)
  echo "queries=${q:-?} paths=${p:-?} resp=$r elapsed=$(grep -oE 'Elapsed: [0-9:]+' "$d/info" 2>/dev/null | head -1)"
}

run_one () {
  local EXP="$1" tag="$2" fcbc="$3" lcbc="$4" port="$5"
  local FC_OUT="klee-pair-$tag-freecoap-$EXP" LC_OUT="klee-pair-$tag-libcoap-$EXP" out=""
  out+="=== EXP $EXP pair $tag port $port $(date +%T)"$'\n'
  if [ -z "${CHECK_ONLY:-}" ]; then
    rm -rf "$DIFF_DIR/$FC_OUT" "$LC/$LC_OUT"
    ( cd "$DIFF_DIR" && SERVER_PORT="$port" timeout -k 20 "$HARD_CAP" make run EXP="$EXP" OUTDIR="$FC_OUT" MAX_TIME="$MAX_TIME" EXTRA="$EXTRA" BC="$fcbc" >/dev/null 2>&1 )
    out+="    freecoap: $(info_line "$DIFF_DIR/$FC_OUT")"$'\n'
    ( cd "$LC" && SERVER_PORT="$port" timeout -k 20 "$HARD_CAP" make run EXP="$EXP" OUTDIR="$LC_OUT" MAX_TIME="$MAX_TIME" EXTRA="$EXTRA" BC="$lcbc" >/dev/null 2>&1 )
    out+="    libcoap:  $(info_line "$LC/$LC_OUT")"$'\n'
  fi
  if [ -z "${KLEE_ONLY:-}" ]; then
    local LOG="$DIFF_DIR/pair-$tag-exp$EXP.log"
    rm -f "$LOG"
    "$CHECKER/.venv/bin/python" "$CHECKER/main.py" -b "$LC/$LC_OUT" -c "$DIFF_DIR/$FC_OUT" -o "$LOG" -v >/dev/null 2>&1
    out+="    counter-examples: $(grep -c 'Counter-example:' "$LOG" 2>/dev/null)"$'\n'
  fi
  printf '%s' "$out"
}

lane=0
for EXP in "$@"; do
  for pair in $PAIRS; do
    IFS=: read -r tag fcbc lcbc <<< "$pair"
    if [ "$JOBS" -le 1 ]; then
      run_one "$EXP" "$tag" "$fcbc" "$lcbc" "$PORT"
    else
      while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
      run_one "$EXP" "$tag" "$fcbc" "$lcbc" "$((PORT + lane))" &
      lane=$((lane + 1))
    fi
  done
done
wait
echo "=== pairs done $(date +%T)"
