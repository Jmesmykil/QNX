// qd_HotCornerRightOverlay.hpp — Plutonium Element that paints the top-right
// hot-corner visual marker (cyan accent borders) on top of any layout.
//
// Mirrors qd_HotCornerOverlay.hpp (the top-LEFT widget), but for the top-right
// 96×72 zone at (SCREEN_W − LP_HOTCORNER_W, 0).  Differences:
//   • No dark background fill — Plutonium's system status icons (battery,
//     time, network, volume) live in the top bar at this position; a solid
//     fill would obscure them.
//   • No centre Q-glyph for the same reason — the status icons are the
//     "glyph" the user sees on the right side.
//   • Accent borders mirror the left widget (left edge + bottom edge here,
//     vs right edge + bottom edge on the left widget).
//
// Suppression:
//   The Launchpad search-bar focus suppresses the LEFT overlay.  The right
//   overlay does NOT need that suppression (the right hot zone never goes
//   under the search bar geometry), so SetSearchActiveRef is omitted.
//
// Usage:
//   Add this element LAST to any user-facing layout so it renders above all
//   other elements (Plutonium renders in insertion order; last = highest
//   Z-order).  Pair with QdHotCornerOverlay (the LEFT one) — both should be
//   added together for symmetric chrome.
#pragma once

#ifdef QDESKTOP_MODE

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>

namespace ul::menu::qdesktop {

class QdHotCornerRightOverlay : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdHotCornerRightOverlay>;

    static Ref New() {
        return std::make_shared<QdHotCornerRightOverlay>();
    }

    QdHotCornerRightOverlay() = default;
    ~QdHotCornerRightOverlay() = default;

    // ── Element interface ─────────────────────────────────────────────────────
    // Position covers full screen so Plutonium's hit-test and clip logic don't
    // clip our right-corner paint.  The widget paint is only at the top-right
    // 96×72 zone.
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override {
        (void)keys_down; (void)keys_up; (void)keys_held; (void)touch_pos;
    }
};

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
