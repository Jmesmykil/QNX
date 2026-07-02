// qd_ContextMenu.hpp — Lightweight ZL context-menu overlay for uMenu v1.10.3+.
//
// Presents a small vertical menu panel anchored at (anchor_x, anchor_y),
// clamped so the panel stays within SCREEN_W × SCREEN_H.
//
// Palette matches QdHotCornerDropdown:
//   Panel bg  : g_QdTheme.surface_glass at A=0xEA (navy, 92% opaque)
//   Hover bg  : g_QdTheme.titlebar_inactive at A=0xFF
//   Label text: #FFFFFF
//   Border    : g_QdTheme.accent (cyan, 1 px)
//
// Z2.0 (2026-05-19): added optional one-level submenu support via MenuItem.
//   - Existing string-vector callers keep working (Open(std::vector<std::string>...)).
//   - New callers can pass MenuItem with .submenu_items populated.
//   - A row with a submenu shows a chevron ('▸') at the right edge.
//   - Activating that row (A / ZR / tap) opens the submenu to the right
//     of the parent panel (auto-flips left if no room).
//   - Pressing B / ZL / Left while in submenu closes submenu only (back to
//     parent). Pressing it again closes the whole menu (cancel = -1).
//   - Selection result is now Selection{parent_index, sub_index}.
//     GetSelectedIndex() returns parent_index for back-compat (no submenu
//     callers always see sub_index = -1).
//
// Lifecycle:
//   Open(renderer, items, anchor_x, anchor_y) — pre-renders textures; sets open_=true
//   Render(renderer)                           — blits cached textures; no-op if !open_
//   HandleInput(keys_down, keys_up, cx, cy, ...) — D-pad nav + A/B; returns true when consumed
//   Close()                                    — frees textures; open_=false
//   IsOpen()                                   — query
//   GetSelectedIndex()                         — parent index (or -1)
//   GetSelection()                             — full {parent_index, sub_index}
//
// D-pad nav (top-level): Up/Down moves hovered_ within [0, item_count_).
//   A / ZR on a row with submenu → opens submenu (does NOT confirm).
//   A / ZR on a leaf row → confirms; selected_parent_index_ = hovered_;
//                           selected_sub_index_ = -1; Close().
//   B / ZL → cancels; selected_*_index_ = -1; Close().
//
// D-pad nav (submenu): Up/Down moves submenu_hovered_ within [0, submenu_item_count_).
//   A / ZR → confirms; selected_parent_index_ = submenu_parent_index_;
//                       selected_sub_index_ = submenu_hovered_; Close().
//   B / ZL / Left → closes submenu only (back to top-level).
//
// Touch: fire-on-release (finger-up inside a row confirms that row).
//        Touch outside any panel while armed closes menu without selection (cancel).
//        Tap on a row with submenu opens that submenu.
//
// Z-order (caller's responsibility):
//   Render AFTER wm_.RenderAll(), tooltip, and snap preview.
//   Render BEFORE help overlay.
//   HandleInput BEFORE other input handlers (checked first in OnInput).
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <string>
#include <vector>

#include <ul/menu/qdesktop/qd_WmConstants.hpp>

namespace ul::menu::qdesktop {

// Per-row description for the rich Open() overload.  Empty submenu_items
// makes it a leaf (same behavior as the legacy string-vector overload).
struct QdContextMenuItem {
    std::string              label;
    std::vector<std::string> submenu_items;   // empty = leaf row
    bool                     disabled = false; // plumbed; render path TBD in Z2.0+
    // QoL-D2 (2026-05-19) — optional keybind hint shown right-aligned in
    // the row.  Examples: "[A]", "[B]", "[X]", "[ZR]".  Empty = no hint.
    // Renders to the LEFT of the submenu chevron when both are present.
    std::string              key_hint;
};

// Full selection result.  parent_index == -1 means cancelled.
// sub_index == -1 means the parent row was a leaf (or no submenu used).
struct QdContextMenuSelection {
    int parent_index = -1;
    int sub_index    = -1;
};

class QdContextMenu {
public:
    QdContextMenu();
    ~QdContextMenu();

    // Non-copyable, non-movable (owns SDL textures).
    QdContextMenu(const QdContextMenu&)            = delete;
    QdContextMenu& operator=(const QdContextMenu&) = delete;
    QdContextMenu(QdContextMenu&&)                 = delete;
    QdContextMenu& operator=(QdContextMenu&&)      = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────────

    // Pre-render item label textures and open the panel at (anchor_x, anchor_y).
    // Panel is clamped so it does not extend past SCREEN_W or SCREEN_H.
    // Safe to call when already open — replaces previous state.
    //
    // Two overloads:
    //  (a) string vector  — legacy, all items are leafs (back-compat for v1.10 callers)
    //  (b) MenuItem vector — supports per-row submenus and disabled flag
    void Open(SDL_Renderer *r,
              const std::vector<std::string> &items,
              s32 anchor_x, s32 anchor_y);
    void Open(SDL_Renderer *r,
              const std::vector<QdContextMenuItem> &items,
              s32 anchor_x, s32 anchor_y);

    // Free all cached textures and set open_=false.
    // Safe to call when already closed.
    void Close();

    // QoL-T1 v2 (2026-05-19, BUG-7 hardening): Long-press touch path opens
    // the menu while a finger is still down.  Calling SetSkipFirstLift()
    // right after Open() tells the fire-on-release logic to IGNORE the
    // lift that releases the opening touch — the menu stays open for a
    // separate tap-confirm.  Without this, natural finger drift during
    // the long-press lift would hit a random row and immediately fire it
    // (the "must keep finger pressed to navigate" complaint).
    //
    // After Open()+SetSkipFirstLift(), the open sequence is:
    //   1. Long-press fires, menu opens, flag set
    //   2. User lifts the opening finger → lift CONSUMED, menu stays open
    //   3. User taps a row separately → row confirms (normal fire-on-release)
    //
    // (The previous name PreArm() implemented slide-and-release which
    // wasn't the expected gesture — see qd_HotCornerDropdown.cpp comments.)
    void SetSkipFirstLift();

    // Blit the panel onto r. No-op if !open_.
    void Render(SDL_Renderer *r) const;

    // Process input when open. Returns true if input was consumed.
    // keys_down  — edge-triggered buttons this frame
    // keys_up    — edge-released buttons this frame
    // cx / cy    — current software cursor position (for mouse-mode hover)
    // touch_x / touch_y — current touch position; -1/-1 when no touch
    // Returns false if !open_.
    bool HandleInput(u64 keys_down, u64 keys_up, s32 cx, s32 cy,
                     s32 touch_x, s32 touch_y);

    // ── Queries ───────────────────────────────────────────────────────────────

    bool IsOpen() const { return open_; }

    // Returns confirmed parent-row index, or -1 if cancelled / not yet closed.
    // For leaf rows this is the index of the activated row.  For submenu
    // rows this is the index of the parent row (the caller must also check
    // GetSelection().sub_index to know which submenu item was picked).
    // Reset to -1 by the next Open() call.
    int GetSelectedIndex() const { return selected_parent_index_; }

    // Full selection result.  Callers that pass MenuItem with submenus
    // MUST check sub_index to disambiguate parent-row dispatch from
    // submenu-row dispatch.
    QdContextMenuSelection GetSelection() const {
        return { selected_parent_index_, selected_sub_index_ };
    }

private:
    // ── Helpers ────────────────────────────────────────────────────────────────

    static void FillRoundedRect(SDL_Renderer *r, SDL_Rect rect, int radius);

    static void MakeText(SDL_Renderer *r,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);

    static void FreeTexture(SDL_Texture **tex);
    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // Z2.0 — submenu open/close.  parent_index is the top-level row whose
    // submenu_items should be rendered.  No-op if items_[parent_index] has
    // no submenu_items.  Replaces any currently-open submenu.
    void OpenSubmenu(SDL_Renderer *r, int parent_index);
    void CloseSubmenu();

    // ── Capacity ──────────────────────────────────────────────────────────────

    static constexpr int kMaxItems    = 8;    // top-level cap (design constraint)
    static constexpr int kMaxSubItems = 16;   // submenu cap (theme picker = 10)

    // ── Top-level state ───────────────────────────────────────────────────────

    bool open_                  = false;
    int  selected_parent_index_ = -1;
    int  selected_sub_index_    = -1;
    int  item_count_            = 0;
    int  hovered_               = 0;   // always in [0, item_count_)

    // Touch fire-on-release.
    bool was_touch_active_internal_ = false;
    // First no-touch frame after Open() arms outside-close.
    bool armed_for_outside_close_   = false;
    // QoL-T1 v2 / BUG-7 — set by SetSkipFirstLift() when the menu opens
    // while a touch is active.  The next lift detected by fire-on-release
    // is consumed without firing a row (and without closing the menu).
    // Cleared after the consumed lift; subsequent touches behave normally.
    bool skip_first_lift_           = false;

    // Top-level panel geometry.
    int panel_x_ = 0;
    int panel_y_ = 0;
    int panel_w_ = 0;
    int panel_h_ = 0;

    // Per-item row geometry + textures.
    int          row_y_[kMaxItems]     = {};
    SDL_Texture *tex_item_[kMaxItems]  = {};
    int          item_tex_w_[kMaxItems] = {};
    int          item_tex_h_[kMaxItems] = {};

    // Z2.0 — owned copy of the current MenuItem vector.  Used to determine
    // which rows have submenus and to render them.  Empty when opened via
    // the string-vector overload.
    std::vector<QdContextMenuItem> items_;

    // Z2.0 — chevron textures for rows with submenus.  Sparse: only rows
    // with non-empty submenu_items get a chevron.  nullptr for leaf rows.
    SDL_Texture *tex_chevron_[kMaxItems]   = {};
    int          chevron_tex_w_[kMaxItems] = {};
    int          chevron_tex_h_[kMaxItems] = {};

    // QoL-D2 — keybind hint textures, sparse like chevrons.
    SDL_Texture *tex_hint_[kMaxItems]      = {};
    int          hint_tex_w_[kMaxItems]    = {};
    int          hint_tex_h_[kMaxItems]    = {};

    // ── Submenu state ─────────────────────────────────────────────────────────

    bool submenu_open_              = false;
    int  submenu_parent_index_      = -1;   // which top-level row opened it
    int  submenu_item_count_        = 0;
    int  submenu_hovered_           = 0;

    // Submenu panel geometry.
    int submenu_panel_x_ = 0;
    int submenu_panel_y_ = 0;
    int submenu_panel_w_ = 0;
    int submenu_panel_h_ = 0;

    // Per-submenu-item row geometry + textures.
    int          submenu_row_y_[kMaxSubItems]     = {};
    SDL_Texture *submenu_tex_item_[kMaxSubItems]  = {};
    int          submenu_item_tex_w_[kMaxSubItems] = {};
    int          submenu_item_tex_h_[kMaxSubItems] = {};
};

} // namespace ul::menu::qdesktop
