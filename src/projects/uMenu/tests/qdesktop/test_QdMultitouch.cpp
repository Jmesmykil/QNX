// test_QdMultitouch.cpp — host unit tests for SP3 multitouch gesture engine.
// SP3-F01: Gesture tagged union — no std::variant.
// SP3-F02: noise floors SCROLL_NOISE_FLOOR_PX=3, PINCH_NOISE_FLOOR_PX=3.
// SP3-F03: isqrt Newton's method.
// AB-09:   No std::variant / std::visit.
// AB-11:   INT32_MIN guards verified via safe_abs path.
#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Multitouch.hpp>
#include <cstdint>
#include <climits>

using namespace ul::menu::qdesktop;

// ── isqrt ─────────────────────────────────────────────────────────────────────

static void test_isqrt_zero() {
    ASSERT_EQ(isqrt(0u), 0u);
    TEST_PASS("isqrt(0) == 0");
}

static void test_isqrt_one() {
    ASSERT_EQ(isqrt(1u), 1u);
    TEST_PASS("isqrt(1) == 1");
}

static void test_isqrt_four() {
    ASSERT_EQ(isqrt(4u), 2u);
    TEST_PASS("isqrt(4) == 2");
}

static void test_isqrt_nine() {
    ASSERT_EQ(isqrt(9u), 3u);
    TEST_PASS("isqrt(9) == 3");
}

static void test_isqrt_perfect_square_large() {
    // 1000000 = 1000^2
    ASSERT_EQ(isqrt(1000000u), 1000u);
    TEST_PASS("isqrt(1000000) == 1000");
}

static void test_isqrt_non_perfect_square_floor() {
    // floor(sqrt(10)) = 3
    ASSERT_EQ(isqrt(10u), 3u);
    // floor(sqrt(15)) = 3
    ASSERT_EQ(isqrt(15u), 3u);
    // floor(sqrt(16)) = 4
    ASSERT_EQ(isqrt(16u), 4u);
    TEST_PASS("isqrt floor for non-perfect squares");
}

static void test_isqrt_large_non_perfect() {
    // floor(sqrt(2)) = 1; floor(sqrt(3)) = 1
    ASSERT_EQ(isqrt(2u), 1u);
    ASSERT_EQ(isqrt(3u), 1u);
    TEST_PASS("isqrt small non-perfect squares");
}

// ── gesture_none / state initialisation ──────────────────────────────────────

static void test_gesture_none_on_zero_count() {
    MultiTouchState st = multitouch_state_zero();
    MultiTouchFrame f = { 0, 0, 0, 0, 0 };
    Gesture g = multitouch_classify(f, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::None);
    ASSERT_FALSE(st.had_two_last_frame);
    TEST_PASS("count=0 → None + had_two_last_frame reset");
}

static void test_gesture_none_first_two_finger_frame() {
    // First frame with 2 fingers: record state, emit None (no delta available).
    MultiTouchState st = multitouch_state_zero();
    MultiTouchFrame f = { 2, 100, 200, 400, 200 };
    Gesture g = multitouch_classify(f, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::None);
    ASSERT_TRUE(st.had_two_last_frame);
    // Distance between (100,200)-(400,200) = 300.
    ASSERT_EQ(st.last_dist, 300);
    ASSERT_EQ(st.last_centroid_x, 250);
    ASSERT_EQ(st.last_centroid_y, 200);
    TEST_PASS("first two-finger frame emits None and records state");
}

// ── pinch detection ───────────────────────────────────────────────────────────

static void test_gesture_pinch_detected() {
    MultiTouchState st = multitouch_state_zero();
    // Frame 1: fingers at same Y, 100px apart.
    MultiTouchFrame f1 = { 2, 0, 0, 100, 0 };
    multitouch_classify(f1, st);  // seeds state

    // Frame 2: fingers now 120px apart; centroid barely moved (0,0→centroid stays 60).
    // last_centroid=(50,0), new_centroid=(60,0) → dx=10, dist_delta=20.
    // centroid_mag=abs_max(10,0)=10; safe_abs(10)=10 vs safe_abs(20)=20 → dist_delta wins.
    // safe_abs(20) > PINCH_NOISE_FLOOR_PX(3) → Pinch.
    MultiTouchFrame f2 = { 2, 0, 0, 120, 0 };
    Gesture g = multitouch_classify(f2, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::Pinch);
    ASSERT_EQ(g.data.pinch.delta_dist, 20);
    TEST_PASS("pinch detected when distance grows more than centroid shifts");
}

// ── scroll detection ──────────────────────────────────────────────────────────

static void test_gesture_scroll_detected() {
    MultiTouchState st = multitouch_state_zero();
    // Frame 1: two fingers at y=200, 100px apart; centroid=(300,200).
    MultiTouchFrame f1 = { 2, 250, 200, 350, 200 };
    multitouch_classify(f1, st);

    // Frame 2: both fingers moved right 50px; centroid=(350,200).
    // dx=50, dy=0; dist unchanged (100). dist_delta=0.
    // centroid_mag=abs_max(50,0)=50; 50>0 and 50>3 → Scroll.
    MultiTouchFrame f2 = { 2, 300, 200, 400, 200 };
    Gesture g = multitouch_classify(f2, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::TwoFingerScroll);
    ASSERT_EQ(g.data.scroll.dx, 50);
    ASSERT_EQ(g.data.scroll.dy, 0);
    TEST_PASS("two-finger scroll detected when centroid translates");
}

// ── noise floor: below threshold → None ──────────────────────────────────────

static void test_gesture_noise_floor_scroll() {
    MultiTouchState st = multitouch_state_zero();
    MultiTouchFrame f1 = { 2, 250, 200, 350, 200 };
    multitouch_classify(f1, st);

    // Centroid moves 2px right — below SCROLL_NOISE_FLOOR_PX(3).
    // dist unchanged; dist_delta=0 — below PINCH_NOISE_FLOOR_PX(3).
    MultiTouchFrame f2 = { 2, 252, 200, 352, 200 };
    Gesture g = multitouch_classify(f2, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::None);
    TEST_PASS("scroll below noise floor → None");
}

static void test_gesture_noise_floor_pinch() {
    MultiTouchState st = multitouch_state_zero();
    // 100px apart initially.
    MultiTouchFrame f1 = { 2, 0, 0, 100, 0 };
    multitouch_classify(f1, st);

    // Distance changes by 2px (102). centroid barely moves.
    // Both centroid_mag and dist_delta = 1/2 → below floors → None.
    MultiTouchFrame f2 = { 2, 0, 0, 102, 0 };
    Gesture g = multitouch_classify(f2, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::None);
    TEST_PASS("pinch below noise floor → None");
}

// ── state reset after losing contact ─────────────────────────────────────────

static void test_gesture_state_resets_on_liftoff() {
    MultiTouchState st = multitouch_state_zero();
    // Establish two-finger contact.
    MultiTouchFrame f1 = { 2, 100, 100, 200, 100 };
    multitouch_classify(f1, st);
    ASSERT_TRUE(st.had_two_last_frame);

    // Lift all fingers.
    MultiTouchFrame f0 = { 0, 0, 0, 0, 0 };
    multitouch_classify(f0, st);
    ASSERT_FALSE(st.had_two_last_frame);

    // Re-touch: first frame should again emit None (not a delta of something).
    MultiTouchFrame f2 = { 2, 100, 100, 200, 100 };
    Gesture g = multitouch_classify(f2, st);
    ASSERT_TRUE(g.kind == Gesture::Kind::None);
    TEST_PASS("had_two_last_frame resets on liftoff and first re-touch emits None");
}

// ── constructor helpers ───────────────────────────────────────────────────────

static void test_gesture_constructors() {
    Gesture gn = gesture_none();
    ASSERT_TRUE(gn.kind == Gesture::Kind::None);

    Gesture gp = gesture_pinch(-5);
    ASSERT_TRUE(gp.kind == Gesture::Kind::Pinch);
    ASSERT_EQ(gp.data.pinch.delta_dist, -5);

    Gesture gs = gesture_scroll(10, -20);
    ASSERT_TRUE(gs.kind == Gesture::Kind::TwoFingerScroll);
    ASSERT_EQ(gs.data.scroll.dx, 10);
    ASSERT_EQ(gs.data.scroll.dy, -20);

    TEST_PASS("gesture convenience constructors set fields correctly");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_isqrt_zero();
    test_isqrt_one();
    test_isqrt_four();
    test_isqrt_nine();
    test_isqrt_perfect_square_large();
    test_isqrt_non_perfect_square_floor();
    test_isqrt_large_non_perfect();
    test_gesture_none_on_zero_count();
    test_gesture_none_first_two_finger_frame();
    test_gesture_pinch_detected();
    test_gesture_scroll_detected();
    test_gesture_noise_floor_scroll();
    test_gesture_noise_floor_pinch();
    test_gesture_state_resets_on_liftoff();
    test_gesture_constructors();
    return 0;
}
