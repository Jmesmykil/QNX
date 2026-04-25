// qd_Curve.hpp — Five-zone dynamic mouse-acceleration curve for uMenu qdesktop.
// Ported from tools/mock-nro-desktop-gui/src/curve.rs (v0.5.0).
//
// Zone layout (raw HID deflection units; hardware values, never remapped):
//   1. dead-zone       [0,      4500)   → 0 px/frame
//   2. precision-band  [4500,  12000)   → 0.5 – 2.0 px/frame  (linear)
//   3. linear-band     [12000, 22000)   → 2.0 – 8.0 px/frame  (linear)
//   4. acceleration    [22000, 30000)   → 8.0 – 24.0 px/frame (quadratic ramp)
//   5. burst-band      [30000, 32767]   → 24.0 – 40.0 px/frame (linear cap)
//
// All arithmetic is fixed-point ×100 integer (no floats). Per-axis state is
// owned by the caller (one StickState per axis). Slow-mode and snap-gravity
// helpers are pure functions; the caller drives them.
//
// Snap pixel constants are scaled ×1.5 from the Rust 1280×720 reference:
//   CURVE_SNAP_RADIUS_PX    8 → 12
//   CURVE_SNAP_MAX_SPEED_PX 3 → 5  (4.5 rounded up)
//   CURVE_SNAP_OVERRIDE_PX  6 → 9
//   CURVE_SNAP_PULL_PX      1 → 1  (sub-rounding threshold)
//
// HID range / frame-count / boost constants are NOT remapped.
#pragma once
#include <pu/Plutonium>
#include <cstdint>
#include <climits>

namespace ul::menu::qdesktop {

// ── Per-axis persistent state ─────────────────────────────────────────────────

// Per-axis persistent state for the boost integrator.
// Caller owns one StickState per axis (X and Y), passes it back each frame.
struct StickState {
    int8_t  last_direction;  // -1 / 0 / +1
    uint32_t boost_frames;
    uint32_t dead_frames;
};

inline StickState stick_state_zero() {
    return StickState{ 0, 0u, 0u };
}

// ── Snap-gravity toggle ───────────────────────────────────────────────────────

enum class SnapMode : uint8_t { Off = 0, On = 1, Suppressed = 2 };

// ── Zone boundary constants (raw HID units) ───────────────────────────────────

static constexpr int32_t CURVE_DEADZONE       = 4500;
static constexpr int32_t CURVE_PRECISION_END  = 12000;
static constexpr int32_t CURVE_LINEAR_END     = 22000;
static constexpr int32_t CURVE_ACCEL_END      = 30000;
static constexpr int32_t CURVE_BURST_END      = 32767;

// ── Adaptive boost constants ──────────────────────────────────────────────────

static constexpr uint32_t CURVE_BOOST_THRESHOLD_FRAMES = 12u;
static constexpr uint32_t CURVE_BOOST_RAMP_FRAMES      = 24u;
static constexpr uint32_t CURVE_BOOST_RESET_FRAMES     = 3u;
static constexpr int32_t  CURVE_BOOST_MIN_X100         = 100;  // 1.0
static constexpr int32_t  CURVE_BOOST_MAX_X100         = 150;  // 1.5

// ── Slow-mode multiplier ──────────────────────────────────────────────────────

static constexpr int32_t CURVE_SLOW_MODE_X100 = 40;  // 0.4

// ── Snap gravity constants (×1.5 remapped for 1920×1080) ─────────────────────

static constexpr int32_t CURVE_SNAP_RADIUS_PX    = 12;  // Rust: 8
static constexpr int32_t CURVE_SNAP_MAX_SPEED_PX = 5;   // Rust: 3 (×1.5 = 4.5 → 5)
static constexpr int32_t CURVE_SNAP_OVERRIDE_PX  = 9;   // Rust: 6
static constexpr int32_t CURVE_SNAP_PULL_PX      = 1;   // Rust: 1 (below rounding threshold)

// ── Public API ────────────────────────────────────────────────────────────────

// Core curve: deflection (signed ±32767) → signed pixel delta this frame.
// Updates `state` in place (boost/dead frame counters, direction tracking).
// Apply slow_mode_active == true when ZR is held.
int32_t ComputeStickSpeed(int32_t deflection, StickState &state, bool slow_mode_active);

// Optional inspector: returns the current boost factor ×100 given state.
// Used by telemetry HUD.
int32_t BoostFactorX100(const StickState &state);

// Snap helpers (SP3.4 wiring; exposed now, not called from OnMenuUpdate yet).

// Returns true if snap gravity should engage this frame.
// cursor_speed_x / cursor_speed_y: signed raw curve output (px/frame) BEFORE snap.
// mode: caller-owned SnapMode.
bool ShouldApplySnap(int32_t cursor_speed_x, int32_t cursor_speed_y, SnapMode mode);

// Pull strength toward target from cursor, per axis. Returns (out_dx, out_dy)
// in pixels — at most CURVE_SNAP_PULL_PX per axis. Returns (0, 0) when the
// cursor is outside CURVE_SNAP_RADIUS_PX.
void SnapPull(int32_t cursor_x, int32_t cursor_y,
              int32_t target_x, int32_t target_y,
              int32_t &out_dx, int32_t &out_dy);

} // namespace ul::menu::qdesktop
