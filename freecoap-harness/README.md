# FreeCoAP harnesses

Two separate harnesses live here, one per subdirectory, so each has a single job.

| Directory | Exchange | Runs under | What it is for |
|-----------|----------|-----------|----------------|
| `lc-to-fc/` | libcoap client to FreeCoAP server | Kleener / KLEE | The differential control side: a libcoap client drives a FreeCoAP server so the two stacks can be compared on the same request. `diff.sh` uses this one. |
| `fc-to-fc/` | FreeCoAP client to FreeCoAP server | native | A standalone sanity demo that forks a FreeCoAP client and server and runs one exchange over loopback. No KLEE involved. |

Each subdirectory is self-contained, with its own `Makefile` and `.gitignore`.
Build `lc-to-fc/` with `make build` (which runs `fc-build.sh`), and `fc-to-fc/`
with `make`.
