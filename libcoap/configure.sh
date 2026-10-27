#!/usr/bin/env bash
# Build LibCoAP 4.3.5 and 4.3.1 as whole-program bitcode and link the harness against each.
set -euo pipefail
cd "$(dirname "$0")"
REPOS="$(cd .. && pwd)/repos"
mkdir -p "$REPOS"
export LLVM_COMPILER=clang

for ver in 4.3.5 4.3.1; do
    src="$REPOS/libcoap-$ver"
    [ -d "$src" ] || git clone -q --depth 1 --branch "v$ver" https://github.com/obgm/libcoap.git "$src"
    (
        cd "$src"
        [ -x configure ] || ./autogen.sh
        ./configure --disable-dtls --disable-tcp --disable-async \
                    --disable-documentation --disable-doxygen --disable-manpages \
                    --disable-shared --enable-static --disable-examples CC=wllvm
        # skips the ctags-dependent symbol-file rule (static build only)
        touch libcoap-3.sym
        make -j"$(nproc)"
        extract-bc -b .libs/libcoap-3-notls.a
        ln -sf libcoap-3-notls.a.bc .libs/libcoap-3-notls.bc
    )
    make linked LINKED="libcoap-${ver//./}-linked.bc" LIBCOAP_DIR="$src"
done
