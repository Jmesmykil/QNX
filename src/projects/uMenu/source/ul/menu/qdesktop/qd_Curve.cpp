// qd_Curve.cpp — HID stick curve + snap engine implementation for uMenu C++ SP2.
// Ported from tools/mock-nro-desktop-gui/src/curve.rs.
// SP2-F07: i64 widening in accel zone interpolation.
// SP2-F08: safe_abs_i32 guards abs(INT32_MIN).
// SP2-F09: sat_add_u32 guards uint32 overflow in boost counters.
// AB-10:   All (diff * 100) widen to int64_t before division.

#include <ul/menu/qdesktop/qd_Curve.hpp>
#include <algorithm>  // std::min, std::max
#include <cstdint>
#include <cstdlib>    // abs (for int32_t, host tests)

namespace ul::menu::qdesktop {

// ── Internal helpers ──────────────────────────────────────────────────────────

// Linear interpolation ×100, range [min_x100, max_x100].
// t is in [0,100], representing fractional progress across the zone.
// AB-10: numerator uses int64_t to avoid overflow at large ×100 values.
static int32_t lerp_x100(int32_t t, int32_t min_x100, int32_t max_x100) {
    const int64_t diff = static_cast<int64_t>(max_x100 - min_x100);
    const int64_t num  = diff * static_cast<int64_t>(t);
    return min_x100 + static_cast<int32_t>(num / 100);
}

// Compute speed ×100 for a given unsigned deflection magnitude and zone data.
// SP2-F07: accel zone uses int64_t for interpolation numerator.
static int32_t zone_speed_x100(int32_t magnitude) {
    if (magnitude <= HID_DEADZONE) {
        return 0;
    }
    if (magnitude <= HID_PRECISION_END) {
        // Precision zone: lerp PRECISION_MIN → PRECISION_MAX
        const int64_t range = HID_PRECISION_END - HID_DEADZONE;
        const int64_t pos   = static_cast<int64_t>(magnitude - HID_DEADZONE);
        const int32_t t     = static_cast<int32_t>(pos * 100 / range);
        return lerp_x100(t, SPEED_PRECISION_MIN_X100, SPEED_PRECISION_MAX_X100);
    }
    if (magnitude <= HID_LINEAR_END) {
        // Linear zone: lerp LINEAR_MIN → LINEAR_MAX
        const int64_t range = HID_LINEAR_END - HID_PRECISION_END;
        const int64_t pos   = static_cast<int64_t>(magnitude - HID_PRECISION_END);
        const int32_t t     = static_cast<int32_t>(pos * 100 / range);
        return lerp_x100(t, SPEED_LINEAR_MIN_X100, SPEED_LINEAR_MAX_X100);
    }
    if (magnitude <= HID_ACCEL_END) {
        // SP2-F07: accel zone mandatory i64 widening.
        const int64_t range = static_cast<int64_t>(HID_ACCEL_END - HID_LINEAR_END);
        const int64_t pos   = static_cast<int64_t>(magnitude - HID_LINEAR_END);
        // SP2-F07: (pos * 100) in i64, then divide.
        const int32_t t     = static_cast<int32_t>((pos * 100LL) / range);
        return lerp_x100(t, SPEED_ACCEL_MIN_X100, SPEED_ACCEL_MAX_X100);
    }
    // Burst zone: lerp BURST_MIN → BURST_MAX
    {
        const int64_t range = HID_BURST_END - HID_ACCEL_END;
        const int64_t pos   = static_cast<int64_t>(magnitude - HID_ACCEL_END);
        const int32_t t     = static_cast<int32_t>(pos * 100 / range);
        return lerp_x100(t, SPEED_BURST_MIN_X100, SPEED_BURST_MAX_X100);
    }
}

// Multiply two ×100 values to get a ×100 result.
// (a/100) * (b/100) * 100 = a*b/100.
// AB-10: use int64_t for the multiplication.
static int32_t mul_x100(int32_t a_x100, int32_t b_x100) {
    return static_cast<int32_t>(static_cast<int64_t>(a_x100) * b_x100 / 100LL);
}

// Compute boost factor ×100 given boost_frames in [0, BOOST_RAMP_FRAMES].
static int32_t boost_factor_x100(uint32_t boost_frames) {
    if (boost_frames < BOOST_THRESHOLD_FRAMES) {
        return BOOST_MIN_X100;
    }
    const uint32_t ramp_frames = boost_frames - BOOST_THRESHOLD_FRAMES;
    if (ramp_frames >= BOOST_RAMP_FRAMES) {
        return BOOST_MAX_X100;
    }
    // AB-10: int64_t for numerator math.
    const int64_t range = BOOST_MAX_X100 - BOOST_MIN_X100;
    const int64_t t     = static_cast<int64_t>(ramp_frames) * 100LL /
                          static_cast<int64_t>(BOOST_RAMP_FRAMES);
    return BOOST_MIN_X100 + static_cast<int32_t>(range * t / 100LL);
}

// Update boost/dead frame counters for one axis.
// direction: -1, 0, or +1.
static void update_boost_state(CurveState &state, int8_t direction,
                                bool in_deadzone) {
    if (in_deadzone) {
        // Accumulate dead frames; reset boost on sustained dead period.
        state.dead_frames = sat_add_u32(state.dead_frames, 1u);
        if (state.dead_frames >= BOOST_RESET_FRAMES) {
            state.boost_frames = 0u;
        }
        state.last_direction = 0;
    } else {
        state.dead_frames = 0u;
        if (direction == state.last_direction && direction != 0) {
            // Same direction: accumulate boost, capped at BOOST_RAMP_FRAMES.
            state.boost_frames = sat_add_u32(state.boost_frames, 1u);
            if (state.boost_frames > BOOST_RAMP_FRAMES + BOOST_THRESHOLD_FRAMES) {
                state.boost_frames = BOOST_RAMP_FRAMES + BOOST_THRESHOLD_FRAMES;
            }
        } else {
            // Direction changed: reset boost.
            state.boost_frames   = 0u;
            state.last_direction = direction;
        }
    }
}

// ── compute_speed_axis ────────────────────────────────────────────────────────

int32_t compute_speed_axis(int32_t deflection, CurveState &state, bool slow_mode) {
    // SP2-F08: safe_abs_i32 guards abs(INT32_MIN) UB.
    const int32_t magnitude = safe_abs_i32(deflection);
    const bool    in_dz     = (magnitude <= HID_DEADZONE);

    // Determine direction (-1, 0, +1).
    const int8_t direction = in_dz ? 0 : (deflection < 0 ? -1 : 1);

    update_boost_state(state, direction, in_dz);

    if (in_dz) {
        return 0;
    }

    int32_t base_x100 = zone_speed_x100(magnitude);
    // Apply boost.
    const int32_t boost_x100 = boost_factor_x100(state.boost_frames);
    int32_t final_x100 = mul_x100(base_x100, boost_x100);

    // Slow mode: multiply by SLOW_MODE_X100 / 100.
    if (slow_mode) {
        final_x100 = mul_x100(final_x100, SLOW_MODE_X100);
    }

    // Convert ×100 speed to integer pixels (round sub-pixel motion up to 1).
    int32_t speed;
    if (final_x100 >= 100) {
        speed = final_x100 / 100;
    } else if (final_x100 > 0) {
        speed = 1;
    } else {
        speed = 0;
    }

    // Restore sign.
    return (deflection < 0) ? -speed : speed;
}

// ── apply_curve ───────────────────────────────────────────────────────────────

CurveResult apply_curve(int32_t deflection_x, int32_t deflection_y,
                        CurveState &state_x, CurveState &state_y,
                        bool slow_mode) {
    CurveResult result;
    result.speed_x = compute_speed_axis(deflection_x, state_x, slow_mode);
    result.speed_y = compute_speed_axis(deflection_y, state_y, slow_mode);
    return result;
}

// ── snap_pull ─────────────────────────────────────────────────────────────────

int32_t snap_pull(int32_t cursor_pos, int32_t target_pos) {
    const int32_t dist = target_pos - cursor_pos;
    const int32_t dist_abs = safe_abs_i32(dist);

    // Outside snap radius: no pull.
    if (dist_abs > SNAP_RADIUS_PX) {
        return 0;
    }

    // Clamp pull magnitude to SNAP_PULL_PX.
    const int32_t pull_abs = (dist_abs > 0) ? SNAP_PULL_PX : 0;
    return (dist < 0) ? -pull_abs : pull_abs;
}

// ── should_apply_snap ─────────────────────────────────────────────────────────

bool should_apply_snap(int32_t speed_abs, int32_t dist, SnapMode mode) {
    if (!snap_mode_is_active(mode)) {
        return false;
    }
    // Speed override: above SNAP_MAX_SPEED_PX the user is moving too fast.
    if (speed_abs > SNAP_MAX_SPEED_PX) {
        return false;
    }
    // Distance override: cursor already past SNAP_OVERRIDE_PX means snap fired.
    if (dist <= SNAP_OVERRIDE_PX) {
        return true;
    }
    return false;
}

} // namespace ul::menu::qdesktop
