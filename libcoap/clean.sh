#!/usr/bin/env bash
# Remove KLEE output directories from the harness folder.
# A KLEE output dir is detected by a run marker (run.stats / info / assembly.ll)
# rather than a name pattern, so this catches every run regardless of how it was
# tagged (exp-*, v431-*, v435-*, disp-*, byte-test, …).

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
