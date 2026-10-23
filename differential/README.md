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
- `pairs.sh` - runs experiments on the two version pairs and cross-checks each pair.
- `fc-native.c`, `../libcoap/lc-native.c` - native servers with the same request
  handling, for checking KLEE paths against real behaviour.

## Request handling

libcoap routes requests in its core: the libcoap harness registers one resource,
`/test`, with a GET handler. FreeCoAP has no resource registry, so `fc-server.c`
makes the same decisions, in libcoap's order:

1. Proxy-Uri or Proxy-Scheme: 5.05 (not a proxy, RFC 7252 §5.7.2)
2. path is not `/test`: 2.02 for DELETE (§5.8.4), else 4.04
3. If-None-Match on `/test`: 4.12 (§5.10.8.2)
4. method is not GET: 4.05, unknown method codes included (§5.8)
5. otherwise: 2.05 `Hello World`

Error payloads are libcoap's reason phrases with no options. One deliberate
difference: libcoap 4.3.5 answers Proxy-Scheme without Uri-Host with 4.02; we follow
the RFC (5.05, as 4.3.1 does).

## Running

```sh
./diff.sh 100                                             # libcoap vs FreeCoAP, experiment 100
LIBCOAP_BC=libcoap-431-linked.bc TAG=-431 ./diff.sh 101   # libcoap 4.3.5 vs 4.3.1
```

### Version pairs

Two pairs: FreeCoAP 18ee7d4 with libcoap 4.3.5, and FreeCoAP ffc87fd with libcoap
4.3.1. Each FreeCoAP checkout needs one line appended to `lib/src/coap_server.c`:

```c
int coap_server_exchange_once(coap_server_t *server) { return coap_server_exchange(server); }
```

```sh
FC_DIR=../repos/FreeCoAP-18ee7d4 OUT=freecoap-18ee7d4-435-linked.bc BCDIR=bc-18ee7d4-435 ./fc-build.sh
FC_DIR=../repos/FreeCoAP-ffc87fd LIBCOAP_DIR=../repos/libcoap-4.3.1 \
  OUT=freecoap-ffc87fd-431-linked.bc BCDIR=bc-ffc87fd-431 ./fc-build.sh
JOBS=1 MAX_TIME=7200s HARD_CAP=7400 ./pairs.sh 100 101 102   # one KLEE at a time
```

`pairs.sh` runs KLEE with `--search=dfs` and should run one job at a time. Kleener's
socket model shadows each modelled send and receive with a real syscall, so all KLEE
paths share the same kernel sockets. Depth-first search finishes each path's
send-then-receive before the next path sends; other searchers can queue more
datagrams than the socket buffer holds. For the same reason the libcoap harness does
not free its contexts at exit.

### Native servers

```sh
FC=../repos/FreeCoAP-18ee7d4
gcc -I$FC/lib/include fc-native.c fc-server.c \
  $FC/lib/src/coap_msg.c $FC/lib/src/coap_mem.c $FC/lib/src/coap_log.c $FC/lib/src/coap_server.c \
  -o fc-server-18ee7d4
LC=../repos/libcoap
gcc -static -I$LC/include ../libcoap/lc-native.c $LC/.libs/libcoap-3-notls.a -o lc-server-435
```

Both listen on `SERVER_PORT` (default 5683).

Experiment IDs and the full pipeline are documented in the top-level
[`../README.md`](../README.md).
