// qd_HotCornerOverlay.hpp — Plutonium Element that paints the hot-corner widget
// (96x72 dark rect + cyan accent borders + Q-glyph) on top of any layout.
//
// Usage:
//   Add this element LAST to any user-facing layout so it renders above all
//   other elements (Plutonium renders in insertion order; last = highest Z-order).
//
// Suppression (Launchpad search):
//   Call SetSearchActiveRef(&bool_var) to pass a pointer to a bool that is true
//   when the Launchpad search bar has focus.  When the pointer is non-null and
//   the referenced bool is true, OnRender is a no-op.  Pass nullptr (default)
//   for layouts that have no search bar.
//
// Geometry SSOT:
//   LP_HOTCORNER_W = 96, LP_HOTCORNER_H = 72 — defined in qd_Launchpad.hpp.
//   Widget always drawn at (0, 0), which is the top-left corner of the screen.
#pragma once

#ifdef QDESKTOP_MODE

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>

namespace ul::menu::qdesktop {

class QdHotCornerOverlay : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdHotCornerOverlay>;

    static Ref New() {
        return std::make_shared<QdHotCornerOverlay>();
    }

    QdHotCornerOverlay() = default;
    ~QdHotCornerOverlay() = default;

    // ── Element interface ─────────────────────────────────────────────────────

    // Position covers full screen so Plutonium's hit-test and clip logic don't
    // clip our (0,0) paint.  The widget paint is still only at (0,0)...(96,72).
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override {
        // Overlay is paint-only; it never consumes input.
        (void)keys_down; (void)keys_up; (void)keys_held; (void)touch_pos;
    }

    // ── Search-suppression ref ────────────────────────────────────────────────

    // Pass a pointer to the bool that tracks Launchpad search-bar focus.
    // The pointer must remain valid for the lifetime of this object.
    // Passing nullptr disables suppression (default).
    void SetSearchActiveRef(const bool *ref) { search_active_ref_ = ref; }

private:
    const bool *search_active_ref_ = nullptr;
};

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
