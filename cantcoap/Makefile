CXX = clang++
CANTCOAP_DIR ?= $(HOME)/project/kleener-experiments/repos/cantcoap

# cantcoap is a pure PDU codec (static lib, no networking); the harness supplies
# its own loopback UDP sockets.
CXXFLAGS = -Wall -g -O0 -std=c++11 -I$(CANTCOAP_DIR)
LDFLAGS  = $(CANTCOAP_DIR)/libcantcoap.a

# Native client<->server exchange (real UDP on 127.0.0.1:5683, sniffable
# with: sudo tcpdump -i lo -n udp port 5683).
cantcoap-standalone: cantcoap-standalone.cpp $(CANTCOAP_DIR)/libcantcoap.a
	$(CXX) $(CXXFLAGS) cantcoap-standalone.cpp -o $@ $(LDFLAGS)

# Build the SUT static lib if it isn't there yet.
$(CANTCOAP_DIR)/libcantcoap.a:
	$(MAKE) -C $(CANTCOAP_DIR) nethelper.o staticlib

run: cantcoap-standalone
	./cantcoap-standalone

# ---- Bitcode targets (single-process harness for KLEE) ----
# Compile straight to bitcode and supply operator new/delete via cpp_stubs
# instead of libstdc++. The CoAP model + monitors come from Kleener's runtime.
BCFLAGS = -emit-llvm -c -g -O0 -std=c++11 -I$(CANTCOAP_DIR)

cantcoap-standalone-sp.bc: cantcoap-standalone-sp.cpp
	$(CXX) $(BCFLAGS) $< -o $@

cantcoap.bc: $(CANTCOAP_DIR)/cantcoap.cpp
	$(CXX) $(BCFLAGS) $< -o $@

nethelper.bc: $(CANTCOAP_DIR)/nethelper.c
	clang -emit-llvm -c -g -O0 -I$(CANTCOAP_DIR) $< -o $@

cpp_stubs.bc: cpp_stubs.cpp
	$(CXX) $(BCFLAGS) $< -o $@

cantcoap-linked.bc: cantcoap-standalone-sp.bc cantcoap.bc nethelper.bc cpp_stubs.bc
	llvm-link $^ -o $@

bc: cantcoap-linked.bc

experiments: cantcoap-linked.bc
	./run-experiments.sh

clean:
	rm -f cantcoap-standalone *.bc
	rm -rf cc-exp-* smoke-exp*

.PHONY: run bc experiments clean
