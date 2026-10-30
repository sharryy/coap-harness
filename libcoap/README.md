# The libcoap harness

The libcoap client-and-server driver, all in one process. This is the base side
of a differential run, and also the target for the cross-version (4.3.5 vs
4.3.1) runs. The differential runner that drives it
against FreeCoAP lives in [`../differential`](../differential); the pipeline as
a whole is described in the [top-level README](../README.md).

## Layout

- `libcoap-standalone.c` - the single-process client + server harness. The same
  source compiles against both libcoap versions (it carries `#ifndef` compat
  shims for the few identifiers 4.3.1 and 4.3.5 name differently).
- `klee_stubs.c` - stubs for the libc calls KLEE's runtime cannot model
  (`getifaddrs`, the clock), linked with `--override` so they win.
- `Makefile` - builds the linked bitcode and the native binary, and runs KLEE.

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
extract-bc -b .libs/libcoap-3-notls.a
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
The output name is what you then pass to `diff.sh` as `LIBCOAP_BC`.

Clean up KLEE output between runs with `bash clean.sh`.

## Watching packets on the wire

The KLEE runs use Kleener's socket model, which keeps each path's messages in memory
but also mirrors every send and receive on a real loopback socket. The native binary
uses real sockets only: `make run-native` builds `libcoap-standalone` and runs one
real CoAP exchange on loopback `udp/5683`. To watch it, sniff loopback in one
terminal and run the exchange in another:

```bash
sudo tcpdump -i lo -n udp port 5683    # terminal 1: capture
make run-native                        # terminal 2: build + run libcoap-standalone
```
