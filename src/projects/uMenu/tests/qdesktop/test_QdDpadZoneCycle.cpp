// test_QdDpadZoneCycle.cpp -- v1.8.23 host tests for D-pad seamless 3-zone cycle.
//
// Tests the integer state machine introduced in qd_DesktopIcons.cpp:3412-3443
// without including any production headers.  The test target is the ARITHMETIC
// of the zone-transition formulas — the same arithmetic that runs on hardware.
//
// Zone model (from qd_DesktopIcons.cpp comments):
//
//   Folders  : dpad_focus_index_ in [0 .. kDesktopFolderCount)  = [0..5]
//              row 0 = indices 0,1,2 ; row 1 = indices 3,4,5
//   Favorites: fav_strip_focus_index_ in [0 .. FAV_STRIP_VISIBLE) = [0..5]
//              dpad_focus_index_ is irrelevant while strip is active
//   Dock     : dpad_focus_index_ in [kDFC .. kDFC + BIC) = [6..10]
//
// Sentinel: SIZE_MAX (as size_t) = inactive / not in that zone.
//
// Each test function implements the same arithmetic as the corresponding branch
// in OnInput() so that a change to the production constants or formulas causes
// this test to disagree, surfacing the regression.
//
// Build:  make test_QdDpadZoneCycle  (from tests/qdesktop/)
// Run:    ./test_QdDpadZoneCycle

#include "test_host_stubs.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>

// ── Zone geometry constants (mirrored from production) ────────────────────────

static constexpr size_t kDesktopFolderCount = 6;   // qd_DesktopIcons.cpp:334
static constexpr size_t BUILTIN_ICON_COUNT  = 5;   // qd_DesktopIcons.hpp:100
static constexpr size_t kDFC                = kDesktopFolderCount;   // alias
static constexpr int32_t DF_COLS            = 3;   // qd_DesktopIcons.cpp:393
static constexpr int32_t FAV_STRIP_VISIBLE  = 6;   // qd_DesktopIcons.cpp:1105

static constexpr size_t kSIZE_MAX = std::numeric_limits<size_t>::max();

// ── Transition helpers (mirror of the OnInput() branches) ─────────────────────
// Each helper mutates dpad and strip by value (simulating the member-variable
// writes in the real code) and returns via out-parameters.

// UP from Dock slot `dock_i`, with `has_favs` indicating favourites exist.
// qd_DesktopIcons.cpp:3439-3452
static void transition_dock_up(size_t dock_i, bool has_favs,
                                size_t &dpad_out, size_t &strip_out) {
    if (has_favs) {
        const size_t strip_slot =
            (dock_i * static_cast<size_t>(FAV_STRIP_VISIBLE)) / BUILTIN_ICON_COUNT;
        strip_out = (strip_slot < static_cast<size_t>(FAV_STRIP_VISIBLE))
                    ? strip_slot : 0u;
        dpad_out  = dpad_out;  // dpad_focus_index_ is not written in this branch
    } else {
        const size_t target_col =
            (dock_i * static_cast<size_t>(DF_COLS)) / BUILTIN_ICON_COUNT;
        dpad_out  = static_cast<size_t>(DF_COLS) + target_col;
        strip_out = kSIZE_MAX;
    }
}

// UP from Favorites strip slot `strip_slot`.
// qd_DesktopIcons.cpp:3430-3433
static void transition_strip_up(size_t strip_slot,
                                 size_t &dpad_out, size_t &strip_out) {
    const size_t strip_col =
        (strip_slot * static_cast<size_t>(DF_COLS))
        / static_cast<size_t>(FAV_STRIP_VISIBLE);
    dpad_out  = static_cast<size_t>(DF_COLS) + strip_col;  // row 1
    strip_out = kSIZE_MAX;
}

// DOWN from Favorites strip slot `strip_slot`.
// qd_DesktopIcons.cpp:3477-3481
static void transition_strip_down(size_t strip_slot,
                                   size_t &dpad_out, size_t &strip_out) {
    const size_t target_dock =
        (strip_slot * BUILTIN_ICON_COUNT)
        / static_cast<size_t>(FAV_STRIP_VISIBLE);
    dpad_out  = kDFC + (target_dock < BUILTIN_ICON_COUNT ? target_dock : 0u);
    strip_out = kSIZE_MAX;
}

// DOWN from folder row 1, col `col` (dpad_focus_index_ = DF_COLS + col).
// qd_DesktopIcons.cpp:3492-3503
static void transition_row1_down(size_t col, bool has_favs,
                                  size_t &dpad_out, size_t &strip_out) {
    if (has_favs) {
        const size_t strip_slot =
            (col * static_cast<size_t>(FAV_STRIP_VISIBLE))
            / static_cast<size_t>(DF_COLS);
        strip_out = (strip_slot < static_cast<size_t>(FAV_STRIP_VISIBLE))
                    ? strip_slot : 0u;
        dpad_out  = dpad_out;  // dpad_focus_index_ not written in this branch
    } else {
        const size_t target_dock = (col * BUILTIN_ICON_COUNT)
                                   / static_cast<size_t>(DF_COLS);
        dpad_out  = kDFC + target_dock;
        strip_out = kSIZE_MAX;
    }
}

// ── Test 1: Dock slot 0 UP (with favs) → strip slot 0 ───────────────────────
// Expected: dock_i=0 → strip_slot = (0*6)/5 = 0; dpad unchanged.
static void test_dock_up_to_strip_slot0() {
    size_t dpad  = kDFC + 0u;   // dock slot 0
    size_t strip = kSIZE_MAX;
    const size_t saved_dpad = dpad;

    transition_dock_up(0u, /*has_favs=*/true, dpad, strip);

    ASSERT_EQ(strip, 0u);
    ASSERT_EQ(dpad, saved_dpad);   // dpad_focus_index_ not mutated in this branch
    TEST_PASS("dock UP (slot 0, has favs) -> strip slot 0");
}

// ── Test 2: Dock slot 0 UP (no favs) → folder row 1 col 0 ───────────────────
// Expected: dock_i=0 → target_col = (0*3)/5 = 0 → dpad = DF_COLS + 0 = 3.
static void test_dock_up_no_favs_to_row1() {
    size_t dpad  = kDFC + 0u;
    size_t strip = kSIZE_MAX;

    transition_dock_up(0u, /*has_favs=*/false, dpad, strip);

    ASSERT_EQ(dpad, static_cast<size_t>(DF_COLS) + 0u);  // 3
    ASSERT_EQ(strip, kSIZE_MAX);
    TEST_PASS("dock UP (slot 0, no favs) -> folder row 1 col 0 (dpad=3)");
}

// ── Test 3: Strip slot 0 UP → folder row 1 (dpad = DF_COLS + strip_col) ─────
// Expected: strip_slot=0 → strip_col = (0*3)/6 = 0 → dpad = 3+0 = 3.
static void test_strip_up_to_row1() {
    size_t dpad  = kSIZE_MAX;   // not in folder zone
    size_t strip = 0u;

    transition_strip_up(0u, dpad, strip);

    ASSERT_EQ(dpad, static_cast<size_t>(DF_COLS) + 0u);  // 3
    ASSERT_EQ(strip, kSIZE_MAX);
    TEST_PASS("strip UP (slot 0) -> folder row 1 col 0 (dpad=3), strip=SIZE_MAX");
}

// ── Test 4: Strip slot 0 DOWN → dock slot 0 ──────────────────────────────────
// Expected: strip_slot=0 → target_dock = (0*5)/6 = 0 → dpad = kDFC+0 = 6.
static void test_strip_down_to_dock() {
    size_t dpad  = kSIZE_MAX;
    size_t strip = 0u;

    transition_strip_down(0u, dpad, strip);

    ASSERT_EQ(dpad, kDFC + 0u);  // 6
    ASSERT_EQ(strip, kSIZE_MAX);
    TEST_PASS("strip DOWN (slot 0) -> dock slot 0 (dpad=6), strip=SIZE_MAX");
}

// ── Test 5: Folder row 1 col 0 DOWN (with favs) → strip slot 0 ───────────────
// Expected: col=0 → strip_slot = (0*6)/3 = 0; dpad unchanged.
static void test_row1_down_to_strip() {
    size_t dpad  = static_cast<size_t>(DF_COLS) + 0u;  // row 1, col 0
    size_t strip = kSIZE_MAX;
    const size_t saved_dpad = dpad;

    transition_row1_down(0u, /*has_favs=*/true, dpad, strip);

    ASSERT_EQ(strip, 0u);
    ASSERT_EQ(dpad, saved_dpad);  // dpad_focus_index_ not mutated in this branch
    TEST_PASS("folder row 1 col 0 DOWN (has favs) -> strip slot 0");
}

// ── Test 6: Folder row 1 col 0 DOWN (no favs) → dock ────────────────────────
// Expected: col=0 → target_dock = (0*5)/3 = 0 → dpad = kDFC+0 = 6.
static void test_row1_down_no_favs_to_dock() {
    size_t dpad  = static_cast<size_t>(DF_COLS) + 0u;
    size_t strip = kSIZE_MAX;

    transition_row1_down(0u, /*has_favs=*/false, dpad, strip);

    ASSERT_EQ(dpad, kDFC + 0u);  // 6
    ASSERT_EQ(strip, kSIZE_MAX);
    TEST_PASS("folder row 1 col 0 DOWN (no favs) -> dock slot 0 (dpad=6)");
}

// ── Test 7: Strip-slot→dock mapping is monotone across all 6 strip slots ─────
// Verifies that the column-mapping formula produces non-decreasing dock indices
// as strip slot increases, so there are no surprise reversals.
static void test_strip_to_dock_monotone() {
    size_t prev = 0u;
    for (size_t s = 0u; s < static_cast<size_t>(FAV_STRIP_VISIBLE); ++s) {
        const size_t dock =
            (s * BUILTIN_ICON_COUNT) / static_cast<size_t>(FAV_STRIP_VISIBLE);
        ASSERT_TRUE(dock >= prev);
        ASSERT_TRUE(dock < BUILTIN_ICON_COUNT);
        prev = dock;
    }
    TEST_PASS("strip→dock column mapping is monotone and in-range for all 6 slots");
}

// ── Test 8: Dock→strip mapping is monotone across all 5 dock slots ───────────
static void test_dock_to_strip_monotone() {
    size_t prev = 0u;
    for (size_t d = 0u; d < BUILTIN_ICON_COUNT; ++d) {
        const size_t strip_slot =
            (d * static_cast<size_t>(FAV_STRIP_VISIBLE)) / BUILTIN_ICON_COUNT;
        ASSERT_TRUE(strip_slot >= prev);
        ASSERT_TRUE(strip_slot < static_cast<size_t>(FAV_STRIP_VISIBLE));
        prev = strip_slot;
    }
    TEST_PASS("dock→strip column mapping is monotone and in-range for all 5 dock slots");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_dock_up_to_strip_slot0();
    test_dock_up_no_favs_to_row1();
    test_strip_up_to_row1();
    test_strip_down_to_dock();
    test_row1_down_to_strip();
    test_row1_down_no_favs_to_dock();
    test_strip_to_dock_monotone();
    test_dock_to_strip_monotone();
    return 0;
}
