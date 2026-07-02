// qos_CurveEngine.hpp — Five-zone hybrid stick-acceleration curve with adaptive boost.
//
// Ported from Rust: mock-nro-desktop-gui/src/curve.rs (v0.5.0)
// Port date: 2026-04-23
// Source SHA: see STATE.toml in qos-ulaunch-fork
//
// PURE HEADER — no allocator use, no global state, no HID dependency.
// Caller owns one StickState per axis (X and Y), passes it into ComputeSpeed
// each frame. Results are signed px/frame as integer.
//
// Zone table (raw HID deflection units → px/frame ×100 fixed-point):
//   1. dead-zone       [0,     4500)   → 0
//   2. precision-band  [4500,  12000)  → 0.50 – 2.00  (linear)
//   3. linear-band     [12000, 22000)  → 2.00 – 8.00  (linear)
//   4. accel-band      [22000, 30000)  → 8.00 – 24.0  (quadratic)
//   5. burst-band      [30000, 32767]  → 24.0 – 40.0  (linear)
//
// Compile with -fno-exceptions -fno-rtti (libnx constraint).
//
// No __cxa_throw, no RTTI, integer-only hot path.

#pragma once

#include <cstdint>

namespace ul::menu::qos {

// ── Zone boundaries ──────────────────────────────────────────────────────────

static constexpr int32_t kDeadzone       = 4500;
static constexpr int32_t kPrecisionEnd   = 12000;
static constexpr int32_t kLinearEnd      = 22000;
static constexpr int32_t kAccelEnd       = 30000;
static constexpr int32_t kBurstEnd       = 32767;

// ── Zone speed constants (px/frame ×100) ─────────────────────────────────────

static constexpr int32_t kPrecisionMinX100 = 50;    // 0.50 px/frame
static constexpr int32_t kPrecisionMaxX100 = 200;   // 2.00 px/frame
static constexpr int32_t kLinearMinX100    = 200;   // 2.00 px/frame
static constexpr int32_t kLinearMaxX100    = 800;   // 8.00 px/frame
static constexpr int32_t kAccelMinX100     = 800;   // 8.00 px/frame
static constexpr int32_t kAccelMaxX100     = 2400;  // 24.0 px/frame
static constexpr int32_t kBurstMinX100     = 2400;  // 24.0 px/frame
static constexpr int32_t kBurstMaxX100     = 4000;  // 40.0 px/frame

// ── Adaptive boost ────────────────────────────────────────────────────────────

static constexpr uint32_t kBoostThresholdFrames = 12;
static constexpr uint32_t kBoostRampFrames      = 24;
static constexpr uint32_t kBoostResetFrames     = 3;
static constexpr int32_t  kBoostMinX100         = 100; // 1.0×
static constexpr int32_t  kBoostMaxX100         = 150; // 1.5×

// ── Slow mode ─────────────────────────────────────────────────────────────────

static constexpr int32_t kSlowModeX100 = 40; // 0.4× while ZR held

// ── Snap gravity ─────────────────────────────────────────────────────────────

static constexpr int32_t kSnapRadiusPx    = 8;
static constexpr int32_t kSnapMaxSpeedPx  = 3;
static constexpr int32_t kSnapOverridePx  = 6;
static constexpr int32_t kSnapPullPx      = 1;

// ── Per-axis state ────────────────────────────────────────────────────────────

/// Persistent state for the boost integrator, one instance per axis.
/// Caller is responsible for zero-initialising before first use.
struct StickState {
    int8_t  last_direction; ///< -1, 0, or +1. Direction reversal resets boost.
    uint32_t boost_frames;  ///< Frames the same non-zero direction has been held.
    uint32_t dead_frames;   ///< Consecutive dead-zone frames.

    constexpr StickState() : last_direction(0), boost_frames(0), dead_frames(0) {}
};

// ── Snap mode toggle ──────────────────────────────────────────────────────────

enum class SnapMode : uint8_t {
    On,         ///< Default: pull cursor toward nearby targets at low speed.
    Off,        ///< No gravity.
    Suppressed, ///< Disabled for this frame (e.g. modifier key held).
};

// ── Public API ────────────────────────────────────────────────────────────────

/// Map raw stick deflection (signed ±32767) to signed px/frame.
/// Updates `state` in place. `slow_mode_active` = ZR held.
int32_t ComputeSpeed(int32_t deflection, StickState& state, bool slow_mode_active);

/// Read-only accessor for the current boost multiplier (×100).
int32_t BoostFactorX100(const StickState& state);

/// Decide whether snap gravity engages this frame.
bool ShouldApplySnap(int32_t cursor_speed_x, int32_t cursor_speed_y, SnapMode mode);

/// Per-axis pull toward `target` — returns (dx, dy) clamped to ±kSnapPullPx.
/// Returns (0, 0) when the cursor is outside kSnapRadiusPx.
void SnapPull(int32_t cursor_x, int32_t cursor_y,
              int32_t target_x, int32_t target_y,
              int32_t& out_dx, int32_t& out_dy);

// ── Internal helpers (declared here for unit testing) ────────────────────────

/// Zone lookup: magnitude → px/frame ×100. Internal, exposed for tests.
int32_t ZoneSpeedX100(int32_t magnitude);

/// Boost integrator update. Internal, exposed for tests.
void UpdateBoostState(StickState& state, int32_t direction, int32_t magnitude);

} // namespace ul::menu::qos
