// qd_Window.hpp — Generic window primitive for uMenu v1.10.
//
// A QdWindow wraps any Plutonium element (a migrated layout class) and
// renders it inside a native-style window chrome: titlebar, traffic-light
// buttons (close / minimize / maximize), focus ring, and drop shadow.
//
// Lifecycle (owned by QdWindowManager):
//   New(title, elem, x, y, w, h) — factory; sets initial geometry; elem owns content.
//   OnRender(drawer, 0, 0)       — paints window chrome + delegates to elem.
//   PollEvent(keys, touch)       — titlebar drag, traffic-light hit testing.
//   Close()                      — fires on_close_requested.
//   ~QdWindow()                  — FreeTextures(); content elem destroyed by shared_ptr.
//
// Drag-to-minimize:
//   While dragging, win_y_ tracks touch position.
//   When GetTitlebarBottomY() >= DRAG_MINIMIZE_THRESHOLD (912), fires
//   on_minimize_requested → manager calls MinimizeWindow → state = Minimizing.
//
// Animation:
//   state_ == Minimizing → AdvanceAnimation() lerps position+size toward dock tile.
//   state_ == Restoring  → reverse lerp from dock tile back to original position.
//   state_ == Normal     → no animation, full paint.
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
    // elem: the content element (QdVaultLayout etc.); its SetContentSize is called
    //       inside New() with (w, h - TITLEBAR_H) before the first render.
    // x, y: initial top-left of the window frame (not content area).
    // w, h: total window size including TITLEBAR_H.
    static Ref New(const std::string& title,
                   std::shared_ptr<pu::ui::elm::Element> elem,
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
    void OnRender(pu::ui::render::Renderer::Ref& drawer, s32 x, s32 y);

    // ── Input ────────────────────────────────────────────────────────────────

    // Processes raw input. Returns true if this window consumed the event.
    // Handles: titlebar drag, traffic-light tap, drag-to-minimize trigger.
    bool PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                   pu::ui::TouchPoint touch_pos);

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

private:
    QdWindow() = default;

    // ── Texture helpers ───────────────────────────────────────────────────────

    // Frees all SDL textures owned by this window (titlebar_tex_, fbo_).
    // Safe to call multiple times; uses pu::ui::render::DeleteTexture per B41/B42.
    void FreeTextures();

    // Ensures titlebar_tex_ is allocated and up to date.
    // Called from OnRender; no-op if already valid.
    void EnsureTitlebarTexture(pu::ui::render::Renderer::Ref& drawer);

    // ── SDL drawing helpers ───────────────────────────────────────────────────

    // Draws a filled circle at (cx, cy) with given radius and color.
    static void DrawCircle(SDL_Renderer* r, int cx, int cy, int rad, pu::ui::Color col);

    // Draws a filled rounded-rect with corner radius 6 px.
    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col);

    // Animation constants.
    static constexpr int kAnimFrames   = 18;    // ~300 ms at 60 fps
    static constexpr int kTitlebarH    = 42;    // matches TITLEBAR_H in qd_WmConstants.hpp
    static constexpr int kTrafficR     = 11;    // matches TRAFFIC_RADIUS
    static constexpr int kTrafficGap   = 33;    // matches TRAFFIC_GAP
    static constexpr int kTrafficLeft  = 21;    // matches TRAFFIC_LEFT_OFFSET
    static constexpr int kTrafficY     = 21;    // matches TRAFFIC_Y_OFFSET
    static constexpr int kTrafficSlop  =  6;    // matches TRAFFIC_HIT_SLOP
    static constexpr int kFocusRing    =  3;    // matches FOCUS_RING_THICKNESS
    static constexpr int kDragThresh   = 912;   // DRAG_MINIMIZE_THRESHOLD = SCREEN_H-DOCK_H-20
    static constexpr int kMinY         =  48;   // TOPBAR_H — titlebar may not go above this

    // ── Core state ───────────────────────────────────────────────────────────

    std::string                              title_;
    std::shared_ptr<pu::ui::elm::Element>    content_;

    s32 win_x_  = 0;
    s32 win_y_  = 0;
    s32 win_w_  = 0;
    s32 win_h_  = 0;

    bool        focused_    = false;
    WindowState state_      = WindowState::Normal;
    u64         program_id_ = 0;

    // ── Titlebar drag ─────────────────────────────────────────────────────────

    bool dragging_titlebar_  = false;
    s32  drag_touch_id_      = -1;
    s32  drag_start_touch_x_ = 0;
    s32  drag_start_touch_y_ = 0;
    s32  drag_start_win_x_   = 0;
    s32  drag_start_win_y_   = 0;

    // ── Animation ────────────────────────────────────────────────────────────

    s32 anim_frame_     =  0;
    u8  anim_alpha_     = 255;
    s32 anim_orig_x_    =  0;
    s32 anim_orig_y_    =  0;
    s32 anim_orig_w_    =  0;
    s32 anim_orig_h_    =  0;
    s32 anim_target_x_  =  0;
    s32 anim_target_y_  =  0;

    // ── Traffic-light hit rects (recomputed each frame) ───────────────────────

    SDL_Rect traffic_close_ = {};
    SDL_Rect traffic_min_   = {};
    SDL_Rect traffic_max_   = {};

    // ── Textures ─────────────────────────────────────────────────────────────

    SDL_Texture* titlebar_tex_  = nullptr; // lazy, freed by FreeTextures()
    int          titlebar_tex_w_ = 0;
    int          titlebar_tex_h_ = 0;

    // v1.11 reserved: NRO framebuffer. Always nullptr in v1.10.
    SDL_Texture* fbo_ = nullptr;
};

} // namespace ul::menu::qdesktop
