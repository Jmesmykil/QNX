// qd_ContentElement.hpp — Abstract passive-content base for QdWindow v1.10.3.10.
//
// QdContentElement is the contract between QdWindow (the host / scaler) and any
// content that lives inside a window (QdVaultLayout, QdAboutElement,
// QdSettingsElement, QdMonitorLayout, QdFolderLayout, …).
//
// Architecture (SP3 centralized-scale model):
//   QdWindow owns ALL viewport arithmetic — scale, scroll, clip rect.
//   Content is completely passive: it paints at natural coordinates and never
//   applies SDL_RenderSetScale itself.
//
// Render contract (QdWindow guarantees before calling OnRender):
//   1. SDL_RenderSetClipRect is set to the viewport rect.
//   2. SDL_RenderSetScale(r, scale_x, scale_y) has been applied.
//   3. x = -scroll_x_, y = -scroll_y_  (scroll already baked into origin).
//   Content paints as if the origin is (0, 0) at natural 1:1 coordinates.
//   No scale division, no scroll addition inside OnRender.
//
// Input contract (QdWindow guarantees before calling OnInput):
//   touch_pos is already converted to content-local coordinates:
//     local.x = (screen_x - win_x_ - 1) / scale_x  + scroll_x_
//     local.y = (screen_y - win_y_ - TITLEBAR_H) / scale_y + scroll_y_
//   Content hit-tests against its natural-coordinate rects directly.
//
// Natural size:
//   GetNaturalW() / GetNaturalH() return the pixel dimensions of the content
//   canvas in natural (unscaled) coordinates.  QdWindow reads these once at
//   SetContent() time and caches them; call InvalidateNaturalSize() if the
//   content dimensions change at runtime (e.g. folder list grows).
//
// Refresh:
//   Refresh() is called by QdWindow every kTickRefreshHz frames while the
//   window is in Normal state.  Override to push live data (battery voltage,
//   clock, task list) into the element's internal state.  Base no-ops.
//
// Native references:
//   • pu::ui::elm::Element — Plutonium base class (OnRender / OnInput signatures)
//   • SDL_RenderSetScale   — SDL2 2.0 renderer scale API (SDL_render.h:490)
//   • SDL_RenderSetClipRect — SDL2 clip region API (SDL_render.h:462)
//   • HOME applet window-scale convention — per qos-native-logic-first.md §3
#pragma once

#include <pu/ui/elm/elm_Element.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <string>
#include <switch.h>

namespace ul::menu::qdesktop {

// ── QdContentElement ─────────────────────────────────────────────────────────

/// Abstract base for content elements hosted inside a QdWindow.
///
/// Subclasses MUST implement:
///   GetNaturalW(), GetNaturalH() — natural canvas dimensions (px, unscaled).
///   OnRender(drawer, x, y)       — paint at natural coords; scale is pre-applied.
///   OnInput(keys_down, ...)      — handle input; touch is pre-translated to
///                                   content-local natural coordinates.
///
/// Subclasses MAY override:
///   Refresh()                    — called every ~60 frames to update live data.
///   InvalidateNaturalSize()      — called by the subclass when GetNaturalW/H
///                                   would return a different value than before
///                                   (e.g. folder list grew); QdWindow re-reads
///                                   cached natural_w_ / natural_h_.
class QdContentElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdContentElement>;

    ~QdContentElement() override = default;

    // ── Natural dimensions ────────────────────────────────────────────────────

    /// Width of the content canvas in natural (1:1) pixels.
    /// Must remain stable between calls to InvalidateNaturalSize().
    virtual s32 GetNaturalW() const = 0;

    /// Height of the content canvas in natural (1:1) pixels.
    /// Must remain stable between calls to InvalidateNaturalSize().
    virtual s32 GetNaturalH() const = 0;

    // ── Passive render / input ────────────────────────────────────────────────

    /// Paint content at origin (x, y) in natural coordinates.
    /// SDL scale and clip are pre-applied by QdWindow.
    /// x = -scroll_x_, y = -scroll_y_.  Never apply SDL_RenderSetScale here.
    void OnRender(pu::ui::render::Renderer::Ref& drawer, s32 x, s32 y) override = 0;

    /// Handle input forwarded from QdWindow.
    /// touch_pos is already in content-local natural coordinates:
    ///   natural_x = (screen_x - win_x - 1) / scale_x + scroll_x
    ///   natural_y = (screen_y - win_y - TITLEBAR_H) / scale_y + scroll_y
    /// Hit-test your rects against these values directly.
    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override = 0;

    // ── Periodic refresh ──────────────────────────────────────────────────────

    /// Called by QdWindow every ~60 frames while in Normal state.
    /// Override to push live-data updates (clock, battery, task list, etc.)
    /// into the element's internal state without needing a separate timer.
    /// Base no-ops; override only when live data is needed.
    virtual void Refresh() {}

    // ── Hierarchical Back (B) — v3.7.7 ────────────────────────────────────────
    //
    // QdWindow consults this when B is pressed, BEFORE closing the window.
    // Return true to CONSUME B because the content popped an internal sub-view
    // (e.g. album Image → grid → top); the window stays open.  Return false
    // (default) to let the window close.  This makes B one consistent
    // hierarchical-back across EVERY window: pop one level, close only at the
    // top.  Without it, the chrome closed on B unconditionally and any content
    // that also wanted B got a dead handler (see qd_Window.cpp B-dispatch).
    virtual bool OnBackRequested() { return false; }

    /// Compact one-line debug snapshot of this content's internal mode, for the
    /// /ui debug route.  Short ASCII, no newlines; empty = nothing useful.
    /// Called on the RENDER THREAD only — read member state directly, no locks.
    virtual std::string GetDebugState() const { return {}; }

    // ── Scaling-mode opt-in (v2.0.0) ──────────────────────────────────────────
    //
    // Default: uniform-scale (preserves aspect ratio; introduced v1.10.3.10.3).
    //
    // Opt-in: layouts with content that GROWS vertically with data (file lists,
    // task lists, log windows) override `PrefersWidthBoundScale()` to return
    // true.  QdWindow then uses scale_x = scale_y = vw / natural_w (width-bound
    // only) so cells render at full design size and excess vertical content
    // surfaces VSB-driven scrolling instead of getting shrunk to fit a fixed
    // aspect.  The Vault file grid is the canonical case.
    //
    // Layouts whose content is fixed-extent (Settings card, About card, Monitor
    // tile grid) keep the default uniform-scale and stay visually centered.
    virtual bool PrefersWidthBoundScale() const { return false; }

    // ── Natural-size invalidation ─────────────────────────────────────────────

    /// Signal that GetNaturalW() / GetNaturalH() will return a new value.
    /// QdWindow calls GetNaturalW/H again on the next SetContent or the next
    /// frame its window_manager integration syncs natural size.
    /// The flag is read and reset by QdWindow::SetContent and during OnRender.
    void InvalidateNaturalSize() { natural_size_dirty_ = true; }

    /// Returns true if the natural size has changed since the last time
    /// QdWindow read it.  QdWindow calls this each frame and re-caches
    /// natural_w_ / natural_h_ when true.
    bool IsNaturalSizeDirty() const { return natural_size_dirty_; }

    /// Called by QdWindow after it re-reads GetNaturalW/H.
    void ClearNaturalSizeDirty() { natural_size_dirty_ = false; }

    // ── pu::ui::elm::Element positional stubs (QdWindow controls geometry) ───
    // QdContentElement elements are never placed by a Plutonium Layout — QdWindow
    // drives all geometry.  These satisfy the Element interface with zero-cost
    // accessors; the values are never used for rendering decisions inside content.

    s32 GetX() override { return 0; }
    s32 GetY() override { return 0; }
    s32 GetWidth() override  { return GetNaturalW(); }
    s32 GetHeight() override { return GetNaturalH(); }

protected:
    QdContentElement() = default;

private:
    bool natural_size_dirty_ = false;
};

} // namespace ul::menu::qdesktop
