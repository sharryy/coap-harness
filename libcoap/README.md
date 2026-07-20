# The libcoap harness

The libcoap client-and-server driver, all in one process. This is the base side
of a differential run, and also the target for the conformance monitors and the
cross-version (4.3.5 vs 4.3.1) runs. The differential runner that drives it
against FreeCoAP lives in [`../differential`](../differential); the pipeline as
a whole is described in the [top-level README](../README.md).

## Layout

- `libcoap-standalone.c` - the single-process client + server harness. The same
  source compiles against both libcoap versions (it carries `#ifndef` compat
  shims for the few identifiers 4.3.1 and 4.3.5 name differently).
- `klee_stubs.c` - stubs for the libc calls KLEE's runtime cannot model
  (`getifaddrs`, the clock), linked with `--override` so they win.
- `Makefile` - builds the linked bitcode and the native binary, and runs KLEE.
- `run-experiments.sh` - runs the conformance monitors (IDs 1-10) and reports
  which ones fired.

## The libcoap trees used here

Two source trees under `../repos`, built the same way. All paths below are
absolute so the commands work from any directory.

```bash
HARNESS=$HOME/project/kleener-experiments/libcoap
SRC435=$HOME/project/kleener-experiments/repos/libcoap          # tag v4.3.5 (fixed)
SRC431=$HOME/project/kleener-experiments/repos/libcoap-4.3.1    # tag v4.3.1 (buggy)
```

The harness is version-agnostic: point `LIBCOAP_DIR` at any libcoap source tree
and it links against that. This work used two, 4.3.5 (fixed) and 4.3.1 (buggy),
to compare a release against its predecessor. Nothing about a version is baked
into the build.

## Build the whole-program bitcode

Needed once per source tree. Configure with `wllvm` so a whole-program bitcode
file falls out beside the static library, then extract it:

```bash
cd "$SRC435"                      # or any libcoap tree
export LLVM_COMPILER=clang
./autogen.sh                      # only if ./configure is missing
./configure --disable-dtls --disable-tcp --disable-async \
            --disable-documentation --disable-doxygen --disable-manpages \
            --disable-shared --enable-static --enable-examples CC=wllvm
make -j$(nproc)
extract-bc .libs/libcoap-3-notls.a
ln -sf libcoap-3-notls.a.bc .libs/libcoap-3-notls.bc   # name the harness links against
```

Those configure flags are the subset common to 4.3.1 and 4.3.5, so the one line
works for both. An older tree may need `touch libcoap-3.sym` before `make` (it
skips a ctags-dependent rule when ctags is missing).

## Link the harness bitcode

Point `LIBCOAP_DIR` at the tree and name the output:

```bash
cd "$HARNESS"
make libcoap-linked.bc                                          # default tree (LIBCOAP_DIR)
make linked LINKED=libcoap-431-linked.bc LIBCOAP_DIR=$SRC431    # any other tree
```

`llvm-link` combines the harness with the libcoap bitcode and `klee_stubs.bc`,
whose `--override` replaces `getifaddrs` and the logging path KLEE cannot model.
The output name is what you then pass to `diff.sh` as `LIBCOAP_BC` or to
`run-experiments.sh` as `--bc`.

## Run the conformance monitors

`run-experiments.sh` runs each conformance monitor in turn and reports whether it
fired. The fixed release should stay silent; the buggy one should fire on four
requirements.

```bash
cd "$HARNESS"

# Fixed release: expect 0/9 monitors fire (fully conformant baseline)
bash run-experiments.sh --bc=libcoap-linked.bc      --tag=v435

# Buggy release: expect 4/9 fire (PRs #1376, #1300, #1295, #1389)
bash run-experiments.sh --bc=libcoap-431-linked.bc  --tag=v431
```

To run a single requirement (good for a live walk-through):

```bash
# Experiment 1 = version validity (PR #1376). Fires on 4.3.1, silent on 4.3.5.
bash run-experiments.sh --bc=libcoap-431-linked.bc --tag=demo 1
bash run-experiments.sh --bc=libcoap-linked.bc     --tag=demo 1

# Read the violation KLEE recorded for the buggy build:
cat demo-exp-1-version/*.assert.err
```

Experiment IDs: `1` version, `2` token-length, `3` code, `4` empty-message,
`6` mid-match, `7` token-match, `8` matching-type, `9` repeatable-options,
`10` unrecognized-options. (`5` is retired.)

The raw KLEE command behind the runner, if you want to drive it by hand:

```bash
cd "$HARNESS"
env STATE_TO_CHECK=SERVER KLEE_SYMBOLIC_EXPERIMENT=1 PROTOCOL=CoAP SERVER_PORT=5683 \
    klee --libc=uclibc --posix-runtime \
         --output-dir=demo-out \
         --exit-on-error-type=Assert \
         --max-time=60s \
         libcoap-431-linked.bc
```

Clean up KLEE output between runs with `bash clean.sh`.

## Watching packets on the wire

The KLEE runs use an in-process socket model and never touch a real socket. The
native binary does: `make run-native` builds `libcoap-standalone` and runs one
real CoAP exchange on loopback `udp/5683`. To watch it, sniff loopback in one
terminal and run the exchange in another:

```bash
sudo tcpdump -i lo -n udp port 5683    # terminal 1: capture
make run-native                        # terminal 2: build + run libcoap-standalone
```

## Expected result

| Exp | Monitor | 4.3.5 | 4.3.1 | PR |
|----:|---------|:-----:|:-----:|----|
| 1 | version            | silent | **fire** | #1376 |
| 3 | code               | silent | **fire** | #1300 |
| 8 | matching-type      | silent | **fire** | #1295 |
| 9 | repeatable-options | silent | **fire** | #1389 |
| 2,4,6,7,10 | (rest)    | silent | silent   | conformant on both |

So 4.3.5 gives 0/9 fired and 4.3.1 gives 4/9. An `.assert.err` in an
experiment's output dir means the monitor caught the violation; no `.err` means
it stayed silent.
