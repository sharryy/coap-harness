#!/usr/bin/env bash
# Run each KLEE_SYMBOLIC_EXPERIMENT against the cantcoap single-process bitcode
# and report whether the monitor fired. Mirrors the libcoap-harness runner.
#
# Usage:  bash run-experiments.sh [--side=SERVER|CLIENT] [experiment_id...]
set -u
cd "$(dirname "$0")"

BC="cantcoap-linked.bc"
SIDE="SERVER"
REST=()
for arg in "$@"; do
    case "$arg" in
        --side=*) SIDE="${arg#--side=}" ;;
        *)        REST+=("$arg") ;;
    esac
done
set -- "${REST[@]+"${REST[@]}"}"

[ -f "$BC" ] || { echo "missing $BC (run 'make bc' first)"; exit 1; }

EXPERIMENTS=(
    "1:version" "2:token-length" "3:code" "4:empty-message"
    "6:mid-match" "7:token-match" "8:matching-type"
    "9:repeatable-options" "10:unrecognized-options"
)

if [ $# -gt 0 ]; then SELECTED=("$@"); else
    SELECTED=(); for e in "${EXPERIMENTS[@]}"; do SELECTED+=("${e%%:*}"); done
fi

total=0; fired=0
for entry in "${EXPERIMENTS[@]}"; do
    exp_id="${entry%%:*}"; exp_name="${entry##*:}"
    skip=1; for s in "${SELECTED[@]}"; do [ "$s" = "$exp_id" ] && skip=0; done
    [ $skip -eq 1 ] && continue

    out="cc-exp-${exp_id}-${exp_name}"; rm -rf "$out"
    env STATE_TO_CHECK=$SIDE KLEE_SYMBOLIC_EXPERIMENT=$exp_id \
        PROTOCOL=CoAP SERVER_PORT=5683 \
        klee --libc=uclibc --posix-runtime --output-dir="$out" \
             --exit-on-error-type=Assert --max-time=60s "$BC" >/dev/null 2>&1

    total=$((total + 1))
    errs=( "$out"/*.err )
    if [ -e "${errs[0]}" ]; then
        fired=$((fired + 1))
        msg=$(grep -m1 'ASSERTION FAIL' "${errs[0]}" | sed 's/.*&& //')
        printf "exp %-2s %-20s FIRED  %s\n" "$exp_id" "$exp_name" "$msg"
    else
        printf "exp %-2s %-20s no fire\n" "$exp_id" "$exp_name"
    fi
done
echo "------------------------------------------------------------"
echo "Summary: $fired / $total monitors fired"
