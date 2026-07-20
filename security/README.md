# Memory-safety reproducers for libcoap

Native, deterministic reproducers for the memory-safety bugs found in libcoap
during this work. Each one builds against an AddressSanitizer libcoap and prints a
clean ASan report in under a second, so a reviewer can confirm the bug without
reading the whole code path. All of these are **fixed upstream and disclosed** by
the time they appear here.

These bugs did not come out of the symbolic monitors. They came from reading the
less-fuzzed, stateful parts of libcoap by hand (the proxy path and the
message-matching path) and then writing a small program to prove each one on the
real library. The monitors and the differential runs are a separate line of work;
this directory is the memory-safety side.

## What was found

**Proxy association use-after-free (two distinct bugs).** libcoap's reverse-proxy
code keeps, for each upstream request, a small record tied to the incoming client
session. When that incoming session goes away, those records have to be cleaned up
before the session memory is freed. Two things went wrong there:

- **Bug A** (`poc-bug-a.c`). The cleanup routine
  `coap_proxy_remove_association` removed only the *first* matching request record
  and stopped, so with more than one in-flight request through the proxy the rest
  were left pointing at freed session memory. When the upstream reply arrived it was
  matched against a dangling record. Reproduced on 4.3.5. Fixed on the development
  branch, where that cleanup was rewritten (the associations, previously kept in an
  array, became a linked list walked in full), so the early return that caused the bug
  is gone. The fix came as part of that rewrite, so there is no separate fix commit.

- **Bug B** (`poc-bug-b.c`). The cleanup was reached only inside a branch that ran
  when the application had registered an event callback. An application that never
  registered one skipped the cleanup entirely on session teardown, leaving the same
  dangling records. This one was still live on the development branch when it was
  found, not only in the release, which made it the stronger finding. Fixed upstream
  in commit [`abde6a5`](https://github.com/obgm/libcoap/commit/abde6a513a29d1dd3fb9d23feb8e1218ad81d1e0),
  which moves the association cleanup ahead of the event-callback dispatch so it runs
  even when no callback is registered.

**Piggybacked-ACK token spoof** (`token-spoof-repro.c`). When a server answers a
confirmable request with a piggybacked ACK,
libcoap matched that ACK to the pending request by Message ID alone and did not check
the token. RFC 7252 section 5.3.2 requires the token to match as well, so a response
carrying the right Message ID but the wrong token was accepted as the answer. The
reproducer stands up a client, has a forger reply with a correct-MID / wrong-token
ACK, and shows libcoap delivering it. Fixed upstream in commit
[`2e995218`](https://github.com/obgm/libcoap/commit/2e995218f356d63070afee0517f3c89dd44a944e),
which reworked the send-queue matching (`coap_remove_from_queue`) to key on both the
Message ID and the token, so an ACK is taken as the answer only when its token
matches too.

## Building

The PoCs link against an **ASan-instrumented** libcoap built with gcc (not the
Kleener clang toolchain):

```sh
gcc -fsanitize=address -g -O0 -fno-omit-frame-pointer \
    -I <libcoap-asan>/include \
    poc-bug-b.c <libcoap-asan>/.libs/libcoap-3-notls.a \
    -o poc-bug-b
./poc-bug-b        # expect: heap-use-after-free reported by ASan
```

Point `<libcoap-asan>` at the ASan build of the version the PoC targets: 4.3.5 for
Bug A, the development branch for Bug B, and the matching version for the token-spoof
reproducer. The proxy PoCs need libcoap configured with proxy support; the
token-spoof one needs only a normal build.
