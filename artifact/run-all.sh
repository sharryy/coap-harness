#!/usr/bin/env bash
# Run every experiment on LibCoAP and FreeCoAP for both versions, cross-check each pair, print the statistics.
# usage: run-all.sh [results-dir] [experiment...]   (env: VERSIONS, CHECKER, PYTHON)
set -uo pipefail
H="$(cd "$(dirname "$0")/.." && pwd)"
RES="$(mkdir -p "${1:-results}" && cd "${1:-results}" && pwd)"
shift || true
EXPERIMENTS="${*:-code version type token-length mid type-code token uri-query-len
  opt-len-if-match opt-len-uri-host opt-len-if-none-match opt-len-uri-port
  opt-len-location-path opt-len-uri-path opt-len-content-format opt-len-max-age
  opt-len-accept opt-len-location-query}"
CHECKER="${CHECKER:-/artifact/tools/smt-cross-checker-coap/main.py}"

# run.sh in harness dir $1, then move its output dir to $RES/$2
run () {
  local before after
  before=$(ls -d "$H/$1/$v-$e-SERVER-"* 2>/dev/null)
  ( cd "$H/$1" && ./run.sh -e "$e" -r SERVER -V "$v" ) > "$RES/$2.klee.log" 2>&1
  after=$(comm -13 <(echo "$before") <(ls -d "$H/$1/$v-$e-SERVER-"* 2>/dev/null))
  rm -rf "${RES:?}/$2"
  mv "$after" "$RES/$2"
}

for v in ${VERSIONS:-latest original}; do
  for e in $EXPERIMENTS; do
    run libcoap "libcoap-$v-$e"
    run differential "freecoap-$v-$e"
    rm -f "$RES/check-$v-$e.log"
    "${PYTHON:-python3}" "$CHECKER" -b "$RES/libcoap-$v-$e" -c "$RES/freecoap-$v-$e" \
      -o "$RES/check-$v-$e.log" -v > /dev/null 2>&1
    echo "[run-all] $v $e done"
  done
done
python3 "$H/artifact/stats.py" "$RES" | tee "$RES/stats.md"
