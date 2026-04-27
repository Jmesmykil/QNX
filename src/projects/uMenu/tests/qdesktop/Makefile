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

TESTS := test_QdTheme test_QdPixelCanvas test_QdIconCache test_QdNroAsset test_QdIconCategory \
         test_QdAnim test_QdCurve test_QdDockMagnify \
         test_QdMultitouch test_QdInput test_QdWmConstants \
         test_QdLaunchpad_HotCorner test_LoginChime test_QdDesktopIcons_Hover \
         test_StructSize_Pinning

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

# SP2: Animation engine — links real production qd_Anim.cpp.
# qd_Anim.cpp depends only on <algorithm> and <cstdint>; no SDL2/libnx.
test_QdAnim: test_QdAnim.cpp $(PROD_SRC)/qd_Anim.cpp \
             test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) \
	    test_QdAnim.cpp $(PROD_SRC)/qd_Anim.cpp \
	    -o $@

# SP2: HID curve engine — links real production qd_Curve.cpp.
# qd_Curve.cpp depends only on <algorithm>/<cstdint>/<cstdlib>; no SDL2/libnx.
test_QdCurve: test_QdCurve.cpp $(PROD_SRC)/qd_Curve.cpp \
              test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) \
	    test_QdCurve.cpp $(PROD_SRC)/qd_Curve.cpp \
	    -o $@

# SP2: Dock magnify — self-contained inline port; no production source needed.
test_QdDockMagnify: test_QdDockMagnify.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_QdDockMagnify.cpp -o $@

# SP3: Multitouch gesture engine — links real production qd_Multitouch.cpp.
# qd_Multitouch.cpp depends only on <cstdlib>; no SDL2/libnx.
test_QdMultitouch: test_QdMultitouch.cpp $(PROD_SRC)/qd_Multitouch.cpp \
                   test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) \
	    test_QdMultitouch.cpp $(PROD_SRC)/qd_Multitouch.cpp \
	    -o $@

# SP3: HID input pump helpers — links real production qd_Input.cpp.
# qd_Input.cpp is guarded by #ifdef __SWITCH__ for libnx calls;
# the host-testable helpers compile cleanly without that flag.
test_QdInput: test_QdInput.cpp $(PROD_SRC)/qd_Input.cpp \
              test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) \
	    test_QdInput.cpp $(PROD_SRC)/qd_Input.cpp \
	    -o $@

# SP3: WM constants + focus model — header-only; no production .cpp needed.
test_QdWmConstants: test_QdWmConstants.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_QdWmConstants.cpp -o $@

# v1.7.0-stabilize-2: hot-corner geometry pin + edge-trigger state machine.
# Cannot include qd_Launchpad.hpp directly because it transitively pulls
# real SDL2 (via qd_DesktopIcons.hpp -> qd_Cursor.hpp). Test mirrors the
# constants and exercises the FSM contract.
test_QdLaunchpad_HotCorner: test_QdLaunchpad_HotCorner.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_QdLaunchpad_HotCorner.cpp -o $@

# v1.7.0-stabilize-2: REC-01 chime gate. Models the StartPlayBgm() control
# flow with the gate armed BEFORE the bgm-null check.
test_LoginChime: test_LoginChime.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_LoginChime.cpp -o $@

# v1.7.0-stabilize-2: REC-02 hover-ring suppression during D-pad nav.
test_QdDesktopIcons_Hover: test_QdDesktopIcons_Hover.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_QdDesktopIcons_Hover.cpp -o $@

# v1.7.0-stabilize-2: A7+A8 struct-size pinning. Mirrors NroEntry/LpItem
# layouts and asserts sizes match the v1.6.x reconstruction snapshot.
test_StructSize_Pinning: test_StructSize_Pinning.cpp test_host_stubs.hpp pu/Plutonium
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_StructSize_Pinning.cpp -o $@

run: $(TESTS)
	@for t in $(TESTS); do \
	  echo "=== $$t ==="; \
	  ./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS)
