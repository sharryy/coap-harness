#!/usr/bin/env python3
"""Statistics table for a run-all.sh results directory."""
import re
import sqlite3
import sys
from pathlib import Path

res = Path(sys.argv[1] if len(sys.argv) > 1 else '.')


def klee(d):
    """(test cases, wall time in s) of one Kleener output dir."""
    if not d.is_dir():
        return None
    tests = len(list(d.glob('*.ktest')))
    try:
        con = sqlite3.connect(d / 'run.stats')
        t = con.execute('select WallTime from stats order by rowid desc limit 1').fetchone()[0] / 1e6
    except Exception:
        m = re.search(r'Elapsed: (\d+):(\d+):(\d+)', (d / 'info').read_text())
        t = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])
    return tests, t


def check(log):
    """(all, unique, time in s) from a cross-checker log."""
    if not log.is_file():
        return None
    txt = log.read_text()
    g = lambda k: re.search(k, txt)
    if not g(r'discrepancies \(all\): (\d+)'):
        return None
    return int(g(r'discrepancies \(all\): (\d+)')[1]), int(g(r'discrepancies \(unique\): (\d+)')[1]), \
        int(g(r'exec\. time (\d+)s')[1])


print('| Version | Monitor | LibCoAP test cases | LibCoAP time | FreeCoAP test cases | FreeCoAP time '
      '| Discrepancies (all) | Discrepancies (unique) | Cross-check time |')
print('|---|---|--:|--:|--:|--:|--:|--:|--:|')
for log in sorted(res.glob('check-*.log'), key=lambda p: p.stat().st_mtime):
    v, m = log.stem[len('check-'):].split('-', 1)
    lc, fc, ck = klee(res / f'libcoap-{v}-{m}'), klee(res / f'freecoap-{v}-{m}'), check(log)
    cell = lambda x, i, f: f.format(x[i]) if x else '?'
    print(f'| {v} | {m} | {cell(lc, 0, "{}")} | {cell(lc, 1, "{:.1f} s")} | {cell(fc, 0, "{}")} '
          f'| {cell(fc, 1, "{:.1f} s")} | {cell(ck, 0, "{}")} | {cell(ck, 1, "{}")} | {cell(ck, 2, "{} s")} |')
