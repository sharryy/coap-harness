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
