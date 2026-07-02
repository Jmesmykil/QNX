// test_QdAnim.cpp — Host-side unit tests for qd_Anim.
// Ports all 11 Rust anim.rs tests + 5 additional boundary cases.
// Compile: c++ -std=c++23 -I../../include -I. test_QdAnim.cpp
//          ../../source/ul/menu/qdesktop/qd_Anim.cpp -o test_QdAnim
//
// Includes test_host_stubs.hpp BEFORE any ul/menu/qdesktop header.

#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <cstdio>

using namespace ul::menu::qdesktop;

// ── Ported Rust tests (11) ────────────────────────────────────────────────────

// 1. ms_to_ticks rounds up correctly.
static void test_ms_to_ticks() {
    // 200ms @ 60fps = 12 ticks (200*60/1000 = 12 exact)
    ASSERT_EQ(ms_to_ticks(200), 12);
    // 150ms @ 60fps = 9 ticks (150*60/1000 = 9 exact)
    ASSERT_EQ(ms_to_ticks(150), 9);
    // 80ms @ 60fps = 4.8 → rounds up to 5
    ASSERT_EQ(ms_to_ticks(80), 5);
    // 100ms @ 60fps = 6 exact
    ASSERT_EQ(ms_to_ticks(100), 6);
    TEST_PASS("ms_to_ticks");
}

// 2. Linear easing is passthrough.
static void test_linear_passthrough() {
    ASSERT_EQ(easing_apply_x100(Easing::Linear, 0),   0);
    ASSERT_EQ(easing_apply_x100(Easing::Linear, 50),  50);
    ASSERT_EQ(easing_apply_x100(Easing::Linear, 100), 100);
    TEST_PASS("linear_passthrough");
}

// 3. EaseOut at t=50 gives 75.
// Rust: 100 - (50 * 50) / 100 = 100 - 25 = 75.
static void test_ease_out_midpoint() {
    ASSERT_EQ(easing_apply_x100(Easing::EaseOut, 50), 75);
    TEST_PASS("ease_out_midpoint");
}

// 4. EaseIn at t=50 gives 25.
// Rust: (50 * 50) / 100 = 25.
static void test_ease_in_midpoint() {
    ASSERT_EQ(easing_apply_x100(Easing::EaseIn, 50), 25);
    TEST_PASS("ease_in_midpoint");
}

// 5. Bounce peaks at 15 at t=50.
static void test_bounce_peaks_at_midpoint() {
    ASSERT_EQ(easing_apply_x100(Easing::Bounce, 50), 15);
    TEST_PASS("bounce_peaks_at_midpoint");
}

// 6. WindowOpen scale: starts at 100, ends at 105.
static void test_window_open_scale() {
    Animation anim = Animation::window_open(0);
    // At tick 0 (before start), value_at=0, scale=100.
    ASSERT_EQ(anim.scale_x100(0), 100);
    // After duration (tick >= WINDOW_OPEN_TICKS), value_at=100, scale=100+100*5/100=105.
    ASSERT_EQ(anim.scale_x100(WINDOW_OPEN_TICKS), 105);
    TEST_PASS("window_open_scale");
}

// 7. WindowClose alpha goes 100→0 over the animation.
static void test_window_close_alpha() {
    Animation anim = Animation::window_close(0);
    // At tick 0 (before start), value_at=0, alpha = 100 - 0 = 100.
    ASSERT_EQ(anim.alpha_x100(0), 100);
    // After duration, value_at=100, alpha = 100 - 100 = 0.
    ASSERT_EQ(anim.alpha_x100(WINDOW_CLOSE_TICKS), 0);
    TEST_PASS("window_close_alpha");
}

// 8. DockBounce: scale returns to 100 after animation (Bounce easing returns 0 at end).
static void test_dock_bounce_returns_to_baseline() {
    Animation anim = Animation::dock_bounce(0);
    // After duration, Bounce returns 0, so scale = 100 + 0 = 100.
    ASSERT_EQ(anim.scale_x100(DOCK_BOUNCE_TICKS), 100);
    // DockBounce alpha is always 100.
    ASSERT_EQ(anim.alpha_x100(0), 100);
    ASSERT_EQ(anim.alpha_x100(DOCK_BOUNCE_TICKS), 100);
    TEST_PASS("dock_bounce_returns_to_baseline");
}

// 9. is_done semantics.
static void test_is_done() {
    Animation anim = Animation::window_open(10);
    ASSERT_FALSE(anim.is_done(10));
    ASSERT_FALSE(anim.is_done(10 + WINDOW_OPEN_TICKS - 1));
    ASSERT_TRUE(anim.is_done(10 + WINDOW_OPEN_TICKS));
    ASSERT_TRUE(anim.is_done(10 + WINDOW_OPEN_TICKS + 100));
    TEST_PASS("is_done");
}

// 10. value_before_start_is_zero.
static void test_value_before_start_is_zero() {
    Animation anim = Animation::window_open(100);
    ASSERT_EQ(anim.value_at(0),   0);
    ASSERT_EQ(anim.value_at(99),  0);
    ASSERT_EQ(anim.value_at(100), 0); // start_tick exactly = 0
    TEST_PASS("value_before_start_is_zero");
}

// 11. value_after_end_clamps to 100 (non-Bounce).
static void test_value_after_end_clamps() {
    Animation anim = Animation::window_close(0);
    ASSERT_EQ(anim.value_at(WINDOW_CLOSE_TICKS),     100);
    ASSERT_EQ(anim.value_at(WINDOW_CLOSE_TICKS + 99), 100);
    TEST_PASS("value_after_end_clamps");
}

// ── Additional boundary cases (5) ────────────────────────────────────────────

// 12. Bounce after end returns 0 (triangular wave collapses).
static void test_bounce_after_end_is_zero() {
    Animation anim = Animation::dock_bounce(0);
    ASSERT_EQ(anim.value_at(DOCK_BOUNCE_TICKS),      0);
    ASSERT_EQ(anim.value_at(DOCK_BOUNCE_TICKS + 10), 0);
    TEST_PASS("bounce_after_end_is_zero");
}

// 13. EaseOut at boundaries.
static void test_ease_out_boundaries() {
    ASSERT_EQ(easing_apply_x100(Easing::EaseOut, 0),   0);
    ASSERT_EQ(easing_apply_x100(Easing::EaseOut, 100), 100);
    TEST_PASS("ease_out_boundaries");
}

// 14. EaseIn at boundaries.
static void test_ease_in_boundaries() {
    ASSERT_EQ(easing_apply_x100(Easing::EaseIn, 0),   0);
    ASSERT_EQ(easing_apply_x100(Easing::EaseIn, 100), 100);
    TEST_PASS("ease_in_boundaries");
}

// 15. WindowSnap alpha goes 100→0 (same as WindowClose).
static void test_window_snap_alpha() {
    Animation anim = Animation::window_snap(0);
    ASSERT_EQ(anim.alpha_x100(0),                  100);
    ASSERT_EQ(anim.alpha_x100(WINDOW_SNAP_TICKS), 0);
    TEST_PASS("window_snap_alpha");
}

// 16. WindowOpen alpha goes 0→100.
static void test_window_open_alpha() {
    Animation anim = Animation::window_open(0);
    ASSERT_EQ(anim.alpha_x100(0),                  0);
    ASSERT_EQ(anim.alpha_x100(WINDOW_OPEN_TICKS), 100);
    TEST_PASS("window_open_alpha");
}

int main() {
    test_ms_to_ticks();
    test_linear_passthrough();
    test_ease_out_midpoint();
    test_ease_in_midpoint();
    test_bounce_peaks_at_midpoint();
    test_window_open_scale();
    test_window_close_alpha();
    test_dock_bounce_returns_to_baseline();
    test_is_done();
    test_value_before_start_is_zero();
    test_value_after_end_clamps();
    test_bounce_after_end_is_zero();
    test_ease_out_boundaries();
    test_ease_in_boundaries();
    test_window_snap_alpha();
    test_window_open_alpha();

    fprintf(stderr, "\nAll test_QdAnim tests PASSED.\n");
    return 0;
}
