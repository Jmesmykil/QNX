// qd_HotCornerDropdown.hpp — Hot-corner dropdown for uMenu v1.9.
//
// Tapping the hot-corner widget (top-left, LP_HOTCORNER_W×LP_HOTCORNER_H px)
// now opens this small popout instead of jumping directly to Launchpad.
//
// Five actions (top to bottom):
//   0  Open Launchpad   — g_MenuApplication->LoadMenu(MenuType::Launchpad)
//   1  Settings         — g_MenuApplication->LoadMenu(MenuType::Settings)
//   2  Power            — g_MenuApplication->LoadMenu(MenuType::Vault)  (power proxy)
//   3  Notifications    — disabled, reserved for v1.14
//   4  About            — g_MenuApplication->LoadMenu(MenuType::About)
//
// Dismiss: B button, Plus button, or tap outside the panel.
//
// Lifecycle mirrors QdHelpOverlay:
//   Open(renderer)  — pre-renders all text textures; sets open_=true
//   Render(renderer)— blits cached textures; no per-frame RenderText
//   HandleInput(keys_down, keys_held, touch_pos) — returns true if input consumed
//   Close()         — frees textures via pu::ui::render::DeleteTexture; open_=false
//   ~QdHotCornerDropdown() — calls Close()
//
// Z-order (caller's responsibility):
//   Render AFTER tooltip, BEFORE help overlay.
//   HandleInput AFTER task_mgr_ early-out, BEFORE other handlers.
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>

namespace ul::menu::qdesktop {

// v1.10.1: forward declaration so FireAction can call back into the
// desktop icons element to open Settings / About as floating windows
// instead of the legacy full-screen LoadMenu() path.
class QdDesktopIconsElement;

// Number of items in the dropdown.
static constexpr int kDropdownItems = 5;

class QdHotCornerDropdown {
public:
    QdHotCornerDropdown();
    ~QdHotCornerDropdown();

    // Non-copyable, non-movable (owns SDL textures).
    QdHotCornerDropdown(const QdHotCornerDropdown&) = delete;
    QdHotCornerDropdown& operator=(const QdHotCornerDropdown&) = delete;
    QdHotCornerDropdown(QdHotCornerDropdown&&) = delete;
    QdHotCornerDropdown& operator=(QdHotCornerDropdown&&) = delete;

    // Returns whether the dropdown is currently visible.
    bool IsOpen() const { return open_; }

    // Pre-renders all text textures and sets open_=true.
    // Safe to call when already open — re-renders (handles resolution changes).
    void Open(SDL_Renderer *r);

    // Frees all cached textures and sets open_=false.
    // Safe to call when already closed.
    void Close();

    // Blits the dropdown panel onto r. No-op if !open_.
    // Call after tooltip render, before help overlay render.
    void Render(SDL_Renderer *r);

    // Returns true (and may call Close() or fire an action) if input was consumed.
    // keys_down  — edge-triggered buttons this frame
    // keys_held  — level-triggered buttons this frame
    // touch_x/y  — current touch position (-1 if no touch)
    // Returns false if !open_.
    bool HandleInput(u64 keys_down, u64 keys_held, s32 touch_x, s32 touch_y);

    // v1.9.4: mouse-mode hover update.  Called every frame the dropdown is
    // open from qd_DesktopIcons OnInput when no touch is active so the
    // hover highlight tracks the cursor.  Sets hovered_ to the row index
    // under (x, y), or -1 if outside the panel.  No-op if !open_.
    void UpdateHover(s32 x, s32 y);

    // v1.9.4: ZR-driven (mouse-click) row activation.  If (x, y) hits a
    // panel row, fire that row's action (or Close() for disabled rows)
    // and return true.  Returns false if (x, y) is outside the panel —
    // caller decides what to do (typically Close()).  No-op if !open_.
    bool TryClickAt(s32 x, s32 y);

    // BUG-7 (2026-05-19) — Mirror of QdHotCornerRightDropdown::SetSkipFirstLift.
    // Call right after Open() when the dropdown opens via active touch
    // (hot-corner tap).  Causes HandleInput to consume the next touch lift
    // without firing a row.  Dropdown stays open for separate tap-confirm.
    void SetSkipFirstLift();

    // v1.10.3.2: Fire the currently-hovered row (set by UpdateHover) if
    // hovered_ >= 0 and the row is not disabled.  Returns true if an action
    // was fired; false if hovered_ == -1 or the row is disabled (caller may
    // then Close()).  No-op (returns false) if !open_.
    // Used by the ZR handler in qd_DesktopIcons when the cursor is outside
    // the panel x-bounds but UpdateHover still set a valid hovered_ on a
    // previous no-touch frame.
    bool FireHovered();

    // v1.10.1: back-reference to the desktop icons element so FireAction
    // can call OpenSettingsWindow() / OpenAboutWindow() instead of the
    // full-screen LoadMenu() path.  Set once from QdDesktopIconsElement's
    // constructor; nullptr means "fall back to LoadMenu" (used while the
    // ref hasn't been wired yet, e.g. during the dropdown's first frame).
    void SetDesktopIconsRef(QdDesktopIconsElement *ref) { desktop_icons_ref_ = ref; }

private:
    // ── Helpers (mirror QdHelpOverlay static helpers) ─────────────────────────
    static void MakeText(SDL_Renderer *r,
                         pu::ui::DefaultFontSize font_size,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);
    static void FreeTexture(SDL_Texture **tex);
    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // ── Fires the action for item index i. Calls Close() then routes. ─────────
    void FireAction(int i);

    // ── Visibility ────────────────────────────────────────────────────────────
    bool open_ = false;

    // v1.9.4: track touch-active-last-frame inside the dropdown so the
    // no-touch fire branch in HandleInput fires ONLY on the transition
    // touch-active → no-touch (the touch-release event), not on every
    // no-touch frame.  Without this gate, mouse-mode UpdateHover would
    // keep hovered_ set continuously and HandleInput would fire that row
    // every frame the cursor was over it — which is not what the user wants.
    bool was_touch_active_internal_ = false;

    // v1.9.3: release-arming gate for outside-tap close.
    //
    // The frame-counter pattern from v1.9.1/v1.9.2 (just_opened_frames_) was
    // unreliable: if the finger that opened the dropdown lingered on the
    // corner widget for longer than the grace window (~83 ms at 5 frames),
    // the next HandleInput call observed an "outside the panel" touch and
    // fired Close().  Symptom: dropdown closes the moment the user releases.
    //
    // The fix is a release-arming pattern.  armed_for_outside_close_ starts
    // false in Open().  HandleInput sets it true on the first frame with no
    // touch present (the finger has lifted).  Outside-tap close is gated on
    // armed_for_outside_close_, so a held-down finger on the corner widget
    // can never fire close — only a new tap after release can.
    bool armed_for_outside_close_ = false;

    // BUG-7 (2026-05-19) — separate from armed_for_outside_close_, this
    // skips the NEXT lift entirely (don't fire a row, don't close).  Set
    // via SetSkipFirstLift() after Open()-via-touch.  Cleared after the
    // first lift is consumed.
    bool skip_first_lift_ = false;

    // ── Panel geometry (computed on Open()) ───────────────────────────────────
    int panel_x_ = 0;
    int panel_y_ = 0;
    int panel_w_ = 0;
    int panel_h_ = 0;

    // ── Item row geometry (parallel arrays, length kDropdownItems) ────────────
    int row_y_[kDropdownItems]    = {};
    int row_h_[kDropdownItems]    = {};
    bool disabled_[kDropdownItems] = {};

    // ── Text textures for item labels ─────────────────────────────────────────
    SDL_Texture *tex_item_[kDropdownItems] = {};
    int item_w_[kDropdownItems]            = {};
    int item_h_[kDropdownItems]            = {};

    // ── Hovered item index (-1 = none) ───────────────────────────────────────
    int hovered_ = -1;

    // v1.10.1: back-ref for window-opener dispatch.  See SetDesktopIconsRef.
    QdDesktopIconsElement *desktop_icons_ref_ = nullptr;

    // v1.9.6: previous cursor position observed by UpdateHover.  Used to
    // short-circuit hover updates when the cursor is stationary.  Without
    // this gate, every no-touch frame in qd_DesktopIcons OnInput called
    // UpdateHover with the SAME (cx, cy) and overwrote a D-pad-set
    // hovered_ back to -1 (because the cursor sits outside the panel
    // when the user is using D-pad), making D-pad nav appear broken.
    s32 prev_cursor_x_ = -1;
    s32 prev_cursor_y_ = -1;
};

} // namespace ul::menu::qdesktop
