// test_QdDockMagnify.cpp — Host-side unit tests for dock magnify logic.
// Verifies magnify scale table for 5 synthetic dock slots with 30px proximity zone.
// Mirrors wm.rs UpdateDockMagnify + dock_magnify_scale_x100 semantics.
// Compile: c++ -std=c++23 -I../../include -I. test_QdDockMagnify.cpp -o test_QdDockMagnify

#include "test_host_stubs.hpp"
#include <cstdio>

// ── Inline port of dock magnify helpers (SP2-F12, SP2-F13) ───────────────────
// These replicate exactly the logic in qd_DesktopIcons.cpp so this test is
// self-contained on the host without linking the full element.

static constexpr int32_t DOCK_H_TEST           = 108;    // ×1.5 from Rust 72
static constexpr int32_t DOCK_NOMINAL_TOP_TEST  = 1080 - DOCK_H_TEST;  // 972
static constexpr int32_t DOCK_PROX_ZONE_TEST    = 30;    // SP2-F12: ×1.5 from Rust 20
static constexpr int32_t BUILTIN_ICON_COUNT_TEST = 6;

// Returns magnify scale ×100 for slot `slot_idx` when `magnify_center` is the
// focused slot (or -1 for no magnify).
static int32_t dock_magnify_scale_x100(int32_t slot_idx, int32_t magnify_center) {
    if (magnify_center < 0) {
        return 100;
    }
    const int32_t dist = (slot_idx >= magnify_center)
                       ? (slot_idx - magnify_center)
                       : (magnify_center - slot_idx);
    switch (dist) {
        case 0: return 140;
        case 1: return 120;
        case 2: return 105;
        default: return 100;
    }
}

// Simulates the UpdateDockMagnify cursor_y probe:
// returns -1 if cursor_y is above the dock proximity zone, else 0 (enters zone).
static int32_t compute_magnify_center_from_y(int32_t cursor_y,
                                              int32_t prev_magnify_center) {
    if (cursor_y < DOCK_NOMINAL_TOP_TEST - DOCK_PROX_ZONE_TEST) {
        return -1;
    }
    // Entering dock zone.
    if (prev_magnify_center == -1) {
        return 0;  // SP2-F13: first entry → slot 0.
    }
    return prev_magnify_center;  // persist.
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. Outside dock zone (cursor_y well above): magnify_center = -1, all scales = 100.
static void test_outside_zone_no_magnify() {
    const int32_t center = compute_magnify_center_from_y(0, -1);
    ASSERT_EQ(center, -1);
    for (int32_t s = 0; s < BUILTIN_ICON_COUNT_TEST; ++s) {
        ASSERT_EQ(dock_magnify_scale_x100(s, center), 100);
    }
    TEST_PASS("outside_zone_no_magnify");
}

// 2. Cursor at exactly the proximity boundary (DOCK_NOMINAL_TOP - DOCK_PROX_ZONE):
//    this is outside (strict <), so no magnify.
static void test_cursor_at_prox_boundary_outside() {
    const int32_t y = DOCK_NOMINAL_TOP_TEST - DOCK_PROX_ZONE_TEST - 1;
    const int32_t center = compute_magnify_center_from_y(y, -1);
    ASSERT_EQ(center, -1);
    TEST_PASS("cursor_at_prox_boundary_outside");
}

// 3. Cursor just inside proximity zone: magnify_center enters as 0.
static void test_cursor_enters_proximity_zone() {
    const int32_t y = DOCK_NOMINAL_TOP_TEST - DOCK_PROX_ZONE_TEST;
    const int32_t center = compute_magnify_center_from_y(y, -1);
    ASSERT_EQ(center, 0);
    TEST_PASS("cursor_enters_proximity_zone");
}

// 4. Magnify scale table: center=2, 5 slots → expected scales.
//   slot 0: dist=2 → 105
//   slot 1: dist=1 → 120
//   slot 2: dist=0 → 140
//   slot 3: dist=1 → 120
//   slot 4: dist=2 → 105
//   slot 5: dist=3 → 100
static void test_magnify_scale_table_center_2() {
    const int32_t center = 2;
    ASSERT_EQ(dock_magnify_scale_x100(0, center), 105);
    ASSERT_EQ(dock_magnify_scale_x100(1, center), 120);
    ASSERT_EQ(dock_magnify_scale_x100(2, center), 140);
    ASSERT_EQ(dock_magnify_scale_x100(3, center), 120);
    ASSERT_EQ(dock_magnify_scale_x100(4, center), 105);
    ASSERT_EQ(dock_magnify_scale_x100(5, center), 100);
    TEST_PASS("magnify_scale_table_center_2");
}

// 5. Magnify persists while cursor stays in zone (prev_magnify_center carried forward).
static void test_magnify_persists_in_zone() {
    int32_t center = -1;
    // First frame in zone.
    center = compute_magnify_center_from_y(DOCK_NOMINAL_TOP_TEST, center);
    ASSERT_EQ(center, 0);
    // Second frame: still in zone, center persists.
    center = compute_magnify_center_from_y(DOCK_NOMINAL_TOP_TEST + 10, center);
    ASSERT_EQ(center, 0);
    TEST_PASS("magnify_persists_in_zone");
}

// 6. Magnify resets when cursor leaves zone.
static void test_magnify_resets_on_exit() {
    int32_t center = -1;
    // Enter zone.
    center = compute_magnify_center_from_y(DOCK_NOMINAL_TOP_TEST, center);
    ASSERT_EQ(center, 0);
    // Leave zone.
    center = compute_magnify_center_from_y(0, center);
    ASSERT_EQ(center, -1);
    // All slots back to 100.
    for (int32_t s = 0; s < BUILTIN_ICON_COUNT_TEST; ++s) {
        ASSERT_EQ(dock_magnify_scale_x100(s, center), 100);
    }
    TEST_PASS("magnify_resets_on_exit");
}

// 7. Scale table at center=0 (leftmost slot).
//   slot 0: dist=0 → 140
//   slot 1: dist=1 → 120
//   slot 2: dist=2 → 105
//   slot 3+: dist≥3 → 100
static void test_magnify_scale_table_center_0() {
    ASSERT_EQ(dock_magnify_scale_x100(0, 0), 140);
    ASSERT_EQ(dock_magnify_scale_x100(1, 0), 120);
    ASSERT_EQ(dock_magnify_scale_x100(2, 0), 105);
    ASSERT_EQ(dock_magnify_scale_x100(3, 0), 100);
    ASSERT_EQ(dock_magnify_scale_x100(4, 0), 100);
    ASSERT_EQ(dock_magnify_scale_x100(5, 0), 100);
    TEST_PASS("magnify_scale_table_center_0");
}

// 8. Scale table at center=5 (rightmost slot).
//   slot 5: dist=0 → 140
//   slot 4: dist=1 → 120
//   slot 3: dist=2 → 105
//   slot 2: dist=3 → 100
static void test_magnify_scale_table_center_5() {
    ASSERT_EQ(dock_magnify_scale_x100(5, 5), 140);
    ASSERT_EQ(dock_magnify_scale_x100(4, 5), 120);
    ASSERT_EQ(dock_magnify_scale_x100(3, 5), 105);
    ASSERT_EQ(dock_magnify_scale_x100(2, 5), 100);
    ASSERT_EQ(dock_magnify_scale_x100(1, 5), 100);
    ASSERT_EQ(dock_magnify_scale_x100(0, 5), 100);
    TEST_PASS("magnify_scale_table_center_5");
}

int main() {
    test_outside_zone_no_magnify();
    test_cursor_at_prox_boundary_outside();
    test_cursor_enters_proximity_zone();
    test_magnify_scale_table_center_2();
    test_magnify_persists_in_zone();
    test_magnify_resets_on_exit();
    test_magnify_scale_table_center_0();
    test_magnify_scale_table_center_5();

    fprintf(stderr, "\nAll test_QdDockMagnify tests PASSED.\n");
    return 0;
}
