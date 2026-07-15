# security — libcoap memory-safety reproducers

Native, deterministic ASan proof-of-concept reproducers for the memory-safety
bugs found in libcoap during this work. All are for **fixed / disclosed** issues.

| PoC | Bug | Status |
|-----|-----|--------|
| `proxy-uaf-repro.c` | Proxy association cleanup use-after-free (combined A+B) | fixed |
| `poc-bug-a.c` | Bug A — `coap_proxy_remove_association` removes only the first matching req entry | fixed in v4.3.5c |
| `poc-bug-b.c` | Bug B — proxy cleanup gated on an optional event callback → dangling incoming session | fixed on develop |
| `poc-bug-b-groom.c` | Bug B via the idle-reap free path (heap-groomable variant) | — |
| `token-spoof-repro.c` / `-dbg.c` | Piggybacked-ACK accepted on MID match without verifying the token | fixed upstream (merged) |

## Building

These link against an **ASan-instrumented** libcoap build (gcc, not the Kleener
clang toolchain). For example:

```sh
gcc -fsanitize=address -g -O0 -fno-omit-frame-pointer \
    -I <libcoap-asan>/include \
    poc-bug-b.c <libcoap-asan>/.libs/libcoap-3-notls.a \
    -o poc-bug-b
./poc-bug-b        # expect: heap-use-after-free reported by ASan
```

Point `<libcoap-asan>` at the ASan build of the relevant version (4.3.5 / 4.3.5b /
develop) depending on which bug the PoC targets.
