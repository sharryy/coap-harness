#!/usr/bin/env bash
# Build the FreeCoAP harnesses: FreeCoAP 18ee7d4 with a LibCoAP 4.3.5 client, ffc87fd with 4.3.1.
# Needs the LibCoAP trees built by ../libcoap/configure.sh.
set -euo pipefail
cd "$(dirname "$0")"
REPOS="$(cd .. && pwd)/repos"

for pair in 18ee7d4:4.3.5 ffc87fd:4.3.1; do
    rev="${pair%:*}" ver="${pair#*:}"
    src="$REPOS/FreeCoAP-$rev"
    if [ ! -d "$src" ]; then
        git clone -q https://github.com/keith-cullen/FreeCoAP.git "$src"
        git -C "$src" checkout -q "$rev"
    fi
    # the harness needs one exchange step; FreeCoAP only exposes the endless coap_server_run
    grep -q coap_server_exchange_once "$src/lib/src/coap_server.c" ||
        printf '\nint coap_server_exchange_once(coap_server_t *server) { return coap_server_exchange(server); }\n' \
            >> "$src/lib/src/coap_server.c"
    LIBCOAP_DIR="$REPOS/libcoap-$ver" FC_DIR="$src" OUT="freecoap-$rev-${ver//./}-linked.bc" \
        BCDIR="bc-$rev" ./fc-build.sh
done
