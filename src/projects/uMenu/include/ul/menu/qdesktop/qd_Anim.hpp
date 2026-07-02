// qd_Anim.hpp — Animation engine for uMenu C++ SP2 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/anim.rs.
// SP2-F01: ms_to_ticks is constexpr.
// SP2-F02: elapsed cast to int32_t in value_at.
// SP2-F03: plain enum class : uint8_t, no std::variant.
#pragma once
#include <pu/Plutonium>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Frame rate ────────────────────────────────────────────────────────────────

static constexpr int32_t ANIM_FPS = 60;

// SP2-F01: constexpr conversion — identical to anim.rs ms_to_ticks.
// Rounds up: (ms * FPS + 999) / 1000.
static constexpr int32_t ms_to_ticks(int32_t ms) {
    return (ms * ANIM_FPS + 999) / 1000;
}

// ── Duration constants (computed at compile time) ─────────────────────────────

static constexpr int32_t WINDOW_OPEN_MS       = 200;
static constexpr int32_t WINDOW_CLOSE_MS      = 150;
static constexpr int32_t DOCK_BOUNCE_MS       = 80;
static constexpr int32_t WINDOW_SNAP_MS       = 100;

static constexpr int32_t WINDOW_OPEN_TICKS    = ms_to_ticks(WINDOW_OPEN_MS);   // 12
static constexpr int32_t WINDOW_CLOSE_TICKS   = ms_to_ticks(WINDOW_CLOSE_MS);  // 9
static constexpr int32_t DOCK_BOUNCE_TICKS    = ms_to_ticks(DOCK_BOUNCE_MS);   // 5
static constexpr int32_t WINDOW_SNAP_TICKS    = ms_to_ticks(WINDOW_SNAP_MS);   // 6

// ── Easing ────────────────────────────────────────────────────────────────────

// SP2-F03: plain enum class : uint8_t, no associated data.
enum class Easing : uint8_t {
    Linear,
    EaseOut,
    EaseIn,
    Bounce,
};

// Apply easing to a [0,100] progress value, returning [0,100].
// Mirrors anim.rs Easing::apply_x100 exactly.
int32_t easing_apply_x100(Easing e, int32_t t);

// ── Animation kind ────────────────────────────────────────────────────────────

// SP2-F03: plain enum class : uint8_t.
enum class AnimKind : uint8_t {
    WindowOpen,
    WindowClose,
    DockBounce,
    WindowSnap,
};

// ── Animation ─────────────────────────────────────────────────────────────────

struct Animation {
    AnimKind kind;
    Easing   easing;
    int32_t  start_tick;   // absolute tick when the animation started
    int32_t  duration;     // duration in ticks

    // ── Factory constructors ──────────────────────────────────────────────────

    static Animation window_open(int32_t start_tick_) {
        return { AnimKind::WindowOpen, Easing::EaseOut, start_tick_, WINDOW_OPEN_TICKS };
    }
    static Animation window_close(int32_t start_tick_) {
        return { AnimKind::WindowClose, Easing::EaseIn, start_tick_, WINDOW_CLOSE_TICKS };
    }
    static Animation dock_bounce(int32_t start_tick_) {
        return { AnimKind::DockBounce, Easing::Bounce, start_tick_, DOCK_BOUNCE_TICKS };
    }
    static Animation window_snap(int32_t start_tick_) {
        return { AnimKind::WindowSnap, Easing::EaseOut, start_tick_, WINDOW_SNAP_TICKS };
    }

    // ── Query ─────────────────────────────────────────────────────────────────

    // Returns true when the animation has fully completed.
    bool is_done(int32_t now_tick) const;

    // SP2-F02: Returns progress [0,100] at now_tick.
    // Returns 0 before start, 100 (or 0 for Bounce) after end.
    // Cast elapsed to int32_t before multiplying per SP2-F02.
    int32_t value_at(int32_t now_tick) const;

    // Returns the scale factor ×100 for this animation kind at now_tick.
    // WindowOpen: 100 + value*5/100 (→ [100,105])
    // DockBounce: 100 + value        (→ [100,115])
    // WindowClose/WindowSnap: 100 (no scale change)
    int32_t scale_x100(int32_t now_tick) const;

    // Returns the alpha value ×100 for this animation kind at now_tick.
    // WindowOpen:  value_at   (0→100)
    // WindowClose: 100-value  (100→0)
    // WindowSnap:  100-value  (100→0)
    // DockBounce:  100        (always fully opaque)
    int32_t alpha_x100(int32_t now_tick) const;
};

} // namespace ul::menu::qdesktop
