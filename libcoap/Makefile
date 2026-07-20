CC = clang
LIBCOAP_DIR ?= $(HOME)/project/kleener-experiments/repos/libcoap

CFLAGS = -Wall -Wextra -std=c11 -g -O0 -fno-omit-frame-pointer -I$(LIBCOAP_DIR)/include
LDFLAGS = $(LIBCOAP_DIR)/.libs/libcoap-3-notls.a

BC          ?= libcoap-linked.bc
LINKED      ?= libcoap-linked.bc
SERVER_PORT ?= 5683
EXP         ?= 0
SIDE        ?= SERVER
MAX_TIME    ?= 60s
OUTDIR      ?= klee-exp-$(EXP)
EXTRA       ?=

WRITE_SMT2  := $(shell [ "$(EXP)" -ge 100 ] 2>/dev/null && echo --write-smt2s)

libcoap-standalone: libcoap-standalone.c
	$(CC) $(CFLAGS) libcoap-standalone.c -o libcoap-standalone $(LDFLAGS)

run-native: libcoap-standalone
	./libcoap-standalone

klee_stubs.bc: klee_stubs.c
	$(CC) -emit-llvm -c -g -O0 $< -o $@

# Link the harness against the tree in LIBCOAP_DIR. For another version, point
# LIBCOAP_DIR at it and set LINKED to the output name:
#   make linked LINKED=libcoap-431-linked.bc LIBCOAP_DIR=<4.3.1 tree>
$(LINKED): libcoap-standalone.c klee_stubs.bc
	$(CC) -emit-llvm -c -g -O0 -I$(LIBCOAP_DIR)/include libcoap-standalone.c -o $(LINKED:.bc=.std.bc)
	llvm-link $(LINKED:.bc=.std.bc) --override=klee_stubs.bc \
	    $(LIBCOAP_DIR)/.libs/libcoap-3-notls.bc -o $@
	@rm -f $(LINKED:.bc=.std.bc)

linked: $(LINKED)

run: $(BC)
	PROTOCOL=CoAP SERVER_PORT=$(SERVER_PORT) KLEE_SYMBOLIC_EXPERIMENT=$(EXP) STATE_TO_CHECK=$(SIDE) \
	    klee --libc=uclibc --posix-runtime --output-dir=$(OUTDIR) $(WRITE_SMT2) \
	         --max-time=$(MAX_TIME) $(EXTRA) $(BC)

clean:
	rm -f libcoap-standalone *.bc

.PHONY: run run-native clean linked
