// qd_HotCornerRightDropdown.hpp — Top-right hot-corner dropdown for uMenu v1.10.3.11.
//
// Tapping the top-right hot-corner widget (LP_HOTCORNER_W × LP_HOTCORNER_H at
// x=SCREEN_W-LP_HOTCORNER_W, y=0) opens this panel with two sections:
//
//   System Status rows (read-only, grayed out):
//     0  Battery      — percentage + charger type (psm)
//     1  Time/Date    — current time from libnx time service
//     2  Network      — connection status (nifm)
//     3  Volume       — master output volume (audctl)
//
//   Quick Action rows (enabled):
//     4  Sleep        — appletStartSleepSequence(true)
//     5  Restart      — ul::menu::qdesktop::power::Reboot()
//     6  Reboot Hekate— ul::menu::qdesktop::power::RebootToHekate()
//     7  Lock Screen  — appletStartLockScreen()
//
//   Developer rows (enabled, v2.2.0+):
//     8  nxlink server toggle — dev::TryEnableNxlinkServer / DisableNxlinkServer
//        Label dynamically reflects current state on Open():
//          "nxlink: OFF"          when not running
//          "nxlink: ON @<ip>"     when listening
//          "nxlink: Init failed"  when bind failed (network not ready)
//
// Panel geometry (computed on Open()):
//   Anchored to screen right edge (panel_x_ = 1600 = SCREEN_W − 320).
//   Panel width = 320 px.  Panel top = TOPBAR_H − 8 = 40 px (flush with status
//   icon bottom edge).  Panel height = N × 48 + 2 × 8 where N =
//   kRightDropdownItems (currently 9 → height 448).
//
// Dismiss: B button, Plus button, or tap outside the panel.
//
// Lifecycle:
//   Open(renderer)  — snapshots system status, pre-renders text textures, open_=true.
//   Render(renderer)— blits cached textures; no per-frame IPC or RenderText calls.
//   HandleInput(keys_down, keys_held, touch_x, touch_y)
//   UpdateHover(x, y)
//   TryClickAt(x, y)
//   Close()         — frees textures via pu::ui::render::DeleteTexture; open_=false.
//   ~QdHotCornerRightDropdown() — calls Close().
//
// Z-order: render AFTER left dropdown, BEFORE help overlay.
//          HandleInput AFTER left dropdown early-out, BEFORE desktop D-pad nav.
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
// v2.2.1: QdNxlinkServer::State used by last_dev_row_state_ member.
// The guard mirrors qd_NxlinkServer.hpp's own #ifdef QDESKTOP_MODE block;
// without it the struct member below would be an undeclared type in non-desktop
// translation units.
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_NxlinkServer.hpp>
#include <ul/menu/qdesktop/qd_RemoteShellServer.hpp>
#endif

namespace ul::menu::qdesktop {

// Total number of items (4 status + 4 actions + 3 dev).
// v2.2.0: row 8 added for nxlink server toggle.
// v2.3.0: row 9 added for remote shell toggle.
// v3.7.12: row 10 added for the remote-test debug server toggle (HTTP :6010).
static constexpr int kRightDropdownItems = 11;

class QdHotCornerRightDropdown {
public:
    QdHotCornerRightDropdown();
    ~QdHotCornerRightDropdown();

    // Non-copyable, non-movable (owns SDL textures).
    QdHotCornerRightDropdown(const QdHotCornerRightDropdown&) = delete;
    QdHotCornerRightDropdown& operator=(const QdHotCornerRightDropdown&) = delete;
    QdHotCornerRightDropdown(QdHotCornerRightDropdown&&) = delete;
    QdHotCornerRightDropdown& operator=(QdHotCornerRightDropdown&&) = delete;

    // Returns true when the panel is visible.
    bool IsOpen() const { return open_; }

    // Snapshots system status, pre-renders text textures, sets open_=true.
    // Safe to call when already open — re-renders (handles resolution changes).
    void Open(SDL_Renderer *r);

    // Frees all cached textures and sets open_=false.
    // Safe to call when already closed.
    void Close();

    // Blits the panel onto r. No-op if !open_.
    void Render(SDL_Renderer *r);

    // Returns true (and may Close() or fire an action) if input was consumed.
    // keys_down  — edge-triggered buttons this frame.
    // keys_held  — level-triggered buttons this frame.
    // touch_x/y  — current touch position (-1 if no touch).
    // Returns false if !open_.
    bool HandleInput(u64 keys_down, u64 keys_held, s32 touch_x, s32 touch_y);

    // Cursor hover update — call on no-touch frames while the dropdown is open.
    // Sets hovered_ to the row under (x, y), or -1 if outside the panel.
    // Short-circuits when the cursor hasn't moved (D-pad-set hovered_ protection).
    // No-op if !open_.
    void UpdateHover(s32 x, s32 y);

    // ZR-driven click: if (x, y) hits an action row, fire it and return true.
    // If (x, y) hits a status row (disabled), close and return true.
    // Returns false if (x, y) is outside the panel.
    // No-op (returns false) if !open_.
    bool TryClickAt(s32 x, s32 y);

    // BUG-7 (2026-05-19) — Tell HandleInput to IGNORE the next touch lift.
    // Call right after Open() when the dropdown is being opened by an
    // active touch (e.g., hot-corner tap).  Without this, natural finger
    // drift during the lift sometimes hit a row and fired the action.
    // The first lift after this call is consumed silently; the dropdown
    // stays open for a separate tap-confirm gesture.
    void SetSkipFirstLift();

private:
    // ── Static helpers (mirror QdHotCornerDropdown helpers) ──────────────────
    static void MakeText(SDL_Renderer *r,
                         pu::ui::DefaultFontSize font_size,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);
    static void FreeTexture(SDL_Texture **tex);
    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // Fires the action for item index i (0-3 are disabled status rows — no-op).
    // Calls Close() before dispatching so the panel is gone before the action runs.
    void FireAction(int i);

    // v2.2.1: refresh the row-8 (nxlink) label texture if the server state has
    // changed since the last call.  No-op when state is unchanged (cheap equality
    // check, no allocation).  Frees the old tex_item_[8] via
    // pu::ui::render::DeleteTexture (P-B safe) before building the new one.
    // Call at the top of Render() when open_ is true.
    // Only defined when QDESKTOP_MODE is active (requires qd_NxlinkServer).
#ifdef QDESKTOP_MODE
    void RefreshDevRowLabel(SDL_Renderer *r);
#endif

    // ── Visibility ────────────────────────────────────────────────────────────
    bool open_ = false;

    // Release-arming gate for outside-tap close: stays false until the first
    // no-touch frame after Open() so the opening finger can linger anywhere.
    bool armed_for_outside_close_ = false;

    // Fire-on-release gate: prevents UpdateHover (continuous) from tripping
    // the touch-release fire path on no-touch frames.
    bool was_touch_active_internal_ = false;

    // BUG-7 hardening — set by SetSkipFirstLift() when the dropdown opens
    // while a touch is active.  The NEXT lift detected in HandleInput is
    // consumed without firing or closing; dropdown stays open for the user
    // to tap a row separately.  Cleared after the consumed lift.
    bool skip_first_lift_ = false;

    // ── Panel geometry (computed on Open()) ───────────────────────────────────
    int panel_x_ = 0;
    int panel_y_ = 0;
    int panel_w_ = 0;
    int panel_h_ = 0;

    // ── Item row geometry (parallel arrays, length kRightDropdownItems) ───────
    int  row_y_[kRightDropdownItems]    = {};
    int  row_h_[kRightDropdownItems]    = {};
    bool disabled_[kRightDropdownItems] = {};

    // ── Text textures for item labels (pre-rendered in Open()) ───────────────
    SDL_Texture *tex_item_[kRightDropdownItems] = {};
    int item_w_[kRightDropdownItems]            = {};
    int item_h_[kRightDropdownItems]            = {};

    // ── Hovered item index (-1 = none) ───────────────────────────────────────
    int hovered_ = -1;

    // Previous cursor position observed by UpdateHover (stationary-cursor guard).
    s32 prev_cursor_x_ = -1;
    s32 prev_cursor_y_ = -1;

    // v2.2.1: last-seen nxlink server state for RefreshDevRowLabel (row 8).
    // v2.3.0: last-seen remote shell state for RefreshDevRowLabel (row 9).
    // Initialised to Stopped + not-running so the first Render() call after
    // Open() is a cheap no-op (Open() built the textures with the same state).
    // Reset in Close() so the next Open() starts clean.
#ifdef QDESKTOP_MODE
    QdNxlinkServer::State      last_dev_row_state_        = QdNxlinkServer::State::Stopped;
    bool                       last_dev_row_running_      = false;
    QdRemoteShellServer::State last_shell_row_state_      = QdRemoteShellServer::State::Stopped;
    bool                       last_shell_row_running_    = false;
#endif
};

} // namespace ul::menu::qdesktop
