# CoAP testing harnesses for Kleener

Testing harnesses for CoAP implementations, built for two related jobs:

1. **Differential and conformance testing** under symbolic execution. A libcoap
   client drives a server (either libcoap itself or FreeCoAP) inside Kleener, a
   monitor turns one field of the request into a symbolic value, and KLEE records
   the server's response on every path it explores. Comparing the two servers'
   recorded responses, or the same server across two libcoap versions, surfaces
   places where they disagree about RFC 7252. Those disagreements are the leads.
2. **Memory-safety reproducers.** Small native programs under `security/` that
   trigger the memory bugs found in libcoap during this work, each one an ASan
   crash you can run in a second. See [`security/README.md`](security/README.md).

This README covers what you need installed, how the differential pipeline fits
together, and how to point it at an implementation. If you just want a scripted
walk-through of the older per-requirement conformance monitors, read
[`libcoap/README.md`](libcoap/README.md) instead.

## Repository layout

| Path | What it is |
|------|-----------|
| `libcoap/` | A single-process libcoap client+server driver. This is the base side of a differential run, and also the target for the conformance monitors and the cross-version (4.3.1 vs 4.3.5) runs. |
| `freecoap/` | A native FreeCoAP client-and-server round-trip, kept as a standalone sanity demo (no KLEE). |
| `differential/` | The cross-implementation comparison: the libcoap-client to FreeCoAP-server harness (the control side) plus `diff.sh`, the runner that drives it against `libcoap/` and cross-checks the two. |
| `security/` | Native ASan reproducers for the libcoap memory-safety findings. |
| `Makefile` | `make clean` / `make clean-logs` to wipe run artifacts. |

## Prerequisites

This repository holds the harnesses and the reproducers. It does **not** carry the
symbolic-execution engine, the monitors, or the implementations under test. Those
live elsewhere and have to be in place before anything here runs.

- **Kleener** - the KLEE fork that provides the CoAP socket model and the monitors.
  The harnesses do not link against it; instead you run them under a `klee` binary
  built from Kleener, and the monitor is selected at run time by the
  `KLEE_SYMBOLIC_EXPERIMENT` environment variable. The differential recording
  monitors (IDs 100 and up) and the conformance monitors (IDs 1 to 10) both live in
  `runtime/Intrinsic/Protocols/CoAP/` inside that fork. If you change a monitor you
  rebuild Kleener's runtime, not this repo.
- **LLVM / Clang 13**, plus `llvm-link`, and **wllvm** with `extract-bc`. wllvm is
  what lets you compile an implementation normally and still get one whole-program
  bitcode file out the other end, which is what KLEE consumes.
- **KLEE 3.0** (the one built from Kleener) on your `PATH`.
- **The cross-checker.** `diff.sh` calls
  [`smt-cross-checker`](https://gitlab.com/symbolic-differential-testing/smt-cross-checker),
  which is not part of this repo. Clone it into `tools/smt-cross-checker` and make a
  virtualenv with its `requirements.txt` (it needs `z3-solver` and `pysmt`):
  ```sh
  git clone https://gitlab.com/symbolic-differential-testing/smt-cross-checker tools/smt-cross-checker
  python3 -m venv tools/smt-cross-checker/.venv
  tools/smt-cross-checker/.venv/bin/pip install -r tools/smt-cross-checker/requirements.txt
  ```
- **The implementation source trees**, under `repos/` (also not tracked here):
  libcoap at tag `v4.3.5`, libcoap at tag `v4.3.1` for the cross-version runs, and
  FreeCoAP. The build recipe for the libcoap bitcode is in
  [`libcoap/README.md`](libcoap/README.md), "Build the whole-program bitcode".
- **For the security reproducers only:** `gcc` and an ASan-instrumented libcoap
  build. No KLEE involved there.

A note on paths: `differential/diff.sh` and the Makefiles currently assume this tree
lives at `$HOME/project/kleener-experiments`, and `diff.sh` hard-codes the Kleener
build directory it rebuilds when you pass `BUILD=1`. If you work somewhere else,
adjust those spots.

## How a differential run fits together

Every run comes down to three bitcode ingredients linked into one module that KLEE
executes:

1. **The implementation as whole-program bitcode.** Configure and build the SUT with
   `CC=wllvm`, then `extract-bc` the static library. For libcoap this yields
   `.libs/libcoap-3-notls.bc`. This step is only needed once per SUT version.
2. **The harness as bitcode.** `clang -emit-llvm -c` on `libcoap-standalone.c` (or
   the FreeCoAP harness sources).
3. **A small set of stubs**, linked with `llvm-link --override=` so they win over the
   real symbols. These replace host calls KLEE cannot model cleanly (logging,
   `getifaddrs`, and for FreeCoAP `getaddrinfo` and `timerfd`).

`make libcoap-linked.bc` in `libcoap/` does steps 2 and 3, linking against the tree
in `LIBCOAP_DIR`. Build another version by pointing `LIBCOAP_DIR` at it and naming
the output (`make linked LINKED=libcoap-431-linked.bc LIBCOAP_DIR=...`).
`differential/fc-build.sh` does the equivalent for the FreeCoAP side.

## Running an experiment

Once the linked bitcode exists on both sides:

```sh
differential/diff.sh 100    # run experiment 100 on libcoap and FreeCoAP, then cross-check
```

`diff.sh` runs the experiment on each side, checks that the monitor actually went
symbolic (the sanity gate), and then runs the cross-checker, which conjoins the two
runs' path constraints with "the recorded outputs differ" and asks Z3 whether that is
satisfiable. A satisfiable answer is a concrete request on which the two servers
respond differently. The divergence report lands in
`differential/diff-exp<ID>.log`.

The recording monitors currently registered:

| ID | Field made symbolic | Notes |
|----|---------------------|-------|
| 100 | request code | the method / code byte |
| 101 | version | the 2-bit version field |
| 102 | type | CON / NON / ACK / RST |
| 103 | token length | the TKL nibble |
| 104 | message ID | plus type, since the echo obligation depends on it |
| 105 | type and code together | the joint header-acceptance matrix; subsumes 102 |

To compare libcoap against an older libcoap instead of against FreeCoAP, point the
base side at the other version and tag the output so it does not collide:

```sh
LIBCOAP_BC=libcoap-431-linked.bc TAG=-431 differential/diff.sh 101
```

This cross-version axis is worth keeping even though it looks redundant: when both
implementations share a bug, the cross-implementation comparison can hide it (both
agree), while comparing a fixed libcoap against a buggy one shows it plainly.

Useful knobs, all environment variables read by `diff.sh`: `MAX_TIME` (KLEE time cap,
default `60s`), `LIBCOAP_BC` (which base bitcode), `TAG` (suffix for output dirs and
the log), and `BUILD=1` (rebuild the Kleener runtime first).

## Adding another implementation

The pieces generalise to any CoAP server you can compile to bitcode:

1. Build the server to whole-program bitcode with wllvm and `extract-bc`.
2. Write a harness that drives it. It has to obey the Kleener execution model: one
   process, no forking and no blocking loop, a single valid CoAP request/response
   exchange, and the small hooks the socket model expects. `differential/` is the
   worked example of adapting a server whose own API wanted to block. Reuse the
   libcoap client (`differential/lc-client.c`) so the client is a fixed variable and
   only the server changes.
3. Compile the harness to bitcode and `llvm-link` it with the server bitcode and the
   override stubs.
4. Add the new side to `diff.sh` as either the base or the control, and run.

The one part that does not come for free is deciding whether a server can be driven
in a single non-blocking process at all. That is a design call, not a mechanical one.

## Watching packets on the wire

The differential runs execute under KLEE with an in-process socket model, so they
never touch a real socket. The native builds do: `libcoap/`'s `make run-native` and
`freecoap/`'s `make run` each do a real CoAP exchange on loopback `udp/5683`, which
you can capture with tcpdump in another terminal:

```sh
sudo tcpdump -i lo -n udp port 5683
```

## Cleaning up

`make clean` from the repo root removes KLEE output directories and logs across the
harnesses; `make clean-logs` removes just the logs. Each harness also has its own
`clean.sh` for its local run directories.
