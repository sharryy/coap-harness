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
  loopback UDP, visible to tcpdump. This is the main demo.
- `cantcoap-standalone-sp.cpp` - a single-process variant shaped so the Kleener
  socket model can drive it under the conformance monitors. Experimental.
- `cpp_stubs.cpp` - operator new/delete over malloc, so the bitcode needs no
  libstdc++.
- `Makefile`, `run-experiments.sh` - build, and an exploratory monitor run.

## Native demo

```sh
make            # build cantcoap-standalone (links libcantcoap.a)
make run        # one CoAP exchange on loopback udp/5683
```

To watch it on the wire, sniff loopback in another terminal before `make run`:

```sh
sudo tcpdump -i lo -n udp port 5683
```

## Running the monitors (exploratory, not part of the results)

```sh
make bc                 # build cantcoap-linked.bc
./run-experiments.sh    # run the conformance monitors, report which fired
```

This path needs the Kleener runtime on `PATH` (same as the other harnesses). It
is included for completeness only; its output is not validated and is not used in
the report.
