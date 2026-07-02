// qd_WindowOverlay.hpp — Plutonium Element that renders the floating window
// layer (open windows, minimized dock, context menus, snap preview) at the
// correct Z-order: above the top-bar status elements (date, battery, wifi)
// but below the hot-corner overlay and cursor.
//
// W10-BUG1B fix: previously wm_.RenderAll() was called inside
// QdDesktopIconsElement::OnRender, which runs BEFORE the status-bar Plutonium
// elements (date_text, battery_top_icon, etc.) are rendered.  Because Plutonium
// renders elements in insertion order (first-added = behind), the status bar
// always appeared on top of windows.
//
// Fix: add this element to MainMenuLayout AFTER the status-bar elements and
// BEFORE the hot-corner overlay.  Its OnRender delegates to
// QdDesktopIconsElement::RenderWindowLayer().
//
// Usage (ui_MainMenuLayout.cpp):
//   auto win_overlay = QdWindowOverlay::New(this->qdesktop_icons);
//   this->Add(win_overlay);  // add after bt icon, before qdesktop_overlay_
#pragma once

#ifdef QDESKTOP_MODE

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>

namespace ul::menu::qdesktop {

class QdWindowOverlay : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdWindowOverlay>;

    static Ref New(QdDesktopIconsElement::Ref icons) {
        return std::make_shared<QdWindowOverlay>(std::move(icons));
    }

    explicit QdWindowOverlay(QdDesktopIconsElement::Ref icons)
        : icons_(std::move(icons)) {}
    ~QdWindowOverlay() = default;

    // ── Element interface ─────────────────────────────────────────────────────

    // Full-screen bounds so Plutonium's clip logic never clips window draws.
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 /*x*/, s32 /*y*/) override {
        if (icons_) {
            icons_->RenderWindowLayer(drawer);
        }
    }

    void OnInput(u64 /*keys_down*/, u64 /*keys_up*/, u64 /*keys_held*/,
                 pu::ui::TouchPoint /*touch_pos*/) override {
        // Paint-only overlay — never consumes input.
    }

private:
    QdDesktopIconsElement::Ref icons_;
};

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
