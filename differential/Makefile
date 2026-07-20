# libcoap-client -> FreeCoAP-server differential harness. See README.md.
# Run API mirrors ../libcoap/Makefile; only the default BC differs.
BC          ?= freecoap-linked.bc
SERVER_PORT ?= 5683
EXP         ?= 0
SIDE        ?= SERVER
MAX_TIME    ?= 60s
OUTDIR      ?= klee-exp-$(EXP)
EXTRA       ?=

# Recording monitors (EXP >= 100) need per-path constraints for the offline
# cross-checker; conformance runs assert in-run and don't.
WRITE_SMT2  := $(shell [ "$(EXP)" -ge 100 ] 2>/dev/null && echo --write-smt2s)

# Keep libcoap at WARN: at DEBUG it calls coap_show_pdu, which hits an
# unimplemented intrinsic under this KLEE build. Do NOT set COAP_LOG_LEVEL>=7.

freecoap-linked.bc: fc-build.sh bridge.h lc-client.c fc-server.c fc-main.c freecoap_klee_stubs.c
	./fc-build.sh

build: freecoap-linked.bc

run: $(BC)
	PROTOCOL=CoAP SERVER_PORT=$(SERVER_PORT) KLEE_SYMBOLIC_EXPERIMENT=$(EXP) STATE_TO_CHECK=$(SIDE) \
	    klee --libc=uclibc --posix-runtime --output-dir=$(OUTDIR) $(WRITE_SMT2) \
	         --max-time=$(MAX_TIME) $(EXTRA) $(BC)

clean:
	./clean.sh

.PHONY: build run clean
