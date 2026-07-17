#!/usr/bin/env bash

set -u
cd "$(dirname "$0")"

removed=0

# 1) KLEE output directories (marker-based, name-agnostic)
for d in */; do
    [ -d "$d" ] || continue
    if [ -e "$d/run.stats" ] || [ -e "$d/info" ] || [ -e "$d/assembly.ll" ]; then
        rm -rf "$d"
        echo "removed $d"
        removed=$((removed + 1))
    fi
done

# 2) klee-last symlink KLEE leaves behind
if [ -L klee-last ] || [ -e klee-last ]; then
    rm -rf klee-last
    echo "removed klee-last"
    removed=$((removed + 1))
fi

# 3) build products (regenerate with fc-build.sh / make)
for f in freecoap-linked.bc bc; do
    if [ -e "$f" ]; then
        rm -rf "$f"
        echo "removed $f"
        removed=$((removed + 1))
    fi
done

[ "$removed" -eq 0 ] && echo "nothing to clean"
echo "done ($removed removed)"
