CC = clang
LIBCOAP_DIR ?= $(HOME)/project/kleener-experiments/repos/libcoap

CFLAGS = -Wall -Wextra -std=c11 -g -O0 \
		-fno-omit-frame-pointer \
		-I$(LIBCOAP_DIR)/include

LDFLAGS = $(LIBCOAP_DIR)/.libs/libcoap-3-notls.a

BC          ?= libcoap-linked.bc
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

libcoap-standalone.bc: libcoap-standalone.c
	$(CC) -emit-llvm -c -g -O0 -I$(LIBCOAP_DIR)/include $< -o $@

klee_stubs.bc: klee_stubs.c
	$(CC) -emit-llvm -c -g -O0 $< -o $@

libcoap-linked.bc: libcoap-standalone.bc klee_stubs.bc
	llvm-link libcoap-standalone.bc \
	    --override=klee_stubs.bc \
	    $(LIBCOAP_DIR)/.libs/libcoap-3-notls.bc -o $@

run: $(BC)
	PROTOCOL=CoAP SERVER_PORT=$(SERVER_PORT) KLEE_SYMBOLIC_EXPERIMENT=$(EXP) STATE_TO_CHECK=$(SIDE) \
	    klee --libc=uclibc --posix-runtime --output-dir=$(OUTDIR) $(WRITE_SMT2) \
	         --max-time=$(MAX_TIME) $(EXTRA) $(BC)

clean:
	rm -f libcoap-standalone libcoap-standalone.bc klee_stubs.bc libcoap-linked.bc

.PHONY: run run-native clean
