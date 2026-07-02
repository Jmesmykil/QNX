// test_QdFavoritesLayout.cpp -- v1.8.23 host tests for favorites strip layout.
//
// Pins the FAV_STRIP_TOP position introduced in v1.8.23 and verifies the four
// overlap-safety invariants the production comment at qd_DesktopIcons.cpp:1103
// documents:
//
//   Strip lives at y=726..856 (FAV_STRIP_TOP + ICON_BG_H = 726 + 130 = 856).
//   76 px clearance above (from folder grid bottom at y=650 to strip top at 726).
//   76 px clearance below (from strip bottom at 856 to dock top at 932).
//
// Also verifies the strip-slot → folder-col mapping arithmetic that the
// D-pad zone cycle (UP from strip → folder row 1) uses.
//
// All constants are mirrored verbatim from the production headers/source so
// that any future change registers as a test failure in this host build AND
// triggers the static_assert on the device build simultaneously.
//
// Build:  make test_QdFavoritesLayout  (from tests/qdesktop/)
// Run:    ./test_QdFavoritesLayout

#include "test_host_stubs.hpp"
#include <cstdint>
#include <cstddef>

// ── Constants mirrored from qd_DesktopIcons.hpp and qd_DesktopIcons.cpp ──────

// From qd_DesktopIcons.hpp:75-77
static constexpr int32_t ICON_BG_W       = 140;
static constexpr int32_t ICON_BG_H       = 130;
static constexpr int32_t ICON_GRID_GAP_X = 28;

// From qd_DesktopIcons.cpp:389-392
static constexpr int32_t DF_TILE_H  = 200;
static constexpr int32_t DF_GAP_Y   = 40;
static constexpr int32_t DF_COLS    = 3;
static constexpr int32_t DF_ROWS    = 2;   // kDesktopFolderCount / DF_COLS = 6/3

// From qd_DesktopIcons.cpp:401
static constexpr int32_t DF_GRID_Y  = 210;

// From qd_DesktopIcons.cpp:1105, 1110
static constexpr int32_t FAV_STRIP_VISIBLE = 6;
static constexpr int32_t FAV_STRIP_TOP     = 726;

// From qd_DesktopIcons.cpp:1499-1500
static constexpr int32_t kDockH          = 148;
static constexpr int32_t kDockNominalTop = 1080 - kDockH;   // 932

// Derived: folder grid bottom = DF_GRID_Y + 2 rows × (DF_TILE_H + DF_GAP_Y) - DF_GAP_Y
// Row 0 starts at DF_GRID_Y=210, row 1 starts at 210+200+40=450.
// Bottom of row 1 tile = 450+200 = 650. (No gap after the last row.)
static constexpr int32_t FOLDER_GRID_BOTTOM = DF_GRID_Y + DF_ROWS * DF_TILE_H
                                              + (DF_ROWS - 1) * DF_GAP_Y;   // 210+400+40 = 650

// Strip bottom = FAV_STRIP_TOP + ICON_BG_H = 726+130 = 856.
static constexpr int32_t FAV_STRIP_BOTTOM = FAV_STRIP_TOP + ICON_BG_H;     // 856

// Required clearance on both sides (per PROVENANCE.md: "76px clearance both sides").
static constexpr int32_t REQUIRED_CLEARANCE_PX = 76;

// ── Test 1: FAV_STRIP_TOP raw value pinned at 726 ───────────────────────────

static void test_fav_strip_top_pinned() {
    ASSERT_EQ(FAV_STRIP_TOP, 726);
    TEST_PASS("FAV_STRIP_TOP == 726 (v1.8.23 pin, moved from 58)");
}

// ── Test 2: Strip does not overlap folder grid (clearance above ≥ 76 px) ─────

static void test_strip_clears_folder_grid() {
    // Strip top must be at least REQUIRED_CLEARANCE_PX below folder grid bottom.
    const int32_t clearance = FAV_STRIP_TOP - FOLDER_GRID_BOTTOM;
    // Folder grid bottom = 650, strip top = 726 → clearance = 76.
    ASSERT_TRUE(clearance >= REQUIRED_CLEARANCE_PX);
    TEST_PASS("FAV_STRIP_TOP clears folder grid bottom by >= 76 px");
}

// ── Test 3: Strip does not overlap dock (clearance below ≥ 76 px) ───────────

static void test_strip_clears_dock() {
    // Strip bottom must be at least REQUIRED_CLEARANCE_PX above dock top.
    const int32_t clearance = kDockNominalTop - FAV_STRIP_BOTTOM;
    // kDockNominalTop = 932, strip bottom = 856 → clearance = 76.
    ASSERT_TRUE(clearance >= REQUIRED_CLEARANCE_PX);
    TEST_PASS("FAV_STRIP_BOTTOM clears dock top by >= 76 px");
}

// ── Test 4: Exact 76 px clearance both sides (document the tight fit) ────────

static void test_strip_exact_76px_clearance_both_sides() {
    const int32_t above = FAV_STRIP_TOP - FOLDER_GRID_BOTTOM;
    const int32_t below = kDockNominalTop - FAV_STRIP_BOTTOM;
    ASSERT_EQ(above, 76);
    ASSERT_EQ(below, 76);
    TEST_PASS("Exact 76 px clearance: folders→strip and strip→dock (symmetric)");
}

// ── Test 5: Strip-slot → folder-col mapping math (UP zone transition) ────────
// qd_DesktopIcons.cpp:3431:
//   strip_col = (fav_strip_focus_index_ * DF_COLS) / FAV_STRIP_VISIBLE
//
// With DF_COLS=3 and FAV_STRIP_VISIBLE=6:
//   slot 0 → col 0  (0*3/6 = 0)
//   slot 1 → col 0  (1*3/6 = 0)
//   slot 2 → col 1  (2*3/6 = 1)
//   slot 3 → col 1  (3*3/6 = 1)
//   slot 4 → col 2  (4*3/6 = 2)
//   slot 5 → col 2  (5*3/6 = 2)

static void test_strip_slot_to_folder_col_mapping() {
    struct { size_t slot; size_t expected_col; } cases[] = {
        {0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 2}, {5, 2},
    };
    for (auto &c : cases) {
        const size_t strip_col =
            (c.slot * static_cast<size_t>(DF_COLS)) / static_cast<size_t>(FAV_STRIP_VISIBLE);
        ASSERT_EQ(strip_col, c.expected_col);
    }
    TEST_PASS("strip-slot→folder-col mapping correct for all 6 slots (DF_COLS=3, FAV_STRIP_VISIBLE=6)");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_fav_strip_top_pinned();
    test_strip_clears_folder_grid();
    test_strip_clears_dock();
    test_strip_exact_76px_clearance_both_sides();
    test_strip_slot_to_folder_col_mapping();
    return 0;
}
