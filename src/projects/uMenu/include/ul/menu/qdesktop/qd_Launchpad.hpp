// qd_Launchpad.hpp — Full-screen app-grid overlay for Q OS uMenu (v1.0.0).
// Ported from tools/mock-nro-desktop-gui/src/launchpad.rs (v1.1.0).
//
// The Launchpad is a full-screen opaque overlay that shows every installed
// application, NRO, and Q OS built-in in a searchable 10-column icon grid.
// It is triggered by:
//   - A small 60×48 px hot-corner widget at the far left of the top bar, or
//   - The Plus button (gamepad shortcut), handled by MainMenuLayout.
//
// # Lifecycle
//
// QdLaunchpadElement lives as a member of MainMenuLayout alongside
// QdDesktopIconsElement.  It is constructed once; Open/Close toggle visibility.
//
// # Data model
//
// Open() deep-copies the current icon list from QdDesktopIconsElement::icons_[]
// into a local std::vector<LpItem>.  All subsequent rendering and navigation
// operate on this private snapshot — the desktop array is not read again until
// the next Open() call.  The original index of each snapshot entry in the
// desktop array is preserved in LpItem::desktop_idx so FocusedDesktopIdx()
// can return a stable index for the host to call
// QdDesktopIconsElement::LaunchIcon().
//
// # Search
//
// Filtering is case-insensitive ASCII substring match on LpItem::name (mirrors
// launchpad.rs Launchpad::filtered_indices()).  The filtered list is rebuilt
// every frame from the current query; the result is small (≤ MAX_ICONS = 48).
//
// # OSK wire-up
//
// PushQueryChar() / PopQueryChar() / ClearQuery() are the entry points for
// future OSK integration.  In v1.0 no gamepad key is bound to PushQueryChar
// because the on-screen keyboard ships in the next batch.  The entry points are
// fully implemented — they are not stubs — they simply have no caller yet.
//
// # Integration checklist (for MainMenuLayout)
//   1. Add member:  QdLaunchpadElement::Ref launchpad_;
//   2. Construct:   launchpad_ = QdLaunchpadElement::New(theme_);
//   3. Add to layout via Add() AFTER desktop_icons_ so it renders on top.
//   4. Call launchpad_->SetVisible(false) after Add().
//   5. Hot-corner open: in QdDesktopIconsElement::OnInput, after HitTest, add:
//        if (!launchpad_->IsOpen() && tx < LP_HOTCORNER_W && ty < LP_HOTCORNER_H)
//            { launchpad_->Open(desktop_icons_ptr); launchpad_->SetVisible(true); }
//      where desktop_icons_ptr is a raw pointer to the QdDesktopIconsElement.
//   6. Plus button open (in MainMenuLayout::OnInput):
//        if (keys_down & HidNpadButton_Plus) {
//            if (!launchpad_->IsOpen()) {
//                launchpad_->Open(desktop_icons_.get());
//                launchpad_->SetVisible(true);
//            }
//        }
//   7. Launch dispatch (in MainMenuLayout::OnInput or directly in Launchpad):
//        if (launchpad_->IsOpen()) {
//            const size_t idx = launchpad_->FocusedDesktopIdx();
//            if (idx != SIZE_MAX) {
//                desktop_icons_->LaunchIcon(idx);
//                launchpad_->Close();
//                launchpad_->SetVisible(false);
//            }
//        }
//      The Launchpad calls Close() on A/ZR internally after setting the
//      pending-launch flag; host reads FocusedDesktopIdx() and dispatches.
//
// # AdvanceTick
//
// Call launchpad_->AdvanceTick() once per frame (same cadence as desktop icons)
// so the search-bar caret blinks correctly.
#pragma once

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <string>
#include <vector>
#include <cstddef>

namespace ul::menu::qdesktop {

// ── Layout constants (×1.5 from Rust 1280×720) ───────────────────────────────
// Rust LP_COLS=10 unchanged — 10 columns at 1920 px.
// Rust LP_CELL_W=104 → C++: 156.
// Rust LP_CELL_H=100 → C++: 150.
// Rust LP_GAP_X=8    → C++: 12.
// Rust LP_GAP_Y=8    → C++: 12.
// Rust LP_GRID_X=40  → C++: 60.
// Rust LP_GRID_Y=140 → C++: 144 (search 84, search_h 48 → grid below search bar).
// Search bar and hot-corner are spec-defined values from the task.

constexpr s32 LP_COLS          = 10;
constexpr s32 LP_CELL_W        = 156;   // icon + label column width
constexpr s32 LP_CELL_H        = 150;   // icon + label row height (incl. label)
constexpr s32 LP_GAP_X         = 12;
constexpr s32 LP_GAP_Y         = 12;
constexpr s32 LP_GRID_X        = 60;    // safe-area left margin
constexpr s32 LP_GRID_Y        = 144;   // below search bar (84 + 48 + 12 gap)
constexpr s32 LP_SEARCH_BAR_X  = 300;
constexpr s32 LP_SEARCH_BAR_Y  = 84;
constexpr s32 LP_SEARCH_BAR_W  = 1320;
constexpr s32 LP_SEARCH_BAR_H  = 48;
constexpr s32 LP_HOTCORNER_W   = 60;    // top-left widget width = safe-left
constexpr s32 LP_HOTCORNER_H   = 48;    // = TOPBAR_H

// Icon art dimensions within each grid cell.
// Matches PaintIconCell pattern from qd_DesktopIcons.cpp (bg rect proportions).
constexpr s32 LP_ICON_W  = 104;  // art width  (slightly smaller than cell — centred)
constexpr s32 LP_ICON_H  = 104;  // art height (square; label below)

// ── LpSortKind ────────────────────────────────────────────────────────────────
// Used during Open() sort pass to group items by category.
// Sort order: Nintendo (alpha) -> Homebrew (alpha) -> Extras (alpha) ->
//             Builtin (dock_slot order).
// Mapped from NroEntry::icon_category at Open() time.
enum class LpSortKind : u8 {
    Nintendo    = 0,
    Homebrew    = 1,
    Extras      = 2,
    Builtin     = 3,
};

// ── LpItem ────────────────────────────────────────────────────────────────────
// Snapshot of one desktop entry, copied from NroEntry at Open() time.
// The snapshot is immutable for the lifetime of one Open/Close cycle.
struct LpItem {
    // Display name (null-terminated, from NroEntry::name).
    char    name[64];
    // Category glyph (from NroEntry::glyph).
    char    glyph;
    // Fallback background colour.
    u8      bg_r, bg_g, bg_b;
    // Absolute path to the NRO (empty for Application and Builtin entries).
    // Used as the QdIconCache lookup key for NRO art.
    char    nro_path[769];
    // Absolute path to the JPEG icon (non-empty for Application entries with
    // a custom icon; empty otherwise).
    // Used as the QdIconCache lookup key for Application art.
    char    icon_path[769];
    // Application title ID (non-zero for Application entries only).
    u64     app_id;
    // True if this is a Q OS built-in dock app.
    bool    is_builtin;
    // Dock slot index (only meaningful when is_builtin == true).
    u8      dock_slot;
    // Source category (copied from NroEntry::icon_category).
    IconCategory icon_category;
    // Resolved kind for sorting (derived from icon_category at Open() time).
    LpSortKind sort_kind;
    // Index of this entry in QdDesktopIconsElement::icons_[].
    // Returned by FocusedDesktopIdx() so the host can dispatch LaunchIcon(idx).
    size_t  desktop_idx;
};

// ── QdLaunchpadElement ────────────────────────────────────────────────────────
// Pu Element rendering the full-screen Launchpad overlay.
// Covers 1920×1080 at position (0, 0) so it occludes every element below it
// in the Plutonium layout stack.
class QdLaunchpadElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdLaunchpadElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdLaunchpadElement>(theme);
    }

    explicit QdLaunchpadElement(const QdTheme &theme);
    ~QdLaunchpadElement();

    // ── Element geometry ─────────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    // ── Plutonium callbacks ───────────────────────────────────────────────────
    // OnRender paints the full overlay every frame this element is visible.
    void OnRender(pu::ui::render::Renderer::Ref &drawer, s32 x, s32 y) override;

    // OnInput handles D-pad navigation, A/ZR launch trigger, B/Plus close,
    // and StickL-press backspace.  The host is responsible for:
    //   1. Calling desktop_icons->LaunchIcon(FocusedDesktopIdx()) when
    //      IsPendingLaunch() returns true (checked after OnInput returns).
    //   2. Calling Close() and SetVisible(false) after the launch dispatch.
    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    // ── Open / Close ─────────────────────────────────────────────────────────
    // Open: snapshot desktop icons, sort, reset focus and query.
    // desktop_icons must not be null; its lifetime must exceed both this call
    // and any subsequent DispatchPendingLaunch() until Close() returns.
    // The pointer IS retained between Open and Close so that
    // DispatchPendingLaunch can call desktop_icons_ptr_->LaunchIcon(idx).
    void Open(QdDesktopIconsElement *desktop_icons);
    void Close();
    bool IsOpen() const { return is_open_; }

    // ── Query entry points ───────────────────────────────────────────────────
    // These are the OSK wire-up entry points.  In v1.0 no gamepad key calls
    // PushQueryChar() — the OSK integration is the next batch.  The functions
    // are fully implemented; they simply have no gamepad caller yet.
    void PushQueryChar(char c);
    void PopQueryChar();
    void ClearQuery();

    // ── Launch query ─────────────────────────────────────────────────────────
    // Returns the desktop icons array index (into QdDesktopIconsElement::icons_[])
    // of the currently focused, filtered item, or SIZE_MAX if nothing is focused.
    // Check this after OnInput signals IsPendingLaunch(); if non-SIZE_MAX, pass
    // the result to QdDesktopIconsElement::LaunchIcon(idx).
    size_t FocusedDesktopIdx() const;

    // Returns true when the user pressed A or ZR on a valid item this input
    // frame.  The host calls DispatchPendingLaunch() to actually fire the
    // launch through the desktop icons element, then Close().
    // Cleared on the next call to OnInput.
    bool IsPendingLaunch() const { return pending_launch_; }

    // Forwards to desktop_icons_ptr_->LaunchIcon(FocusedDesktopIdx()) when
    // the focused index is valid. No-op when desktop_icons_ptr_ is null,
    // when the focused index is out of range, or when the Launchpad is
    // closed. The launch path itself owns any subsequent menu transition
    // (FadeOut for app launches, LoadMenu for builtin specials).
    void DispatchPendingLaunch();

    // ── Frame tick ───────────────────────────────────────────────────────────
    // Advance the caret blink counter.  Call once per frame.
    void AdvanceTick();

private:
    // ── State ─────────────────────────────────────────────────────────────────
    QdTheme                 theme_;
    bool                    is_open_;
    bool                    pending_launch_;

    // Non-owning pointer set by Open(); cleared on Close(). Used by
    // DispatchPendingLaunch to fire LaunchIcon on the focused entry.
    // Lifetime obligation lives on the caller of Open.
    QdDesktopIconsElement  *desktop_icons_ptr_;

    // Snapshot of all items, sorted as described in LpSortKind.
    std::vector<LpItem>     items_;

    // D-pad focus index into the *filtered* item list.
    // Index 0 refers to filtered_items_[0] (not items_[0]).
    size_t                  focus_filtered_;

    // Current ASCII search query.
    std::string             query_;

    // Cached filtered index list, rebuilt when query_ or items_ changes.
    // Each entry is an index into items_.
    std::vector<size_t>     filtered_idxs_;

    // True when filtered_idxs_ needs to be rebuilt before next render.
    bool                    filter_dirty_;

    // Monotonic frame counter for the search-bar caret blink (30-frame phase).
    s32                     frame_tick_;

    // Per-slot cached text and icon textures — same pattern as DesktopIcons.
    // Max LP items == MAX_ICONS == 48; vector index mirrors items_ index.
    std::vector<SDL_Texture *> name_tex_;
    std::vector<SDL_Texture *> glyph_tex_;
    std::vector<SDL_Texture *> icon_tex_;
    // True when the icon cache has been checked for this slot.
    std::vector<bool>          icon_loaded_;

    // Shared icon cache reference (borrowed — host owns QdIconCache lifetime).
    // Set by Open() from the desktop_icons parameter.  Reset to nullptr on Close().
    // We store a non-owning pointer; the cache lives in QdDesktopIconsElement
    // which outlives all Launchpad open/close cycles.
    QdIconCache                *icon_cache_;

    // ── Helpers ───────────────────────────────────────────────────────────────
    // Rebuild filtered_idxs_ from query_ and items_.  Called lazily.
    void RebuildFilter();

    // Returns the number of visible items in the current filtered set.
    size_t FilteredCount() const;

    // Return the cell top-left (screen-space) for visual position vpos.
    static void CellXY(size_t vpos, s32 &out_x, s32 &out_y);

    // Paint one grid cell at screen position (cx, cy).
    void PaintCell(SDL_Renderer *r,
                   const LpItem &item,
                   size_t item_idx,
                   s32 cx, s32 cy,
                   bool is_focused);

    // Free all cached SDL textures for a single item slot.
    void FreeSlotTextures(size_t item_idx);

    // Free all cached SDL textures for every slot (called from dtor and Close()).
    void FreeAllTextures();

    // Build a section header label string for the given LpSortKind.
    // Returns a pointer to a static string (no allocation).
    static const char *SectionLabel(LpSortKind kind);

    // Paint the status line at the bottom of the overlay.
    void PaintStatusLine(SDL_Renderer *r, size_t total_nintendo,
                         size_t total_homebrew, size_t total_extras,
                         size_t total_builtins) const;
};

} // namespace ul::menu::qdesktop
