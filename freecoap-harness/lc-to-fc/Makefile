# libcoap-client -> FreeCoAP-server differential harness (KLEE / Kleener).
#
# This directory is only the lc->fc exchange, driven under Kleener. The native
# FreeCoAP client<->server round-trip lives in ../fc-to-fc.
#   build : ./fc-build.sh links the libcoap client + FreeCoAP server bitcode into
#           freecoap-linked.bc.
#   run   : KLEE interprets it with the CoAP socket model (PROTOCOL=CoAP) shuttling
#           datagrams client<->server and running the selected monitor once each.

# ---- Shared KLEE run API (identical to libcoap-harness/Makefile) ----
# The only difference between the two harnesses is the default BC and which
# experiment IDs are meaningful; every flag below is the same.
BC          ?= freecoap-linked.bc
SERVER_PORT ?= 5683
# KLEE_SYMBOLIC_EXPERIMENT: 0 = clean exchange, 0-17 = conformance, >=100 = diff-testing
EXP         ?= 0
# STATE_TO_CHECK
SIDE        ?= SERVER
MAX_TIME    ?= 60s
OUTDIR      ?= klee-exp-$(EXP)
# extra KLEE flags (e.g. path-explosion guards from diff.sh)
EXTRA       ?=

# Diff-testing experiments (IDs >= 100) are RECORDING monitors: their verdict is
# produced offline by the smt-cross-checker, which needs the per-path constraints
# as test*.smt2. Auto-enable --write-smt2s for those; conformance runs (0-17)
# assert in-run and don't need it.
WRITE_SMT2  := $(shell [ "$(EXP)" -ge 100 ] 2>/dev/null && echo --write-smt2s)

# NOTE: leave libcoap at its default WARN log level. At DEBUG it calls
# coap_show_pdu, which hits an unimplemented llvm.load.relative.i64 intrinsic
# under this KLEE build and kills the path — so do NOT set COAP_LOG_LEVEL>=7.

freecoap-linked.bc: fc-build.sh bridge.h lc-client.c fc-server.c fc-main.c freecoap_klee_stubs.c
	./fc-build.sh

build: freecoap-linked.bc

# lc -> fc exchange under KLEE. Override on the command line, e.g.:
#   make run EXP=1        (version conformance monitor)
#   make run EXP=2 MAX_TIME=120s
#   make run EXP=100      (code diff-testing — auto-writes test*.smt2)
run: $(BC)
	PROTOCOL=CoAP SERVER_PORT=$(SERVER_PORT) KLEE_SYMBOLIC_EXPERIMENT=$(EXP) STATE_TO_CHECK=$(SIDE) \
	    klee --libc=uclibc --posix-runtime --output-dir=$(OUTDIR) $(WRITE_SMT2) \
	         --max-time=$(MAX_TIME) $(EXTRA) $(BC)

clean:
	./clean.sh

.PHONY: build run clean
