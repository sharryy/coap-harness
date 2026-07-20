# Differential testing

The cross-implementation comparison. A libcoap client drives a FreeCoAP server here
(the control side), `diff.sh` runs the same experiment against the libcoap base
harness in `../libcoap`, and the two recorded responses are cross-checked with Z3.

File prefixes follow the implementation they belong to: `lc-` is libcoap, `fc-` is
FreeCoAP.

## Layout

- `diff.sh` - the runner. It builds both sides, runs the experiment, and
  cross-checks. Base side is `../libcoap`; the control side is here.
- `lc-client.c`, `fc-server.c`, `fc-main.c`, `bridge.h`, `freecoap_klee_stubs.c` -
  the libcoap-client to FreeCoAP-server harness.
- `fc-build.sh`, `Makefile` - build `freecoap-linked.bc` and run it under KLEE.

## Running

```sh
./diff.sh 100                                             # libcoap vs FreeCoAP, experiment 100
LIBCOAP_BC=libcoap-431-linked.bc TAG=-431 ./diff.sh 101   # libcoap 4.3.5 vs 4.3.1
```

Experiment IDs and the full pipeline are documented in the top-level
[`../README.md`](../README.md).
