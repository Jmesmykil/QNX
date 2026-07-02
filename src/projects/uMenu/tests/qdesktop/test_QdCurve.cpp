// test_QdCurve.cpp — Host-side unit tests for qd_Curve (five-zone acceleration).
// Mirrors the 30+ tests in tools/mock-nro-desktop-gui/src/curve.rs (v0.5.0)
// plus boundary stress tests for C++-specific overflow guards.
//
// Compile:
//   c++ -std=c++23 -I../../include -I. \
//       test_QdCurve.cpp ../../source/ul/menu/qdesktop/qd_Curve.cpp \
//       -o test_QdCurve && ./test_QdCurve

#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Curve.hpp>
#include <cstdio>
#include <climits>    // INT32_MIN, INT32_MAX, UINT32_MAX

using namespace ul::menu::qdesktop;

// ── helpers ───────────────────────────────────────────────────────────────────

static inline StickState fresh() { return stick_state_zero(); }

// Drive update_boost_state indirectly by calling ComputeStickSpeed.
// We only need the state side-effect here; discard return value.
static void feed(StickState &st, int32_t defl, bool slow = false) {
    (void)ComputeStickSpeed(defl, st, slow);
}

// ── zone boundary tests (mirrors Rust zone_* tests) ──────────────────────────

static void test_zone_dead_returns_zero() {
    // zone_speed_x100 is internal; exercise it through ComputeStickSpeed
    // with a fresh (no-boost) state so multipliers are 1.0×.
    StickState st = fresh();
    ASSERT_EQ(ComputeStickSpeed(0,                st, false), 0);
    ASSERT_EQ(ComputeStickSpeed(2000,             st, false), 0);
    ASSERT_EQ(ComputeStickSpeed(CURVE_DEADZONE-1, st, false), 0);
    TEST_PASS("zone_dead_returns_zero");
}

static void test_zone_precision_at_low_end_is_50_x100() {
    // Rust: zone_speed_x100(DEADZONE) == PRECISION_MIN_X100 == 50.
    // In C++ the multiplier chain preserves this: 50 x100 < 100 rounds up to 1.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_DEADZONE, st, false);
    ASSERT_EQ(v, 1);     // 50 x100 → rounds to 1 px (sub-pixel round-up)
    TEST_PASS("zone_precision_at_low_end_is_50_x100");
}

static void test_zone_precision_at_high_end_approaches_200_x100() {
    // Rust: v > 195 && v <= PRECISION_MAX_X100.  C++: 195 x100 / 100 = 1 or 2.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_PRECISION_END - 1, st, false);
    ASSERT_TRUE(v >= 1 && v <= 2);
    TEST_PASS("zone_precision_at_high_end_approaches_200_x100");
}

static void test_zone_linear_at_low_end_is_200_x100() {
    // Rust: zone_speed_x100(PRECISION_END) == LINEAR_MIN_X100 == 200 → 2 px.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_PRECISION_END, st, false);
    ASSERT_EQ(v, 2);
    TEST_PASS("zone_linear_at_low_end_is_200_x100");
}

static void test_zone_linear_at_high_end_approaches_800_x100() {
    // Rust: v > 795 && v <= LINEAR_MAX_X100 (800).  800 x100 → 8 px.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_LINEAR_END - 1, st, false);
    ASSERT_TRUE(v >= 7 && v <= 8);
    TEST_PASS("zone_linear_at_high_end_approaches_800_x100");
}

static void test_zone_accel_at_low_end_is_800_x100() {
    // t=0 → bonus=0 → 800 x100 / 100 = 8.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_LINEAR_END, st, false);
    ASSERT_EQ(v, 8);
    TEST_PASS("zone_accel_at_low_end_is_800_x100");
}

static void test_zone_accel_at_high_end_is_2400_x100() {
    // Rust: 2350 < v <= 2400 (discretisation).  /100 → 23–24 px.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_ACCEL_END - 1, st, false);
    ASSERT_TRUE(v >= 23 && v <= 24);
    TEST_PASS("zone_accel_at_high_end_is_2400_x100");
}

static void test_zone_burst_low_end_is_2400_x100() {
    // ACCEL_END transitions to BURST → BURST_MIN = 2400 x100 → 24 px.
    StickState st = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_ACCEL_END, st, false);
    ASSERT_EQ(v, 24);
    TEST_PASS("zone_burst_low_end_is_2400_x100");
}

static void test_zone_burst_at_max_clamps_to_4000_x100() {
    // BURST_END → 4000 x100 → 40 px without boost.
    StickState st = fresh();
    ASSERT_EQ(ComputeStickSpeed(CURVE_BURST_END, st, false), 40);
    // Above BURST_END clamps to same ceiling.
    StickState st2 = fresh();
    ASSERT_EQ(ComputeStickSpeed(40000, st2, false), 40);
    TEST_PASS("zone_burst_at_max_clamps_to_4000_x100");
}

static void test_zone_curve_is_monotonic_non_decreasing() {
    // Sample every 500 raw units.  No boost, so multiplier is exactly 1.0.
    int32_t prev = 0;
    for (int32_t m = 0; m <= CURVE_BURST_END; m += 500) {
        StickState st = fresh();
        const int32_t v = ComputeStickSpeed(m, st, false);
        ASSERT_TRUE(v >= prev);
        prev = v;
    }
    TEST_PASS("zone_curve_is_monotonic_non_decreasing");
}

// ── compute_speed: sign + dead-zone (mirrors Rust compute_speed tests) ────────

static void test_compute_speed_zero_in_deadzone() {
    StickState s = fresh();
    ASSERT_EQ(ComputeStickSpeed(0,    s, false), 0);
    ASSERT_EQ(ComputeStickSpeed(2000, s, false), 0);
    ASSERT_EQ(ComputeStickSpeed(-2000, s, false), 0);
    TEST_PASS("compute_speed_zero_in_deadzone");
}

static void test_compute_speed_preserves_sign() {
    StickState s1 = fresh();
    ASSERT_TRUE(ComputeStickSpeed(20000, s1, false) > 0);
    StickState s2 = fresh();
    ASSERT_TRUE(ComputeStickSpeed(-20000, s2, false) < 0);
    TEST_PASS("compute_speed_preserves_sign");
}

static void test_compute_speed_precision_band_at_least_one_pixel() {
    // Just past dead-zone: raw 50 x100 rounds up to 1.
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_DEADZONE + 100, s, false);
    ASSERT_EQ(v, 1);
    TEST_PASS("compute_speed_precision_band_at_least_one_pixel");
}

static void test_compute_speed_burst_band_yields_high_speed() {
    // Rust: v >= 40 without boost.
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_BURST_END, s, false);
    ASSERT_TRUE(v >= 40);
    TEST_PASS("compute_speed_burst_band_yields_high_speed");
}

// ── Boost integration tests (mirrors Rust boost_* tests) ─────────────────────

static void test_boost_starts_at_one() {
    const StickState s = fresh();
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MIN_X100);
    TEST_PASS("boost_starts_at_one");
}

static void test_boost_below_threshold_stays_at_min() {
    StickState s = fresh();
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES - 1u; ++i) {
        feed(s, 20000);
    }
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MIN_X100);
    TEST_PASS("boost_below_threshold_stays_at_min");
}

static void test_boost_at_threshold_starts_ramping() {
    // Rust: after exactly BOOST_THRESHOLD_FRAMES frames, ramp_frames == 0 → still MIN.
    StickState s = fresh();
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES; ++i) {
        feed(s, 20000);
    }
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MIN_X100);
    TEST_PASS("boost_at_threshold_starts_ramping");
}

static void test_boost_after_full_ramp_at_max() {
    StickState s = fresh();
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES + CURVE_BOOST_RAMP_FRAMES; ++i) {
        feed(s, 20000);
    }
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MAX_X100);
    TEST_PASS("boost_after_full_ramp_at_max");
}

static void test_boost_resets_after_dead_frames() {
    StickState s = fresh();
    // Build to max.
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES + CURVE_BOOST_RAMP_FRAMES; ++i) {
        feed(s, 20000);
    }
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MAX_X100);
    // Three dead frames resets.
    for (uint32_t i = 0; i < CURVE_BOOST_RESET_FRAMES; ++i) {
        feed(s, 0);
    }
    ASSERT_EQ(BoostFactorX100(s), CURVE_BOOST_MIN_X100);
    ASSERT_EQ(s.last_direction, (int8_t)0);
    TEST_PASS("boost_resets_after_dead_frames");
}

static void test_boost_resets_on_direction_reversal() {
    StickState s = fresh();
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES + CURVE_BOOST_RAMP_FRAMES; ++i) {
        feed(s, 20000);
    }
    ASSERT_EQ(s.last_direction, (int8_t)1);
    // One frame in opposite direction restarts counter.
    feed(s, -20000);
    ASSERT_EQ(s.boost_frames, 1u);
    ASSERT_EQ(s.last_direction, (int8_t)-1);
    TEST_PASS("boost_resets_on_direction_reversal");
}

static void test_boost_within_few_dead_frames_does_not_reset() {
    // 1-2 dead frames (below RESET threshold=3) must NOT reset boost.
    StickState s = fresh();
    for (uint32_t i = 0; i < CURVE_BOOST_THRESHOLD_FRAMES + 5u; ++i) {
        feed(s, 20000);
    }
    const uint32_t frames_before = s.boost_frames;
    feed(s, 0);  // dead frame 1
    feed(s, 0);  // dead frame 2 (threshold is 3)
    ASSERT_EQ(s.boost_frames, frames_before);  // counter unchanged
    TEST_PASS("boost_within_few_dead_frames_does_not_reset");
}

// ── Slow-mode tests (mirrors Rust slow_mode_* tests) ─────────────────────────

static void test_slow_mode_reduces_speed() {
    StickState sn = fresh(), ss = fresh();
    const int32_t normal = ComputeStickSpeed(20000, sn, false);
    const int32_t slow   = ComputeStickSpeed(20000, ss, true);
    ASSERT_TRUE(slow > 0);
    ASSERT_TRUE(slow < normal);
    TEST_PASS("slow_mode_reduces_speed");
}

static void test_slow_mode_in_burst_band_still_yields_motion() {
    // Rust: burst(40) × 0.4 = 16 → v >= 10.
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_BURST_END, s, true);
    ASSERT_TRUE(v >= 10);
    TEST_PASS("slow_mode_in_burst_band_still_yields_motion");
}

// ── Snap helpers (mirrors Rust snap_* tests) ──────────────────────────────────

static void test_snap_inside_radius_pulls_toward_target() {
    // Rust: snap_pull(100,100, 105,100) → dx=1, dy=0.
    int32_t dx = 0, dy = 0;
    SnapPull(100, 100, 105, 100, dx, dy);
    ASSERT_EQ(dx, 1);
    ASSERT_EQ(dy, 0);
    TEST_PASS("snap_inside_radius_pulls_toward_target");
}

static void test_snap_outside_radius_returns_zero() {
    int32_t dx = 0, dy = 0;
    SnapPull(0, 0, 100, 100, dx, dy);
    ASSERT_EQ(dx, 0);
    ASSERT_EQ(dy, 0);
    TEST_PASS("snap_outside_radius_returns_zero");
}

static void test_snap_does_not_overshoot() {
    // Cursor 1 px away from target — pull should be exactly 1 (== dx), not SNAP_PULL_PX.
    int32_t dx = 0, dy = 0;
    SnapPull(99, 100, 100, 100, dx, dy);
    ASSERT_EQ(dx, 1);
    ASSERT_EQ(dy, 0);
    TEST_PASS("snap_does_not_overshoot");
}

static void test_snap_should_apply_when_slow_and_on() {
    ASSERT_TRUE(ShouldApplySnap(2, 1, SnapMode::On));
    TEST_PASS("snap_should_apply_when_slow_and_on");
}

static void test_snap_should_not_apply_when_off() {
    ASSERT_FALSE(ShouldApplySnap(2, 1, SnapMode::Off));
    TEST_PASS("snap_should_not_apply_when_off");
}

static void test_snap_should_not_apply_when_suppressed() {
    ASSERT_FALSE(ShouldApplySnap(2, 1, SnapMode::Suppressed));
    TEST_PASS("snap_should_not_apply_when_suppressed");
}

static void test_snap_cancelled_by_fast_motion() {
    // Any axis > SNAP_OVERRIDE_PX kills snap.
    ASSERT_FALSE(ShouldApplySnap(CURVE_SNAP_OVERRIDE_PX + 1, 0, SnapMode::On));
    ASSERT_FALSE(ShouldApplySnap(0, CURVE_SNAP_OVERRIDE_PX + 1, SnapMode::On));
    TEST_PASS("snap_cancelled_by_fast_motion");
}

static void test_snap_does_not_apply_above_max_speed() {
    // Speed in (SNAP_MAX_SPEED_PX, SNAP_OVERRIDE_PX] → snap NOT applied.
    ASSERT_FALSE(ShouldApplySnap(CURVE_SNAP_MAX_SPEED_PX + 1, 0, SnapMode::On));
    TEST_PASS("snap_does_not_apply_above_max_speed");
}

// ── Per-axis independence (mirrors Rust per_axis_* test) ─────────────────────

static void test_per_axis_diagonal_full_deflection_yields_full_speed_per_axis() {
    StickState sx = fresh(), sy = fresh();
    const int32_t vx = ComputeStickSpeed(CURVE_BURST_END, sx, false);
    const int32_t vy = ComputeStickSpeed(CURVE_BURST_END, sy, false);
    ASSERT_TRUE(vx >= 40);
    ASSERT_TRUE(vy >= 40);
    TEST_PASS("per_axis_diagonal_full_deflection_yields_full_speed_per_axis");
}

// ── Boundary stress (mirrors Rust boundary_stress tests) ─────────────────────

static void test_compute_speed_handles_negative_burst() {
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(-CURVE_BURST_END, s, false);
    ASSERT_TRUE(v <= -40);
    TEST_PASS("compute_speed_handles_negative_burst");
}

static void test_compute_speed_handles_max_i32_clamps_safely() {
    // Rust: compute_speed(40000) should be 40..60.
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(40000, s, false);
    ASSERT_TRUE(v >= 40 && v <= 60);
    TEST_PASS("compute_speed_handles_max_i32_clamps_safely");
}

// ── C++-specific overflow guard tests ────────────────────────────────────────

static void test_boost_factor_intermediate_ramp_value() {
    // After exactly THRESHOLD + RAMP/2 frames the boost should be ~125 x100 (midpoint).
    StickState s = fresh();
    const uint32_t half = CURVE_BOOST_THRESHOLD_FRAMES + CURVE_BOOST_RAMP_FRAMES / 2u;
    for (uint32_t i = 0; i < half; ++i) { feed(s, 20000); }
    const int32_t b = BoostFactorX100(s);
    // Linear ramp: MIN=100, MAX=150, midpoint ≈ 125 (± 1 for integer div).
    ASSERT_TRUE(b >= 123 && b <= 127);
    TEST_PASS("boost_factor_intermediate_ramp_value");
}

static void test_snap_pull_neg_direction() {
    // Cursor to the right of target → dx negative.
    int32_t dx = 0, dy = 0;
    SnapPull(105, 100, 100, 100, dx, dy);
    ASSERT_EQ(dx, -1);
    ASSERT_EQ(dy, 0);
    TEST_PASS("snap_pull_neg_direction");
}

static void test_snap_pull_both_axes() {
    // Cursor 3 px down, 3 px right — within SNAP_RADIUS_PX (12) → pull both axes.
    int32_t dx = 0, dy = 0;
    SnapPull(100, 100, 103, 103, dx, dy);
    ASSERT_EQ(dx, 1);
    ASSERT_EQ(dy, 1);
    TEST_PASS("snap_pull_both_axes");
}

static void test_boost_saturating_add_no_wrap() {
    // Drive boost_frames to saturation without triggering UB.
    StickState s = fresh();
    s.boost_frames = UINT32_MAX - 1u;
    s.last_direction = 1;
    // One more frame in same direction → should saturate to UINT32_MAX, not wrap.
    feed(s, 20000);
    ASSERT_EQ(s.boost_frames, UINT32_MAX);
    TEST_PASS("boost_saturating_add_no_wrap");
}

static void test_accel_zone_quadratic_midpoint() {
    // At LINEAR_END + 4000 (midpoint of accel zone):
    // t_x100 = 4000*100/8000 = 50
    // t_sq = 50*50/100 = 25
    // bonus = 25*16 = 400 → result x100 = 800+400 = 1200 → 12 px
    StickState s = fresh();
    const int32_t v = ComputeStickSpeed(CURVE_LINEAR_END + 4000, s, false);
    ASSERT_EQ(v, 12);
    TEST_PASS("accel_zone_quadratic_midpoint");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    // Zone coverage (mirrors Rust zone_* tests)
    test_zone_dead_returns_zero();
    test_zone_precision_at_low_end_is_50_x100();
    test_zone_precision_at_high_end_approaches_200_x100();
    test_zone_linear_at_low_end_is_200_x100();
    test_zone_linear_at_high_end_approaches_800_x100();
    test_zone_accel_at_low_end_is_800_x100();
    test_zone_accel_at_high_end_is_2400_x100();
    test_zone_burst_low_end_is_2400_x100();
    test_zone_burst_at_max_clamps_to_4000_x100();
    test_zone_curve_is_monotonic_non_decreasing();

    // compute_speed basics
    test_compute_speed_zero_in_deadzone();
    test_compute_speed_preserves_sign();
    test_compute_speed_precision_band_at_least_one_pixel();
    test_compute_speed_burst_band_yields_high_speed();

    // Boost integrator
    test_boost_starts_at_one();
    test_boost_below_threshold_stays_at_min();
    test_boost_at_threshold_starts_ramping();
    test_boost_after_full_ramp_at_max();
    test_boost_resets_after_dead_frames();
    test_boost_resets_on_direction_reversal();
    test_boost_within_few_dead_frames_does_not_reset();

    // Slow mode
    test_slow_mode_reduces_speed();
    test_slow_mode_in_burst_band_still_yields_motion();

    // Snap helpers
    test_snap_inside_radius_pulls_toward_target();
    test_snap_outside_radius_returns_zero();
    test_snap_does_not_overshoot();
    test_snap_should_apply_when_slow_and_on();
    test_snap_should_not_apply_when_off();
    test_snap_should_not_apply_when_suppressed();
    test_snap_cancelled_by_fast_motion();
    test_snap_does_not_apply_above_max_speed();

    // Per-axis independence
    test_per_axis_diagonal_full_deflection_yields_full_speed_per_axis();

    // Boundary stress
    test_compute_speed_handles_negative_burst();
    test_compute_speed_handles_max_i32_clamps_safely();

    // C++-specific overflow guard + extra coverage
    test_boost_factor_intermediate_ramp_value();
    test_snap_pull_neg_direction();
    test_snap_pull_both_axes();
    test_boost_saturating_add_no_wrap();
    test_accel_zone_quadratic_midpoint();

    fprintf(stderr, "\nAll %d test_QdCurve tests PASSED.\n", 39);
    return 0;
}
