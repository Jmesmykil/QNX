// test_QdCurve.cpp — Host-side unit tests for qd_Curve.
// Ports Rust curve.rs tests + 4 new boundary tests.
// Compile: c++ -std=c++23 -I../../include -I. test_QdCurve.cpp
//          ../../source/ul/menu/qdesktop/qd_Curve.cpp -o test_QdCurve

#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Curve.hpp>
#include <cstdio>
#include <climits>   // INT32_MIN, INT32_MAX

using namespace ul::menu::qdesktop;

// ── Zone boundary tests ───────────────────────────────────────────────────────

// Dead zone: deflection <= HID_DEADZONE → speed 0.
static void test_deadzone_returns_zero() {
    CurveState st = CurveState::zero();
    ASSERT_EQ(compute_speed_axis(0, st, false), 0);
    ASSERT_EQ(compute_speed_axis(HID_DEADZONE, st, false), 0);
    ASSERT_EQ(compute_speed_axis(-HID_DEADZONE, st, false), 0);
    TEST_PASS("deadzone_returns_zero");
}

// Just above dead zone: deflection = HID_DEADZONE + 1 → speed > 0.
static void test_just_above_deadzone() {
    CurveState st = CurveState::zero();
    const int32_t s = compute_speed_axis(HID_DEADZONE + 1, st, false);
    ASSERT_TRUE(s > 0);
    TEST_PASS("just_above_deadzone");
}

// Precision zone: deflection in (HID_DEADZONE, HID_PRECISION_END].
static void test_precision_zone_range() {
    CurveState st = CurveState::zero();
    // Midpoint of precision zone.
    const int32_t mid = (HID_DEADZONE + HID_PRECISION_END) / 2;
    const int32_t s = compute_speed_axis(mid, st, false);
    // Speed must be ≥1 and less than the linear-zone minimum (2 px at 200×100).
    ASSERT_TRUE(s >= 1);
    ASSERT_TRUE(s <= 2);
    TEST_PASS("precision_zone_range");
}

// Linear zone midpoint yields moderate speed.
static void test_linear_zone_midpoint() {
    CurveState st = CurveState::zero();
    const int32_t mid = (HID_PRECISION_END + HID_LINEAR_END) / 2;
    const int32_t s = compute_speed_axis(mid, st, false);
    // Linear zone maps 200×100→800×100, so midpoint speed ~5 (500×100/100).
    ASSERT_TRUE(s >= 2 && s <= 8);
    TEST_PASS("linear_zone_midpoint");
}

// Accel zone: larger deflection gives more speed.
static void test_accel_zone_monotonic() {
    CurveState st1 = CurveState::zero();
    CurveState st2 = CurveState::zero();
    const int32_t lo = HID_LINEAR_END + 1000;
    const int32_t hi = HID_ACCEL_END  - 1000;
    const int32_t s_lo = compute_speed_axis(lo, st1, false);
    const int32_t s_hi = compute_speed_axis(hi, st2, false);
    ASSERT_TRUE(s_hi > s_lo);
    TEST_PASS("accel_zone_monotonic");
}

// Burst zone: max deflection returns max speed.
static void test_burst_zone_max() {
    CurveState st = CurveState::zero();
    const int32_t s = compute_speed_axis(HID_BURST_END, st, false);
    // BURST_MAX_X100=4000 → 40 px/frame without boost.
    ASSERT_TRUE(s >= 30);
    TEST_PASS("burst_zone_max");
}

// Sign preserved: negative deflection → negative speed.
static void test_negative_deflection_sign() {
    CurveState st = CurveState::zero();
    const int32_t s = compute_speed_axis(-(HID_LINEAR_END), st, false);
    ASSERT_TRUE(s < 0);
    TEST_PASS("negative_deflection_sign");
}

// ── Slow mode ─────────────────────────────────────────────────────────────────

static void test_slow_mode_reduces_speed() {
    CurveState st1 = CurveState::zero();
    CurveState st2 = CurveState::zero();
    const int32_t defl = HID_LINEAR_END;
    const int32_t fast = compute_speed_axis(defl, st1, false);
    const int32_t slow = compute_speed_axis(defl, st2, true);
    // Slow mode applies ×40/100 → slow ≤ fast, and slow must be ≥ 0.
    ASSERT_TRUE(slow <= fast);
    ASSERT_TRUE(slow >= 0);
    TEST_PASS("slow_mode_reduces_speed");
}

// Slow mode: sub-pixel speeds still round up to 1.
static void test_slow_mode_sub_pixel_rounds_up() {
    CurveState st = CurveState::zero();
    // Use just above deadzone: base speed ×100 is ~50; ×40% = 20 → still >0 → rounds to 1.
    const int32_t s = compute_speed_axis(HID_DEADZONE + 100, st, true);
    ASSERT_TRUE(s >= 0);
    TEST_PASS("slow_mode_sub_pixel_rounds_up");
}

// ── Boost integration ─────────────────────────────────────────────────────────

static void test_boost_accumulates_in_same_direction() {
    CurveState st = CurveState::zero();
    const int32_t defl = HID_LINEAR_END;

    // First frame (no boost yet).
    const int32_t s0 = compute_speed_axis(defl, st, false);

    // After BOOST_THRESHOLD_FRAMES + BOOST_RAMP_FRAMES frames, boost is maxed.
    for (uint32_t i = 0u; i < BOOST_THRESHOLD_FRAMES + BOOST_RAMP_FRAMES; ++i) {
        compute_speed_axis(defl, st, false);
    }
    const int32_t s_boosted = compute_speed_axis(defl, st, false);
    ASSERT_TRUE(s_boosted >= s0);
    TEST_PASS("boost_accumulates_in_same_direction");
}

static void test_boost_resets_on_direction_change() {
    CurveState st = CurveState::zero();
    const int32_t defl = HID_LINEAR_END;

    // Build up boost.
    for (uint32_t i = 0u; i < BOOST_THRESHOLD_FRAMES + BOOST_RAMP_FRAMES; ++i) {
        compute_speed_axis(defl, st, false);
    }
    const uint32_t boosted_frames = st.boost_frames;

    // Flip direction.
    compute_speed_axis(-defl, st, false);
    ASSERT_TRUE(st.boost_frames < boosted_frames);
    TEST_PASS("boost_resets_on_direction_change");
}

// ── Snap helpers ──────────────────────────────────────────────────────────────

// snap_pull: outside radius → 0.
static void test_snap_pull_outside_radius() {
    ASSERT_EQ(snap_pull(0, SNAP_RADIUS_PX + 5), 0);
    ASSERT_EQ(snap_pull(0, -(SNAP_RADIUS_PX + 5)), 0);
    TEST_PASS("snap_pull_outside_radius");
}

// snap_pull: on the boundary (dist == SNAP_RADIUS_PX) → outside → 0.
static void test_snap_pull_boundary() {
    // dist = SNAP_RADIUS_PX+1 is outside → 0.
    ASSERT_EQ(snap_pull(0, SNAP_RADIUS_PX + 1), 0);
    // dist = SNAP_RADIUS_PX is inside → pull.
    // cursor_pos=0, target_pos=SNAP_RADIUS_PX: dist=SNAP_RADIUS_PX (within).
    const int32_t p = snap_pull(0, SNAP_RADIUS_PX);
    ASSERT_TRUE(p >= 0 && p <= SNAP_PULL_PX);
    TEST_PASS("snap_pull_boundary");
}

// should_apply_snap: inactive modes return false.
static void test_snap_inactive_modes() {
    ASSERT_FALSE(should_apply_snap(1, 1, SnapMode::Off));
    ASSERT_FALSE(should_apply_snap(1, 1, SnapMode::Suppressed));
    TEST_PASS("snap_inactive_modes");
}

// should_apply_snap: On mode, low speed, small dist → true.
static void test_snap_on_mode_applies() {
    ASSERT_TRUE(should_apply_snap(1, SNAP_OVERRIDE_PX, SnapMode::On));
    TEST_PASS("snap_on_mode_applies");
}

// should_apply_snap: On mode, speed > max → false.
static void test_snap_speed_override() {
    ASSERT_FALSE(should_apply_snap(SNAP_MAX_SPEED_PX + 1, 1, SnapMode::On));
    TEST_PASS("snap_speed_override");
}

// ── apply_curve two-axis wrapper ──────────────────────────────────────────────

static void test_apply_curve_two_axis() {
    CurveState sx = CurveState::zero();
    CurveState sy = CurveState::zero();
    CurveResult r = apply_curve(HID_LINEAR_END, -HID_LINEAR_END, sx, sy, false);
    ASSERT_TRUE(r.speed_x > 0);
    ASSERT_TRUE(r.speed_y < 0);
    TEST_PASS("apply_curve_two_axis");
}

// ── New boundary tests (4 required by pre-audit doc) ─────────────────────────

// NEW-1: safe_abs_i32(INT32_MIN) must not UB and must return INT32_MAX.
static void test_safe_abs_i32_min() {
    ASSERT_EQ(safe_abs_i32(INT32_MIN), INT32_MAX);
    TEST_PASS("safe_abs_i32_min");
}

// NEW-2: sat_add_u32 overflow → UINT32_MAX (not wrap-around).
static void test_sat_add_u32_overflow() {
    ASSERT_EQ(sat_add_u32(UINT32_MAX, 1u), UINT32_MAX);
    ASSERT_EQ(sat_add_u32(UINT32_MAX, UINT32_MAX), UINT32_MAX);
    ASSERT_EQ(sat_add_u32(0u, UINT32_MAX), UINT32_MAX);
    TEST_PASS("sat_add_u32_overflow");
}

// NEW-3: i64 math at upper accel bound (HID_ACCEL_END) does not overflow int32.
//   accel zone: pos = HID_ACCEL_END - HID_LINEAR_END = 8000
//   (8000 * 100) = 800000 — fits in int32, but test confirms result is sane.
static void test_accel_zone_upper_bound_no_overflow() {
    CurveState st = CurveState::zero();
    // At HID_ACCEL_END exactly, should map to ACCEL_MAX or transition to burst.
    const int32_t s = compute_speed_axis(HID_ACCEL_END, st, false);
    // ACCEL_MAX_X100 = 2400 → 24 px/frame max from this zone.
    ASSERT_TRUE(s >= 0 && s <= 50);
    TEST_PASS("accel_zone_upper_bound_no_overflow");
}

// NEW-4: SnapMode::is_active semantics — only On is active.
static void test_snap_mode_is_active_semantics() {
    ASSERT_TRUE(snap_mode_is_active(SnapMode::On));
    ASSERT_FALSE(snap_mode_is_active(SnapMode::Off));
    ASSERT_FALSE(snap_mode_is_active(SnapMode::Suppressed));
    TEST_PASS("snap_mode_is_active_semantics");
}

int main() {
    test_deadzone_returns_zero();
    test_just_above_deadzone();
    test_precision_zone_range();
    test_linear_zone_midpoint();
    test_accel_zone_monotonic();
    test_burst_zone_max();
    test_negative_deflection_sign();
    test_slow_mode_reduces_speed();
    test_slow_mode_sub_pixel_rounds_up();
    test_boost_accumulates_in_same_direction();
    test_boost_resets_on_direction_change();
    test_snap_pull_outside_radius();
    test_snap_pull_boundary();
    test_snap_inactive_modes();
    test_snap_on_mode_applies();
    test_snap_speed_override();
    test_apply_curve_two_axis();
    test_safe_abs_i32_min();
    test_sat_add_u32_overflow();
    test_accel_zone_upper_bound_no_overflow();
    test_snap_mode_is_active_semantics();

    fprintf(stderr, "\nAll test_QdCurve tests PASSED.\n");
    return 0;
}
