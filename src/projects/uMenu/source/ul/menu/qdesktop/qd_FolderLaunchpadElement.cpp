// qd_FolderLaunchpadElement.cpp — implementation.
// See qd_FolderLaunchpadElement.hpp for architecture notes.

#include <ul/menu/qdesktop/qd_FolderLaunchpadElement.hpp>
#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/ul_Result.hpp>  // UL_LOG_INFO

namespace ul::menu::qdesktop {

// ── Constructor / destructor ──────────────────────────────────────────────────

QdFolderLaunchpadElement::QdFolderLaunchpadElement(
        const QdTheme &theme,
        QdDesktopIconsElement *desktop_icons,
        AutoFolderIdx category)
    : lp_(QdLaunchpadElement::New(theme))
    , desktop_icons_(desktop_icons)
    , category_(category)
    , opened_(false)
{
    // v2.0.3.1: suppress full-screen-overlay chrome that's redundant inside a
    // QdWindow — Q hot-corner, auto-folder tab strip, and the bottom status
    // line.  See QdLaunchpadElement::SetWindowedMode for the chrome list.
    lp_->SetWindowedMode(true);
}

QdFolderLaunchpadElement::~QdFolderLaunchpadElement() {
    if (opened_ && lp_->IsOpen()) {
        lp_->Close();
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────
//
// First call: Open() the inner launchpad with the desktop_icons snapshot, then
// immediately apply the category filter via SetFolderFilter().  Open() resets
// active_folder_ to None (or the ConsumePendingLaunchpadFolder side-table value,
// which is None when we route through the window path), so the filter must be
// applied AFTER Open() returns.
//
// Subsequent calls: the filter is already applied; OnRender delegates directly.
//
// Origin propagation (v2.0.3)
// ──────────────────────────────────────────────────────────────────────────────
// Earlier (v2.0.2 / v2.0.2.1 / v2.0.2.2) we tried to translate the launchpad's
// drawing via SDL_RenderSetViewport.  That fails on hardware: SDL2 multiplies
// the viewport rect by the active scale on Set, so handing pre-scale physical
// pixels to SetViewport while QdWindow's scale is live shifts content by
// `scale²` of intent — only ~17 % of what we wanted at scale 0.417.  The bridge
// kept the launchpad anchored at top-left of the framebuffer regardless of
// window position.
//
// v2.0.3 abandons the SDL hack.  QdLaunchpadElement now honours its (x, y)
// parameters directly: it caches them as `render_origin_x_/y_` members and
// every SDL_Rect literal in OnRender + PaintFolderTile + PaintCell +
// PaintStatusLine + PaintPageDots adds the origin.  This bridge becomes a
// trivial pass-through.  Same pattern QdVaultLayout already uses.

void QdFolderLaunchpadElement::OnRender(
        pu::ui::render::Renderer::Ref &drawer, s32 x, s32 y) {

    if (!opened_) {
        lp_->Open(desktop_icons_);
        // Open() resets active_folder_ to None then consumes the pending-folder
        // side-table (which is None for the windowed path).  Apply our filter now
        // so the grid is pre-filtered before the first frame is painted.
        lp_->SetFolderFilter(category_);
        opened_ = true;
    }

    lp_->OnRender(drawer, x, y);

    // Consume any pending launch that occurred during the previous OnInput.
    if (lp_->IsPendingLaunch()) {
        lp_->DispatchPendingLaunch();
        lp_->Close();
        opened_ = false;
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────
//
// touch_pos is pre-translated to content-local coordinates by QdWindow.
// The inner launchpad uses the same 1920×1080 natural coordinate space, so
// no additional translation is needed.

void QdFolderLaunchpadElement::OnInput(
        u64 keys_down, u64 keys_up, u64 keys_held,
        pu::ui::TouchPoint touch_pos) {

    // v2.0.3.6: open eagerly so the very first user tap on a freshly-opened
    // folder window actually fires.  Plutonium dispatches OnInput BEFORE
    // OnRender each frame and lp_->Open() runs in OnRender — without eager
    // open here, frame 1 OnInput drops any touch that arrived on the same
    // frame the window opened.
    if (!opened_) {
        lp_->Open(desktop_icons_);
        lp_->SetFolderFilter(category_);
        opened_ = true;
    }
    lp_->OnInput(keys_down, keys_up, keys_held, touch_pos);

    if (!lp_->IsOpen()) {
        opened_ = false;
    }
}

} // namespace ul::menu::qdesktop
