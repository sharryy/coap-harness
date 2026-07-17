# Native FreeCoAP client <-> server round-trip (no DTLS).
#
# A standalone sanity demo: it forks a FreeCoAP server and a FreeCoAP client in
# one program and runs one CoAP exchange over loopback, so you can confirm the
# FreeCoAP stack works end to end and sniff it with tcpdump on udp/5683.
# This is native only; it does not run under KLEE. The lc->fc KLEE harness is in
# ../differential.

FCDIR  ?= $(HOME)/project/kleener-experiments/repos/FreeCoAP
FC_INC := $(FCDIR)/lib/include
FC_SRC := $(FCDIR)/lib/src
CC     ?= gcc
CFLAGS := -Wall -O0 -g -I$(FC_INC)

# FreeCoAP library sources this demo links against.
FC_LIB_SRCS := $(FC_SRC)/coap_msg.c $(FC_SRC)/coap_mem.c $(FC_SRC)/coap_log.c \
               $(FC_SRC)/coap_client.c $(FC_SRC)/coap_server.c

freecoap-standalone: freecoap-standalone.c $(FC_LIB_SRCS)
	$(CC) $(CFLAGS) $^ -o $@

run: freecoap-standalone
	./freecoap-standalone

clean:
	rm -f freecoap-standalone

.PHONY: run clean
