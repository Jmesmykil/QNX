// qos_CurveEngine_test.cpp — Host-side parity tests for qos_CurveEngine.
//
// Each test case mirrors the Rust #[test] from curve.rs v0.5.0.
// Compiled and run on the host (x86_64 macOS) to verify correctness before
// cross-compiling to aarch64-none-elf.
//
// Build + run:
//   g++ -std=c++17 -I../../../../../../include qos_CurveEngine.cpp qos_CurveEngine_test.cpp -o curve_test && ./curve_test
//
// Exit 0 = all tests passed.  Any failed assertion prints to stderr and exits 1.

#include "ul/menu/qos/qos_CurveEngine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

using namespace ul::menu::qos;

// ── Minimal test harness ──────────────────────────────────────────────────────

static int  s_tests_run    = 0;
static int  s_tests_failed = 0;

#define EXPECT(cond, msg)  do { \
    ++s_tests_run; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s: %s\n", __FILE__, __LINE__, __func__, msg); \
        ++s_tests_failed; \
    } \
} while (0)

#define EXPECT_EQ(a, b)  do { \
    ++s_tests_run; \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "FAIL [%s:%d] %s: expected %lld == %lld\n", \
                __FILE__, __LINE__, __func__, (long long)_a, (long long)_b); \
        ++s_tests_failed; \
    } \
} while (0)

static StickState fresh_state() { return StickState(); }

// ── Zone coverage ─────────────────────────────────────────────────────────────

static void test_zone_dead_returns_zero() {
    EXPECT_EQ(ZoneSpeedX100(0),              0);
    EXPECT_EQ(ZoneSpeedX100(2000),           0);
    EXPECT_EQ(ZoneSpeedX100(kDeadzone - 1),  0);
}

static void test_zone_precision_at_low_end_is_50_x100() {
    EXPECT_EQ(ZoneSpeedX100(kDeadzone), kPrecisionMinX100);
}

static void test_zone_precision_at_high_end_approaches_200_x100() {
    int32_t v = ZoneSpeedX100(kPrecisionEnd - 1);
    EXPECT(v > 195 && v <= kPrecisionMaxX100, "precision high end out of range");
}

static void test_zone_linear_at_low_end_is_200_x100() {
    EXPECT_EQ(ZoneSpeedX100(kPrecisionEnd), kLinearMinX100);
}

static void test_zone_linear_at_high_end_approaches_800_x100() {
    int32_t v = ZoneSpeedX100(kLinearEnd - 1);
    EXPECT(v > 795 && v <= kLinearMaxX100, "linear high end out of range");
}

static void test_zone_accel_at_low_end_is_800_x100() {
    EXPECT_EQ(ZoneSpeedX100(kLinearEnd), kAccelMinX100);
}

static void test_zone_accel_at_high_end_is_2400_x100() {
    int32_t v = ZoneSpeedX100(kAccelEnd - 1);
    EXPECT(v > 2350 && v <= 2400, "accel high end out of range");
}

static void test_zone_burst_low_end_is_2400_x100() {
    EXPECT_EQ(ZoneSpeedX100(kAccelEnd), kBurstMinX100);
}

static void test_zone_burst_at_max_clamps_to_4000_x100() {
    EXPECT_EQ(ZoneSpeedX100(kBurstEnd), kBurstMaxX100);
    EXPECT_EQ(ZoneSpeedX100(40000),     kBurstMaxX100); // beyond BURST_END clamps
}

static void test_zone_curve_is_monotonic_non_decreasing() {
    int32_t prev = 0;
    for (int32_t m = 0; m <= kBurstEnd; m += 500) {
        int32_t v = ZoneSpeedX100(m);
        EXPECT(v >= prev, "non-monotonic zone curve");
        prev = v;
    }
}

// ── ComputeSpeed: sign + dead-zone ────────────────────────────────────────────

static void test_compute_speed_zero_in_deadzone() {
    auto s = fresh_state();
    EXPECT_EQ(ComputeSpeed(0,      s, false), 0);
    s = fresh_state();
    EXPECT_EQ(ComputeSpeed(2000,   s, false), 0);
    s = fresh_state();
    EXPECT_EQ(ComputeSpeed(-2000,  s, false), 0);
}

static void test_compute_speed_preserves_sign() {
    auto s = fresh_state();
    EXPECT(ComputeSpeed(20000,  s, false) > 0, "positive input should be positive");
    s = fresh_state();
    EXPECT(ComputeSpeed(-20000, s, false) < 0, "negative input should be negative");
}

static void test_compute_speed_precision_band_at_least_one_pixel() {
    auto s = fresh_state();
    int32_t v = ComputeSpeed(kDeadzone + 100, s, false);
    EXPECT_EQ(v, 1); // 0.50 px/frame rounds up to 1
}

static void test_compute_speed_burst_band_yields_high_speed() {
    auto s = fresh_state();
    int32_t v = ComputeSpeed(kBurstEnd, s, false);
    EXPECT(v >= 40, "burst band should yield >=40 px/frame");
}

// ── Boost integration ─────────────────────────────────────────────────────────

static void test_boost_starts_at_one() {
    auto s = fresh_state();
    EXPECT_EQ(BoostFactorX100(s), kBoostMinX100);
}

static void test_boost_below_threshold_stays_at_min() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames - 1; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    EXPECT_EQ(BoostFactorX100(s), kBoostMinX100);
}

static void test_boost_at_threshold_starts_ramping() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    // First frame at threshold: ramp_frames = 0, still at MIN
    EXPECT_EQ(BoostFactorX100(s), kBoostMinX100);
}

static void test_boost_after_full_ramp_at_max() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames + kBoostRampFrames; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    EXPECT_EQ(BoostFactorX100(s), kBoostMaxX100);
}

static void test_boost_resets_after_dead_frames() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames + kBoostRampFrames; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    EXPECT_EQ(BoostFactorX100(s), kBoostMaxX100);
    for (uint32_t i = 0; i < kBoostResetFrames; ++i) {
        UpdateBoostState(s, 0, 0);
    }
    EXPECT_EQ(BoostFactorX100(s), kBoostMinX100);
    EXPECT_EQ(s.last_direction, 0);
}

static void test_boost_resets_on_direction_reversal() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames + kBoostRampFrames; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    UpdateBoostState(s, -1, 20000);
    EXPECT_EQ(s.boost_frames,   1u);
    EXPECT_EQ(s.last_direction, (int8_t)-1);
}

static void test_boost_within_few_dead_frames_does_not_reset() {
    auto s = fresh_state();
    for (uint32_t i = 0; i < kBoostThresholdFrames + 5; ++i) {
        UpdateBoostState(s, 1, 20000);
    }
    uint32_t frames_before = s.boost_frames;
    // 2 dead frames < BOOST_RESET_FRAMES (3)
    UpdateBoostState(s, 0, 0);
    UpdateBoostState(s, 0, 0);
    EXPECT_EQ(s.boost_frames, frames_before);
}

// ── Slow mode ─────────────────────────────────────────────────────────────────

static void test_slow_mode_reduces_speed() {
    auto s_normal = fresh_state();
    auto s_slow   = fresh_state();
    int32_t normal = ComputeSpeed(20000, s_normal, false);
    int32_t slow_v = ComputeSpeed(20000, s_slow,   true);
    int32_t abs_n = normal < 0 ? -normal : normal;
    int32_t abs_s = slow_v < 0 ? -slow_v : slow_v;
    EXPECT(abs_s < abs_n, "slow mode should reduce speed");
}

static void test_slow_mode_in_burst_band_still_yields_motion() {
    auto s = fresh_state();
    // burst (40) × 0.4 = 16 — must not be zero
    int32_t v = ComputeSpeed(kBurstEnd, s, true);
    int32_t abs_v = v < 0 ? -v : v;
    EXPECT(abs_v >= 10, "slow mode burst should still yield motion >=10");
}

// ── Snap ─────────────────────────────────────────────────────────────────────

static void test_snap_inside_radius_pulls_toward_target() {
    int32_t dx, dy;
    SnapPull(100, 100, 105, 100, dx, dy);
    EXPECT_EQ(dx, 1);
    EXPECT_EQ(dy, 0);
}

static void test_snap_outside_radius_returns_zero() {
    int32_t dx, dy;
    SnapPull(0, 0, 100, 100, dx, dy);
    EXPECT_EQ(dx, 0);
    EXPECT_EQ(dy, 0);
}

static void test_snap_does_not_overshoot() {
    // Cursor 1 px away — pull should be exactly 1
    int32_t dx, dy;
    SnapPull(99, 100, 100, 100, dx, dy);
    EXPECT_EQ(dx, 1);
    EXPECT_EQ(dy, 0);
}

static void test_snap_should_apply_when_slow_and_on() {
    EXPECT(ShouldApplySnap(2, 1, SnapMode::On), "snap should apply when slow + On");
}

static void test_snap_should_not_apply_when_off() {
    EXPECT(!ShouldApplySnap(2, 1, SnapMode::Off), "snap must not apply when Off");
}

static void test_snap_should_not_apply_when_suppressed() {
    EXPECT(!ShouldApplySnap(2, 1, SnapMode::Suppressed), "snap must not apply when Suppressed");
}

static void test_snap_cancelled_by_fast_motion() {
    EXPECT(!ShouldApplySnap(kSnapOverridePx + 1, 0, SnapMode::On), "x override kills snap");
    EXPECT(!ShouldApplySnap(0, kSnapOverridePx + 1, SnapMode::On), "y override kills snap");
}

static void test_snap_does_not_apply_above_max_speed() {
    // Between SNAP_MAX_SPEED and SNAP_OVERRIDE — snap NOT applied
    EXPECT(!ShouldApplySnap(kSnapMaxSpeedPx + 1, 0, SnapMode::On),
           "above max_speed snap not applied");
}

// ── Per-axis independence ─────────────────────────────────────────────────────

static void test_per_axis_diagonal_full_deflection_yields_full_speed_per_axis() {
    auto sx = fresh_state();
    auto sy = fresh_state();
    int32_t vx = ComputeSpeed(kBurstEnd, sx, false);
    int32_t vy = ComputeSpeed(kBurstEnd, sy, false);
    EXPECT(vx >= 40, "X axis burst should yield >=40 px/frame");
    EXPECT(vy >= 40, "Y axis burst should yield >=40 px/frame");
}

// ── Boundary stress ───────────────────────────────────────────────────────────

static void test_compute_speed_handles_negative_burst() {
    auto s = fresh_state();
    int32_t v = ComputeSpeed(-kBurstEnd, s, false);
    EXPECT(v <= -40, "negative burst should yield <=-40 px/frame");
}

static void test_compute_speed_handles_max_i32_clamps_safely() {
    auto s = fresh_state();
    // Above BURST_END — clamps to BURST_MAX speed, no panic/UB
    int32_t v = ComputeSpeed(40000, s, false);
    EXPECT(v >= 40 && v <= 60, "out-of-range deflection should clamp safely");
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    // Zone
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
    // ComputeSpeed
    test_compute_speed_zero_in_deadzone();
    test_compute_speed_preserves_sign();
    test_compute_speed_precision_band_at_least_one_pixel();
    test_compute_speed_burst_band_yields_high_speed();
    // Boost
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
    // Snap
    test_snap_inside_radius_pulls_toward_target();
    test_snap_outside_radius_returns_zero();
    test_snap_does_not_overshoot();
    test_snap_should_apply_when_slow_and_on();
    test_snap_should_not_apply_when_off();
    test_snap_should_not_apply_when_suppressed();
    test_snap_cancelled_by_fast_motion();
    test_snap_does_not_apply_above_max_speed();
    // Per-axis
    test_per_axis_diagonal_full_deflection_yields_full_speed_per_axis();
    // Boundary
    test_compute_speed_handles_negative_burst();
    test_compute_speed_handles_max_i32_clamps_safely();

    if (s_tests_failed == 0) {
        printf("OK  %d/%d tests passed\n", s_tests_run, s_tests_run);
        return 0;
    } else {
        fprintf(stderr, "FAILED  %d/%d tests\n", s_tests_failed, s_tests_run);
        return 1;
    }
}
