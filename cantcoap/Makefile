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

clean:
	rm -f cantcoap-standalone

.PHONY: run clean
