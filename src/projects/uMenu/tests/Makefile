# Host-side unit tests for the qdesktop/ subsystem.
# Runs on macOS / Linux host (NOT devkitA64 — these tests exercise pure C++ logic
# against the `pu/` host stubs in this directory and the libnx typedefs in
# test_host_stubs.hpp).
#
# Build:  make
# Run:    make run
# Clean:  make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++23 -Wall -Wextra -Wpedantic -O2
INCLUDES := -I../../include -I.

TESTS := test_QdTheme test_QdPixelCanvas test_QdIconCache test_QdNroAsset test_QdIconCategory

# Real production source directory (relative to this Makefile's location).
PROD_SRC := ../../source/ul/menu/qdesktop

.PHONY: all run clean
all: $(TESTS)

test_QdTheme: test_QdTheme.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

test_QdPixelCanvas: test_QdPixelCanvas.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

test_QdIconCache: test_QdIconCache.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

test_QdNroAsset: test_QdNroAsset.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

# F-09/F-10/F-11 fix: link the REAL production qd_IconCategory.cpp so that
# this test exercises the actual implementation, not an inline copy that can
# silently diverge.  qd_IconCategory.cpp depends only on <cstring>/<cctype>
# and therefore compiles cleanly on the host without SDL2 or libnx.
test_QdIconCategory: test_QdIconCategory.cpp $(PROD_SRC)/qd_IconCategory.cpp \
                     test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) \
	    test_QdIconCategory.cpp $(PROD_SRC)/qd_IconCategory.cpp \
	    -o $@

run: $(TESTS)
	@for t in $(TESTS); do \
	  echo "=== $$t ==="; \
	  ./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS)
