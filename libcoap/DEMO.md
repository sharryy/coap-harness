# Demo: differential CoAP monitoring of libcoap 4.3.5 vs 4.3.1

All paths below are absolute so the commands work from any directory.
`klee`, `wllvm`, `extract-bc`, and `clang-13` are already on `PATH`.

```bash
HARNESS=$HOME/project/kleener-experiments/libcoap
SRC435=$HOME/project/kleener-experiments/repos/libcoap          # tag v4.3.5 (7cf7465b)
SRC431=$HOME/project/kleener-experiments/repos/libcoap-4.3.1    # tag v4.3.1 (c694bae)
```

| Version | Source tree | Whole-program bitcode | Linked harness bitcode |
|---------|-------------|-----------------------|------------------------|
| 4.3.5 (fixed) | `$SRC435` | `$SRC435/.libs/libcoap-3-notls.bc` | `libcoap-linked.bc` |
| 4.3.1 (buggy) | `$SRC431` | `$SRC431/.libs/libcoap-3-notls.bc` | `libcoap-linked-431.bc` |

---

## A. Quick demo — run the differential on the prebuilt bitcode

The two `libcoap-linked*.bc` files are already built, so a demo needs no rebuild.

```bash
cd "$HARNESS"

# Fixed release: expect 0/9 monitors fire (fully conformant baseline)
bash run-experiments.sh --bc=libcoap-linked.bc      --tag=v435

# Buggy release: expect 4/9 fire (PRs #1376, #1300, #1295, #1389)
bash run-experiments.sh --bc=libcoap-linked-431.bc  --tag=v431
```

Each run prints, per experiment, whether the monitor fired and the summary line
`Summary: N / 9 experiments fired their monitor`.

### Run a single requirement (good for a live walk-through)

```bash
cd "$HARNESS"

# Experiment 1 = version validity (PR #1376). FIREs on 4.3.1, silent on 4.3.5.
bash run-experiments.sh --bc=libcoap-linked-431.bc --tag=demo 1
bash run-experiments.sh --bc=libcoap-linked.bc     --tag=demo 1

# Read the violation KLEE recorded for the buggy build:
cat demo-exp-1-version/*.assert.err
```

Experiment IDs: `1` version, `2` token-length, `3` code, `4` empty-message,
`6` mid-match, `7` token-match, `8` matching-type, `9` repeatable-options,
`10` unrecognized-options. (`5` is retired.)

### The raw KLEE command behind the runner

```bash
cd "$HARNESS"
env STATE_TO_CHECK=SERVER KLEE_SYMBOLIC_EXPERIMENT=1 PROTOCOL=CoAP SERVER_PORT=5683 \
    klee --libc=uclibc --posix-runtime \
         --output-dir=demo-out \
         --exit-on-error-type=Assert \
         --max-time=60s \
         libcoap-linked-431.bc
```

### Clean up demo output between runs

```bash
cd "$HARNESS" && bash clean.sh          # removes KLEE output dirs only
```

---

## B. Rebuild the libcoap whole-program bitcode (both versions)

Only needed if a libcoap source tree changed. Both versions use the **same**
configure line — the flags below are exactly the subset that exists in *both*
4.3.1 and 4.3.5 (newer `--disable-*` switches that 4.3.5 added do not exist in
4.3.1, so they are omitted to keep one command working for both).

```bash
export LLVM_COMPILER=clang        # tells wllvm to emit bitcode alongside objects

CONFIGURE_FLAGS="--disable-dtls --disable-tcp --disable-async \
                 --disable-documentation --disable-doxygen --disable-manpages \
                 --disable-shared --enable-static --enable-examples"
```

### 4.3.5

```bash
cd "$SRC435"
git checkout v4.3.5            # ensure the exact release tag
./autogen.sh                  # only if ./configure is missing
./configure $CONFIGURE_FLAGS CC=wllvm
make -j$(nproc)
extract-bc .libs/libcoap-3-notls.a                       # -> libcoap-3-notls.a.bc
ln -sf libcoap-3-notls.a.bc .libs/libcoap-3-notls.bc     # name the harness links against
```

### 4.3.1 (two extra quirks)

```bash
cd "$SRC431"
git checkout v4.3.1
./autogen.sh
./configure $CONFIGURE_FLAGS CC=wllvm
touch libcoap-3.sym           # bypass the ctags-dependent .sym rule (ctags not installed)
make -j$(nproc)
extract-bc .libs/libcoap-3-notls.a
ln -sf libcoap-3-notls.a.bc .libs/libcoap-3-notls.bc
```

> 4.3.1 also renamed three logging/handler identifiers; the harness already
> carries `#ifndef` compatibility shims so the same `libcoap-standalone.c`
> compiles against either version — no source edit needed.

---

## C. Rebuild the linked harness bitcode

After (re)building the libcoap bitcode, relink the harness. The `Makefile`
already points at both source trees.

```bash
cd "$HARNESS"
make libcoap-linked.bc        # harness + klee_stubs + 4.3.5 bitcode
make libcoap-linked-431.bc    # harness + klee_stubs + 4.3.1 bitcode
```

What each target does (from the `Makefile`):
1. `clang -emit-llvm -c` the harness → `libcoap-standalone[-431].bc`
2. `llvm-link <harness> --override=klee_stubs.bc <libcoap-3-notls.bc>` → linked module

`klee_stubs.bc` overrides `getifaddrs` / `coap_log_impl` with KLEE-safe versions
(the `--override` is what lets 4.3.1's `vfprintf`-based logging not trip an
"unimplemented intrinsic").

Native (non-KLEE) sanity binaries, if you want a tcpdump-sniffable exchange:

```bash
make libcoap-standalone        # links against 4.3.5 .libs/libcoap-3-notls.a
make libcoap-standalone-431    # links against 4.3.1
```

To watch the packets on the wire, sniff loopback in one terminal and run the
exchange in another:

```bash
sudo tcpdump -i lo -n udp port 5683    # terminal 1: capture
make run-native                        # terminal 2: build + run libcoap-standalone
```

---

## D. Expected result

| Exp | Monitor | 4.3.5 | 4.3.1 | PR |
|----:|---------|:-----:|:-----:|----|
| 1 | version            | silent | **FIRE** | #1376 |
| 3 | code               | silent | **FIRE** | #1300 |
| 8 | matching-type      | silent | **FIRE** | #1295 |
| 9 | repeatable-options | silent | **FIRE** | #1389 |
| 2,4,6,7,10 | (rest)    | silent | silent   | conformant on both |

**4.3.5 → 0/9 fire, 4.3.1 → 4/9 fire.** A `.assert.err` in an experiment's
output dir means the monitor caught the violation; no `.err` means it stayed
silent.
```
