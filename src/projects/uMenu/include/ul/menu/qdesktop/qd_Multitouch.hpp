// qd_Multitouch.hpp — Multi-touch gesture detection for uMenu C++ SP3 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/multitouch.rs.
//
// SP3-F01: std::variant + std::visit BANNED (-fno-exceptions). Use explicit
//          tagged union pattern throughout.
// SP3-F02: SCROLL_NOISE_FLOOR_PX = 3, PINCH_NOISE_FLOOR_PX = 3 (×1.5 remap).
// SP3-F03: isqrt uses Newton's method — no sqrtf.
// AB-09:   No std::variant / std::visit anywhere.
// AB-12:   No 1280 or 720 literals; use SCREEN_W / SCREEN_H from qd_WmConstants.
#pragma once
#include <pu/Plutonium>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Noise floors (×1.5 remapped from Rust 2 → C++ 3) ─────────────────────────

/// If the centroid moved less than this many px, no scroll gesture is emitted.
static constexpr int32_t SCROLL_NOISE_FLOOR_PX = 3;  // Rust: 2

/// If the inter-finger distance changed less than this many px, no pinch is emitted.
static constexpr int32_t PINCH_NOISE_FLOOR_PX  = 3;  // Rust: 2

// ── isqrt — integer square root, Newton's method, no float ───────────────────

/// Integer square root of n (floor). Mirrors multitouch.rs isqrt exactly.
/// SP3-F03: no sqrtf — Newton's iteration only.
uint32_t isqrt(uint32_t n);

// ── One frame of touch data fed by the WM each tick ──────────────────────────

struct MultiTouchFrame {
    uint8_t count;          ///< Total fingers on screen this frame (0..N).
    int32_t p0_x, p0_y;    ///< First finger position.
    int32_t p1_x, p1_y;    ///< Second finger position (valid when count >= 2).
};

// ── Persistent state — caller stores one in UiState ──────────────────────────

struct MultiTouchState {
    bool    had_two_last_frame;     ///< True iff last frame had >= 2 fingers.
    int32_t last_dist;              ///< Inter-finger distance last frame (integer px).
    int32_t last_centroid_x;        ///< Centroid X last frame.
    int32_t last_centroid_y;        ///< Centroid Y last frame.
};

/// Zero-initialise helper.
inline MultiTouchState multitouch_state_zero() {
    MultiTouchState s;
    s.had_two_last_frame = false;
    s.last_dist          = 0;
    s.last_centroid_x    = 0;
    s.last_centroid_y    = 0;
    return s;
}

// ── Gesture — SP3-F01 tagged union, no std::variant ──────────────────────────

/// Classification of the current multi-touch frame.
/// SP3-F01: explicit tagged union — std::variant / std::visit are banned.
struct Gesture {
    enum class Kind : uint8_t {
        None,           ///< < 2 fingers, or first frame of new two-finger touch.
        Pinch,          ///< Two fingers, distance changing.
        TwoFingerScroll ///< Two fingers, centroid translating.
    } kind;

    union {
        struct { int32_t delta_dist; } pinch;   ///< valid when kind == Pinch
        struct { int32_t dx, dy;     } scroll;  ///< valid when kind == TwoFingerScroll
    } data;
};

// Convenience constructors (mirrors Rust enum variants).
inline Gesture gesture_none() {
    Gesture g;
    g.kind           = Gesture::Kind::None;
    g.data.pinch     = { 0 };
    return g;
}

inline Gesture gesture_pinch(int32_t delta_dist) {
    Gesture g;
    g.kind               = Gesture::Kind::Pinch;
    g.data.pinch.delta_dist = delta_dist;
    return g;
}

inline Gesture gesture_scroll(int32_t dx, int32_t dy) {
    Gesture g;
    g.kind           = Gesture::Kind::TwoFingerScroll;
    g.data.scroll.dx = dx;
    g.data.scroll.dy = dy;
    return g;
}

// ── Public API ────────────────────────────────────────────────────────────────

/// Classify one frame of multi-touch data.
/// state is updated in place for use in the next frame.
/// Mirrors multitouch.rs classify exactly (including noise-floor logic).
Gesture multitouch_classify(MultiTouchFrame frame, MultiTouchState &state);

} // namespace ul::menu::qdesktop
