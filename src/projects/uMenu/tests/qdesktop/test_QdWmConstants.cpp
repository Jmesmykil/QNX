// test_QdWmConstants.cpp — host unit tests for SP3 WM pixel constants and focus model.
//
// SP3-F09: Verifies ALL 25 pixel constants are remapped ×1.5 from Rust values.
// SP3-F10: Verifies 3 count constants are NOT remapped.
// SP3-F12: Verifies FocusLevel tagged union: Screen and Window variants.
// SP3-F13: Verifies FocusElement neighbours initialised to -1 sentinel (AB-15).
// AB-12:   No 1280 or 720 literals in sources.
#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <cstdint>

using namespace ul::menu::qdesktop;

// ── Screen dimensions ─────────────────────────────────────────────────────────

static void test_screen_dimensions() {
    ASSERT_EQ(SCREEN_W, 1920u);  // Rust: 1280 × 1.5
    ASSERT_EQ(SCREEN_H, 1080u);  // Rust:  720 × 1.5
    TEST_PASS("SCREEN_W=1920, SCREEN_H=1080 (×1.5 from 1280×720)");
}

// ── Top bar ───────────────────────────────────────────────────────────────────

static void test_topbar_h() {
    ASSERT_EQ(TOPBAR_H, 48u);  // Rust: 32 × 1.5
    TEST_PASS("TOPBAR_H=48");
}

// ── Dock ──────────────────────────────────────────────────────────────────────

static void test_dock_constants() {
    ASSERT_EQ(DOCK_H,              108u);  // Rust: 72
    ASSERT_EQ(DOCK_PADDING_BOTTOM,  12u);  // Rust:  8
    ASSERT_EQ(DOCK_SLOT_SIZE,       84u);  // Rust: 56
    ASSERT_EQ(DOCK_SLOT_GAP,        18u);  // Rust: 12
    TEST_PASS("dock pixel constants all ×1.5 remapped");
}

// ── Count constants (no remap — SP3-F10) ─────────────────────────────────────

static void test_count_constants_not_remapped() {
    ASSERT_EQ(DOCK_SLOT_COUNT,        6u);   // unchanged
    ASSERT_EQ(WORKSPACE_COUNT,        9u);   // unchanged
    ASSERT_EQ(WORKSPACE_SLIDE_FRAMES, 12u);  // unchanged
    TEST_PASS("DOCK_SLOT_COUNT=6, WORKSPACE_COUNT=9, WORKSPACE_SLIDE_FRAMES=12 (not remapped)");
}

// ── Title bar and traffic lights ──────────────────────────────────────────────

static void test_titlebar_and_traffic() {
    ASSERT_EQ(TITLEBAR_H,          42u);   // Rust: 28
    ASSERT_EQ(TRAFFIC_RADIUS,      11u);   // Rust:  7 (10.5 → 11, round up)
    ASSERT_EQ(TRAFFIC_GAP,         33);    // Rust: 22
    ASSERT_EQ(TRAFFIC_LEFT_OFFSET, 21);    // Rust: 14
    ASSERT_EQ(TRAFFIC_Y_OFFSET,    21);    // Rust: 14
    ASSERT_EQ(TRAFFIC_HIT_SLOP,     6);    // Rust:  4
    TEST_PASS("title bar and traffic light constants ×1.5 remapped");
}

// ── Resize grip ───────────────────────────────────────────────────────────────

static void test_grip_size() {
    ASSERT_EQ(GRIP_SIZE, 18u);  // Rust: 12
    TEST_PASS("GRIP_SIZE=18");
}

// ── Min/max window dimensions ─────────────────────────────────────────────────

static void test_win_min_max() {
    ASSERT_EQ(WIN_MIN_W,  300u);  // Rust: 200
    ASSERT_EQ(WIN_MIN_H,  180u);  // Rust: 120
    ASSERT_EQ(MAX_INSET_X, 24);   // Rust: 16
    TEST_PASS("WIN_MIN_W=300, WIN_MIN_H=180, MAX_INSET_X=24");
}

// ── Window sizing / stagger ───────────────────────────────────────────────────

static void test_win_sizing() {
    ASSERT_EQ(LAUNCH_STAGGER,  36);    // Rust: 24
    ASSERT_EQ(DEFAULT_WIN_W,  780u);   // Rust: 520
    ASSERT_EQ(DEFAULT_WIN_H,  480u);   // Rust: 320
    TEST_PASS("LAUNCH_STAGGER=36, DEFAULT_WIN_W=780, DEFAULT_WIN_H=480");
}

// ── Focus ring ────────────────────────────────────────────────────────────────

static void test_focus_ring() {
    ASSERT_EQ(FOCUS_RING_THICKNESS, 3u);  // Rust: 2
    TEST_PASS("FOCUS_RING_THICKNESS=3");
}

// ── Cursor start position ─────────────────────────────────────────────────────

static void test_cursor_start() {
    ASSERT_EQ(CURSOR_START_X, 960);   // SCREEN_W / 2
    ASSERT_EQ(CURSOR_START_Y, 540);   // SCREEN_H / 2
    TEST_PASS("CURSOR_START_X=960, CURSOR_START_Y=540 (half of 1920×1080)");
}

// ── Hot corners ───────────────────────────────────────────────────────────────

static void test_hot_corners() {
    ASSERT_EQ(HOT_CORNER_W, 30u);  // Rust: 20
    ASSERT_EQ(HOT_CORNER_H, 30u);  // Rust: 20
    TEST_PASS("HOT_CORNER_W=30, HOT_CORNER_H=30");
}

// ── FocusLevel tagged union (SP3-F12) ─────────────────────────────────────────

static void test_focus_level_screen_variant() {
    FocusLevel fl = focus_level_screen();
    ASSERT_TRUE(fl.kind == FocusLevel::Kind::Screen);
    // window_idx is not meaningful here, but should be 0.
    ASSERT_EQ(fl.window_idx, 0u);
    TEST_PASS("focus_level_screen() → Kind::Screen, window_idx=0");
}

static void test_focus_level_window_variant() {
    FocusLevel fl = focus_level_window(3u);
    ASSERT_TRUE(fl.kind == FocusLevel::Kind::Window);
    ASSERT_EQ(fl.window_idx, 3u);
    TEST_PASS("focus_level_window(3) → Kind::Window, window_idx=3");
}

// ── FocusElement -1 sentinel (SP3-F13, AB-15) ────────────────────────────────

static void test_focus_element_neighbours_sentinel() {
    FocusElement fe = FocusElement::make(FocusElementId::WindowBody);
    ASSERT_EQ(fe.id, FocusElementId::WindowBody);
    // All four neighbours must be -1 (not 0) per AB-15.
    ASSERT_EQ(fe.neighbors[0], -1);  // Up
    ASSERT_EQ(fe.neighbors[1], -1);  // Down
    ASSERT_EQ(fe.neighbors[2], -1);  // Left
    ASSERT_EQ(fe.neighbors[3], -1);  // Right
    TEST_PASS("FocusElement::make initialises all neighbours to -1 sentinel");
}

static void test_focus_element_neighbour_set() {
    FocusElement fe = FocusElement::make(FocusElementId::VaultSidebar);
    fe.neighbors[static_cast<int>(Dir::Right)] = 5;
    ASSERT_EQ(fe.neighbors[static_cast<int>(Dir::Right)], 5);
    // Others still -1.
    ASSERT_EQ(fe.neighbors[static_cast<int>(Dir::Up)],   -1);
    ASSERT_EQ(fe.neighbors[static_cast<int>(Dir::Down)], -1);
    ASSERT_EQ(fe.neighbors[static_cast<int>(Dir::Left)], -1);
    TEST_PASS("neighbour can be set individually; unset neighbours remain -1");
}

// ── Dir enum index mapping ────────────────────────────────────────────────────

static void test_dir_enum_indices() {
    ASSERT_EQ(static_cast<int>(Dir::Up),    0);
    ASSERT_EQ(static_cast<int>(Dir::Down),  1);
    ASSERT_EQ(static_cast<int>(Dir::Left),  2);
    ASSERT_EQ(static_cast<int>(Dir::Right), 3);
    TEST_PASS("Dir enum values: Up=0, Down=1, Left=2, Right=3");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_screen_dimensions();
    test_topbar_h();
    test_dock_constants();
    test_count_constants_not_remapped();
    test_titlebar_and_traffic();
    test_grip_size();
    test_win_min_max();
    test_win_sizing();
    test_focus_ring();
    test_cursor_start();
    test_hot_corners();
    test_focus_level_screen_variant();
    test_focus_level_window_variant();
    test_focus_element_neighbours_sentinel();
    test_focus_element_neighbour_set();
    test_dir_enum_indices();
    return 0;
}
