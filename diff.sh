#!/usr/bin/env bash
#
# diff.sh — run ONE CoAP differential-testing experiment end to end:
#   [rebuild runtime] -> libcoap run (base) -> freecoap run (control)
#   -> sanity-gate each run -> smt-cross-checker
#
# Usage:
#   ./diff.sh <EXP>              # run experiment EXP on both impls + cross-check
#   BUILD=1 ./diff.sh <EXP>      # rebuild the Kleener runtime first
#   MAX_TIME=120s ./diff.sh <EXP>
#
# Recording-monitor experiment IDs (must be >= 100 so --write-smt2s kicks in):
#   100 = code   101 = version   102 = type   103 = token_length
#
set -euo pipefail

EXP="${1:-}"
if [ -z "$EXP" ]; then
    echo "usage: $0 <EXP-id>   (100=code 101=version 102=type 103=token_length)" >&2
    exit 2
fi

# Base libcoap bitcode to diff against FreeCoAP. Default = the 4.3.5 build; set
# LIBCOAP_BC=libcoap-431-linked.bc to reproduce the paper's 4.3.1 non-conformances.
# TAG suffixes the output dirs + log so 4.3.1 and 4.3.5 runs don't collide.
LIBCOAP_BC="${LIBCOAP_BC:-libcoap-linked.bc}"
TAG="${TAG:-}"

MAX_TIME="${MAX_TIME:-60s}"
# Path-explosion / crash guards passed to KLEE. --max-memory caps RAM (a crash
# guard, not a coverage cap); --max-time bounds wall-clock. We deliberately do
# NOT cap forks: for a differential tool a silent fork cap could drop the very
# path that diverges. Instead the gate below REPORTS incomplete exploration.
EXTRA="${EXTRA:---max-memory=4000}"

# All paths are derived from this script's own location.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBCOAP_HARNESS="$ROOT/libcoap-harness"
FREECOAP_HARNESS="$ROOT/freecoap-harness"
CHECKER="$ROOT/tools/smt-cross-checker"
BUILD_DIR="/home/shehryar/kleener-install/klee_build130stp_z3"

LIB_OUT="klee-diff-libcoap-${EXP}${TAG}"
FC_OUT="klee-diff-freecoap-${EXP}"
LOG="$ROOT/diff-exp${EXP}${TAG}.log"

# ---- sanity gate: prove the monitor actually fired --------------------------
# The failure this morning: a run defaulted to EXP=0, so it produced no .smt2 /
# .resp. Guard against it — the info file must show --write-smt2s AND a non-zero
# query count, or we abort loudly instead of cross-checking empty dirs.
sanity_gate () {
    local dir="$1" name="$2" info="$1/info"
    [ -f "$info" ] || { echo "FAIL[$name]: no info file in $dir" >&2; exit 1; }
    grep -q -- '--write-smt2s' "$info" || {
        echo "FAIL[$name]: --write-smt2s absent — EXP<100 or recipe not applied" >&2; exit 1; }
    local q
    q="$(grep -oE 'total queries = [0-9]+' "$info" | grep -oE '[0-9]+$' | head -1 || true)"
    if [ "${q:-0}" -eq 0 ] 2>/dev/null; then
        # queries==0 is LEGITIMATE for a pure-echo monitor: a symbolic input that
        # is copied into the response but never branched on (token/MID echo) needs
        # no solver call, yet the recorded output still depends on the symbolic
        # input. Only FAIL if the output is ALSO fully concrete — the real failure
        # this gate was added for (a run that defaulted to EXP=0 / monitor never
        # fired). A symbolic .resp contains a `(select <array> ...)` expression.
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
    # A path ends one of two ways here: it RECORDS a response
    # (terminateStateOnResponse, which KLEE tallies as "partially completed"), or
    # the server DROPPED and the client's bounded loop exits normally (KLEE
    # "completed"). So partially-completed == responses, completed == drops —
    # NEITHER is a time-cap signal.
    responded="$(grep -oE 'partially completed paths = [0-9]+' "$info" | grep -oE '[0-9]+$' | head -1 || echo 0)"
    dropped="$(grep -E 'done: completed paths' "$info" | grep -oE '[0-9]+' | tail -1 || echo 0)"
    echo "OK[$name]: queries=$q  paths=$explored  responded=$responded  dropped=$dropped  (smt2=$nsmt resp=$nresp)"
    [ "$nresp" -gt 0 ] || echo "NOTE[$name]: 0 responses — every explored path dropped the request (respond-vs-drop only)"
    # REAL incompleteness signal: did the run consume ~all of MAX_TIME? If so KLEE
    # halted with states still queued (genuine path/expression explosion). A run
    # that finishes well under MAX_TIME explored everything.
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
    rm -rf "${harness:?}/$outdir"          # KLEE refuses to reuse an existing --output-dir
    ( cd "$harness" && make run EXP="$EXP" OUTDIR="$outdir" MAX_TIME="$MAX_TIME" ${bc:+BC="$bc"} )
    sanity_gate "$harness/$outdir" "$name"
}

# ---- optional runtime rebuild ----------------------------------------------
if [ "${BUILD:-0}" = "1" ]; then
    echo "== rebuilding Kleener runtime =="
    make -C "$BUILD_DIR" -j"$(nproc)"
fi

# ---- run both implementations ----------------------------------------------
run_side "$LIBCOAP_HARNESS"  "$LIB_OUT" "libcoap(base:$LIBCOAP_BC)" "$LIBCOAP_BC"
run_side "$FREECOAP_HARNESS" "$FC_OUT"  "freecoap(control)"

# ---- cross-check ------------------------------------------------------------
# main.py's check_output_file refuses a pre-existing log, so clear it first.
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
