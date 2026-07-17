#!/usr/bin/env bash
# Run one CoAP differential experiment: libcoap (base) vs freecoap (control) + cross-check.
set -euo pipefail

EXP="${1:-}"
if [ -z "$EXP" ]; then
    echo "usage: $0 <EXP-id>   (100=code 101=version 102=type 103=token_length 104=mid 105=type+code)" >&2
    exit 2
fi

LIBCOAP_BC="${LIBCOAP_BC:-libcoap-linked.bc}"
TAG="${TAG:-}"

MAX_TIME="${MAX_TIME:-60s}"
EXTRA="${EXTRA:---max-memory=4000}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBCOAP_HARNESS="$ROOT/libcoap-harness"
FREECOAP_HARNESS="$ROOT/freecoap-harness/lc-to-fc"
CHECKER="$ROOT/tools/smt-cross-checker"
BUILD_DIR="/home/shehryar/kleener-install/klee_build130stp_z3"

LIB_OUT="klee-diff-libcoap-${EXP}${TAG}"
FC_OUT="klee-diff-freecoap-${EXP}"
LOG="$ROOT/diff-exp${EXP}${TAG}.log"

# sanity gate: prove the monitor actually fired
sanity_gate () {
    local dir="$1" name="$2" info="$1/info"
    [ -f "$info" ] || { echo "FAIL[$name]: no info file in $dir" >&2; exit 1; }
    grep -q -- '--write-smt2s' "$info" || {
        echo "FAIL[$name]: --write-smt2s absent — EXP<100 or recipe not applied" >&2; exit 1; }
    local q
    q="$(grep -oE 'total queries = [0-9]+' "$info" | grep -oE '[0-9]+$' | head -1 || true)"
    if [ "${q:-0}" -eq 0 ] 2>/dev/null; then
        # queries==0 is OK for a pure-echo monitor if output is still symbolic
        if grep -rqE '\(select ' "$dir"/*.resp 2>/dev/null; then
            echo "NOTE[$name]: total queries = 0 but output is symbolic (pure-echo monitor) — OK"
        else
            echo "FAIL[$name]: total queries = 0 and output is concrete — monitor never went symbolic" >&2; exit 1
        fi
    fi
    local nsmt nresp explored responded dropped mt el elsec
    nsmt="$(find "$dir" -maxdepth 1 -name 'test*.smt2' | wc -l)"
    nresp="$(find "$dir" -maxdepth 1 -name 'test*.resp' | wc -l)"
    explored="$(grep -oE 'explored paths = [0-9]+' "$info" | grep -oE '[0-9]+$' | head -1 || echo '?')"
    # partially-completed == responses, completed == drops
    responded="$(grep -oE 'partially completed paths = [0-9]+' "$info" | grep -oE '[0-9]+$' | head -1 || echo 0)"
    dropped="$(grep -E 'done: completed paths' "$info" | grep -oE '[0-9]+' | tail -1 || echo 0)"
    echo "OK[$name]: queries=$q  paths=$explored  responded=$responded  dropped=$dropped  (smt2=$nsmt resp=$nresp)"
    [ "$nresp" -gt 0 ] || echo "NOTE[$name]: 0 responses — every explored path dropped the request (respond-vs-drop only)"
    # incompleteness signal: run consumed ~all of MAX_TIME
    mt="$(printf '%s' "$MAX_TIME" | grep -oE '[0-9]+' | head -1)"
    el="$(grep -oE 'Elapsed: [0-9:]+' "$info" | grep -oE '[0-9:]+$' | head -1)"
    if [ -n "$el" ] && [ -n "$mt" ]; then
        elsec="$(printf '%s' "$el" | awk -F: '{print ($1*3600)+($2*60)+$3}')"
        if [ "${elsec:-0}" -ge "$((mt - 2))" ] 2>/dev/null; then
            echo "WARN[$name]: ran ${elsec}s of ${mt}s cap — exploration likely INCOMPLETE (path/expression explosion). Raise MAX_TIME or narrow the symbolic field."
        fi
    fi
}

run_side () {
    local harness="$1" outdir="$2" name="$3" bc="${4:-}"
    echo "== $name: make run EXP=$EXP MAX_TIME=$MAX_TIME ${bc:+BC=$bc} =="
    rm -rf "${harness:?}/$outdir"
    ( cd "$harness" && make run EXP="$EXP" OUTDIR="$outdir" MAX_TIME="$MAX_TIME" ${bc:+BC="$bc"} )
    sanity_gate "$harness/$outdir" "$name"
}

if [ "${BUILD:-0}" = "1" ]; then
    echo "== rebuilding Kleener runtime =="
    make -C "$BUILD_DIR" -j"$(nproc)"
fi

run_side "$LIBCOAP_HARNESS"  "$LIB_OUT" "libcoap(base:$LIBCOAP_BC)" "$LIBCOAP_BC"
run_side "$FREECOAP_HARNESS" "$FC_OUT"  "freecoap(control)"

rm -f "$LOG"
echo "== cross-check: base=libcoap  control=freecoap =="
"$CHECKER/.venv/bin/python" "$CHECKER/main.py" \
    -b "$LIBCOAP_HARNESS/$LIB_OUT" \
    -c "$FREECOAP_HARNESS/$FC_OUT" \
    -o "$LOG" -v

echo
echo "Done. Outputs:"
echo "  base  (.smt2/.resp): $LIBCOAP_HARNESS/$LIB_OUT"
echo "  control            : $FREECOAP_HARNESS/$FC_OUT"
echo "  divergence report  : $LOG"
