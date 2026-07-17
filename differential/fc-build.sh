#!/usr/bin/env bash
# Build the libcoap-client -> FreeCoAP-server KLEE target (freecoap-linked.bc).
#   fc-main.c   : lib-agnostic orchestrator
#   lc-client.c : libcoap client  (libcoap headers)
#   fc-server.c : FreeCoAP server  (FreeCoAP headers)
#   *_stubs     : getaddrinfo/timerfd/getifaddrs stubs (--override so they win)
# Linked against the prebuilt libcoap bitcode + FreeCoAP server bitcode.
set -euo pipefail
cd "$(dirname "$0")"

LIBCOAP_DIR=${LIBCOAP_DIR:-$HOME/project/kleener-experiments/repos/libcoap}
FC=$HOME/project/kleener-experiments/repos/FreeCoAP
FC_INC=$FC/lib/include
LC_INC=$LIBCOAP_DIR/include
FC_LIB=$LIBCOAP_DIR/.libs/libcoap-3-notls.bc

mkdir -p bc
E="clang -emit-llvm -c -g -O0"

# per-module compile, each with only its own library's headers
$E -I$LC_INC          lc-client.c            -o bc/lc-client.bc
$E -I$FC_INC          fc-server.c            -o bc/fc-server.bc
$E                    fc-main.c              -o bc/fc-main.bc
$E                    freecoap_klee_stubs.c  -o bc/stubs.bc

for f in coap_msg coap_mem coap_log coap_server; do
    $E -I$FC_INC $FC/lib/src/$f.c -o bc/$f.bc
done

llvm-link \
    bc/fc-main.bc bc/lc-client.bc bc/fc-server.bc \
    --override=bc/stubs.bc \
    bc/coap_server.bc bc/coap_msg.bc bc/coap_mem.bc bc/coap_log.bc \
    "$FC_LIB" \
    -o freecoap-linked.bc

echo "[fc-build] wrote freecoap-linked.bc"
