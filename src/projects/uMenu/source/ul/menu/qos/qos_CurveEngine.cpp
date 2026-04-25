// qos_CurveEngine.cpp — Five-zone hybrid stick-acceleration curve with adaptive boost.
//
// Ported from Rust: mock-nro-desktop-gui/src/curve.rs (v0.5.0)
// Port date: 2026-04-23
//
// Compiled with -fno-exceptions -fno-rtti per uMenu Makefile defaults.

#include <ul/menu/qos/qos_CurveEngine.hpp>
#include <cstdint>
#include <cstdlib> // abs()

namespace ul::menu::qos {

// ── Internal helpers ──────────────────────────────────────────────────────────

static inline int32_t Sign(int32_t x) {
    if (x > 0) return  1;
    if (x < 0) return -1;
    return 0;
}

/// Linear interpolation in fixed-point ×100.
/// Returns y0 when m == m0, y1 when m == m1.
static int32_t LerpX100(int32_t m, int32_t m0, int32_t m1,
                        int32_t y0_x100, int32_t y1_x100) {
    if (m1 == m0) return y0_x100;
    int64_t t_num = static_cast<int64_t>(m - m0) * (y1_x100 - y0_x100);
    int64_t t_den = static_cast<int64_t>(m1 - m0);
    return y0_x100 + static_cast<int32_t>(t_num / t_den);
}

/// Multiply two fixed-point ×100 values; result is also ×100.
static inline int32_t MulX100(int32_t a_x100, int32_t b_x100) {
    return static_cast<int32_t>(
        (static_cast<int64_t>(a_x100) * b_x100) / 100);
}

/// Saturating add for uint32_t — stops at UINT32_MAX, never wraps.
static inline uint32_t SatAddU32(uint32_t a, uint32_t b) {
    if (a > UINT32_MAX - b) return UINT32_MAX;
    return a + b;
}

// ── Zone lookup ───────────────────────────────────────────────────────────────

int32_t ZoneSpeedX100(int32_t magnitude) {
    if (magnitude < kDeadzone) {
        return 0;
    }
    if (magnitude < kPrecisionEnd) {
        // Linear: 0.50 → 2.00 over [4500, 12000]
        return LerpX100(magnitude, kDeadzone, kPrecisionEnd,
                        kPrecisionMinX100, kPrecisionMaxX100);
    }
    if (magnitude < kLinearEnd) {
        // Linear: 2.00 → 8.00 over [12000, 22000]
        return LerpX100(magnitude, kPrecisionEnd, kLinearEnd,
                        kLinearMinX100, kLinearMaxX100);
    }
    if (magnitude < kAccelEnd) {
        // Quadratic: 8.00 + ((m-22000)/8000)² × 16 over [22000, 30000]
        // t_x100 = (m - 22000) * 100 / 8000
        int64_t t_x100 = (static_cast<int64_t>(magnitude - kLinearEnd) * 100)
                         / (kAccelEnd - kLinearEnd);
        // t² × 16 in ×100 form: (t_x100² / 100) * 16
        int64_t t_sq_x100 = (t_x100 * t_x100) / 100;
        int32_t bonus_x100 = static_cast<int32_t>(t_sq_x100 * 16);
        return kAccelMinX100 + bonus_x100;
    }
    // Burst band: 24.0 → 40.0 over [30000, 32767]; clamp beyond BURST_END
    int32_t m_clamped = (magnitude < kBurstEnd) ? magnitude : kBurstEnd;
    return LerpX100(m_clamped, kAccelEnd, kBurstEnd,
                    kBurstMinX100, kBurstMaxX100);
}

// ── Boost integrator ──────────────────────────────────────────────────────────

void UpdateBoostState(StickState& state, int32_t direction, int32_t magnitude) {
    if (magnitude < kDeadzone) {
        state.dead_frames = SatAddU32(state.dead_frames, 1);
        if (state.dead_frames >= kBoostResetFrames) {
            state.boost_frames    = 0;
            state.last_direction  = 0;
        }
        return;
    }

    state.dead_frames = 0;

    int8_t dir_i8 = static_cast<int8_t>(direction);
    if (state.last_direction == 0) {
        // First non-zero frame — start counting
        state.last_direction = dir_i8;
        state.boost_frames   = 1;
    } else if (state.last_direction == dir_i8) {
        // Same direction — continue ramping
        state.boost_frames = SatAddU32(state.boost_frames, 1);
    } else {
        // Direction reversed — restart counter
        state.last_direction = dir_i8;
        state.boost_frames   = 1;
    }
}

int32_t BoostFactorX100(const StickState& state) {
    if (state.boost_frames < kBoostThresholdFrames) {
        return kBoostMinX100;
    }
    uint32_t ramp_frames = state.boost_frames - kBoostThresholdFrames;
    if (ramp_frames >= kBoostRampFrames) {
        return kBoostMaxX100;
    }
    // Linear ramp from kBoostMinX100 to kBoostMaxX100 over kBoostRampFrames
    int32_t span = kBoostMaxX100 - kBoostMinX100;
    return kBoostMinX100
           + static_cast<int32_t>(
               static_cast<uint32_t>(span) * ramp_frames / kBoostRampFrames);
}

// ── Primary output ────────────────────────────────────────────────────────────

int32_t ComputeSpeed(int32_t deflection, StickState& state, bool slow_mode_active) {
    int32_t direction = Sign(deflection);
    // uint32_t magnitude keeps sign information out of the zone table
    int32_t magnitude = static_cast<int32_t>(
        static_cast<uint32_t>(deflection < 0 ? -deflection : deflection));

    UpdateBoostState(state, direction, magnitude);

    int32_t raw_x100 = ZoneSpeedX100(magnitude);
    if (raw_x100 == 0) return 0;

    int32_t boost_x100   = BoostFactorX100(state);
    int32_t boosted_x100 = MulX100(raw_x100, boost_x100);

    int32_t final_x100 = slow_mode_active
                         ? MulX100(boosted_x100, kSlowModeX100)
                         : boosted_x100;

    // Convert ×100 → integer px/frame; precision band rounds 0.5 up to 1
    int32_t abs_speed;
    if (final_x100 >= 100) {
        abs_speed = final_x100 / 100;
    } else if (final_x100 > 0) {
        abs_speed = 1;
    } else {
        abs_speed = 0;
    }

    return (direction < 0) ? -abs_speed : abs_speed;
}

// ── Snap gravity ──────────────────────────────────────────────────────────────

bool ShouldApplySnap(int32_t cursor_speed_x, int32_t cursor_speed_y, SnapMode mode) {
    if (mode != SnapMode::On) return false;

    int32_t abs_x = cursor_speed_x < 0 ? -cursor_speed_x : cursor_speed_x;
    int32_t abs_y = cursor_speed_y < 0 ? -cursor_speed_y : cursor_speed_y;

    // Any axis exceeding override threshold kills snap this frame
    if (abs_x > kSnapOverridePx || abs_y > kSnapOverridePx) return false;

    // Both axes must be within the slow-engagement threshold
    return (abs_x <= kSnapMaxSpeedPx && abs_y <= kSnapMaxSpeedPx);
}

void SnapPull(int32_t cursor_x, int32_t cursor_y,
              int32_t target_x, int32_t target_y,
              int32_t& out_dx, int32_t& out_dy) {
    int32_t dx = target_x - cursor_x;
    int32_t dy = target_y - cursor_y;

    // Use integer distance² to avoid sqrt — compare against radius²
    // kSnapRadiusPx = 8, radius² = 64; int32_t is safe for ±1280px coords
    int64_t dist_sq = static_cast<int64_t>(dx) * dx
                    + static_cast<int64_t>(dy) * dy;
    if (dist_sq > static_cast<int64_t>(kSnapRadiusPx) * kSnapRadiusPx) {
        out_dx = 0;
        out_dy = 0;
        return;
    }

    int32_t pull_x = Sign(dx) * kSnapPullPx;
    int32_t pull_y = Sign(dy) * kSnapPullPx;

    // Clamp: don't overshoot the remaining distance
    int32_t abs_dx = dx < 0 ? -dx : dx;
    int32_t abs_dy = dy < 0 ? -dy : dy;
    out_dx = (abs_dx < kSnapPullPx) ? dx : pull_x;
    out_dy = (abs_dy < kSnapPullPx) ? dy : pull_y;
}

} // namespace ul::menu::qos
