# The cantcoap harness

A harness for cantcoap, kept here for reference and completeness. It is not part
of the differential pipeline and its results are not among the report's findings.
Unlike the libcoap and FreeCoAP harnesses, it is not wired into the Kleener
toolchain used for the results; treat it as an extra.

cantcoap is a CoAP PDU codec only: a `CoapPDU` encoder and decoder with no network
layer of its own. So the harness supplies its own loopback UDP sockets and uses
cantcoap purely as the system under test for building, parsing, and validating
PDUs.

## Layout

- `cantcoap-standalone.cpp` - native fork-based client/server exchange over real
  loopback UDP, visible to tcpdump. This is the demo.
- `Makefile` - builds it against `libcantcoap.a`.
- `cantcoap-standalone-sp.cpp`, `cpp_stubs.cpp`, `run-experiments.sh` -
  experimental single-process KLEE scaffolding, left in but not wired into the
  toolchain.

## Native demo

```sh
make            # build cantcoap-standalone (links libcantcoap.a)
make run        # one CoAP exchange on loopback udp/5683
```

To watch it on the wire, sniff loopback in another terminal before `make run`:

```sh
sudo tcpdump -i lo -n udp port 5683
```
