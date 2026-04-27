// test_QdDesktopIcons_Hover.cpp -- v1.7.0-stabilize-2 host tests for REC-02.
//
// Models the last_input_was_dpad_ flag from QdDesktopIconsElement.
// Production logic lives in:
//   src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp
//     OnInput: sets last_input_was_dpad_ = true on Up/Down/Left/Right/A
//     OnInput: sets last_input_was_dpad_ = false on touch_down
//     OnRender: sets last_input_was_dpad_ = false on cursor delta != 0
//   PaintIconCell: const bool show_hover = is_mouse_hovered && !last_input_was_dpad_;
//
// The original A1 audit flagged the v1 plan's REC-02 as broken (using
// `dpad_focus_index_ < icon_count_` defaulted to 0 -> permanently
// suppressed hover ring -> broke ZR-launches-cursor-target). The corrected
// fix uses an explicit input-modality flag that flips between modes.
//
// We can't link QdDesktopIconsElement (depends on SDL2_image, libnx, etc.).
// Instead we reproduce the flag-flip rules and pin the show_hover_ring
// formula.
//
// Build:  make test_QdDesktopIcons_Hover
// Run:    ./test_QdDesktopIcons_Hover

#include "test_host_stubs.hpp"
#include <cstdio>

namespace v2 {

// Mirror of the input-modality state machine in qd_DesktopIcons.cpp.
struct HoverState {
    bool last_input_was_dpad = false;  // initial: cursor mode
    s32  prev_cursor_x       = -1;
    s32  prev_cursor_y       = -1;

    // Match OnInput's mask: Up/Down/Left/Right/A flip the flag to true.
    void on_dpad_or_a_press() { last_input_was_dpad = true; }

    // Match OnInput's TouchDown branch: any new touch flips back to "mouse".
    void on_touch_down() { last_input_was_dpad = false; }

    // Match OnRender's per-frame cursor delta check.
    void on_cursor_sample(s32 cx, s32 cy) {
        if (prev_cursor_x != -1 && prev_cursor_y != -1) {
            if (cx != prev_cursor_x || cy != prev_cursor_y) {
                last_input_was_dpad = false;
            }
        }
        prev_cursor_x = cx;
        prev_cursor_y = cy;
    }

    // PaintIconCell formula: hover ring shown iff cursor is hovering AND
    // the last meaningful input was NOT a D-pad/A press.
    bool show_hover_ring(bool is_mouse_hovered) const {
        return is_mouse_hovered && !last_input_was_dpad;
    }
};

} // namespace v2

// ── Test 1: D-pad press transitions flag to dpad mode ────────────────────────

static void test_dpad_press_sets_flag() {
    v2::HoverState hs;
    ASSERT_FALSE(hs.last_input_was_dpad);  // boot default = cursor mode

    hs.on_dpad_or_a_press();
    ASSERT_TRUE(hs.last_input_was_dpad);
    TEST_PASS("D-pad/A press flips last_input_was_dpad_ to true");
}

// ── Test 2: cursor motion transitions flag back to cursor mode ───────────────

static void test_cursor_motion_clears_flag() {
    v2::HoverState hs;
    // Seed the prev sample.
    hs.on_cursor_sample(960, 540);
    // Switch to D-pad navigation.
    hs.on_dpad_or_a_press();
    ASSERT_TRUE(hs.last_input_was_dpad);

    // Same sample again: NO motion -> flag stays.
    hs.on_cursor_sample(960, 540);
    ASSERT_TRUE(hs.last_input_was_dpad);

    // Cursor moves -> flag flips back to cursor mode.
    hs.on_cursor_sample(961, 540);
    ASSERT_FALSE(hs.last_input_was_dpad);
    TEST_PASS("cursor delta != 0 flips last_input_was_dpad_ back to false");
}

// ── Test 3: hover ring suppressed during D-pad navigation ────────────────────

static void test_hover_ring_suppressed_during_dpad() {
    v2::HoverState hs;
    hs.on_dpad_or_a_press();
    // Cursor is hovered over an icon, but D-pad is the active modality.
    // The hover ring must NOT render so only ONE highlight (the dpad-focus
    // ring) is visible.
    ASSERT_FALSE(hs.show_hover_ring(/*is_mouse_hovered=*/true));
    TEST_PASS("hover ring suppressed when last input was D-pad (no double-highlight)");
}

// ── Test 4: hover ring restored after cursor motion or touch ─────────────────

static void test_hover_ring_restored_after_cursor_or_touch() {
    v2::HoverState hs;
    // D-pad mode.
    hs.on_dpad_or_a_press();
    ASSERT_FALSE(hs.show_hover_ring(/*is_mouse_hovered=*/true));

    // Path A: cursor motion restores hover ring.
    hs.on_cursor_sample(500, 500);  // seed
    hs.on_cursor_sample(501, 500);  // motion
    ASSERT_TRUE(hs.show_hover_ring(/*is_mouse_hovered=*/true));

    // Path B: D-pad press again, then touch_down restores hover ring.
    hs.on_dpad_or_a_press();
    ASSERT_FALSE(hs.show_hover_ring(/*is_mouse_hovered=*/true));
    hs.on_touch_down();
    ASSERT_TRUE(hs.show_hover_ring(/*is_mouse_hovered=*/true));
    TEST_PASS("hover ring restored on cursor motion OR touch arrival");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_dpad_press_sets_flag();
    test_cursor_motion_clears_flag();
    test_hover_ring_suppressed_during_dpad();
    test_hover_ring_restored_after_cursor_or_touch();
    return 0;
}
