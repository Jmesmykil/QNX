// qd_Multitouch.cpp — Multi-touch gesture detection for uMenu C++ SP3 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/multitouch.rs.
//
// SP3-F01: tagged union Gesture — no std::variant/std::visit.
// SP3-F02: SCROLL_NOISE_FLOOR_PX = 3, PINCH_NOISE_FLOOR_PX = 3.
// SP3-F03: isqrt uses Newton's method loop — no sqrtf, no float.
// AB-09:   No std::variant / std::visit.
// AB-11:   abs(INT32_MIN) guard applied before abs() calls.
// AB-12:   No 1280 / 720 literals — screen dims come from qd_WmConstants.hpp.
#include <ul/menu/qdesktop/qd_Multitouch.hpp>
#include <cstdlib>   // abs

namespace ul::menu::qdesktop {

// ── isqrt — Newton's method, no float ────────────────────────────────────────

uint32_t isqrt(uint32_t n) {
    if (n < 2u) {
        return n;
    }
    uint32_t x = n;
    uint32_t y = (x + 1u) / 2u;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2u;
    }
    return x;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

/// Integer Euclidean distance between two points. Result fits in int32_t for
/// any pair of screen coordinates (max diagonal ~2203 px on a 1920×1080 screen).
static int32_t euclid_dist(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    const int32_t dx = ax - bx;
    const int32_t dy = ay - by;
    // Widen before squaring to avoid int32 overflow on large distances.
    const int64_t sq = (int64_t)dx * dx + (int64_t)dy * dy;
    // sq fits in uint32_t for screen coordinates (max ~4.8M).
    return (int32_t)isqrt((uint32_t)sq);
}

/// L∞ norm — returns whichever component has the larger absolute value.
/// AB-11: guard abs(INT32_MIN) before calling abs().
static int32_t abs_max(int32_t a, int32_t b) {
    // Guard INT32_MIN per AB-11.
    const int32_t aa = (a == INT32_MIN) ? INT32_MAX : (a < 0 ? -a : a);
    const int32_t ba = (b == INT32_MIN) ? INT32_MAX : (b < 0 ? -b : b);
    return (aa > ba) ? a : b;
}

/// Absolute value of int32_t with INT32_MIN guard (AB-11).
static int32_t safe_abs(int32_t v) {
    if (v == INT32_MIN) { return INT32_MAX; }
    return (v < 0) ? -v : v;
}

// ── multitouch_classify ───────────────────────────────────────────────────────

Gesture multitouch_classify(MultiTouchFrame frame, MultiTouchState &state) {
    if (frame.count < 2) {
        // Lost contact — reset state so next two-finger touch starts fresh.
        state.had_two_last_frame = false;
        return gesture_none();
    }

    const int32_t dist = euclid_dist(frame.p0_x, frame.p0_y, frame.p1_x, frame.p1_y);
    const int32_t centroid_x = (frame.p0_x + frame.p1_x) / 2;
    const int32_t centroid_y = (frame.p0_y + frame.p1_y) / 2;

    if (!state.had_two_last_frame) {
        // First frame of two-finger contact — record state but emit nothing.
        state.had_two_last_frame = true;
        state.last_dist          = dist;
        state.last_centroid_x    = centroid_x;
        state.last_centroid_y    = centroid_y;
        return gesture_none();
    }

    const int32_t dist_delta  = dist - state.last_dist;
    const int32_t dx          = centroid_x - state.last_centroid_x;
    const int32_t dy          = centroid_y - state.last_centroid_y;
    const int32_t centroid_mag = abs_max(dx, dy);  // L∞ norm

    // Advance persistent state for the next frame.
    state.last_dist       = dist;
    state.last_centroid_x = centroid_x;
    state.last_centroid_y = centroid_y;

    // Classify: scroll wins when centroid moved more than distance changed AND
    // the motion exceeds the noise floor. Otherwise try pinch. Otherwise None.
    if (safe_abs(centroid_mag) > safe_abs(dist_delta) &&
        safe_abs(centroid_mag) > SCROLL_NOISE_FLOOR_PX)
    {
        return gesture_scroll(dx, dy);
    }
    if (safe_abs(dist_delta) > PINCH_NOISE_FLOOR_PX) {
        return gesture_pinch(dist_delta);
    }
    return gesture_none();
}

} // namespace ul::menu::qdesktop
