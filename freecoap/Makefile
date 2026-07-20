# Native FreeCoAP client/server round-trip (no DTLS, no KLEE). See README.md.
FCDIR  ?= $(HOME)/project/kleener-experiments/repos/FreeCoAP
FC_INC := $(FCDIR)/lib/include
FC_SRC := $(FCDIR)/lib/src
CC     ?= gcc
CFLAGS := -Wall -O0 -g -I$(FC_INC)

FC_LIB_SRCS := $(FC_SRC)/coap_msg.c $(FC_SRC)/coap_mem.c $(FC_SRC)/coap_log.c \
               $(FC_SRC)/coap_client.c $(FC_SRC)/coap_server.c

freecoap-standalone: freecoap-standalone.c $(FC_LIB_SRCS)
	$(CC) $(CFLAGS) $^ -o $@

run: freecoap-standalone
	./freecoap-standalone

clean:
	rm -f freecoap-standalone

.PHONY: run clean
