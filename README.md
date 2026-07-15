# coap-harness

Testing harnesses for CoAP implementations (libcoap and FreeCoAP), covering two
complementary axes:

- **Conformance / differential testing** — symbolic-execution harnesses that drive
  a libcoap client against a FreeCoAP server under Kleener, run recording monitors
  that make a request field symbolic, and cross-check the two implementations'
  responses to surface RFC 7252 non-conformances.
- **Memory-safety / CVE hunting** — native ASan reproducers for the memory bugs
  found in libcoap (see `security/`).

## Layout

| Path | What |
|------|------|
| `libcoap-harness/` | libcoap client+server standalone driven under Kleener (the base SUT for differential runs) |
| `freecoap-harness/` | libcoap-client → FreeCoAP-server exchange under Kleener |
| `diff.sh` | orchestrator: runs one experiment on both implementations and cross-checks their recorded responses |
| `security/` | native ASan proof-of-concept reproducers for the libcoap memory-safety findings |

## External dependencies (not vendored here)

- **Kleener** (the symbolic-execution runtime + the CoAP recording monitors) — the
  harnesses are driven by it; pin the branch/commit that provides the CoAP monitors.
- **SUTs** — libcoap `v4.3.1` and `v4.3.5`, FreeCoAP (`keith-cullen/FreeCoAP`), plus
  ASan-instrumented libcoap builds for `security/`.
- **smt-cross-checker** — the offline SMT response comparator (to be consolidated
  under the Kleener org).

> First push is a rough snapshot; structure and docs will be cleaned up and the
> external dependencies wired in afterwards.
