// qd_Anim.cpp — Animation engine implementation for uMenu C++ SP2 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/anim.rs.
// No Plutonium/SDL dependencies — pure arithmetic only.

#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <algorithm> // std::min, std::max

namespace ul::menu::qdesktop {

// ── easing_apply_x100 ─────────────────────────────────────────────────────────
// Mirrors anim.rs Easing::apply_x100 exactly.
// t is in [0,100].

int32_t easing_apply_x100(Easing e, int32_t t) {
    switch (e) {
        case Easing::Linear:
            return t;

        case Easing::EaseOut: {
            // Rust: 100 - (inv * inv) / 100
            // inv = 100 - t
            const int32_t inv = 100 - t;
            return 100 - (inv * inv) / 100;
        }

        case Easing::EaseIn: {
            // Rust: (t * t) / 100
            return (t * t) / 100;
        }

        case Easing::Bounce: {
            // Rust triangular: peaks at 15 over [0,100].
            // First half [0,50]: rises 0→15; second half [50,100]: falls 15→0.
            if (t <= 50) {
                return (t * 15) / 50;
            } else {
                return ((100 - t) * 15) / 50;
            }
        }
    }
    // Unreachable — satisfy compiler.
    return t;
}

// ── Animation::value_at ───────────────────────────────────────────────────────

int32_t Animation::value_at(int32_t now_tick) const {
    // Before animation starts: 0.
    if (now_tick <= start_tick) {
        return 0;
    }

    // SP2-F02: cast elapsed to int32_t before multiplying.
    const int32_t elapsed = static_cast<int32_t>(now_tick - start_tick);

    if (elapsed >= duration) {
        // After end: Bounce returns to 0; all others clamp to 100.
        return (easing == Easing::Bounce) ? 0 : 100;
    }

    // Progress in [0,100].
    // SP2-F02: (elapsed * 100) / duration — elapsed is already int32_t above.
    const int32_t progress = (elapsed * 100) / duration;
    return easing_apply_x100(easing, progress);
}

// ── Animation::is_done ────────────────────────────────────────────────────────

bool Animation::is_done(int32_t now_tick) const {
    const int32_t elapsed = static_cast<int32_t>(now_tick - start_tick);
    return elapsed >= duration;
}

// ── Animation::scale_x100 ────────────────────────────────────────────────────

int32_t Animation::scale_x100(int32_t now_tick) const {
    switch (kind) {
        case AnimKind::WindowOpen: {
            // 100 + value_at * 5 / 100  → [100, 105]
            const int32_t v = value_at(now_tick);
            return 100 + v * 5 / 100;
        }
        case AnimKind::DockBounce: {
            // 100 + value_at → [100, 115]
            return 100 + value_at(now_tick);
        }
        case AnimKind::WindowClose:
        case AnimKind::WindowSnap:
            return 100;
    }
    return 100;
}

// ── Animation::alpha_x100 ────────────────────────────────────────────────────

int32_t Animation::alpha_x100(int32_t now_tick) const {
    const int32_t v = value_at(now_tick);
    switch (kind) {
        case AnimKind::WindowOpen:
            return v;
        case AnimKind::WindowClose:
        case AnimKind::WindowSnap:
            return 100 - v;
        case AnimKind::DockBounce:
            return 100;
    }
    return 100;
}

} // namespace ul::menu::qdesktop
