// qd_FolderLaunchpadElement.hpp — QdContentElement wrapper for a category-
// filtered QdLaunchpadElement, used by desktop folder-tile windows.
//
// Architecture
// ──────────────────────────────────────────────────────────────────────────────
// QdWindow::New() requires a QdContentElement::Ref as its content argument.
// QdLaunchpadElement is a pu::ui::elm::Element (NOT a QdContentElement), so it
// cannot be passed to QdWindow directly.
//
// QdFolderLaunchpadElement bridges the two:
//   • Inherits QdContentElement so QdWindow can host it.
//   • Owns one QdLaunchpadElement (private member).
//   • Takes a QdDesktopIconsElement* (non-owning) and an AutoFolderIdx category
//     at construction.
//   • Calls lp_->Open(desktop_icons_) once on the first OnRender call, then
//     applies SetFolderFilter(category_) to restrict the grid to one bucket.
//   • Delegates OnRender / OnInput to the owned QdLaunchpadElement.
//   • Uses lp_->PrefersWidthBoundScale() = false (fixed 1920×1080 canvas).
//
// Natural canvas
// ──────────────────────────────────────────────────────────────────────────────
// QdLaunchpadElement always renders at 1920×1080 (full-screen coordinates).
// QdWindow applies uniform scale so the canvas fits in the window viewport.
// GetNaturalW/H return 1920/1080 — identical to QdLaunchpadElement::GetWidth/H.
//
// Folder → AutoFolderIdx mapping (caller side, in qd_DesktopIcons_WmBridge.cpp)
// ──────────────────────────────────────────────────────────────────────────────
//   DesktopFolderId::Games(0)     → AutoFolderIdx::NxGames
//   DesktopFolderId::Emulators(1) → AutoFolderIdx::Homebrew
//   DesktopFolderId::Tools(2)     → AutoFolderIdx::Homebrew
//   DesktopFolderId::System(3)    → AutoFolderIdx::System
//   DesktopFolderId::QOS(4)       → AutoFolderIdx::Builtin
//   DesktopFolderId::Other(5)     → AutoFolderIdx::None  (show all)
#pragma once

#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>

namespace ul::menu::qdesktop {

class QdFolderLaunchpadElement : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdFolderLaunchpadElement>;

    /// desktop_icons must outlive this element (non-owning pointer).
    /// category filters the launchpad to one AutoFolderIdx bucket;
    /// AutoFolderIdx::None shows all items.
    static Ref New(const QdTheme &theme,
                   QdDesktopIconsElement *desktop_icons,
                   AutoFolderIdx category) {
        return std::make_shared<QdFolderLaunchpadElement>(theme, desktop_icons, category);
    }

    QdFolderLaunchpadElement(const QdTheme &theme,
                              QdDesktopIconsElement *desktop_icons,
                              AutoFolderIdx category);
    ~QdFolderLaunchpadElement() override;

    // ── QdContentElement interface ─────────────────────────────────────────────

    // The launchpad canvas is always 1920×1080; QdWindow scales to fit.
    s32 GetNaturalW() const override { return 1920; }
    s32 GetNaturalH() const override { return 1080; }

    // QdWindow calls this with SDL scale + clip pre-applied.
    // Opens the launchpad on first call, then applies the category filter.
    void OnRender(pu::ui::render::Renderer::Ref &drawer, s32 x, s32 y) override;

    // QdWindow pre-translates touch_pos to content-local coords.
    // Forwarded directly to the owned launchpad.
    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    // ── Element positional stubs ───────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return GetNaturalW(); }
    s32 GetHeight() override { return GetNaturalH(); }

private:
    QdLaunchpadElement::Ref   lp_;
    QdDesktopIconsElement    *desktop_icons_;   // non-owning
    AutoFolderIdx             category_;
    bool                      opened_;           // true after first Open() call
};

} // namespace ul::menu::qdesktop
