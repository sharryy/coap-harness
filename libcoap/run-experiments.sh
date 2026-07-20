#!/usr/bin/env bash
# Run each conformance experiment and report whether its monitor fired.
#
# Usage:  bash run-experiments.sh [--bc=FILE] [--tag=NAME] [--side=SERVER|CLIENT] [experiment_id...]
#         no args      = run all against libcoap-linked.bc (4.3.5)
#         --bc=FILE    = use a different bitcode (e.g. libcoap-431-linked.bc)
#         --tag=NAME   = prefix output dirs with NAME-  (default: empty)
#         --side=SIDE  = which side the monitor checks (default: SERVER)
#         id args      = run only the given experiment IDs

set -u

cd "$(dirname "$0")"

BC="libcoap-linked.bc"
TAG=""
SIDE="SERVER"

REST=()
for arg in "$@"; do
    case "$arg" in
        --bc=*)      BC="${arg#--bc=}" ;;
        --tag=*)     TAG="${arg#--tag=}" ;;
        --side=*)    SIDE="${arg#--side=}" ;;
        *)           REST+=("$arg") ;;
    esac
done
set -- "${REST[@]+"${REST[@]}"}"

if [ ! -f "$BC" ]; then
    echo "missing $BC (run the link step in README.md first)"
    exit 1
fi
[ -n "$TAG" ] && echo "[runner] using bitcode=$BC  tag=$TAG"

# experiment_id : human-readable name (matches the coap_*_requirement IDs in coap_monitors.h)
EXPERIMENTS=(
    "1:version"
    "2:token-length"
    "3:code"
    "4:empty-message"
    "6:mid-match"
    "7:token-match"
    "8:matching-type"
    "9:repeatable-options"
    "10:unrecognized-options"
    "11:token-spoof"
    "14:separate-token-spoof"
    "15:separate-token-ok-control"
    "17:mid-spoof"
)

if [ $# -gt 0 ]; then
    SELECTED=("$@")
else
    SELECTED=()
    for entry in "${EXPERIMENTS[@]}"; do SELECTED+=("${entry%%:*}"); done
fi

total=0
fired=0

for entry in "${EXPERIMENTS[@]}"; do
    exp_id="${entry%%:*}"
    exp_name="${entry##*:}"

    skip=1
    for s in "${SELECTED[@]}"; do
        if [ "$s" = "$exp_id" ]; then skip=0; break; fi
    done
    [ $skip -eq 1 ] && continue

    out_dir="${TAG:+${TAG}-}exp-${exp_id}-${exp_name}"
    rm -rf "$out_dir"

    echo "=============================================================="
    echo " Experiment $exp_id ($exp_name)   [bc=$BC  side=$SIDE]"
    echo "=============================================================="

    env STATE_TO_CHECK=$SIDE \
        KLEE_SYMBOLIC_EXPERIMENT=$exp_id PROTOCOL=CoAP SERVER_PORT=5683 \
        klee --libc=uclibc --posix-runtime \
             --output-dir="$out_dir" \
             --exit-on-error-type=Assert \
             --max-time=60s \
             "$BC" 2>&1 | tail -6

    err_files=( "$out_dir"/*.err )
    total=$((total + 1))
    if [ -e "${err_files[0]}" ]; then
        fired=$((fired + 1))
        first_err=$(basename "${err_files[0]}")
        first_line=$(head -2 "${err_files[0]}" | tail -1)
        echo " RESULT: monitor fired - $first_err"
        echo "         $first_line"
    else
        echo " RESULT: monitor did NOT fire (no .err files)"
    fi
    echo
done

echo "=============================================================="
echo " Summary: $fired / $total experiments fired their monitor"
echo "=============================================================="
