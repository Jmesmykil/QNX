// qd_Window.hpp — Generic window primitive for uMenu v1.10 / v1.10.3.10.
//
// A QdWindow wraps a QdContentElement (passive content with natural dimensions)
// and renders it inside a native-style window chrome: titlebar, corner-button
// controls (close TL / maximize TR / minimize BL / resize BR), focus ring,
// and drop shadow.
//
// SP3 centralized-scale model (v1.10.3.10):
//   QdWindow owns ALL viewport arithmetic — scale, scroll, clip rect.
//   Content is completely passive: it paints at natural coordinates and never
//   applies SDL_RenderSetScale itself.  QdWindow reads GetNaturalW/H() once at
//   SetContent() time and re-caches when IsNaturalSizeDirty() is true.
//
// v3.7 design-language overhaul (chrome + glyphs + focus ring):
//   - Window body / titlebar / bottom bar corners radiused to radius/md (12 px,
//     tokens.json) — DrawRoundedRect gained an optional `rad` arg (default 8 so
//     scrollbar-track call sites are byte-for-byte unchanged).
//   - Body fill is g_QdTheme.surface_glass (was desktop_bg); the 1-px frame ring
//     is focus_ring/accent on the focused window, titlebar_inactive otherwise.
//   - Focus ring rebuilt to the unified SELECTION-SPEC language: a 3-px rounded
//     focus_ring outline at the 12-px body radius PLUS a real 2-pass soft glow
//     halo (focus_ring @ 0x40 then 0x20) drawn outside the ring — reads as a
//     genuine flare, not a 1-px line.  DrawRoundedRectOutline added for the ring
//     + halo strokes (matches qd_Launchpad.cpp's PaintCell glow approach).
//   - Corner-button glyphs redrawn to GLYPH-SPEC masters: Close X, Maximize
//     rounded square (radius/sm corners), Minimize dash, Restore double-square
//     (shown when maximized — the maximize/restore state pair), Resize diagonal
//     DOUBLE-arrow.  Still code-drawn + procedural, just to spec proportions.
//   - Drop shadow radiused to match the body (elevation/2 token: 6,6 @ 0x80).
//
// v1.10.3 changes vs v1.10.2:
//   - Traffic-light strip replaced with four 48×48 corner hit zones.
//   - Drag model: origin-delta (records drag_origin_win_* + drag_origin_touch_*)
//     so held-frame (-1,-1) touch_pos values no longer stall drag.
//   - Mouse/ZR hover: UpdateHoverForCursor(cx, cy) sets hover_* booleans for
//     corner-button highlighting; ZR cursor-drag via BeginCursorDrag / etc.
//   - Maximize toggle: toggle between maximized state and pre_max_* saved geometry.
//   - Snap state: snapped_ / pre_snap_* stored; geometry applied externally.
//   - Content input forwarding: PollEvent dispatches to content_->OnInput().
//   - Tick refresh: OnRender calls content_->Refresh() every ~60 frames for
//     live-data elements (Settings battery/clock/etc.).
//
// Lifecycle (owned by QdWindowManager):
//   New(title, elem, x, y, w, h) — factory; sets initial geometry.
//   OnRender(drawer, 0, 0)       — paints chrome + delegates to elem.
//   PollEvent(keys, touch)       — titlebar drag, corner-button tap, drag-to-minimize.
//   UpdateHoverForCursor(cx, cy) — called each frame by WM for mouse-mode highlights.
//   TryActivateAtCursor(cx, cy)  — called by WM when ZR is pressed.
//   Close()                      — fires on_close_requested.
//   ~QdWindow()                  — FreeTextures(); content elem destroyed by shared_ptr.
//
// fbo_ is reserved for v1.11 (NRO-in-window). NULL in v1.10.
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/elm/elm_Element.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <functional>
#include <memory>
#include <string>

#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Frame.hpp>

namespace ul::menu::qdesktop {

// Window lifecycle state machine.
enum class WindowState : uint8_t {
    Normal,      // fully open, interactive
    Minimizing,  // shrink-to-dock animation in progress
    Minimized,   // hidden; represented by QdMinimizedDockEntry in dock band
    Restoring,   // grow-from-dock animation in progress
    Closing,     // fade-out (one-frame instant close for v1.10)
};

// ── QdWindow ─────────────────────────────────────────────────────────────────

class QdWindow {
public:
    using Ref = std::shared_ptr<QdWindow>;

    // Factory. title: UTF-8 window title string.
    // elem: the content element (QdVaultLayout etc.); natural dimensions read from
    //       elem->GetNaturalW/H() at SetContent() time.
    // x, y: initial top-left of the window frame (not content area).
    // w, h: total window size including TITLEBAR_H.
    static Ref New(const std::string& title,
                   QdContentElement::Ref elem,
                   s32 x, s32 y, s32 w, s32 h);

    ~QdWindow();

    // Non-copyable, non-movable (owns SDL textures and callbacks).
    QdWindow(const QdWindow&)            = delete;
    QdWindow& operator=(const QdWindow&) = delete;
    QdWindow(QdWindow&&)                 = delete;
    QdWindow& operator=(QdWindow&&)      = delete;

    // ── Rendering ─────────────────────────────────────────────────────────────

    // Renders the full window (chrome + content) onto the given renderer.
    // x, y ignored (window uses its own win_x_, win_y_ for position).
    //
    // visible_clip (WIN-5 SCISSOR): when non-null, restricts rendering to the
    // bounding box of this window's visible (unoccluded) region, pre-inflated
    // by 12 px and capped to the window envelope.  nullptr = no extra clip
    // (fully visible or untracked — render the whole window).  The clip is
    // applied OUTSIDE content paint so chrome, content, and scrollbars all
    // obey it; it is reset to nullptr after PaintScrollbars completes.
    //
    // Lifecycle: the manager-level clip is set HERE (inside OnRender), not
    // before the call, so it is not destroyed by the internal content-clip
    // resets at lines :446/:461/:494.
    void OnRender(pu::ui::render::Renderer::Ref& drawer, s32 x, s32 y,
                  const SDL_Rect* visible_clip = nullptr);

    // ── Input ────────────────────────────────────────────────────────────────

    // Processes raw input. Returns true if this window consumed the event.
    // Handles: titlebar drag, corner-button tap, drag-to-minimize trigger.
    // Also dispatches to content_->OnInput() when content focus is active.
    bool PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                   pu::ui::TouchPoint touch_pos);

    // ── Mouse / ZR cursor interaction (v1.10.3) ──────────────────────────────

    // Called every frame by QdWindowManager with the current cursor position.
    // Updates hover_close_, hover_maximize_, hover_minimize_, hover_resize_
    // based on which corner button (if any) the cursor overlaps.
    // No-op if cursor is not over this window.
    void UpdateHoverForCursor(s32 cx, s32 cy);

    // Returns true if the cursor is currently over any part of this window's
    // chrome or content area (used by WM to decide ZR routing).
    bool ContainsCursor(s32 cx, s32 cy) const;

    // Returns true if the cursor is over the CHROME (titlebar or bottom bar),
    // NOT over the scrollable content viewport.  Used by the desktop ZL handler
    // to restrict the Close/Min/Max menu to chrome-only; ZL over content area
    // is forwarded to the window content (e.g. launchpad per-item ctx menu).
    // v3.1.3 (BUG-ZL)
    bool IsCursorOverChrome(s32 cx, s32 cy) const {
        if (!ContainsCursor(cx, cy)) return false;
        const bool in_titlebar  = (cy >= win_y_
                                   && cy < win_y_ + static_cast<s32>(kTitlebarH));
        const bool in_statusbar = (cy >= win_y_ + win_h_ - static_cast<s32>(kStatusH)
                                   && cy < win_y_ + win_h_);
        return in_titlebar || in_statusbar;
    }

    // If the cursor is over a corner button, fire that button's action and
    // return true.  If over the content area, return false (let WM decide).
    // Used by ZR press.
    bool TryActivateAtCursor(s32 cx, s32 cy);

    // Begin a cursor-drag of the titlebar.  Called by WM when ZR is held and
    // the cursor is over the titlebar.
    void BeginCursorDrag(s32 cx, s32 cy);

    // Called every frame while ZR is held to move the window with the cursor.
    void UpdateCursorDrag(s32 cx, s32 cy);

    // End cursor-drag.  Called when ZR is released.
    void EndCursorDrag();

    // Returns true if a cursor-drag is in progress.
    bool IsCursorDragging() const { return cursor_drag_active_; }

    // Begin a resize-drag from the BR corner.  Called by WM when ZR is held
    // and the cursor is over the BR corner button.
    void BeginResizeDrag(s32 cx, s32 cy);

    // Called every frame while ZR is held to resize the window with the cursor.
    // Width = origin_ww + (cx - origin_cx); height = origin_wh + (cy - origin_cy).
    // Both clamped: width in [MIN_WIN_W=320, MAX_WIN_W=1600], height in [MIN_WIN_H=240, MAX_WIN_H=920].
    void UpdateResizeDrag(s32 cx, s32 cy);

    // End resize-drag.  Called when ZR is released.
    void EndResizeDrag();

    // Returns true if a resize-drag is in progress.
    bool IsResizeDragging() const { return resize_drag_active_; }

    // W8-FIX Bug 2/4: returns true while the user is dragging this window's
    // titlebar (origin-delta drag active, finger still held).
    bool IsTitlebarDragging() const { return dragging_titlebar_; }

    // v2.9.11 — reset ALL transient interaction state (resize/cursor/titlebar
    // drag flags).  Called by WindowManager on focus loss, minimize, close,
    // or whenever a stuck-flag scenario is suspected.  This is the safety
    // valve for the "nothing opens afterwards" HW bug — a stuck
    // resize_drag_active_ caused PollEvent to consume every input frame.
    void ResetInteractionState();

    // ── Snap / maximize state (v1.10.3) ─────────────────────────────────────

    // Apply the geometry for the given snap target immediately (no animation).
    // Saves the pre-snap geometry into pre_snap_* so RestoreFromSnap() can undo it.
    // SnapTarget::None is a no-op.
    void ApplySnap(SnapTarget target, s32 content_x, s32 content_y,
                   s32 content_w, s32 content_h);

    // Restore to the geometry saved before the most recent snap.  No-op if not snapped.
    void RestoreFromSnap();

    bool IsSnapped() const { return snapped_; }

    // Toggle maximize (full content-area).  Saves / restores pre-max geometry.
    void ToggleMaximize(s32 content_x, s32 content_y, s32 content_w, s32 content_h);

    bool IsMaximized() const { return maximized_; }

    // Z2.7 — Move the window to an absolute (win_x, win_y) position.  Used by
    // the "Move to Center" context-menu option.  Clears snapped_ / maximized_
    // since explicit movement breaks any prior snap binding (mirrors what
    // happens when the user drags a snapped window in the existing code).
    void MoveTo(s32 win_x, s32 win_y);

    // ── Animation ────────────────────────────────────────────────────────────

    // Called from OnRender when state_ is Minimizing or Restoring.
    // Advances anim_frame_ and lerps geometry. Calls callbacks when done.
    void AdvanceAnimation();

    // ── Geometry queries ─────────────────────────────────────────────────────

    s32  GetX()               const { return win_x_; }
    s32  GetY()               const { return win_y_; }
    s32  GetW()               const { return win_w_; }
    s32  GetH()               const { return win_h_; }
    s32  GetTitlebarBottomY() const { return win_y_ + static_cast<s32>(kTitlebarH); }

    // ── State queries ────────────────────────────────────────────────────────

    WindowState         GetState()     const { return state_; }
    bool                IsFocused()    const { return focused_; }
    u64                 GetProgramId() const { return program_id_; }
    const std::string&  GetTitle()     const { return title_; }

    // ── State setters ────────────────────────────────────────────────────────

    void SetFocused(bool f)         { focused_ = f; }
    void SetProgramId(u64 pid)      { program_id_ = pid; }

    // Called by QdWindowManager during RestoreWindow to set animation targets.
    void BeginRestoreAnimation(s32 target_x, s32 target_y, s32 target_w, s32 target_h,
                               s32 dock_x, s32 dock_y);

    // Called by QdWindowManager to set the dock tile position for the minimize animation.
    void SetMinimizeTarget(s32 target_x, s32 target_y);

    // Transition to Minimizing state (called by manager after on_minimize_requested fires).
    void BeginMinimizeAnimation();

    // ── Callbacks (wired by QdWindowManager at open time) ────────────────────

    std::function<void(QdWindow*)> on_close_requested;
    std::function<void(QdWindow*)> on_minimize_requested;

    // Called when BL corner or drag-to-minimize is triggered, BEFORE the
    // animation starts. Caller should enqueue the window pointer for
    // QdWindowManager::MinimizeWindow (which needs a Renderer::Ref only
    // available in OnRender). on_minimize_requested continues to fire from
    // QdWindowManager::FinalizeMinimize at animation end (unchanged).
    std::function<void(QdWindow*)> on_minimize_begin_;

    // Optional periodic tick callback (v1.10.3).
    // If set, called every kTickRefreshHz render frames while in Normal state.
    // Used to call Refresh() on content layouts that don't expose it on Element.
    std::function<void()> on_tick;

    // Optional scroll-update callback (v1.10.3.5).
    // Called whenever scroll_x_ or scroll_y_ changes (wheel, thumb drag, D-pad).
    // Content layouts may use this to trigger incremental data loads.
    std::function<void(s32 scroll_x, s32 scroll_y)> on_scroll_update;

    // Set keybind hint text to be rendered inside the bottom chrome bar (v1.10.3.6.1).
    // Call once from the content layout's SetOwnerWindow() (or constructor if preferred).
    // The window owns the SDL_Texture; it is re-created when the text changes and freed
    // in FreeTextures().  Pass an empty string to clear the hint.
    void SetHintText(const std::string& hint);

    // ── Content API (SP3 centralized-scale model, v1.10.3.10) ────────────────
    //
    // SetContent() replaces the old SetContentLogicalSize + SetScaleToViewport pair.
    // QdWindow reads natural_w/h from content->GetNaturalW/H() at call time and
    // re-reads every frame that IsNaturalSizeDirty() is true.
    //
    // Render contract: QdWindow applies SDL_RenderSetScale(scale_x, scale_y) before
    // calling content->OnRender and resets to 1:1 after.  Content MUST NOT call
    // SDL_RenderSetScale itself.
    //
    // Input contract: touch_pos forwarded to content->OnInput is already translated
    // to content-local natural coordinates:
    //   local.x = (screen_x - win_x_ - 1) / scale_x + scroll_x_
    //   local.y = (screen_y - win_y_ - TITLEBAR_H) / scale_y + scroll_y_

    // Replace the content element.  Resets scroll to (0,0) and re-reads natural dims.
    void SetContent(QdContentElement::Ref content);

    // ── WIN-2: frozen-content render-to-texture bake ─────────────────────────
    //
    // Mark the content bake texture dirty so it is re-rendered on the next
    // OnRender call.  Call this at every content-mutation site.
    void MarkContentDirty() { content_dirty_ = true; }

    // ── WIN-SCALE-FIX-2: non-visible texture eviction ────────────────────────
    //
    // EvictBakeTextures(): free the large per-window SDL_Textures (content bake
    //   + shadow + ring) when this window is known to be fully occluded or
    //   non-visible.  Content is re-baked lazily on the next OnRender after the
    //   window becomes visible again.  Disc textures (30×30) are left intact.
    //   Called by QdWindowManager::EvictNonVisibleTextures() each frame after
    //   the occlusion pass.
    //
    // HasBakeTextures(): returns true if the content bake texture currently
    //   exists (i.e., the window has resident VRAM for its content cache).
    //   Used by QdWindowManager to decide whether eviction is worthwhile.
    void EvictBakeTextures();
    bool HasBakeTextures() const;

    // ── Scroll viewport API (v1.10.3.5) ─────────────────────────────────────
    //
    // Pattern: NSScrollView / GTK scrolled window.
    // The content element is rendered at offset (-scroll_x_, -scroll_y_) relative
    // to the viewport origin.  Scrollbars appear when natural_h * scale_y > vh
    // (VSB) or natural_w * scale_x > vw (HSB).

    // Clamp-safe scroll setter.  Values are clamped to [0, max_scroll].
    // Fires on_scroll_update if the offset actually changes.
    void SetScrollOffset(s32 x, s32 y);

    // Returns the visible viewport size (window content area minus any scrollbar strips).
    // vw = win_w_ - 2 - (vsb_visible ? kScrollbarW : 0)
    // vh = win_h_ - kTitlebarH - BOTTOM_BAR_H - 1 - (hsb_visible ? kScrollbarW : 0)
    void GetViewportSize(s32& vw, s32& vh) const;

    // ── Public layout constants (needed by WmBridge for kContentH arithmetic) ──
    /// Height of each corner button hit-zone (BL/BR/TL/TR); also reserved as
    /// a bottom strip in the content viewport.  48 px = CORNER_BTN_SIZE.
    static constexpr int kCornerBtn = 48;

private:
    QdWindow() = default;

    // ── Texture helpers ───────────────────────────────────────────────────────

    // Frees all SDL textures owned by this window.
    void FreeTextures();

    // ── SDL drawing helpers (used by PaintScrollbars) ─────────────────────────
    static void DrawCircle(SDL_Renderer* r, int cx, int cy, int rad, pu::ui::Color col);
    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col, int rad = 8);

    // Paint vertical and/or horizontal scrollbars.
    void PaintScrollbars(SDL_Renderer* r, u8 alpha);

    // Animation constants.
    static constexpr int kAnimFrames    = 16;    // ~267 ms at 60 fps — matches Nintendo HOME applet
    // v3.7: chrome heights from QdFrame (40/34). Legacy TITLEBAR_H/BOTTOM_BAR_H (42) stay in
    // qd_WmConstants.hpp for other call sites; only QdWindow internals use these aliases.
    static constexpr int kTitlebarH     = QdFrame::kTitlebarH;  // 40 px
    static constexpr int kStatusH       = QdFrame::kStatusH;    // 34 px
    static constexpr int kDragThresh    = 912;   // DRAG_MINIMIZE_THRESHOLD = SCREEN_H-DOCK_H-20
    // W11-BUG1: relaxed from 48 (TOPBAR_H) to 0 so windows can reach the very top.
    static constexpr int kMinY          =   0;
    static constexpr int kTickRefreshHz =  60;   // call content_->Refresh() every N render frames
    static constexpr int kScrollbarW    =   6;   // scrollbar track width in px (Q OS v1.10.3.5)

    // ── Core state ───────────────────────────────────────────────────────────

    std::string               title_;
    QdContentElement::Ref     content_;

    s32 win_x_  = 0;
    s32 win_y_  = 0;
    s32 win_w_  = 0;
    s32 win_h_  = 0;

    // Intra-desktop window-stack focus (which window is topmost in our window list).
    // NOT a mirror of libnx AppletFocusState — uMenu IS the foreground applet
    // (Atmosphère hijacks slot 0100000000001000), so AppletFocusState is always
    // Focused. OS-level focus transitions arrive via uSystem daemon's
    // FocusStateChanged message — see ul/system/system_Message.hpp:13.
    bool        focused_    = false;
    WindowState state_      = WindowState::Normal;
    u64         program_id_ = 0;

    // ── Origin-delta titlebar drag (v1.10.3 fix) ──────────────────────────────
    // Records window + touch positions at drag-start and computes the new
    // position as origin + (current_touch - origin_touch) so held-frame
    // (-1,-1) touch_pos values do not advance the drag position.

    bool dragging_titlebar_   = false;
    s32  drag_origin_win_x_   = 0;    // window x at drag-start
    s32  drag_origin_win_y_   = 0;    // window y at drag-start

    // QoL-T8 (2026-05-19) — track last titlebar touch-down for double-tap
    // detection (double-tap = toggle maximize, mirrors macOS/Win convention).
    // Reset to 0 when the touch actually drags the window, so a drag-tap
    // sequence doesn't accidentally fire maximize.
    u64  last_titlebar_tap_tick_ = 0;
    s32  drag_origin_touch_x_ = 0;    // touch x at drag-start
    s32  drag_origin_touch_y_ = 0;    // touch y at drag-start
    s32  drag_last_touch_x_   = 0;    // most recent valid touch x
    s32  drag_last_touch_y_   = 0;    // most recent valid touch y

    // ── Cursor-drag state (v1.10.3 ZR drag) ───────────────────────────────────

    bool cursor_drag_active_    = false;
    s32  cursor_drag_origin_wx_ = 0;
    s32  cursor_drag_origin_wy_ = 0;
    s32  cursor_drag_origin_cx_ = 0;
    s32  cursor_drag_origin_cy_ = 0;

    // ── Resize-drag state (v1.10.3.1 BR corner drag) ─────────────────────────
    // Origin-delta resize: new_w = origin_ww + (cx - origin_cx), clamped to
    // [MIN_WIN_W, MAX_WIN_W] x [MIN_WIN_H, MAX_WIN_H].

    bool resize_drag_active_    = false;
    // v2.9.11 — watchdog frame counter.  PollEvent increments this every
    // frame the resize drag has no touch update; if it exceeds ~10 frames
    // (~167 ms at 60 fps) the drag is force-ended.  Guards against the
    // touch-up event being lost or routed to a different window.
    s32  resize_no_touch_frames_ = 0;
    s32  resize_drag_origin_wx_ = 0;
    s32  resize_drag_origin_wy_ = 0;
    s32  resize_drag_origin_ww_ = 0;   // window width at drag-start
    s32  resize_drag_origin_wh_ = 0;   // window height at drag-start
    s32  resize_drag_origin_cx_ = 0;
    s32  resize_drag_origin_cy_ = 0;

    // ── Natural content dimensions (SP3 centralized-scale model, v1.10.3.10) ──
    // Set by SetContent(); re-read each frame when IsNaturalSizeDirty() is true.
    s32  natural_w_ = 0;   // content->GetNaturalW() at last cache time
    s32  natural_h_ = 0;   // content->GetNaturalH() at last cache time

    // Cached scale factors — computed in OnRender, read by PollEvent for touch
    // coordinate translation.  Updated every frame that content is rendered.
    float cur_scale_x_ = 1.0f;
    float cur_scale_y_ = 1.0f;

    // Cached centering offsets in PRE-SCALE (natural-coord) units.  Computed in
    // OnRender so the natural canvas is centered inside the viewport when
    // uniform scaling leaves margin in one axis (v1.10.3.10.4).  Read by the
    // touch handler to invert the same offset:
    //   render: origin_x = (cx_pos+1)/scale_x - scroll_x_ + cur_offset_x_
    //   touch : local.x  = (tx - win_x_ - 1) / cur_scale_x_ + scroll_x_ - cur_offset_x_
    float cur_offset_x_ = 0.0f;
    float cur_offset_y_ = 0.0f;

    // ── Scroll state (v1.10.3.5) ─────────────────────────────────────────────

    // Current scroll offsets (clamped to [0, max_scroll]).
    s32  scroll_x_ = 0;
    s32  scroll_y_ = 0;

    // Vertical scrollbar thumb drag state.
    bool vsb_drag_active_    = false;
    s32  vsb_drag_origin_y_  = 0;   // touch y at drag-start
    s32  vsb_drag_origin_sy_ = 0;   // scroll_y_ at drag-start

    // Horizontal scrollbar thumb drag state.
    bool hsb_drag_active_    = false;
    s32  hsb_drag_origin_x_  = 0;   // touch x at drag-start
    s32  hsb_drag_origin_sx_ = 0;   // scroll_x_ at drag-start

    // ── QoL-T2 finger drag-scroll over content area (2026-05-19) ─────────────
    // State machine: detect touch-down in the content zone (not chrome /
    // scrollbar / corner), track movement, engage drag once movement exceeds
    // kContentDragJitterPx.  Below threshold = tap pass-through to content;
    // above = consume the touch as a scroll gesture (don't forward to content).
    bool content_drag_active_    = false;  // touch-down received, watching movement
    bool content_drag_engaged_   = false;  // movement > threshold → drag mode
    s32  content_drag_origin_y_  = 0;      // touch y at touch-down
    s32  content_drag_origin_x_  = 0;      // touch x at touch-down (HSB axis)
    s32  content_drag_origin_sy_ = 0;      // scroll_y_ at touch-down
    s32  content_drag_origin_sx_ = 0;      // scroll_x_ at touch-down
    s32  content_drag_last_y_    = 0;      // last frame's touch y (reserved)

    // ── Button hover (drives BtnState passed to QdFrame::Paint) ─────────────

    bool hover_close_    = false;
    bool hover_maximize_ = false;
    bool hover_minimize_ = false;
    bool hover_resize_   = false;  // BR grip hover (no disc — just cursor feedback)

    // ── Maximize state (v1.10.3) ──────────────────────────────────────────────

    bool maximized_   = false;
    s32  pre_max_x_   = 0;
    s32  pre_max_y_   = 0;
    s32  pre_max_w_   = 0;
    s32  pre_max_h_   = 0;

    // ── Snap state (v1.10.3) ──────────────────────────────────────────────────

    bool       snapped_     = false;
    s32        pre_snap_x_  = 0;
    s32        pre_snap_y_  = 0;
    s32        pre_snap_w_  = 0;
    s32        pre_snap_h_  = 0;

    // ── Tick counter for periodic Refresh() ───────────────────────────────────

    int tick_counter_ = 0;

    // ── A2-OPT-3: cached viewport size (one GetViewportSize() per frame) ──────
    // Computed once at the top of OnRender; invalidated by geometry changes.
    // PollEvent reads these directly instead of calling GetViewportSize() again.
    mutable s32  cached_vw_      = 0;
    mutable s32  cached_vh_      = 0;
    mutable bool vp_cache_dirty_ = true;

    // ── WIN-2: content render-to-texture bake ────────────────────────────────
    // content_bake_tex_  — SDL_TEXTUREACCESS_TARGET texture sized to (vw, vh).
    //                      Cached content render; blitted instead of re-rendering
    //                      every frame when content has not changed.
    // content_dirty_     — true when content must be re-rendered into the bake.
    //                      Set by MarkContentDirty() and at every mutation site.
    //                      Cleared after each successful bake.
    // bake_vw_, bake_vh_ — dimensions the bake texture was created for; used to
    //                      detect viewport size changes that require re-creation.
    SDL_Texture* content_bake_tex_ = nullptr;
    bool         content_dirty_    = true;
    s32          bake_vw_          = 0;
    s32          bake_vh_          = 0;

    // ── Bottom-bar hint text (v1.10.3.6.1) ───────────────────────────────────
    // Set by content layouts that call SetHintText() from SetOwnerWindow().
    // hint_tex_ is (re-)created on every SetHintText() call and freed in
    // FreeTextures().  nullptr means no hint is rendered.

    std::string   hint_text_;
    SDL_Texture*  hint_tex_   = nullptr;
    int           hint_tex_w_ = 0;
    int           hint_tex_h_ = 0;

    // v3.7 chrome component — nine-patch SVG frame + code-drawn buttons.
    QdFrame frame_;

    // Disc-button hover tooltips shown in the status bar (override hint_tex_).
    // [0]=Close, [1]=Maximize, [2]=Minimize. Lazy-built; freed by FreeTextures().
    SDL_Texture*  corner_tip_tex_[3]   = {};
    int           corner_tip_tex_w_[3] = {};
    int           corner_tip_tex_h_[3] = {};

    // ── Animation ────────────────────────────────────────────────────────────

    s32 anim_frame_     =  0;
    u8  anim_alpha_     = 255;
    s32 anim_orig_x_    =  0;
    s32 anim_orig_y_    =  0;
    s32 anim_orig_w_    =  0;
    s32 anim_orig_h_    =  0;
    s32 anim_target_x_  =  0;
    s32 anim_target_y_  =  0;

};

} // namespace ul::menu::qdesktop
