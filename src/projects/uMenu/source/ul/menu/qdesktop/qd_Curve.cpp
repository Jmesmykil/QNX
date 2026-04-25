// qd_Curve.cpp — Five-zone dynamic mouse-acceleration curve implementation.
// Ported from tools/mock-nro-desktop-gui/src/curve.rs (v0.5.0).
//
// All arithmetic is signed 32-bit fixed-point ×100.  Intermediate products
// that could overflow int32_t are widened to int64_t before division.
// No floats.  No allocations.

#include <ul/menu/qdesktop/qd_Curve.hpp>
#include <cstdint>
#include <climits>

namespace ul::menu::qdesktop {

namespace {

// ── sign ─────────────────────────────────────────────────────────────────────

// Returns -1, 0, or +1 — matches Rust sign() helper.
static inline int32_t curve_sign(int32_t x) {
    if (x > 0) return  1;
    if (x < 0) return -1;
    return 0;
}

// ── safe_abs ─────────────────────────────────────────────────────────────────

// Guards abs(INT32_MIN) UB.  Returns INT32_MAX for INT32_MIN, otherwise |v|.
static inline int32_t safe_abs(int32_t v) {
    if (v == INT32_MIN) return INT32_MAX;
    return (v < 0) ? -v : v;
}

// ── sat_add_u32 ───────────────────────────────────────────────────────────────

static inline uint32_t sat_add_u32(uint32_t a, uint32_t b) {
    const uint32_t r = a + b;
    return (r < a) ? UINT32_MAX : r;
}

// ── lerp_x100 ────────────────────────────────────────────────────────────────

// Linear interpolation in ×100 fixed-point.
// Returns y0_x100 when m == m0, y1_x100 when m == m1.
// Matches Rust lerp_x100() exactly — uses int64_t numerator.
static inline int32_t lerp_x100(int32_t m, int32_t m0, int32_t m1,
                                 int32_t y0_x100, int32_t y1_x100) {
    if (m1 == m0) return y0_x100;
    const int64_t t_num = static_cast<int64_t>(m  - m0)
                        * static_cast<int64_t>(y1_x100 - y0_x100);
    const int64_t t_den = static_cast<int64_t>(m1 - m0);
    return y0_x100 + static_cast<int32_t>(t_num / t_den);
}

// ── mul_x100 ─────────────────────────────────────────────────────────────────

// Multiply two ×100 fixed-point values to get a ×100 result.
// (a/100) * (b/100) * 100 = a*b/100.
// Widened to int64_t to prevent overflow.
static inline int32_t mul_x100(int32_t a_x100, int32_t b_x100) {
    return static_cast<int32_t>(
        static_cast<int64_t>(a_x100) * static_cast<int64_t>(b_x100) / 100LL);
}

// ── zone_speed_x100 ──────────────────────────────────────────────────────────

// Maps a non-negative deflection magnitude to ×100 speed.
// Mirrors Rust zone_speed_x100() exactly, including the quadratic accel zone.
static int32_t zone_speed_x100(int32_t magnitude) {
    // Zone 1: dead-zone → 0.
    if (magnitude < CURVE_DEADZONE) {
        return 0;
    }

    // Zone 2: precision band → linear 0.5–2.0 px/frame.
    if (magnitude < CURVE_PRECISION_END) {
        return lerp_x100(magnitude,
                         CURVE_DEADZONE,      CURVE_PRECISION_END,
                         50,                  200);
    }

    // Zone 3: linear band → linear 2.0–8.0 px/frame.
    if (magnitude < CURVE_LINEAR_END) {
        return lerp_x100(magnitude,
                         CURVE_PRECISION_END, CURVE_LINEAR_END,
                         200,                 800);
    }

    // Zone 4: acceleration → quadratic 8.0–24.0 px/frame.
    // Rust: t_x100 = (m - 22000) * 100 / 8000
    //       t_squared_x100 = t_x100^2 / 100
    //       bonus_x100 = t_squared_x100 * 16
    //       result = 800 + bonus_x100
    if (magnitude < CURVE_ACCEL_END) {
        const int64_t num    = static_cast<int64_t>(magnitude - CURVE_LINEAR_END) * 100LL;
        const int64_t den    = static_cast<int64_t>(CURVE_ACCEL_END - CURVE_LINEAR_END);
        const int32_t t_x100 = static_cast<int32_t>(num / den);

        // t^2 in ×100: (t_x100 * t_x100) / 100.
        const int64_t t_sq_x100  = static_cast<int64_t>(t_x100) * t_x100 / 100LL;
        const int32_t bonus_x100 = static_cast<int32_t>(t_sq_x100 * 16LL);
        return 800 + bonus_x100;
    }

    // Zone 5: burst band → linear 24.0–40.0 px/frame.
    // Clamp magnitude to BURST_END (defends against above-range HID values).
    {
        const int32_t m = (magnitude < CURVE_BURST_END) ? magnitude : CURVE_BURST_END;
        return lerp_x100(m,
                         CURVE_ACCEL_END, CURVE_BURST_END,
                         2400,            4000);
    }
}

// ── update_boost_state ────────────────────────────────────────────────────────

// Update the per-axis boost integrator.
// direction: -1, 0, or +1.  magnitude: unsigned deflection (already abs'd).
// Mirrors Rust update_boost_state() exactly.
static void update_boost_state(StickState &state, int32_t direction, int32_t magnitude) {
    if (magnitude < CURVE_DEADZONE) {
        // In dead-zone: count consecutive dead frames; reset boost when sustained.
        state.dead_frames = sat_add_u32(state.dead_frames, 1u);
        if (state.dead_frames >= CURVE_BOOST_RESET_FRAMES) {
            state.boost_frames   = 0u;
            state.last_direction = 0;
        }
        return;
    }

    // Not in dead-zone.
    state.dead_frames = 0u;

    const int8_t dir_i8 = static_cast<int8_t>(direction);

    if (state.last_direction == 0) {
        // First non-zero frame — start counting.
        state.last_direction = dir_i8;
        state.boost_frames   = 1u;
    } else if (state.last_direction == dir_i8) {
        // Same direction — continue ramping (saturate to prevent wrap).
        state.boost_frames = sat_add_u32(state.boost_frames, 1u);
    } else {
        // Direction reversed — restart counter, update direction.
        state.last_direction = dir_i8;
        state.boost_frames   = 1u;
    }
}

} // anonymous namespace

// ── BoostFactorX100 (public) ──────────────────────────────────────────────────

int32_t BoostFactorX100(const StickState &state) {
    if (state.boost_frames < CURVE_BOOST_THRESHOLD_FRAMES) {
        return CURVE_BOOST_MIN_X100;
    }
    const uint32_t ramp_frames = state.boost_frames - CURVE_BOOST_THRESHOLD_FRAMES;
    if (ramp_frames >= CURVE_BOOST_RAMP_FRAMES) {
        return CURVE_BOOST_MAX_X100;
    }
    // Linear ramp from BOOST_MIN to BOOST_MAX over BOOST_RAMP_FRAMES.
    // Rust: BOOST_MIN + (span * ramp_frames / BOOST_RAMP_FRAMES)
    const int32_t  span = CURVE_BOOST_MAX_X100 - CURVE_BOOST_MIN_X100;
    const int32_t  inc  = static_cast<int32_t>(
        static_cast<uint32_t>(span) * ramp_frames / CURVE_BOOST_RAMP_FRAMES);
    return CURVE_BOOST_MIN_X100 + inc;
}

// ── ComputeStickSpeed (public) ────────────────────────────────────────────────

int32_t ComputeStickSpeed(int32_t deflection, StickState &state, bool slow_mode_active) {
    const int32_t direction = curve_sign(deflection);
    const int32_t magnitude = safe_abs(deflection);

    update_boost_state(state, direction, magnitude);

    const int32_t raw_x100 = zone_speed_x100(magnitude);
    if (raw_x100 == 0) {
        return 0;
    }

    // Apply boost.
    const int32_t boost_x100   = BoostFactorX100(state);
    int32_t       final_x100   = mul_x100(raw_x100, boost_x100);

    // Apply slow-mode.
    if (slow_mode_active) {
        final_x100 = mul_x100(final_x100, CURVE_SLOW_MODE_X100);
    }

    // Convert ×100 speed to integer pixels.
    // Sub-pixel precision (0 < final_x100 < 100) rounds UP to 1 so small
    // deflections in the precision band still move the cursor.
    int32_t abs_speed;
    if (final_x100 >= 100) {
        abs_speed = final_x100 / 100;
    } else if (final_x100 > 0) {
        abs_speed = 1;
    } else {
        abs_speed = 0;
    }

    // Re-apply sign to match input direction.
    return (direction < 0) ? -abs_speed : abs_speed;
}

// ── ShouldApplySnap (public) ──────────────────────────────────────────────────

bool ShouldApplySnap(int32_t cursor_speed_x, int32_t cursor_speed_y, SnapMode mode) {
    if (mode != SnapMode::On) {
        return false;
    }
    const int32_t abs_x = safe_abs(cursor_speed_x);
    const int32_t abs_y = safe_abs(cursor_speed_y);
    // Override: any axis demanding more than SNAP_OVERRIDE_PX kills snap.
    if (abs_x > CURVE_SNAP_OVERRIDE_PX || abs_y > CURVE_SNAP_OVERRIDE_PX) {
        return false;
    }
    // Engage only when both axes are moving slowly (≤ SNAP_MAX_SPEED_PX).
    return (abs_x <= CURVE_SNAP_MAX_SPEED_PX && abs_y <= CURVE_SNAP_MAX_SPEED_PX);
}

// ── SnapPull (public) ─────────────────────────────────────────────────────────

void SnapPull(int32_t cursor_x, int32_t cursor_y,
              int32_t target_x, int32_t target_y,
              int32_t &out_dx, int32_t &out_dy) {
    const int32_t dx = target_x - cursor_x;
    const int32_t dy = target_y - cursor_y;

    // Use int64_t to prevent overflow in dist_sq for large coordinates.
    const int64_t dist_sq = static_cast<int64_t>(dx) * dx
                          + static_cast<int64_t>(dy) * dy;
    const int64_t radius_sq = static_cast<int64_t>(CURVE_SNAP_RADIUS_PX)
                            * CURVE_SNAP_RADIUS_PX;

    if (dist_sq > radius_sq) {
        out_dx = 0;
        out_dy = 0;
        return;
    }

    // Pull toward target by SNAP_PULL_PX per axis; don't overshoot.
    const int32_t pull_x = curve_sign(dx) * CURVE_SNAP_PULL_PX;
    const int32_t pull_y = curve_sign(dy) * CURVE_SNAP_PULL_PX;

    out_dx = (safe_abs(dx) < CURVE_SNAP_PULL_PX) ? dx : pull_x;
    out_dy = (safe_abs(dy) < CURVE_SNAP_PULL_PX) ? dy : pull_y;
}

} // namespace ul::menu::qdesktop
