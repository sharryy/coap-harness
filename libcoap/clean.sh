#!/usr/bin/env bash
# Remove KLEE output dirs, detected by a run marker (run.stats/info/assembly.ll)
# rather than by name, so every run is caught regardless of its tag.

set -u
cd "$(dirname "$0")"

removed=0
for d in */; do
    [ -d "$d" ] || continue
    if [ -e "$d/run.stats" ] || [ -e "$d/info" ] || [ -e "$d/assembly.ll" ]; then
        rm -rf "$d"
        echo "removed $d"
        removed=$((removed + 1))
    fi
done

[ "$removed" -eq 0 ] && echo "nothing to clean"
echo "done ($removed removed)"
