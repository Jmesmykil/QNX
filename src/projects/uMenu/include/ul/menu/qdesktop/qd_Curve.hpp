// qd_Curve.hpp — HID stick curve + snap engine for uMenu C++ SP2 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/curve.rs.
//
// SP2-F07: i64 widening for accel zone interpolation (see qd_Curve.cpp).
// SP2-F08: safe_abs_i32 guards abs(INT32_MIN) UB.
// SP2-F09: sat_add_u32 — no built-in saturating add in C++.
// SP2-F10: SnapMode as plain enum class : uint8_t.
// AB-10:  All (diff * 100) numerators widen to int64_t before division.
// AB-11:  INT32_MIN.unsigned_abs() → safe_abs_i32 returns 0x80000000U as i32 max.
// CC-02:  sat_add_u32 explicit helper.
//
// Pixel constants (×1.5 from Rust 1280×720 → C++ 1920×1080):
//   SNAP_RADIUS_PX    8  → 12
//   SNAP_MAX_SPEED_PX 3  → 5  (4.5 rounded up)
//   SNAP_OVERRIDE_PX  6  → 9
//   SNAP_PULL_PX      1  → 1  (below rounding threshold — stays 1)
//
// HID range/frame/count constants are NOT remapped (hardware-reported values).
#pragma once
#include <pu/Plutonium>
#include <cstdint>
#include <climits>   // INT32_MIN, UINT32_MAX

namespace ul::menu::qdesktop {

// ── Saturating arithmetic helpers ─────────────────────────────────────────────

// SP2-F09 / CC-02: saturating unsigned add (no builtin in C++).
inline uint32_t sat_add_u32(uint32_t a, uint32_t b) {
    const uint32_t result = a + b;
    return (result < a) ? UINT32_MAX : result;
}

// SP2-F08 / AB-11: safe abs for int32_t — guards abs(INT32_MIN) UB.
// Returns INT32_MAX (0x7fffffff) for INT32_MIN; otherwise standard abs.
inline int32_t safe_abs_i32(int32_t v) {
    if (v == INT32_MIN) {
        return INT32_MAX;
    }
    return (v < 0) ? -v : v;
}

// ── HID zone boundary constants (NOT remapped — hardware values) ──────────────

static constexpr int32_t HID_DEADZONE       = 4500;
static constexpr int32_t HID_PRECISION_END  = 12000;
static constexpr int32_t HID_LINEAR_END     = 22000;
static constexpr int32_t HID_ACCEL_END      = 30000;
static constexpr int32_t HID_BURST_END      = 32767;

// ── Speed constants ×100 (NOT remapped — HID-derived) ────────────────────────

static constexpr int32_t SPEED_PRECISION_MIN_X100 = 50;
static constexpr int32_t SPEED_PRECISION_MAX_X100 = 200;
static constexpr int32_t SPEED_LINEAR_MIN_X100    = 200;
static constexpr int32_t SPEED_LINEAR_MAX_X100    = 800;
static constexpr int32_t SPEED_ACCEL_MIN_X100     = 800;
static constexpr int32_t SPEED_ACCEL_MAX_X100     = 2400;
static constexpr int32_t SPEED_BURST_MIN_X100     = 2400;
static constexpr int32_t SPEED_BURST_MAX_X100     = 4000;

// ── Boost constants (NOT remapped) ───────────────────────────────────────────

static constexpr uint32_t BOOST_THRESHOLD_FRAMES = 12;
static constexpr uint32_t BOOST_RAMP_FRAMES      = 24;
static constexpr uint32_t BOOST_RESET_FRAMES     = 3;
static constexpr int32_t  BOOST_MIN_X100         = 100;
static constexpr int32_t  BOOST_MAX_X100         = 150;

// ── Slow mode multiplier (NOT remapped) ──────────────────────────────────────

static constexpr int32_t SLOW_MODE_X100 = 40;

// ── Snap pixel constants (×1.5 remapped) ─────────────────────────────────────

static constexpr int32_t SNAP_RADIUS_PX    = 12;   // Rust: 8
static constexpr int32_t SNAP_MAX_SPEED_PX = 5;    // Rust: 3 (4.5 → 5 round up)
static constexpr int32_t SNAP_OVERRIDE_PX  = 9;    // Rust: 6
static constexpr int32_t SNAP_PULL_PX      = 1;    // Rust: 1 (below threshold)

// ── SnapMode ─────────────────────────────────────────────────────────────────

// SP2-F10: plain enum class : uint8_t.
enum class SnapMode : uint8_t {
    On,
    Off,
    Suppressed,
};

// Returns true only when mode == SnapMode::On.
inline bool snap_mode_is_active(SnapMode mode) {
    return mode == SnapMode::On;
}

// ── CurveState ───────────────────────────────────────────────────────────────
// Per-axis persistent state (≡ StickState in curve.rs).

struct CurveState {
    int8_t   last_direction;  // -1, 0, or +1
    uint32_t boost_frames;    // consecutive frames in boost zone
    uint32_t dead_frames;     // consecutive frames in dead zone

    // Zero-initialise to dead / no-boost state.
    static CurveState zero() {
        return { 0, 0u, 0u };
    }
};

// ── CurveResult ──────────────────────────────────────────────────────────────
// SP2-F11: tuple return (i32, i32) → struct.

struct CurveResult {
    int32_t speed_x;  // signed pixel speed (negative = left/up)
    int32_t speed_y;
};

// ── Public API ───────────────────────────────────────────────────────────────

// Compute signed pixel speed for one axis from a raw HID deflection.
// deflection: raw i16 joystick value in [-32767, 32767].
// state: updated in place (boost/dead frame counters, direction tracking).
// slow_mode: if true, multiply final speed by SLOW_MODE_X100/100.
// Returns signed pixel speed (integer, never fractional).
int32_t compute_speed_axis(int32_t deflection, CurveState &state, bool slow_mode);

// Compute two-axis cursor speed from raw HID deflections.
// Convenience wrapper calling compute_speed_axis for each axis.
CurveResult apply_curve(int32_t deflection_x, int32_t deflection_y,
                        CurveState &state_x, CurveState &state_y,
                        bool slow_mode);

// Compute the snap pull force toward a target pixel.
// cursor_pos: current cursor position on the relevant axis.
// target_pos: snap target pixel.
// Returns a signed pull value, 0 if outside snap radius.
int32_t snap_pull(int32_t cursor_pos, int32_t target_pos);

// Returns true if snap should override normal cursor movement this frame.
// speed_abs: abs(cursor speed) on this axis.
// dist: distance to nearest snap candidate on this axis.
// mode: current SnapMode.
bool should_apply_snap(int32_t speed_abs, int32_t dist, SnapMode mode);

} // namespace ul::menu::qdesktop
