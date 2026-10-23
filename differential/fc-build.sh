#!/usr/bin/env bash
# Build the libcoap-client -> FreeCoAP-server KLEE target (freecoap-linked.bc):
# compile each module against its own library's headers, then llvm-link them
# with the libcoap and FreeCoAP bitcode and the override stubs.
set -euo pipefail
cd "$(dirname "$0")"

LIBCOAP_DIR=${LIBCOAP_DIR:-$HOME/project/kleener-experiments/repos/libcoap}
FC=${FC_DIR:-$HOME/project/kleener-experiments/repos/FreeCoAP}
OUT=${OUT:-freecoap-linked.bc}
BCDIR=${BCDIR:-bc}
FC_INC=$FC/lib/include
LC_INC=$LIBCOAP_DIR/include
FC_LIB=$LIBCOAP_DIR/.libs/libcoap-3-notls.bc

mkdir -p "$BCDIR"
E="clang -emit-llvm -c -g -O0"

# per-module compile, each with only its own library's headers
$E -I$LC_INC          lc-client.c            -o $BCDIR/lc-client.bc
$E -I$FC_INC          fc-server.c            -o $BCDIR/fc-server.bc
$E                    fc-main.c              -o $BCDIR/fc-main.bc
$E                    freecoap_klee_stubs.c  -o $BCDIR/stubs.bc

for f in coap_msg coap_mem coap_log coap_server; do
    $E -I$FC_INC $FC/lib/src/$f.c -o $BCDIR/$f.bc
done

llvm-link \
    $BCDIR/fc-main.bc $BCDIR/lc-client.bc $BCDIR/fc-server.bc \
    --override=$BCDIR/stubs.bc \
    $BCDIR/coap_server.bc $BCDIR/coap_msg.bc $BCDIR/coap_mem.bc $BCDIR/coap_log.bc \
    "$FC_LIB" \
    -o "$OUT"

echo "[fc-build] wrote $OUT from $FC"
