// qd_Launchpad.cpp - Full-screen app-grid overlay for Q OS uMenu (v1.0.0).
// Ported from tools/mock-nro-desktop-gui/src/launchpad.rs (v1.1.0).
//
// Integration note:
//   QdDesktopIconsElement::icons_ is a private member.  This .cpp uses a
//   friend-declaration approach to read icons_ directly.  To enable this, add
//   the following line to qd_DesktopIcons.hpp, inside the
//   QdDesktopIconsElement class declaration (private section):
//
//     friend class QdLaunchpadElement;
//
//   This is the minimal, correct approach: the Launchpad and DesktopIcons are
//   intentionally tightly coupled (Launchpad is a subordinate view of the same
//   data model).  The alternative (adding a public GetIcon(size_t) accessor) is
//   equally valid; in that case replace the direct icons_[] accesses below with
//   calls to that accessor.

#include <ul/menu/qdesktop/qd_Launchpad.hpp>
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>      // Fix D (v1.6.12): LookupFolderIdx, kTopLevelFolders
#include <ul/ul_Result.hpp>                         // UL_LOG_INFO
#include <pu/ui/render/render_Renderer.hpp>          // pu::ui::render::GetMainRenderer
#include <pu/ui/ui_Types.hpp>                        // pu::ui::GetDefaultFont / DefaultFontSize

#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>

// libnx HID constants.
#include <switch.h>

namespace ul::menu::qdesktop {

// ── Constructor ───────────────────────────────────────────────────────────────

QdLaunchpadElement::QdLaunchpadElement(const QdTheme &theme)
    : theme_(theme),
      is_open_(false),
      pending_launch_(false),
      pending_launch_from_mouse_(false),
      desktop_icons_ptr_(nullptr),
      dpad_focus_index_(0),
      mouse_hover_index_(SIZE_MAX),
      filter_dirty_(false),
      frame_tick_(0),
      active_folder_(AutoFolderIdx::None),
      lp_was_touch_active_last_frame_(false),
      icon_cache_(nullptr)
{
    // items_, filtered_idxs_, query_ default-initialise to empty.
    // Texture vectors start empty; slots are pushed in Open().
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdLaunchpadElement::~QdLaunchpadElement() {
    FreeAllTextures();
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdLaunchpadElement::AdvanceTick() {
    ++frame_tick_;
}

// ── Open ─────────────────────────────────────────────────────────────────────
//
// Snapshot the current icon list from QdDesktopIconsElement.  The icons_ array
// is private; this implementation uses the friend declaration described at the
// top of this file.  Sort Application entries alpha-first, NROs alpha-second,
// and Builtins in dock_slot order.

void QdLaunchpadElement::Open(QdDesktopIconsElement *desktop_icons) {
    if (!desktop_icons) {
        return;
    }

    // Free textures from any previous open cycle before overwriting items_.
    FreeAllTextures();
    items_.clear();
    filtered_idxs_.clear();
    query_.clear();
    dpad_focus_index_          = 0;
    mouse_hover_index_         = SIZE_MAX;
    pending_launch_            = false;
    pending_launch_from_mouse_ = false;
    filter_dirty_              = false;
    active_folder_             = AutoFolderIdx::None;  // Fix D (v1.6.12): show all by default
    // v1.7.0-stabilize-2: reset edge-trigger latch so the same finger-down
    // that triggered Open() does not immediately fire the close handler on
    // the very next frame. The latch must be true while a still-down finger
    // is sliding off the corner, then drop to false when the finger lifts.
    lp_was_touch_active_last_frame_ = true;
    desktop_icons_ptr_         = desktop_icons;
    icon_cache_                = &desktop_icons->cache_;

    // Deep-copy every icon entry into items_.
    // Uses the friend-declared access to icons_[] and icon_count_.
    const size_t n = desktop_icons->icon_count_;
    items_.reserve(n);

    for (size_t i = 0u; i < n; ++i) {
        const NroEntry &src = desktop_icons->icons_[i];
        LpItem it;

        // Copy fields with explicit null-termination safety.
        strncpy(it.name,      src.name,      sizeof(it.name)      - 1u);
        it.name[sizeof(it.name) - 1u] = '\0';

        it.glyph  = src.glyph;
        it.bg_r   = src.bg_r;
        it.bg_g   = src.bg_g;
        it.bg_b   = src.bg_b;

        strncpy(it.nro_path,  src.nro_path,  sizeof(it.nro_path)  - 1u);
        it.nro_path[sizeof(it.nro_path) - 1u] = '\0';

        strncpy(it.icon_path, src.icon_path, sizeof(it.icon_path) - 1u);
        it.icon_path[sizeof(it.icon_path) - 1u] = '\0';

        it.app_id       = src.app_id;
        it.is_builtin   = src.is_builtin;
        it.dock_slot    = src.dock_slot;
        it.icon_category = src.icon_category;

        // Map IconCategory to LpSortKind for the grid ordering pass.
        switch (src.icon_category) {
            case IconCategory::Nintendo:  it.sort_kind = LpSortKind::Nintendo;  break;
            case IconCategory::Homebrew:  it.sort_kind = LpSortKind::Homebrew;  break;
            case IconCategory::Extras:    it.sort_kind = LpSortKind::Extras;    break;
            case IconCategory::Payloads:  it.sort_kind = LpSortKind::Extras;    break;
            case IconCategory::Builtin:   it.sort_kind = LpSortKind::Builtin;   break;
        }

        it.desktop_idx = i;  // preserve back-reference for FocusedDesktopIdx()
        items_.push_back(it);
    }

    // Sort: Nintendo (alpha) -> Homebrew (alpha) -> Extras (alpha) ->
    //       Builtin (dock_slot order).
    // std::stable_sort preserves original order within equal-key groups, so
    // builtins retain their dock_slot ordering from the construction pass.
    std::stable_sort(items_.begin(), items_.end(),
        [](const LpItem &a, const LpItem &b) -> bool {
            // Primary: LpSortKind ascending (Nintendo=0, Homebrew=1, Extras=2, Builtin=3).
            if (a.sort_kind != b.sort_kind) {
                return static_cast<u8>(a.sort_kind) < static_cast<u8>(b.sort_kind);
            }
            // Secondary: within Builtin, order by dock_slot.
            if (a.sort_kind == LpSortKind::Builtin) {
                return a.dock_slot < b.dock_slot;
            }
            // Secondary: within Nintendo/Homebrew/Extras, sort alpha (case-insensitive).
            const char *na = a.name;
            const char *nb = b.name;
            while (*na && *nb) {
                const int ca = std::tolower(static_cast<unsigned char>(*na));
                const int cb = std::tolower(static_cast<unsigned char>(*nb));
                if (ca != cb) {
                    return ca < cb;
                }
                ++na; ++nb;
            }
            return *na == '\0' && *nb != '\0';
        }
    );

    // Pre-size per-slot texture vectors to items_.size() with nullptr / false.
    const size_t sz = items_.size();
    name_tex_.assign(sz, nullptr);
    glyph_tex_.assign(sz, nullptr);
    icon_tex_.assign(sz, nullptr);
    icon_loaded_.assign(sz, false);

    // Build the initial (unfiltered) filtered index list.
    RebuildFilter();

    // v1.6.11 Fix 1: pre-warm the icon cache for first-page items before the
    // first frame renders.  Without this, PaintCell() calls icon_cache_->Get()
    // on the very first OnRender tick; the cache is empty so Get() returns
    // nullptr; a neutral-gray tile is drawn permanently because the lazy-load
    // path in QdDesktopIconsElement::OnRender does NOT run while the Launchpad
    // overlay is active (the desktop element is suppressed).
    //
    // Limit to the first page (LP_COLS x visible rows = approximately 40 items
    // for a 1920x1080 layout) so the Open() call does not block noticeably on
    // slow SD cards.  Items beyond the first page are loaded lazily by the
    // Launchpad's own OnRender once the user scrolls.
    //
    // The number of visible rows is derived from the Launchpad geometry:
    //   LP_GRID_Y=144, LP_CELL_H=150, LP_GAP_Y=12; cull threshold ~1032px.
    //   Visible rows = ceil((1032 - LP_GRID_Y) / (LP_CELL_H + LP_GAP_Y)) = 6.
    // First-page item count = LP_COLS * visible_rows = 10 * 6 = 60.
    static constexpr size_t LP_PREWARM_ITEMS = 60u;
    const size_t prewarm_limit = (items_.size() < LP_PREWARM_ITEMS)
                                 ? items_.size()
                                 : LP_PREWARM_ITEMS;

    size_t prewarm_hit = 0u;
    for (size_t i = 0u; i < prewarm_limit; ++i) {
        const LpItem &it = items_[i];

        // NRO-backed entries: load from ASET section.
        if (it.nro_path[0] != '\0') {
            // Cache key mirrors what PaintCell() computes for IconKind::Nro.
            bool loaded = desktop_icons->LoadNroIconToCache(it.nro_path,
                                                            it.nro_path);
            if (loaded) {
                ++prewarm_hit;
            }
            continue;
        }

        // Application entries with a custom icon_path (JPEG on SD):
        // route through LoadJpegIconToCache exactly as OnRender does.
        if (it.icon_path[0] != '\0') {
            bool loaded = desktop_icons->LoadJpegIconToCache(it.icon_path,
                                                              it.icon_path);
            if (loaded) {
                ++prewarm_hit;
            }
            continue;
        }

        // Application entries with empty icon_path require an NS service call
        // (LoadNsIconToCache).  Do NOT block Open() with NS calls; they are
        // deferred to the Launchpad's own OnRender lazy-load path.
    }

    UL_LOG_INFO("qdesktop: Launchpad prewarm: checked=%zu hit=%zu",
                prewarm_limit, prewarm_hit);

    is_open_ = true;

    // Count by category for the log line.
    size_t nintendo_count = 0u, homebrew_count = 0u,
           extras_count = 0u, builtin_count = 0u;
    for (const LpItem &it : items_) {
        switch (it.sort_kind) {
            case LpSortKind::Nintendo:  ++nintendo_count;  break;
            case LpSortKind::Homebrew:  ++homebrew_count;  break;
            case LpSortKind::Extras:    ++extras_count;    break;
            case LpSortKind::Builtin:   ++builtin_count;   break;
        }
    }
    UL_LOG_INFO("qdesktop: Launchpad opened -- nintendo=%zu homebrew=%zu extras=%zu builtins=%zu total=%zu",
                nintendo_count, homebrew_count, extras_count, builtin_count, sz);
}

// ── Close ─────────────────────────────────────────────────────────────────────

void QdLaunchpadElement::Close() {
    // Free every cached SDL texture before clearing items_; the vectors must
    // still be alive while FreeAllTextures walks them.
    FreeAllTextures();

    items_.clear();
    filtered_idxs_.clear();
    query_.clear();
    dpad_focus_index_          = 0;
    mouse_hover_index_         = SIZE_MAX;
    pending_launch_            = false;
    pending_launch_from_mouse_ = false;
    filter_dirty_              = false;
    active_folder_             = AutoFolderIdx::None;  // Fix D (v1.6.12)
    // v1.7.0-stabilize-2: clear edge-trigger latch on close so a re-Open later
    // starts from a known state. The latch is reset to true again at Open()
    // so the still-down finger does not retrigger the close handler.
    lp_was_touch_active_last_frame_ = false;
    icon_cache_                = nullptr;
    desktop_icons_ptr_         = nullptr;
    is_open_                   = false;

    UL_LOG_INFO("qdesktop: Launchpad closed");
}

// ── DispatchPendingLaunch ────────────────────────────────────────────────────
//
// Fires the launch for the currently focused item by forwarding to
// QdDesktopIconsElement::LaunchIcon. The friend declaration on
// QdDesktopIconsElement (see qd_DesktopIcons.hpp) grants access to the private
// LaunchIcon entry point; no public widening of the desktop icons API is
// required.
//
// Safe to call when the Launchpad is closed, when the focused index is
// invalid, or when desktop_icons_ptr_ is null. In any of those cases the
// function is a no-op so the host can call it unconditionally after seeing
// IsPendingLaunch() return true.

void QdLaunchpadElement::DispatchPendingLaunch() {
    if (desktop_icons_ptr_ == nullptr) {
        UL_LOG_WARN("qdesktop: Launchpad DispatchPendingLaunch: desktop_icons_ptr_ is null");
        return;
    }

    // Fix B (v1.6.12): pick the index based on which button triggered the launch.
    size_t idx = SIZE_MAX;
    if (pending_launch_from_mouse_) {
        // ZR launched: resolve mouse_hover_index_ to a desktop_idx.
        if (mouse_hover_index_ < filtered_idxs_.size()) {
            const size_t item_idx = filtered_idxs_[mouse_hover_index_];
            if (item_idx < items_.size()) {
                idx = items_[item_idx].desktop_idx;
            }
        }
    } else {
        // A launched: use the D-pad focused item.
        idx = FocusedDesktopIdx();
    }

    if (idx == SIZE_MAX) {
        UL_LOG_WARN("qdesktop: Launchpad DispatchPendingLaunch: no valid idx"
                    " (from_mouse=%d)", static_cast<int>(pending_launch_from_mouse_));
        return;
    }
    UL_LOG_INFO("qdesktop: Launchpad DispatchPendingLaunch idx=%zu from_mouse=%d",
                idx, static_cast<int>(pending_launch_from_mouse_));
    desktop_icons_ptr_->LaunchIcon(idx);
}

// ── PushQueryChar / PopQueryChar / ClearQuery ─────────────────────────────────

void QdLaunchpadElement::PushQueryChar(char c) {
    query_ += c;
    filter_dirty_ = true;
    // Rebuild now so FilteredCount() is accurate before OnRender.
    RebuildFilter();
    // Clamp focus to the new (possibly shorter) filtered set.
    const size_t n = FilteredCount();
    if (n == 0u) {
        dpad_focus_index_ = 0u;
    } else if (dpad_focus_index_ >= n) {
        dpad_focus_index_ = n - 1u;
    }
}

void QdLaunchpadElement::PopQueryChar() {
    if (!query_.empty()) {
        query_.pop_back();
        filter_dirty_ = true;
        RebuildFilter();
        const size_t n = FilteredCount();
        if (n == 0u) {
            dpad_focus_index_ = 0u;
        } else if (dpad_focus_index_ >= n) {
            dpad_focus_index_ = n - 1u;
        }
    }
}

void QdLaunchpadElement::ClearQuery() {
    query_.clear();
    filter_dirty_ = true;
    RebuildFilter();
    dpad_focus_index_ = 0u;
}

// ── FocusedDesktopIdx ─────────────────────────────────────────────────────────

size_t QdLaunchpadElement::FocusedDesktopIdx() const {
    if (!is_open_ || filtered_idxs_.empty()) {
        return SIZE_MAX;
    }
    if (dpad_focus_index_ >= filtered_idxs_.size()) {
        return SIZE_MAX;
    }
    const size_t item_idx = filtered_idxs_[dpad_focus_index_];
    if (item_idx >= items_.size()) {
        return SIZE_MAX;
    }
    return items_[item_idx].desktop_idx;
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdLaunchpadElement::OnInput(u64 keys_down, u64 /*keys_up*/, u64 /*keys_held*/,
                                  pu::ui::TouchPoint touch_pos)
{
    if (!is_open_) {
        return;
    }

    // Clear the pending-launch flag at the top of each input frame so the host
    // sees a fresh edge-triggered signal from A/ZR.
    pending_launch_ = false;

    // ── B / Plus: close ───────────────────────────────────────────────────────
    if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
        Close();
        SetVisible(false);
        return;
    }

    // ── v1.7.0-stabilize-2: edge-triggered hot-corner CLOSE ──────────────────
    // The hot-corner widget is a 96x72 box at the top-left of the screen
    // (LP_HOTCORNER_W x LP_HOTCORNER_H, defined in qd_Launchpad.hpp). Tapping
    // it from the desktop opens the Launchpad (handled in qd_DesktopIcons.cpp);
    // tapping it from inside the Launchpad must close back to desktop.
    //
    // The handler is edge-triggered: it fires only on the frame where the
    // finger first enters the corner (touch_corner_now && !was_active_last_frame).
    // Without the edge gate, holding the finger inside the corner for several
    // frames would re-fire Close() every frame -- the same level-trigger bug
    // pattern the v2 plan section 2.2.1 describes.
    //
    // The reference implementation for this convention lives in
    // qd_DesktopIcons.cpp around lines 1737-1786 (the touch state machine
    // there uses `was_touch_active_last_frame_` to gate TouchDown vs
    // TouchMove). We mirror that exact pattern here for the close path, with
    // its own per-element latch (`lp_was_touch_active_last_frame_`) so the
    // two state machines do not interfere.
    {
        const bool touch_active_now = (touch_pos.x != 0 || touch_pos.y != 0);
        const s32  tx               = static_cast<s32>(touch_pos.x);
        const s32  ty               = static_cast<s32>(touch_pos.y);
        const bool touch_corner_now = touch_active_now
                                      && tx >= 0 && tx < LP_HOTCORNER_W
                                      && ty >= 0 && ty < LP_HOTCORNER_H;
        const bool touch_corner_edge = touch_corner_now
                                       && !lp_was_touch_active_last_frame_;
        if (touch_corner_edge) {
            UL_LOG_INFO("qdesktop: Launchpad hot-corner CLOSE tap edge tx=%d ty=%d", tx, ty);
            Close();
            SetVisible(false);
            // Latch is cleared by Close(); the next frame will re-arm it on
            // the next finger-down. No further state update needed here.
            return;
        }
        // Update the latch every frame so subsequent Open/Close calls see a
        // consistent edge boundary.
        lp_was_touch_active_last_frame_ = touch_active_now;
    }

    const size_t n = FilteredCount();

    // ── StickL: backspace on query ────────────────────────────────────────────
    if (keys_down & HidNpadButton_StickL) {
        PopQueryChar();
        // After filter change, re-read n for navigation below.
        return;
    }

#if 0  // v1.7.0-stabilize-3: auto-folder tile strip deferred to v1.7.1 K+1 phase 2.
       // Cause: v1.6.12 instability (creator-reported "way too many icons", regressed
       // hot corner / default icons). Re-enable when QdFolderSheet modal lands.
    // ── Fix D (v1.6.12): Touch tap on the auto-folder tile strip ─────────────
    // The folder tile strip occupies a horizontal band starting at:
    //   y = LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6 = 138 px
    //   height = 36 px (FTILE_H from OnRender)
    // "All" tile:  x = LP_SEARCH_BAR_X - 208 .. LP_SEARCH_BAR_X - 9
    //              = 92 .. 291
    // Spec tiles:  starting at LP_SEARCH_BAR_X = 300, 200 px wide, 8 px gap.
    //
    // Touch points are checked against this strip; a valid hit sets
    // active_folder_ and marks filter_dirty_ so RebuildFilter runs next frame.
    {
        // touch_pos.IsEmpty() / valid tap is signalled by keys_down containing
        // the Plutonium touch-tap flag. Use the x/y fields when the point is
        // non-zero (the framework sets {0,0} when there is no active touch).
        const bool has_touch = (touch_pos.x != 0 || touch_pos.y != 0);
        if (has_touch) {
            const s32 tx = static_cast<s32>(touch_pos.x);
            const s32 ty = static_cast<s32>(touch_pos.y);

            // Tile strip geometry (mirrors OnRender step 3.5).
            static constexpr s32 FTILE_W       = 200;
            static constexpr s32 FTILE_H       = 36;
            static constexpr s32 FTILE_GAP     = 8;
            static constexpr s32 FTILE_STRIP_Y = LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6;

            if (ty >= FTILE_STRIP_Y && ty < FTILE_STRIP_Y + FTILE_H) {
                // Check "All" tile: x range [LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP,
                //                             LP_SEARCH_BAR_X - FTILE_GAP)
                const s32 all_tile_x = LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP;
                if (tx >= all_tile_x && tx < all_tile_x + FTILE_W) {
                    if (active_folder_ != AutoFolderIdx::None) {
                        active_folder_ = AutoFolderIdx::None;
                        filter_dirty_  = true;
                    }
                } else {
                    // Walk the spec tiles in the same order as OnRender.
                    // Count bucket occupancy to skip empty tiles (which are not
                    // rendered and thus have no hit area).
                    s32 spec_tile_x = LP_SEARCH_BAR_X;
                    for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
                        // Count items in this bucket (same walk as OnRender 3.5).
                        size_t bucket_count = 0u;
                        for (const LpItem &it : items_) {
                            const std::string sid = StableIdForItem(it);
                            const AutoFolderIdx fidx = LookupFolderIdx(sid);
                            if (fidx == kTopLevelFolders[fi].idx) {
                                ++bucket_count;
                            }
                        }

                        if (bucket_count == 0u) {
                            continue;  // Empty bucket: tile not rendered, skip.
                        }

                        if (tx >= spec_tile_x && tx < spec_tile_x + FTILE_W) {
                            // Hit: set this folder as the active filter.
                            const AutoFolderIdx new_folder = kTopLevelFolders[fi].idx;
                            if (active_folder_ != new_folder) {
                                active_folder_ = new_folder;
                                filter_dirty_  = true;
                                // Clamp D-pad focus to the new filtered set.
                                dpad_focus_index_ = 0u;
                            }
                            break;
                        }
                        spec_tile_x += FTILE_W + FTILE_GAP;
                    }
                }
            }
        }
    }
#endif

    if (n == 0u) {
        return;  // Nothing to navigate.
    }

    // D-pad navigation: clamp at edges, no wrapping, per spec. ──────────────
    if (keys_down & HidNpadButton_Up) {
        if (dpad_focus_index_ >= static_cast<size_t>(LP_COLS)) {
            dpad_focus_index_ -= static_cast<size_t>(LP_COLS);
        } else {
            dpad_focus_index_ = 0u;
        }
    }
    if (keys_down & HidNpadButton_Down) {
        const size_t stepped = dpad_focus_index_ + static_cast<size_t>(LP_COLS);
        dpad_focus_index_ = (stepped < n) ? stepped : (n - 1u);
    }
    if (keys_down & HidNpadButton_Left) {
        if (dpad_focus_index_ > 0u) {
            dpad_focus_index_ -= 1u;
        }
    }
    if (keys_down & HidNpadButton_Right) {
        if (dpad_focus_index_ + 1u < n) {
            dpad_focus_index_ += 1u;
        }
    }

    // Fix B (v1.6.12): A and ZR are independent input sources.
    // A launches the D-pad focused item; ZR launches the mouse-hovered item.
    // pending_launch_from_mouse_ tells DispatchPendingLaunch() which index to use.

    // ── A: launch D-pad focused item ─────────────────────────────────────────
    if (keys_down & HidNpadButton_A) {
        if (dpad_focus_index_ < n) {
            pending_launch_            = true;
            pending_launch_from_mouse_ = false;
        }
    }

    // ── ZR: launch mouse-hovered item ────────────────────────────────────────
    if (keys_down & HidNpadButton_ZR) {
        if (mouse_hover_index_ < n) {
            pending_launch_            = true;
            pending_launch_from_mouse_ = true;
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdLaunchpadElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                   s32 /*x*/, s32 /*y*/)
{
    if (!is_open_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) {
        return;
    }

    // Rebuild the filter if anything changed since the last frame.
    if (filter_dirty_) {
        RebuildFilter();
        filter_dirty_ = false;
    }

    // ── 1. Full-screen opaque background ──────────────────────────────────────
    // topbar_bg = (0x0C, 0x0C, 0x20), matching the Launchpad spec and the
    // Rust paint_launchpad fill_rect call.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,
        theme_.topbar_bg.r,
        theme_.topbar_bg.g,
        theme_.topbar_bg.b,
        0xFFu);
    SDL_Rect full { 0, 0, 1920, 1080 };
    SDL_RenderFillRect(r, &full);

    // ── 2. Hot-corner widget (top-left 60×48 px launcher button) ─────────────
    // Draws a slightly lighter rectangle so the user can see the tap target.
    SDL_SetRenderDrawColor(r,
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.r + 0x18)),
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.g + 0x18)),
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.b + 0x18)),
        0xFFu);
    SDL_Rect hc { 0, 0, LP_HOTCORNER_W, LP_HOTCORNER_H };
    SDL_RenderFillRect(r, &hc);
    // 1px accent border on the right and bottom of the hot-corner.
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_Rect hcbr { LP_HOTCORNER_W - 1, 0, 1, LP_HOTCORNER_H };
    SDL_Rect hcbb { 0, LP_HOTCORNER_H - 1, LP_HOTCORNER_W, 1 };
    SDL_RenderFillRect(r, &hcbr);
    SDL_RenderFillRect(r, &hcbb);
    // Render "Q" glyph inside hot-corner to indicate Launchpad.
    {
        static SDL_Texture *hc_tex = nullptr;
        if (!hc_tex) {
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            hc_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "Q", wh);
        }
        if (hc_tex) {
            int tw = 0, th = 0;
            SDL_QueryTexture(hc_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect td { (LP_HOTCORNER_W - tw) / 2, (LP_HOTCORNER_H - th) / 2, tw, th };
            SDL_RenderCopy(r, hc_tex, nullptr, &td);
        }
    }

    // ── 3. Search bar ─────────────────────────────────────────────────────────
    // Background: surface_glass (0x12, 0x12, 0x2A) with 80% alpha.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r,
        theme_.surface_glass.g,
        theme_.surface_glass.b,
        0xCCu);
    SDL_Rect search_bg { LP_SEARCH_BAR_X, LP_SEARCH_BAR_Y,
                         LP_SEARCH_BAR_W, LP_SEARCH_BAR_H };
    SDL_RenderFillRect(r, &search_bg);

    // 1px accent border around the search bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_Rect search_ring { LP_SEARCH_BAR_X - 1, LP_SEARCH_BAR_Y - 1,
                           LP_SEARCH_BAR_W + 2, LP_SEARCH_BAR_H + 2 };
    SDL_RenderDrawRect(r, &search_ring);

    // Render search query text + blinking caret.
    {
        std::string display_text = query_;
        // Caret blinks at 30-frame phase: visible when (frame_tick_ / 30) % 2 == 0.
        const bool caret_visible = ((frame_tick_ / 30) % 2) == 0;
        if (caret_visible) {
            display_text += '|';
        }

        if (!display_text.empty()) {
            // Re-render every frame (query text can change).
            const pu::ui::Color tc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            SDL_Texture *qt = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                display_text, tc,
                static_cast<u32>(LP_SEARCH_BAR_W - 16));
            if (qt) {
                int tw = 0, th = 0;
                SDL_QueryTexture(qt, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, qt, nullptr, &td);
                SDL_DestroyTexture(qt);
            }
        } else if (caret_visible) {
            // Empty query: render just the caret at the left edge of the bar.
            const pu::ui::Color tc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            SDL_Texture *ct = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "|", tc);
            if (ct) {
                int tw = 0, th = 0;
                SDL_QueryTexture(ct, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, ct, nullptr, &td);
                SDL_DestroyTexture(ct);
            }
        }

        // Placeholder hint when query is empty and caret is hidden.
        if (query_.empty() && !caret_visible) {
            const pu::ui::Color hint_col { 0x88u, 0x88u, 0xAAu, 0xFFu };
            SDL_Texture *ht = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "Search...", hint_col);
            if (ht) {
                int tw = 0, th = 0;
                SDL_QueryTexture(ht, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, ht, nullptr, &td);
                SDL_DestroyTexture(ht);
            }
        }
    }

#if 0  // v1.7.0-stabilize-3: auto-folder tile strip deferred to v1.7.1 K+1 phase 2.
       // Cause: v1.6.12 instability (creator-reported "way too many icons", regressed
       // hot corner / default icons). Re-enable when QdFolderSheet modal lands.
    // ── 3.5. Fix D (v1.6.12): Auto-folder tile strip ─────────────────────────
    // Render up to kTopLevelFolderCount tiles in a horizontal strip between the
    // search bar and the icon grid.  Only tiles for non-empty buckets are drawn;
    // the active bucket (active_folder_) gets an accent border.
    // Tile geometry: strip top = LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6 px gap.
    // Each tile: 200 px wide, 36 px tall, 8 px horizontal gap between tiles.
    // The strip is left-aligned at LP_SEARCH_BAR_X so it aligns with the search bar.
    {
        // Count items per AutoFolderIdx bucket (walk all items, not filtered).
        size_t bucket_count[kTopLevelFolderCount] = {};
        for (const LpItem &it : items_) {
            const std::string sid = StableIdForItem(it);
            const AutoFolderIdx fidx = LookupFolderIdx(sid);
            const u8 raw = static_cast<u8>(fidx);
            if (raw >= 1u && raw <= static_cast<u8>(kTopLevelFolderCount)) {
                bucket_count[raw - 1u] += 1u;  // kTopLevelFolders[0] = NxGames (idx=1)
            }
        }

        static constexpr s32 FTILE_W       = 200;
        static constexpr s32 FTILE_H       = 36;
        static constexpr s32 FTILE_GAP     = 8;
        static constexpr s32 FTILE_STRIP_Y = LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6;

        s32 tile_x = LP_SEARCH_BAR_X;
        for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
            if (bucket_count[fi] == 0u) {
                continue;  // Skip empty buckets -- no tile rendered.
            }
            const TopLevelFolderSpec &spec = kTopLevelFolders[fi];
            const bool is_active = (active_folder_ == spec.idx);
            PaintFolderTile(r, tile_x, FTILE_STRIP_Y, FTILE_W, FTILE_H,
                            spec.display_name, bucket_count[fi], is_active);
            tile_x += FTILE_W + FTILE_GAP;
        }

        // "All" tile: always first; shows all items when active_folder_ == None.
        // Rendered to the LEFT of the spec-based tiles -- insert before the loop.
        // (Re-render: clear what we drew above, prepend "All" tile, re-emit in order.)
        // Simpler approach: render "All" tile at a fixed position 208 px before LP_SEARCH_BAR_X.
        // LP_SEARCH_BAR_X = 300; LP_SEARCH_BAR_X - 208 = 92; safe for 1920-width overlay.
        {
            const bool all_active = (active_folder_ == AutoFolderIdx::None);
            PaintFolderTile(r,
                            LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP,
                            FTILE_STRIP_Y,
                            FTILE_W, FTILE_H,
                            "All",
                            items_.size(),
                            all_active);
        }
    }
#endif

    // ── 4. Section headers ────────────────────────────────────────────────────
    // Walk the filtered list and emit a section label wherever LpSortKind
    // transitions.  The first row header sits at y = LP_GRID_Y - 24 above the
    // first cell row; subsequent headers are emitted inline (painted in empty
    // band above each new section row).
    //
    // We collect the unique set of kinds present in the filtered list, then
    // render a header label 24 px above the top of the first cell for that
    // kind.  The section header for the first kind is always at LP_GRID_Y - 24.
    {
        LpSortKind last_section = static_cast<LpSortKind>(0xFFu); // sentinel
        const size_t nf = filtered_idxs_.size();
        for (size_t vpos = 0u; vpos < nf; ++vpos) {
            const size_t item_idx = filtered_idxs_[vpos];
            if (item_idx >= items_.size()) continue;
            const LpItem &it = items_[item_idx];
            if (it.sort_kind == last_section) continue;

            last_section = it.sort_kind;

            // Compute the cell row for this visual position.
            s32 cx = 0, cy = 0;
            CellXY(vpos, cx, cy);
            const s32 header_y = cy - 28; // 28 px above the cell top

            const char *label = SectionLabel(it.sort_kind);
            const pu::ui::Color sc { theme_.text_secondary.r, theme_.text_secondary.g,
                                     theme_.text_secondary.b, 0xFFu };
            SDL_Texture *st = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                label, sc);
            if (st) {
                int sw = 0, sh = 0;
                SDL_QueryTexture(st, nullptr, nullptr, &sw, &sh);
                SDL_Rect sd { LP_GRID_X, header_y, sw, sh };
                SDL_RenderCopy(r, st, nullptr, &sd);
                SDL_DestroyTexture(st);
            }
        }
    }

    // ── 5. Icon grid ──────────────────────────────────────────────────────────
    const size_t nf = filtered_idxs_.size();
    for (size_t vpos = 0u; vpos < nf; ++vpos) {
        const size_t item_idx = filtered_idxs_[vpos];
        if (item_idx >= items_.size()) { continue; }

        s32 cx = 0, cy = 0;
        CellXY(vpos, cx, cy);

        // Cull cells that would render below the status line (1080 - 48 = 1032).
        if (cy + LP_CELL_H > 1032) { continue; }

        // Fix B (v1.6.12): highlight if D-pad focused OR mouse-hovered.
        const bool cell_highlighted = (vpos == dpad_focus_index_)
                                   || (vpos == mouse_hover_index_);
        PaintCell(r, items_[item_idx], item_idx, cx, cy, cell_highlighted);
    }

    // ── 6. Status line ────────────────────────────────────────────────────────
    size_t nintendo_count = 0u, homebrew_count = 0u,
           extras_count = 0u, builtin_count = 0u;
    for (const LpItem &it : items_) {
        switch (it.sort_kind) {
            case LpSortKind::Nintendo:  ++nintendo_count;  break;
            case LpSortKind::Homebrew:  ++homebrew_count;  break;
            case LpSortKind::Extras:    ++extras_count;    break;
            case LpSortKind::Builtin:   ++builtin_count;   break;
        }
    }
    PaintStatusLine(r, nintendo_count, homebrew_count, extras_count, builtin_count);
}

// ── StableIdForItem ───────────────────────────────────────────────────────────
// Fix D (v1.6.12): reconstruct the stable ID string for an LpItem.
//
// This mirrors the four registration forms in qd_DesktopIcons.cpp exactly:
//   Builtin    -> "builtin:<name>"            (is_builtin == true)
//   Application -> "app:<16 lowercase hex>"   (app_id != 0, !is_builtin)
//   Payload    -> "payload:<basename>"         (icon_category == Payloads)
//   NRO        -> nro_path verbatim            (fallthrough)
//
// The function may NOT extend LpItem. All fields used here already exist in
// the struct (app_id, is_builtin, icon_category, nro_path, name).

// static
std::string QdLaunchpadElement::StableIdForItem(const LpItem &item)
{
    // Builtin entries are identified by the is_builtin flag set during Open().
    if (item.is_builtin) {
        std::string sid;
        sid.reserve(8u + strnlen(item.name, sizeof(item.name)));
        sid = "builtin:";
        sid += item.name;
        return sid;
    }

    // Application entries carry a non-zero app_id (Nintendo title ID).
    if (item.app_id != 0u) {
        char hex[17];
        snprintf(hex, sizeof(hex), "%016lx", static_cast<unsigned long>(item.app_id));
        std::string sid;
        sid.reserve(4u + 16u);
        sid = "app:";
        sid += hex;
        return sid;
    }

    // Payload entries have icon_category == IconCategory::Payloads.
    // The stable ID uses the basename of nro_path (the .bin filename).
    if (item.icon_category == IconCategory::Payloads) {
        // Find the last '/' in nro_path to extract the basename.
        const char *p = item.nro_path;
        const char *slash = nullptr;
        for (const char *q = p; *q != '\0'; ++q) {
            if (*q == '/') {
                slash = q;
            }
        }
        const char *base = (slash != nullptr) ? (slash + 1) : p;
        std::string sid;
        sid.reserve(8u + strnlen(base, sizeof(item.nro_path)));
        sid = "payload:";
        sid += base;
        return sid;
    }

    // Plain NRO: stable ID is the full nro_path verbatim.
    return std::string(item.nro_path);
}

// ── PaintFolderTile ───────────────────────────────────────────────────────────
// Fix D (v1.6.12): render one auto-folder tile at screen rect (tx, ty, tw, th).
//
// Visual design:
//   Background  : surface_glass at 80% alpha; brightened by 0x18 when is_active.
//   Border      : 1px accent-colour ring when is_active; 1px dim ring otherwise.
//   Label text  : display_name label left-padded 6 px; vertically centred.
//   Count badge : small count "(N)" right of label, slightly dimmer colour.
//
// Blend mode on entry is unspecified; this function sets its own blend mode for
// each draw call and does not restore the prior state (callers in OnRender do
// not rely on a particular mode after PaintFolderTile returns).

void QdLaunchpadElement::PaintFolderTile(SDL_Renderer *r,
                                          s32 tx, s32 ty,
                                          s32 tile_w, s32 tile_h,
                                          const char *label,
                                          size_t item_count,
                                          bool is_active) const
{
    // ── Background fill ───────────────────────────────────────────────────────
    const u8 bg_r_base = theme_.surface_glass.r;
    const u8 bg_g_base = theme_.surface_glass.g;
    const u8 bg_b_base = theme_.surface_glass.b;

    const u8 bg_r = is_active
        ? static_cast<u8>(std::min(255, (int)bg_r_base + 0x18))
        : bg_r_base;
    const u8 bg_g = is_active
        ? static_cast<u8>(std::min(255, (int)bg_g_base + 0x18))
        : bg_g_base;
    const u8 bg_b = is_active
        ? static_cast<u8>(std::min(255, (int)bg_b_base + 0x18))
        : bg_b_base;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, bg_r, bg_g, bg_b, 0xCCu);  // 80% alpha
    SDL_Rect bg_rect { tx, ty, tile_w, tile_h };
    SDL_RenderFillRect(r, &bg_rect);

    // ── Border ────────────────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (is_active) {
        SDL_SetRenderDrawColor(r,
            theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    } else {
        // Dim ring: text_secondary colour at 60% opacity.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r,
            theme_.text_secondary.r,
            theme_.text_secondary.g,
            theme_.text_secondary.b,
            0x99u);  // ~60%
    }
    SDL_Rect border { tx, ty, tile_w, tile_h };
    SDL_RenderDrawRect(r, &border);

    // ── Label text ────────────────────────────────────────────────────────────
    // Build "<label> (N)" string. Max label buffer: 64 + 12 = 76 chars.
    char label_buf[80];
    snprintf(label_buf, sizeof(label_buf), "%s (%zu)", label, item_count);

    const pu::ui::Color text_col =
        is_active
            ? pu::ui::Color{ 0xFFu, 0xFFu, 0xFFu, 0xFFu }
            : pu::ui::Color{ theme_.text_secondary.r,
                             theme_.text_secondary.g,
                             theme_.text_secondary.b,
                             0xFFu };

    SDL_Texture *lt = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        label_buf, text_col,
        static_cast<u32>(tile_w - 12));  // wrap at tile width - 6px left + 6px right pad

    if (lt) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(lt, nullptr, nullptr, &lw, &lh);
        const s32 lx = tx + 6;
        const s32 ly = ty + (tile_h - lh) / 2;
        SDL_Rect ldst { lx, ly, lw, lh };
        SDL_RenderCopy(r, lt, nullptr, &ldst);
        SDL_DestroyTexture(lt);
    }
}

// ── RebuildFilter ─────────────────────────────────────────────────────────────

void QdLaunchpadElement::RebuildFilter() {
    filtered_idxs_.clear();

    // Fix D (v1.6.12): build the query-lowercased string once, used below.
    // If query is empty AND no folder filter is active, fast-path all items.
    const bool folder_filter = (active_folder_ != AutoFolderIdx::None);

    if (query_.empty() && !folder_filter) {
        // No query, no folder filter: all items visible.
        filtered_idxs_.reserve(items_.size());
        for (size_t i = 0u; i < items_.size(); ++i) {
            filtered_idxs_.push_back(i);
        }
        filter_dirty_ = false;
        return;
    }

    // Prepare lowercased query (may be empty when only folder filter is active).
    char q_lower[64] = {};
    size_t qlen = 0u;
    if (!query_.empty()) {
        qlen = std::min(query_.size(), sizeof(q_lower) - 1u);
        for (size_t i = 0u; i < qlen; ++i) {
            q_lower[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(query_[i])));
        }
        q_lower[qlen] = '\0';
    }

    for (size_t i = 0u; i < items_.size(); ++i) {
        const LpItem &it = items_[i];

        // Fix D (v1.6.12): apply folder filter first (cheapest check).
        if (folder_filter) {
            const std::string sid = StableIdForItem(it);
            const AutoFolderIdx fidx = LookupFolderIdx(sid);
            if (fidx != active_folder_) {
                continue;  // Item belongs to a different bucket; exclude it.
            }
        }

        // Apply text query filter (when query is non-empty).
        if (qlen > 0u) {
            const char *name = it.name;
            char name_lower[64];
            const size_t nlen = std::min(strnlen(name, sizeof(it.name)),
                                         sizeof(name_lower) - 1u);
            for (size_t j = 0u; j < nlen; ++j) {
                name_lower[j] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(name[j])));
            }
            name_lower[nlen] = '\0';

            if (strstr(name_lower, q_lower) == nullptr) {
                continue;  // Does not match query; exclude.
            }
        }

        filtered_idxs_.push_back(i);
    }

    filter_dirty_ = false;
}

// ── FilteredCount ─────────────────────────────────────────────────────────────

size_t QdLaunchpadElement::FilteredCount() const {
    return filtered_idxs_.size();
}

// ── CellXY ────────────────────────────────────────────────────────────────────
// Compute the top-left screen pixel of the grid cell at visual position vpos.
// Matches lp_cell_xy() from launchpad.rs (scaled ×1.5 to 1920×1080).
//
//   col = vpos % LP_COLS
//   row = vpos / LP_COLS
//   x   = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X)
//   y   = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y)

// static
void QdLaunchpadElement::CellXY(size_t vpos, s32 &out_x, s32 &out_y) {
    const s32 col = static_cast<s32>(vpos % static_cast<size_t>(LP_COLS));
    const s32 row = static_cast<s32>(vpos / static_cast<size_t>(LP_COLS));
    out_x = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X);
    out_y = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y);
}

// ── PaintCell ────────────────────────────────────────────────────────────────
// Paints one grid cell at (cx, cy) using the same pattern as
// QdDesktopIconsElement::PaintIconCell, adapted for the Launchpad's square
// LP_ICON_W × LP_ICON_H icon art area.
//
// Layout within the LP_CELL_W × LP_CELL_H cell:
//   icon art rect: (cx + (LP_CELL_W - LP_ICON_W)/2, cy, LP_ICON_W, LP_ICON_H)
//   name label:    centred horizontally, 4 px below icon art bottom.
//   focus ring:    1px ring 1px outside the icon art rect.

void QdLaunchpadElement::PaintCell(SDL_Renderer *r,
                                    const LpItem &item,
                                    size_t item_idx,
                                    s32 cx, s32 cy,
                                    bool is_focused)
{
    // Centre the icon art horizontally within the cell.
    const s32 icon_x = cx + (LP_CELL_W - LP_ICON_W) / 2;
    const s32 icon_y = cy;

    // ── 1. Background fill ────────────────────────────────────────────────────
    const u8 fill_r = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_r + 40))
        : item.bg_r;
    const u8 fill_g = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_g + 40))
        : item.bg_g;
    const u8 fill_b = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_b + 40))
        : item.bg_b;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, fill_r, fill_g, fill_b, 0xFFu);
    SDL_Rect bg_rect { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
    SDL_RenderFillRect(r, &bg_rect);

    // ── 2. Icon texture ───────────────────────────────────────────────────────
    // Determine the cache key (same selection logic as DesktopIcons).
    const char *cache_key = nullptr;
    if (item.icon_path[0] != '\0') {
        cache_key = item.icon_path;
    } else if (item.nro_path[0] != '\0') {
        cache_key = item.nro_path;
    }

    const u8 *bgra = nullptr;
    if (cache_key && icon_cache_) {
        bgra = icon_cache_->Get(cache_key);
    }

    if (bgra != nullptr && item_idx < icon_tex_.size()) {
        // Lazily create the icon texture for this slot.
        if (!icon_loaded_[item_idx] || icon_tex_[item_idx] == nullptr) {
            if (icon_tex_[item_idx] != nullptr) {
                SDL_DestroyTexture(icon_tex_[item_idx]);
                icon_tex_[item_idx] = nullptr;
            }
            icon_tex_[item_idx] = SDL_CreateTexture(
                r, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,
                static_cast<int>(CACHE_ICON_W),
                static_cast<int>(CACHE_ICON_H));
            if (icon_tex_[item_idx] != nullptr) {
                SDL_UpdateTexture(icon_tex_[item_idx], nullptr, bgra,
                                  static_cast<int>(CACHE_ICON_W) * 4);
            }
            icon_loaded_[item_idx] = true;
        }
        if (icon_tex_[item_idx] != nullptr) {
            SDL_Rect dst { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
            SDL_RenderCopy(r, icon_tex_[item_idx], nullptr, &dst);
        }
    }

    // ── 3. Glyph fallback (when no icon art) ─────────────────────────────────
    if (bgra == nullptr && item.glyph != '\0' && item_idx < glyph_tex_.size()) {
        if (glyph_tex_[item_idx] == nullptr) {
            const std::string gs(1, item.glyph);
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            glyph_tex_[item_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                gs, wh);
        }
        if (glyph_tex_[item_idx] != nullptr) {
            int gw = 0, gh = 0;
            SDL_QueryTexture(glyph_tex_[item_idx], nullptr, nullptr, &gw, &gh);
            SDL_Rect gdst {
                icon_x + (LP_ICON_W - gw) / 2,
                icon_y + (LP_ICON_H - gh) / 2,
                gw, gh
            };
            SDL_RenderCopy(r, glyph_tex_[item_idx], nullptr, &gdst);
        }
    }

    // ── 4. Name label ─────────────────────────────────────────────────────────
    if (item.name[0] != '\0' && item_idx < name_tex_.size()) {
        if (name_tex_[item_idx] == nullptr) {
            // Truncate long names with ellipsis (max 14 chars visible).
            char display[20];
            const size_t name_len = strnlen(item.name, sizeof(item.name));
            if (name_len > 14u) {
                memcpy(display, item.name, 11u);
                display[11] = '.'; display[12] = '.'; display[13] = '.';
                display[14] = '\0';
            } else {
                memcpy(display, item.name, name_len);
                display[name_len] = '\0';
            }
            const pu::ui::Color nc { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            name_tex_[item_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(display), nc,
                static_cast<u32>(LP_CELL_W));
        }
        if (name_tex_[item_idx] != nullptr) {
            int nw = 0, nh = 0;
            SDL_QueryTexture(name_tex_[item_idx], nullptr, nullptr, &nw, &nh);
            SDL_Rect ndst {
                cx + (LP_CELL_W - nw) / 2,
                icon_y + LP_ICON_H + 4,
                nw, nh
            };
            SDL_RenderCopy(r, name_tex_[item_idx], nullptr, &ndst);
        }
    }

    // ── 5. Focus ring ─────────────────────────────────────────────────────────
    if (is_focused) {
        SDL_SetRenderDrawColor(r,
            theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0xFFu);
        SDL_Rect ring { icon_x - 2, icon_y - 2, LP_ICON_W + 4, LP_ICON_H + 4 };
        SDL_RenderDrawRect(r, &ring);
        // Second ring (thicker visual) one pixel inside.
        SDL_Rect ring2 { icon_x - 1, icon_y - 1, LP_ICON_W + 2, LP_ICON_H + 2 };
        SDL_RenderDrawRect(r, &ring2);
    }
}

// ── FreeSlotTextures ──────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeSlotTextures(size_t item_idx) {
    auto free_if = [](SDL_Texture *&t) {
        if (t != nullptr) {
            SDL_DestroyTexture(t);
            t = nullptr;
        }
    };
    if (item_idx < name_tex_.size())   { free_if(name_tex_[item_idx]);  }
    if (item_idx < glyph_tex_.size())  { free_if(glyph_tex_[item_idx]); }
    if (item_idx < icon_tex_.size())   { free_if(icon_tex_[item_idx]);  }
    if (item_idx < icon_loaded_.size()) { icon_loaded_[item_idx] = false; }
}

// ── FreeAllTextures ───────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeAllTextures() {
    for (size_t i = 0u; i < name_tex_.size(); ++i)  {
        if (name_tex_[i])  { SDL_DestroyTexture(name_tex_[i]);  name_tex_[i]  = nullptr; }
    }
    for (size_t i = 0u; i < glyph_tex_.size(); ++i) {
        if (glyph_tex_[i]) { SDL_DestroyTexture(glyph_tex_[i]); glyph_tex_[i] = nullptr; }
    }
    for (size_t i = 0u; i < icon_tex_.size(); ++i)  {
        if (icon_tex_[i])  { SDL_DestroyTexture(icon_tex_[i]);  icon_tex_[i]  = nullptr; }
    }
    name_tex_.clear();
    glyph_tex_.clear();
    icon_tex_.clear();
    icon_loaded_.clear();
}

// ── SectionLabel ─────────────────────────────────────────────────────────────

// static
const char *QdLaunchpadElement::SectionLabel(LpSortKind kind) {
    switch (kind) {
        case LpSortKind::Nintendo:  return "Nintendo";
        case LpSortKind::Homebrew:  return "Homebrew";
        case LpSortKind::Extras:    return "Extras";
        case LpSortKind::Builtin:   return "Built-in";
    }
    return "Other";
}

// ── PaintStatusLine ───────────────────────────────────────────────────────────
// Renders a status string at the bottom of the overlay (y ~= 1048).
// Format: "N nintendo  N homebrew  N extras  N built-in  |  B to close"

void QdLaunchpadElement::PaintStatusLine(SDL_Renderer *r,
                                          size_t total_nintendo,
                                          size_t total_homebrew,
                                          size_t total_extras,
                                          size_t total_builtins) const
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%zu nintendo  %zu homebrew  %zu extras  %zu built-in  |  B to close",
             total_nintendo, total_homebrew, total_extras, total_builtins);

    const pu::ui::Color sc { theme_.text_secondary.r, theme_.text_secondary.g,
                              theme_.text_secondary.b, 0xFFu };
    SDL_Texture *st = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        buf, sc);
    if (st) {
        int sw = 0, sh = 0;
        SDL_QueryTexture(st, nullptr, nullptr, &sw, &sh);
        // Centre horizontally; 8 px above the bottom edge.
        const s32 sx = (1920 - sw) / 2;
        const s32 sy = 1080 - sh - 8;
        SDL_Rect sd { sx, sy, sw, sh };
        SDL_RenderCopy(r, st, nullptr, &sd);
        SDL_DestroyTexture(st);
    }
}

} // namespace ul::menu::qdesktop
