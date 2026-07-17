# FreeCoAP native round-trip

A standalone sanity demo. It forks a FreeCoAP server and a FreeCoAP client in one
process and runs one CoAP exchange over loopback, which confirms the FreeCoAP stack
works end to end and can be sniffed with tcpdump on udp/5683. This is native only;
it does not run under KLEE.

```sh
make        # build freecoap-standalone (links the FreeCoAP library sources)
make run    # run the round-trip
```

To watch the packets on the wire, sniff loopback in another terminal before
`make run`:

```sh
sudo tcpdump -i lo -n udp port 5683
```

The libcoap-client to FreeCoAP-server harness that the differential runs use lives in
[`../differential`](../differential).
