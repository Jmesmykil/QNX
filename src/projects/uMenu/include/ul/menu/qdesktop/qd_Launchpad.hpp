// qd_Launchpad.hpp - Full-screen app-grid overlay for Q OS uMenu (v1.0.0).
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
// operate on this private snapshot; the desktop array is not read again until
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
// fully implemented; they simply have no caller yet.
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
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>     // Fix D (v1.6.12): auto-folder buckets
#include <ul/menu/qdesktop/qd_FolderClassifier.hpp> // v1.9: 9-bucket classifier + LaunchpadTab types
#include <string>
#include <vector>
#include <cstddef>
#include <atomic>   // v1.8.23 Option C: lp_prewarm_stop_ flag (mirrors qd_DesktopIcons.hpp)
#include <thread>   // v1.8.23 Option C: lp_prewarm_thread_ background prewarm

namespace ul::menu::qdesktop {

// ── Layout constants (×1.5 from Rust 1280×720) ───────────────────────────────
// Rust LP_COLS=10 unchanged: 10 columns at 1920 px.
// Rust LP_CELL_W=104 → C++: 156.
// Rust LP_CELL_H=100 → C++: 150.
// Rust LP_GAP_X=8    → C++: 12.
// Rust LP_GAP_Y=8    → C++: 12.
// Rust LP_GRID_X=40  → C++: 60.
// Rust LP_GRID_Y=140 → C++: 192 (search 84, search_h 48 → strip 138..174 → grid below).
// v1.7.0-stabilize-7.1 hotfix: LP_GRID_Y bumped from 144 to 192 so the icon
// grid clears the auto-folder filter strip ("tabs") at y=138..174.  Pre-fix,
// LP_GRID_Y=144 placed cells 30 px INSIDE the strip; All / SystemUtil / etc.
// filter tiles overlapped the first row of icons (creator HW report).  Cull
// at y=1032 still leaves 5 rows visible (row 4 bottom = 990 < 1032), so
// D-1 (v1.7.2.2): LP_ROWS = 5 → 4 (LP_ITEMS_PER_PAGE auto-derives 50 → 40).
//   Resolves the LP_ITEMS_PER_PAGE=50 vs MAX_ICONS=48 mismatch flagged by
//   OPT-2 B1: with page=50 and content cap=48, page 1 was permanently
//   unreachable and the pagination loop allocated dead LpItem structs every
//   Open() call.  Shrinking the page (40) lets the 48-item content cap
//   trigger a meaningful 2-page split (40 + 8) — L/R page nav becomes
//   reachable, page indicator dots fire correctly (MNR §28).
//   MAX_ICONS=48 is preserved (touching it would resize 5+ desktop-side
//   std::array<...> members in qd_DesktopIcons.hpp; out of scope here).
// Search bar and hot-corner are spec-defined values from the task.

constexpr s32 LP_COLS          = 10;
// D-1 (v1.7.2.2): explicit row count so Items-Per-Page is a constant, not
// derived at runtime.  4 rows × 10 cols = 40 items per page.
// Derivation (post-stabilize-7.1, post-D-1): usable height = 1080 - LP_GRID_Y(192) -
// status_line(40) = 848 px.  Each row = LP_CELL_H(150) + LP_GAP_Y(12) = 162 px.
// 4 × 162 = 648 px used; 848 - 648 = 200 px clearance below row 4 bottom
// (y = 192 + 4*162 = 840) before the page-indicator dot row at y≈1040.
// Row 3 (last in grid) bottom = 192 + 3*162 + 150 = 828 → fully visible.
constexpr s32 LP_ROWS          = 4;
constexpr size_t LP_ITEMS_PER_PAGE = static_cast<size_t>(LP_COLS * LP_ROWS);  // 40
constexpr s32 LP_CELL_W        = 156;   // icon + label column width
constexpr s32 LP_CELL_H        = 150;   // icon + label row height (incl. label)
constexpr s32 LP_GAP_X         = 12;
constexpr s32 LP_GAP_Y         = 12;
constexpr s32 LP_GRID_X        = 60;    // safe-area left margin
constexpr s32 LP_GRID_Y        = 192;   // below auto-folder strip (138 + 36 + 18 clearance)
constexpr s32 LP_SEARCH_BAR_X  = 300;
constexpr s32 LP_SEARCH_BAR_Y  = 84;
constexpr s32 LP_SEARCH_BAR_W  = 1320;
constexpr s32 LP_SEARCH_BAR_H  = 48;
// v1.7.0-stabilize-2: hot-corner geometry widened from 60x48 to 96x72.
// Switch Erista capacitive screen has a ~20-30 px corner dead zone that
// suppresses touch events along the extreme edges; the widened box pushes the
// active hit area inward enough to be reliably tappable. The widget is shared
// between qd_DesktopIcons (open Launchpad) and qd_Launchpad (close Launchpad)
// so a single SSOT here drives both render and input sites.
constexpr s32 LP_HOTCORNER_W   = 96;    // top-left widget width (was 60)
constexpr s32 LP_HOTCORNER_H   = 72;    // top-left widget height (was 48 = TOPBAR_H)

// Icon art dimensions within each grid cell.
// Matches PaintIconCell pattern from qd_DesktopIcons.cpp (bg rect proportions).
constexpr s32 LP_ICON_W  = 104;  // art width  (slightly smaller than cell; centred)
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

// ── v1.7.0-stabilize-2: LpItem struct-size pin (A7 + A8) ──────────────────
// Companion to the NroEntry pin in qd_DesktopIcons.hpp. LpItem is the
// snapshot copy created by Launchpad::Open() and traversed by every render
// frame; like NroEntry, growing this struct will silently shift the layout
// the cache traversal depends on (vector<LpItem>, indexed by size_t).
//
// LpItem is NOT a libnx IPC structure (it lives entirely inside
// QdLaunchpadElement), so the failure mode is not a crash -- it would be
// a heap-allocator pressure spike on Open() and a per-render cache stride
// regression. Pin it so any unintentional growth surfaces at compile time.
//
// Computed value: 1632 bytes on aarch64 ARM64 with GCC's standard layout
// (same probe as NroEntry; see qd_DesktopIcons.hpp comment).
static_assert(sizeof(LpItem) == 1632,
              "LpItem size shifted -- v1.7.0 cache-stride regression risk; "
              "use a side table for new state, do not extend the struct");
static_assert(alignof(LpItem) == 8,
              "LpItem alignment shifted -- vector<LpItem> stride assumption violated");

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
    // PushQueryChar(); the OSK integration is the next batch.  The functions
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

    // Returns true when the user pressed A (D-pad focus) or ZR (mouse hover)
    // on a valid item this input frame.  The host calls DispatchPendingLaunch()
    // to actually fire the launch through the desktop icons element, then
    // Close().  Cleared on the next call to OnInput.
    bool IsPendingLaunch() const { return pending_launch_; }

    // Fix B (v1.6.12): Forwards to desktop_icons_ptr_->LaunchIcon() using the
    // appropriate index:
    //   - A button pressed -> uses dpad_focus_index_ (FocusedDesktopIdx())
    //   - ZR button pressed -> uses mouse_hover_index_
    // No-op when desktop_icons_ptr_ is null, the chosen index is out of range,
    // or the Launchpad is closed.
    void DispatchPendingLaunch();

    // v1.9.7: Returns a stable pointer to the search-bar focus flag so the
    // hot-corner overlay can poll it directly each frame without a per-frame
    // function call overhead.  The pointer is valid for the lifetime of this
    // element (it addresses a member variable, not heap storage).
    const bool *GetSearchActivePtr() const { return &search_focus_active_; }

    // ── Frame tick ───────────────────────────────────────────────────────────
    // Advance the caret blink counter.  Call once per frame.
    void AdvanceTick();

private:
    // ── State ─────────────────────────────────────────────────────────────────
    QdTheme                 theme_;
    bool                    is_open_;
    // B68 (v1.8.27): idempotent Close() guard. True after first Close(); reset
    // to false in Open() so each Open/Close cycle arms fresh. Object is reused.
    bool                    closed_;
    bool                    pending_launch_;
    // Fix B (v1.6.12): distinguishes A (D-pad launch) from ZR (mouse launch)
    // so DispatchPendingLaunch can pick the correct index without changing the
    // host-facing IsPendingLaunch()/DispatchPendingLaunch() interface.
    bool                    pending_launch_from_mouse_;

    // Non-owning pointer set by Open(); cleared on Close(). Used by
    // DispatchPendingLaunch to fire LaunchIcon on the focused entry.
    // Lifetime obligation lives on the caller of Open.
    QdDesktopIconsElement  *desktop_icons_ptr_;

    // Snapshot of all items, sorted as described in LpSortKind.
    std::vector<LpItem>     items_;

    // Fix B (v1.6.12): input separation. D-pad and ZR/mouse are independent.
    // dpad_focus_index_: navigated by Up/Down/Left/Right + launched by A button.
    // mouse_hover_index_: driven by ZR/cursor position + launched by ZR button.
    // Both index into the *filtered* item list (filtered_idxs_).
    size_t                  dpad_focus_index_;
    size_t                  mouse_hover_index_;

    // Current ASCII search query.
    std::string             query_;

    // Cached filtered index list, rebuilt when query_ or items_ changes.
    // Each entry is an index into items_.
    std::vector<size_t>     filtered_idxs_;

    // True when filtered_idxs_ needs to be rebuilt before next render.
    bool                    filter_dirty_;

    // Monotonic frame counter for the search-bar caret blink (30-frame phase).
    s32                     frame_tick_;

    // Fix D (v1.6.12): active auto-folder filter.
    // AutoFolderIdx::None = show all items (default).
    // Any other value = show only items whose stable ID maps to that folder bucket.
    // Set by tapping a folder tile; cleared on Close() and on re-Open().
    AutoFolderIdx           active_folder_;

    // v1.9: active tab strip state (replaces single active_folder_ for new 9-bucket system).
    // active_tab_kind_: Favorites tab or Folder tab (from qd_FolderClassifier.hpp).
    // active_folder_fi_: which FolderIdx bucket is selected when kind==Folder.
    // active_tabs_: list of rendered tab descriptors; built once in Open() from
    //   QdFolderClassifier::Get().BucketCount() so empty buckets are skipped.
    // All three are initialised in Open() and cleared in Close().
    LaunchpadTabKind            active_tab_kind_;
    FolderIdx                   active_folder_fi_;
    std::vector<LaunchpadTab>   active_tabs_;

    // v1.7.0-stabilize-2: edge-trigger state for the hot-corner CLOSE handler.
    // Mirrors `was_touch_active_last_frame_` in QdDesktopIconsElement -- both
    // sides use the same convention so a single tap fires open OR close exactly
    // once per finger-down. Initialized to false so the first frame after
    // Open() does not immediately self-close on a still-down finger.
    bool                    lp_was_touch_active_last_frame_;

    // v1.8 Input-source latch: mirrors QdDesktopIconsElement::active_input_source_.
    // Launchpad maintains its own copy so OnInput can make source-sensitive
    // decisions (e.g., A-press launches dpad-focused tile in DPAD mode or
    // cursor-hovered tile in MOUSE mode) without touching the desktop element.
    //
    // Transition rules inside Launchpad:
    //   D-pad Up/Down/Left/Right pressed → InputSource::DPAD
    //   Touch tap OR mouse button (ZR) pressed → InputSource::MOUSE
    //   A/B/X/Y/L/R/ZL/ZR do NOT change source on their own (A defers to
    //   the current source to choose which index to launch).
    //
    // Initialized to DPAD (Switch is a gamepad-first device; no cursor on
    // Launchpad open by default).
    InputSource             active_input_source_;

    // v1.8.29 Slice 1: tab-strip D-pad focus.
    // SIZE_MAX = D-pad focus is in the grid (normal mode).
    // 0        = "All" tab focused.
    // 1..N     = the N-th non-empty category tile focused (fi-th rendered tile,
    //            counting only buckets where folder_bucket_count_[fi] > 0).
    // Transitions:
    //   D-pad UP from grid row 0  → enter tab mode; set to index matching active_folder_
    //   D-pad LEFT/RIGHT in tab   → cycle among visible tab indices
    //   D-pad DOWN in tab         → return to grid (dpad_focus_index_ = 0), SIZE_MAX
    //   A in tab mode             → activate filter, return to grid, SIZE_MAX
    //   B/Plus                    → close (unchanged)
    // Reset to SIZE_MAX in Open() and Close().
    size_t                  tab_focus_idx_;

    // v1.8.33: search-bar focus state.  Three focus zones now: search bar
    // (top), tab strip (middle), grid (bottom).
    //   D-pad UP from tab focus    → enter search-bar focus
    //   D-pad DOWN from search     → return to tab focus
    //   A in search focus          → open swkbd, write result to query
    //   B in search focus          → exit search focus to tab focus
    bool                    search_focus_active_;

    // F10 (stabilize-5): pagination state.
    // page_index_: 0-based current page (0 = first page).
    // page_count_: total pages = ceil(filtered_idxs_.size() / LP_ITEMS_PER_PAGE).
    // Both are recalculated whenever filtered_idxs_ is rebuilt (RebuildFilter).
    // dpad_focus_index_ is always relative to the FILTERED list (global), not the
    // current page; CellXY is called with (vpos - page_start) so the focused cell
    // scrolls onto the screen when the page changes.
    size_t                  page_index_;
    size_t                  page_count_;

    // Per-slot icon textures (NOT text — text is rendered per-frame locally).
    // Max LP items == MAX_ICONS == 48; vector index mirrors items_ index.
    std::vector<SDL_Texture *> icon_tex_;
    // True when the icon cache has been checked for this slot.
    std::vector<bool>          icon_loaded_;
    // v1.8.22f: per-slot one-shot diagnostic log guard.
    // v1.8.23 Option C: removed — diagnostic served its purpose; v1.8.22d
    // 2a-romfs branch state is already proven by cumulative HW logs.

    // ── v1.8.24 F-1: per-slot name + glyph texture cache ─────────────────────
    // Eliminates ~80 RenderText calls/frame for cell names and ~80 for glyphs.
    // Parallel to icon_tex_/icon_loaded_; indexed by items_ index (NOT filtered
    // index).  nullptr = not yet rendered; non-null = reuse.  Freed via
    // DeleteTexture (cache-contract) in FreeSlotTextures / FreeAllTextures.
    // Invalidated in RebuildFilter when name or glyph changes (items_ rebuild
    // from scratch on each Open so names cannot drift within a session; if the
    // filter changes the item set the texture slots for items still present are
    // still valid — only items added mid-session via items_.push_back would need
    // explicit invalidation, which cannot happen during an Open/Close cycle).
    std::vector<SDL_Texture *> name_tex_;
    std::vector<SDL_Texture *> glyph_tex_;

    // ── v1.8.24 F-2: O(1) status bar counters ────────────────────────────────
    // Maintained in RebuildFilter(); read by OnRender status line — no per-frame
    // walk of items_.  Index maps to LpSortKind enum values:
    //   [0] = Nintendo, [1] = Homebrew, [2] = Extras, [3] = Builtin.
    u32 status_counts_[4];

    // ── v1.8.24 F-3: search bar texture cache keyed by (query, caret phase) ──
    // Avoids RenderText every frame for the search bar text + caret.  Recomputed
    // only when query_ changes or caret phase flips (~every 30 frames).
    SDL_Texture *search_bar_tex_;
    std::string  search_bar_cached_text_;   // the display_text that produced search_bar_tex_
    bool         search_bar_caret_visible_; // caret phase at last recompute

    // ── v1.8.24 F-4: Q glyph cached at Open() ────────────────────────────────
    // Rendered once in Open(); PaintHotCornerGlyph reads it every frame.
    // Freed in Close() and ~QdLaunchpadElement before FreeAllTextures.
    SDL_Texture *q_glyph_tex_;

    // v1.8.18: icon_cache_ pointer removed.  PaintCell calls GetSharedIconCache()
    // directly so Desktop and Launchpad always share the same QdIconCache object
    // regardless of lifetime or initialisation order.

    // A-4 (v1.7.2): per-bucket item counts for the auto-folder tile strip.
    // Populated once in RebuildFilter() so OnRender() does not re-walk items_
    // every frame.  Index mirrors kTopLevelFolders[]: bucket_count_[fi] holds
    // the number of items whose StableId maps to kTopLevelFolders[fi].idx.
    size_t                     folder_bucket_count_[kTopLevelFolderCount];

    // ── v1.8.23 Option C: background Launchpad prewarm threading ──────────────
    // Mirrors the v1.8.15 desktop-prewarm pattern in qd_DesktopIcons.hpp:
    // synchronous prewarm in Open() blocked the user with a frozen menu for
    // ~2000 ms (HW evidence).  Now the prewarm body runs on a background
    // std::thread spawned at the end of Open() and reaped in Close()/destructor
    // before any member destruction.  Stop polled per-iteration so join() is
    // prompt.  Cache writes inside the prewarm helpers are serialised through
    // GetSharedIconCacheMutex() (same mutex the desktop prewarm thread uses).
    std::atomic<bool>          lp_prewarm_stop_{false};
    std::thread                lp_prewarm_thread_;

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

    // Fix D (v1.6.12): reconstruct the stable ID string for an LpItem so that
    // LookupFolderIdx() can be called without extending LpItem (struct extension
    // corrupted the libnx IPC command table in v1.6.10 and is permanently banned).
    // Stable ID format mirrors the four forms registered in qd_DesktopIcons.cpp:
    //   Builtin    -> "builtin:<name>"
    //   Application -> "app:<hex16>"
    //   Payload    -> "payload:<basename(nro_path)>"
    //   NRO        -> nro_path verbatim
    static std::string StableIdForItem(const LpItem &item);

    // Fix D (v1.6.12): paint one auto-folder tile at screen position (tx, ty).
    // tile_w / tile_h define the tile bounding rectangle.
    // is_active: true when this folder is the currently active filter; the tile
    // receives an accent border and a brightened background in that state.
    void PaintFolderTile(SDL_Renderer *r,
                         s32 tx, s32 ty, s32 tile_w, s32 tile_h,
                         const char *label,
                         size_t item_count,
                         bool is_active) const;

    // Paint the status line at the bottom of the overlay.
    void PaintStatusLine(SDL_Renderer *r, size_t total_nintendo,
                         size_t total_homebrew, size_t total_extras,
                         size_t total_builtins) const;

    // F10 (stabilize-5): paint page indicator dots when page_count_ > 1.
    // Renders a row of small circles centred horizontally above the status line.
    // The active page's dot is rendered bright; inactive dots are dim.
    void PaintPageDots(SDL_Renderer *r) const;

    // ── v1.8.23 Option C: background prewarm helpers ─────────────────────────
    // Runs the cache prewarm loop on the background thread.  Body relocated
    // from QdLaunchpadElement::Open() (was inline at qd_Launchpad.cpp ~247-329
    // in v1.8.22).  Polls lp_prewarm_stop_ between each item so the destructor
    // / Close() join completes promptly.  Reads items_, icon_tex_, and
    // desktop_icons_ptr_ — must be reaped before any of those are mutated.
    void PrewarmLaunchpadIcons();

    // Spawn the background prewarm thread.  Idempotent — guarded by
    // lp_prewarm_thread_.joinable() so a duplicate call is a no-op.  Resets
    // the stop flag to false before launching.  Called from the END of Open()
    // after items_ + filtered_idxs_ + RebuildFilter() are complete, so the
    // thread sees a fully-built snapshot.
    void SpawnLpPrewarmThread();

    // Set the stop flag and join the thread if it is running.  Idempotent.
    // Called FIRST in Open() (before items_.clear() / FreeAllTextures), in
    // Close() (before FreeAllTextures), and in the destructor (before any
    // member destruction).  Mirrors QdDesktopIconsElement::StopPrewarmThread.
    void StopLpPrewarmThread();
};

} // namespace ul::menu::qdesktop
