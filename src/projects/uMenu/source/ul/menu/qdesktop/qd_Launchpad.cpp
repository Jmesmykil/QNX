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
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>             // v2.6.0 — g_QdTheme accent for hot-corner Q glyph tint
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>      // Fix D (v1.6.12): LookupFolderIdx, kTopLevelFolders
#include <ul/menu/qdesktop/qd_LaunchpadHostLayout.hpp> // v1.8.1: SFX dispatch via LP_PLAY_SFX
#include <ul/menu/ui/ui_MenuApplication.hpp>         // F3 (stabilize-4): g_MenuApplication + MenuType
#include <ul/menu/ui/ui_Common.hpp>                  // F9 (stabilize-5): TryFindLoadImage for Builtin icons
#include <ul/ul_Result.hpp>                         // UL_LOG_INFO
#include <pu/ui/render/render_Renderer.hpp>          // pu::ui::render::GetMainRenderer
#include <pu/ui/ui_Types.hpp>                        // pu::ui::GetDefaultFont / DefaultFontSize
// QoL-T2/T3: context menu + visibility + folder-classifier includes are in qd_Launchpad.hpp

#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <mutex>  // v1.8.18: std::lock_guard for GetSharedIconCacheMutex() in PaintCell
#include <cctype>

// libnx HID constants.
#include <switch.h>

// F3 (stabilize-4): MenuApplication global — defined in ui_MenuApplication.cpp.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

// v1.8.1 Task D: SFX dispatch helper.
// NOTE: codebase compiles with -fno-rtti, so dynamic_pointer_cast is rejected.
// We use static_pointer_cast — this call path (qd_Launchpad OnInput) only
// executes when QdLaunchpadHostLayout is the active layout, so the static cast
// is safe. Each method guard-checks its handle before calling pu::audio::PlaySfx,
// so a NULL handle (asset absent / LoadSfx not yet called) produces no crash.
#define LP_PLAY_SFX(method_name) \
    do { \
        auto _lp_base = g_MenuApplication->GetLayout<ul::menu::ui::IMenuLayout>(); \
        auto _lp_host = std::static_pointer_cast<ul::menu::qdesktop::QdLaunchpadHostLayout>(_lp_base); \
        if(_lp_host) { _lp_host->method_name(); } \
    } while(0)

namespace ul::menu::qdesktop {

// v1.7.0-stabilize-7 Slice 5 (O-F Patch 2): forward declarations for the
// favorites shims defined in qd_DesktopIcons.cpp.  The shims accept an LpItem
// by reference and reconstruct the stable id using is_builtin / app_id /
// nro_path / name fields. Defined where they can access the file-scope
// FavoriteEntry / g_favorites_* state.
bool ToggleFavoriteByLpItem(const LpItem &item);
bool IsFavoriteByLpItem(const LpItem &item);

// ── v3.7 unified selection ring (SELECTION-SPEC) ──────────────────────────────
// One rounded, theme-consistent focus ring drawn in g_QdTheme.focus_ring with a
// cheap 2-pass soft glow.  Replaces the old hard 2×1px double rectangles (and
// the lone hardcoded #0080AA search-bar inner ring).  It paints over arbitrary
// backgrounds (wallpaper / tiles), so it must be a TRUE hollow ring, not a
// filled rect.

// 1px rounded-rectangle OUTLINE: four straight edges + four quarter-circle
// corner arcs (plotted via the circle equation).  Honors col.a (blended).
static void DrawRoundedRectOutline(SDL_Renderer *r, int x, int y, int w, int h,
                                   int rad, pu::ui::Color col) {
    if (w < 2 || h < 2) return;
    if (rad < 1) rad = 1;
    if (rad > w / 2) rad = w / 2;
    if (rad > h / 2) rad = h / 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    const int x1 = x, y1 = y, x2 = x + w - 1, y2 = y + h - 1;
    SDL_RenderDrawLine(r, x1 + rad, y1,       x2 - rad, y1);        // top
    SDL_RenderDrawLine(r, x1 + rad, y2,       x2 - rad, y2);        // bottom
    SDL_RenderDrawLine(r, x1,       y1 + rad, x1,       y2 - rad);  // left
    SDL_RenderDrawLine(r, x2,       y1 + rad, x2,       y2 - rad);  // right
    for (int dy = 0; dy <= rad; ++dy) {
        const int dx = static_cast<int>(
            __builtin_sqrtf(static_cast<float>(rad * rad - dy * dy)) + 0.5f);
        SDL_RenderDrawPoint(r, x1 + rad - dx, y1 + rad - dy);  // TL
        SDL_RenderDrawPoint(r, x2 - rad + dx, y1 + rad - dy);  // TR
        SDL_RenderDrawPoint(r, x1 + rad - dx, y2 - rad + dy);  // BL
        SDL_RenderDrawPoint(r, x2 - rad + dx, y2 - rad + dy);  // BR
    }
}

// The unified focus ring: 2-pass soft glow (focus_ring @ 0x40 then 0x20, one
// expanding rounded outline each) + a solid `thickness`-px rounded ring
// (concentric 1px outlines).  (x,y,w,h) is the ring's outer box.
static void DrawFocusRing(SDL_Renderer *r, int x, int y, int w, int h,
                          int rad, int thickness, pu::ui::Color fr) {
    DrawRoundedRectOutline(r, x - 1, y - 1, w + 2, h + 2, rad + 1,
                           pu::ui::Color(fr.r, fr.g, fr.b, 0x40u));
    DrawRoundedRectOutline(r, x - 2, y - 2, w + 4, h + 4, rad + 2,
                           pu::ui::Color(fr.r, fr.g, fr.b, 0x20u));
    for (int i = 0; i < thickness; ++i) {
        DrawRoundedRectOutline(r, x + i, y + i, w - 2 * i, h - 2 * i,
                               rad - i, pu::ui::Color(fr.r, fr.g, fr.b, 0xFFu));
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

QdLaunchpadElement::QdLaunchpadElement(const QdTheme &theme)
    : theme_(theme),
      is_open_(false),
      closed_(false),  // B68 (v1.8.27): idempotent Close() guard; false = not yet closed.
      pending_launch_(false),
      pending_launch_from_mouse_(false),
      desktop_icons_ptr_(nullptr),
      dpad_focus_index_(0),
      mouse_hover_index_(SIZE_MAX),
      filter_dirty_(false),
      frame_tick_(0),
      active_folder_(AutoFolderIdx::None),
      lp_was_touch_active_last_frame_(false),
      // v1.8 Input-source latch: Launchpad opens in DPAD mode on Switch (the
      // user triggers Open via hot-corner tap or Plus — both are controller/touch
      // actions; D-pad or touch-tile navigation follows immediately).
      active_input_source_(InputSource::DPAD),
      // v1.8.29 Slice 1: tab focus; SIZE_MAX = grid focus (normal mode).
      tab_focus_idx_(SIZE_MAX),
      // v1.8.33: search-bar focus; false = not in search focus mode.
      search_focus_active_(false),
      page_index_(0),    // W11-SCROLL: scroll-step tracker (was pagination page)
      page_count_(1),    // W11-SCROLL: total scroll steps (was pagination count)
      // v1.8.18: icon_cache_ removed; using GetSharedIconCache() singleton.
      // v1.8.24 F-2: status-bar counters; zero-init; populated in RebuildFilter().
      status_counts_{},
      // v1.8.24 F-3: search bar texture cache; null until first render in Open().
      search_bar_tex_(nullptr),
      search_bar_cached_text_(),
      search_bar_caret_visible_(false),
      // v1.8.24 F-4: hot-corner Q glyph; rendered once in Open(), freed in Close()/dtor.
      q_glyph_tex_(nullptr),
      folder_bucket_count_{}  // A-4 (v1.7.2): zero-init; populated in RebuildFilter()
{
    // items_, filtered_idxs_, query_ default-initialise to empty.
    // Texture vectors start empty; slots are pushed in Open().
    // name_tex_, glyph_tex_ start empty; slots are pushed in Open() (F-1).
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdLaunchpadElement::~QdLaunchpadElement() {
    // v1.8.23 Option C: stop+join the background prewarm thread BEFORE any
    // member destruction.  Mirrors ~QdDesktopIconsElement (qd_DesktopIcons.cpp
    // ~1362) — the thread holds `this` and reads items_/icon_tex_ via
    // PrewarmLaunchpadIcons(), so it must release before those vectors are
    // freed below.  StopLpPrewarmThread is idempotent (joinable() guard).
    StopLpPrewarmThread();
    // v1.8.24 F-3/F-4: free per-Launchpad-session cached textures BEFORE
    // FreeAllTextures(), which frees icon_tex_ / name_tex_ / glyph_tex_ vectors.
    // These two are scalars — not in the vectors — so they need explicit release.
    if (search_bar_tex_) {
        pu::ui::render::DeleteTexture(search_bar_tex_);
        search_bar_tex_ = nullptr;
    }
    if (q_glyph_tex_) {
        pu::ui::render::DeleteTexture(q_glyph_tex_);
        q_glyph_tex_ = nullptr;
    }
    // v3.7: Q image is TryFindLoadImage-owned → raw SDL_DestroyTexture.
    if (q_glyph_img_) {
        SDL_DestroyTexture(q_glyph_img_);
        q_glyph_img_ = nullptr;
    }
    // v2.0.3-A5: status_line_tex_ + star_tex_ are RenderText-LRU-owned —
    // CRITICAL: use pu::ui::render::DeleteTexture (NOT raw SDL_DestroyTexture)
    // per AGENT-REGRESSION-RISK §4 rule 2 (P-B cluster, 7 incidents).
    if (status_line_tex_) {
        pu::ui::render::DeleteTexture(status_line_tex_);
        status_line_tex_ = nullptr;
    }
    if (star_tex_) {
        pu::ui::render::DeleteTexture(star_tex_);
        star_tex_ = nullptr;
    }
    FreeAllTextures();
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdLaunchpadElement::AdvanceTick() {
    ++frame_tick_;
}

// ── SetFolderFilter ───────────────────────────────────────────────────────────

void QdLaunchpadElement::SetFolderFilter(AutoFolderIdx filter) {
    if (active_folder_ == filter) {
        return;  // already set — avoid spurious RebuildFilter
    }
    active_folder_ = filter;
    filter_dirty_  = true;
    if (is_open_) {
        RebuildFilter();
        filter_dirty_ = false;
        // W11-SCROLL: reset scroll to top on filter change.
        lp_scroll_y_  = 0;
        page_index_   = 0;
        if (lp_scroll_cb_) {
            lp_scroll_cb_(0);
        }
    }
}

// ── v1.8.23 Option C: PrewarmLaunchpadIcons ──────────────────────────────────
//
// Background-thread body for the Launchpad icon-cache prewarm.  Replaces the
// synchronous loop that previously ran inside Open() (qd_Launchpad.cpp ~247-329
// in v1.8.22).  Mirrors qd_DesktopIcons.cpp::PrewarmAllIcons threading
// contract:
//
//   - Reads items_ and desktop_icons_ptr_, both populated by Open() before
//     this thread is spawned (SpawnLpPrewarmThread runs after RebuildFilter).
//   - Polls lp_prewarm_stop_ at the top of each iteration so Close()/dtor
//     join() completes promptly when the user navigates away mid-prewarm.
//   - Cache writes inside the load helpers are serialised by
//     GetSharedIconCacheMutex() (the same mutex the desktop prewarm thread
//     uses), so concurrent prewarm threads do not race on cache state.
//   - Does NOT mutate g_has_no_asset_ (the negative-cache memoization
//     remains owned exclusively by qd_DesktopIcons.cpp::PrewarmAllIcons).
//
// First-page prewarm window (LP_PREWARM_ITEMS = 60) chosen for Switch's slow
// SD-card I/O budget; entries beyond the first page load lazily in PaintCell
// once the user scrolls.
void QdLaunchpadElement::PrewarmLaunchpadIcons() {
    QdDesktopIconsElement *desktop_icons = desktop_icons_ptr_;
    if (!desktop_icons) {
        return;
    }

    static constexpr size_t LP_PREWARM_ITEMS = 60u;
    const size_t prewarm_limit = (items_.size() < LP_PREWARM_ITEMS)
                                 ? items_.size()
                                 : LP_PREWARM_ITEMS;

    size_t prewarm_hit = 0u;
    for (size_t i = 0u; i < prewarm_limit; ++i) {
        // v1.8.23 Option C: per-iteration stop poll (mirrors qd_DesktopIcons.cpp
        // PrewarmAllIcons :2619-2623) so Close()/dtor join() returns promptly.
        if (lp_prewarm_stop_.load(std::memory_order_relaxed)) {
            UL_LOG_INFO("qdesktop: Launchpad prewarm: stopped early at entry %zu/%zu",
                        i, prewarm_limit);
            return;
        }

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
        // F2b (stabilize-6 / O-C): if icon_path is a pre-written NS cache key
        // ("app:%016llx" — written by SetApplicationEntries per F5), the
        // disk-read path expects a real file, so route through the
        // shipped-icon dual-fallback first; on miss, leave the slot empty
        // and let the OnRender path retry.
        if (it.icon_path[0] != '\0') {
            // v1.8.22d B66: mirror v1.8.21 desktop romfs:/ skip — Launchpad's
            // parallel prewarm path was missed by v1.8.21. LoadJpegIconToCache
            // opens fsdevGetDeviceFileSystem("sdmc") and fails rc=0x2EEA02
            // (FS module 2 / desc 6004 = path-not-found) for romfs:/... paths,
            // then writes a gray-fallback BGRA into the shared cache keyed by
            // the romfs path. PaintCell later reads the gray instead of the
            // themed PNG. Skip prewarm entirely; PaintCell's section 2a-romfs
            // branch (added below) does the real load via LoadImageFromFile.
            if (it.icon_path[0] == 'r' && it.icon_path[1] == 'o' &&
                it.icon_path[2] == 'm' && it.icon_path[3] == 'f' &&
                it.icon_path[4] == 's' && it.icon_path[5] == ':') {
                continue;
            }
            const bool has_ns_key =
                (it.icon_path[0] == 'a' &&
                 it.icon_path[1] == 'p' &&
                 it.icon_path[2] == 'p' &&
                 it.icon_path[3] == ':');
            if (has_ns_key && it.app_id != 0) {
                bool loaded = desktop_icons->LoadAppIconFromUSystemCache(
                    it.app_id, it.icon_path);
                if (loaded) {
                    ++prewarm_hit;
                }
                // No NS fallback in prewarm — NS calls are deferred to
                // OnRender lazy-load to avoid blocking the main thread on
                // sysmodule IPC (matches the empty-icon_path branch below).
                continue;
            }
            bool loaded = desktop_icons->LoadJpegIconToCache(it.icon_path,
                                                              it.icon_path);
            if (loaded) {
                ++prewarm_hit;
            }
            continue;
        }

        // Application entries with empty icon_path: try the shipped-icon
        // dual-fallback first (stat-based, fast), then defer to OnRender
        // lazy-load if both disk paths miss.
        // F2b (stabilize-6 / O-C): same dual-fallback as the icon_path
        // branch above; we synthesise the "app:%016llx" cache key here so
        // PaintCell's lookup matches.
        if (it.app_id != 0) {
            char app_cache_key[32];
            snprintf(app_cache_key, sizeof(app_cache_key),
                     "app:%016llx",
                     static_cast<unsigned long long>(it.app_id));
            bool loaded = desktop_icons->LoadAppIconFromUSystemCache(
                it.app_id, app_cache_key);
            if (loaded) {
                ++prewarm_hit;
            }
            // NS calls remain deferred to OnRender (do not block the prewarm
            // thread on sysmodule IPC).
        }
    }

    UL_LOG_INFO("qdesktop: Launchpad prewarm (bg thread): checked=%zu hit=%zu",
                prewarm_limit, prewarm_hit);
}

// ── v1.8.23 Option C: SpawnLpPrewarmThread ───────────────────────────────────
//
// Launches PrewarmLaunchpadIcons() on a dedicated std::thread.  Mirrors
// qd_DesktopIcons.cpp::SpawnPrewarmThread (~2736).  Idempotent — duplicate
// calls return immediately via the joinable() guard.  Resets the stop flag to
// false before launching so a prior Open()/Close() cycle's stop signal does
// not poison the new thread.
void QdLaunchpadElement::SpawnLpPrewarmThread() {
    if (lp_prewarm_thread_.joinable()) {
        return;
    }
    lp_prewarm_stop_.store(false, std::memory_order_relaxed);
    lp_prewarm_thread_ = std::thread([this]() {
        PrewarmLaunchpadIcons();
    });
}

// ── v1.8.23 Option C: StopLpPrewarmThread ────────────────────────────────────
//
// Sets the stop flag and joins the prewarm thread.  Idempotent — joinable()
// guard makes this a no-op if no thread is running.  Mirrors
// QdDesktopIconsElement::StopPrewarmThread (qd_DesktopIcons.hpp inline ~317).
//
// The atomic write uses release ordering so the prewarm body's relaxed
// reads observe the flag in a finite number of iterations; combined with
// join(), this guarantees the thread has released `this` before any caller
// proceeds to free items_, icon_tex_, or desktop_icons_ptr_.
void QdLaunchpadElement::StopLpPrewarmThread() {
    lp_prewarm_stop_.store(true, std::memory_order_release);
    if (lp_prewarm_thread_.joinable()) {
        lp_prewarm_thread_.join();
    }
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

    // v1.8.23 Option C: reap any background prewarm thread from a prior
    // Open()/Close() cycle BEFORE we mutate items_ / icon_tex_ / icon_loaded_.
    // The background lambda captures `this` and reads items_; if a previous
    // thread is still alive when we clear() below, it would see torn data.
    // StopLpPrewarmThread is idempotent (joinable() guard).
    StopLpPrewarmThread();

    // Free textures from any previous open cycle before overwriting items_.
    FreeAllTextures();
    items_.clear();
    filtered_idxs_.clear();
    query_.clear();
    dpad_focus_index_          = 0;
    mouse_hover_index_         = SIZE_MAX;
    tab_focus_idx_             = SIZE_MAX;  // v1.8.29 Slice 1: grid focus on open
    search_focus_active_       = false;     // v1.8.33: not in search focus on open
    pending_launch_            = false;
    pending_launch_from_mouse_ = false;
    filter_dirty_              = false;
    active_folder_             = AutoFolderIdx::None;  // Fix D (v1.6.12): show all by default
    // v1.7.0-stabilize-7 Slice 4 (O-B Phase 3): consume any pending pre-filter
    // set by a desktop folder tap. ConsumePendingLaunchpadFolder() is a single
    // u8 side-table read+reset; it does NOT clear filter state for the next
    // Open call when the desktop didn't request a pre-filter (then the call
    // returns AutoFolderIdx::None and is a no-op).
    {
        const AutoFolderIdx pending =
            QdDesktopIconsElement::ConsumePendingLaunchpadFolder();
        if (pending != AutoFolderIdx::None) {
            active_folder_ = pending;
            filter_dirty_  = true;
            UL_LOG_INFO("qdesktop: Launchpad Open consumed pending folder=%u",
                        static_cast<unsigned>(pending));
        }
    }
    page_index_                = 0;  // W11-SCROLL: reset scroll-step tracker
    page_count_                = 1;  // W11-SCROLL: recalculated in RebuildFilter
    lp_scroll_y_               = 0;  // W11-SCROLL: reset internal fullscreen scroll
    lp_natural_h_dirty_        = false;
    // v1.7.0-stabilize-2: reset edge-trigger latch so the same finger-down
    // that triggered Open() does not immediately fire the close handler on
    // the very next frame. The latch must be true while a still-down finger
    // is sliding off the corner, then drop to false when the finger lifts.
    //
    // v2.0.3.8: in windowed mode the hot-corner CLOSE handler is suppressed
    // (qd_Launchpad.cpp:705 gate), so the protective latch=true is unnecessary
    // — and harmful.  QdWindow only dispatches OnInput when there IS a touch;
    // during no-touch frames the latch never gets updated.  If Open() set it
    // to true, the user's later "tap an icon" sees lp_was_touch_prev=true and
    // is_touch_tile_edge evaluates to false, so the icon-grid hit-test never
    // fires and the tap is silently dropped.  In windowed mode we initialise
    // the latch to false so the first tap-on-icon properly registers as an
    // edge.
    lp_was_touch_active_last_frame_ = !windowed_mode_;
    // W3 Bug L2: reset long-press / swipe / touch-down trackers so the new
    // session starts cold (otherwise a stale touchdown vpos from the previous
    // open cycle could fire on the first lift inside this one).
    lp_long_press_start_tick_  = 0;
    lp_long_press_start_x_     = -1;
    lp_long_press_start_y_     = -1;
    lp_long_press_fired_       = false;
    lp_swipe_start_x_          = -1;
    lp_swipe_start_y_          = -1;
    lp_swipe_fired_            = false;
    lp_touch_down_vpos_        = SIZE_MAX;
    // W12-FIX Bug 1: reset drag-scroll state so an in-progress drag from the
    // previous open cycle cannot carry over into the new session.
    lp_drag_scroll_origin_y_      = 0;
    lp_drag_scroll_origin_offset_ = 0;
    lp_drag_scroll_engaged_       = false;
    desktop_icons_ptr_         = desktop_icons;
    // v1.8.18: icon_cache_ pointer removed; PaintCell uses GetSharedIconCache() directly.

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
        // v1.8.10: Payloads removed from IconCategory; payloads now enter as Extras.
        switch (src.icon_category) {
            case IconCategory::Nintendo:  it.sort_kind = LpSortKind::Nintendo;  break;
            case IconCategory::Homebrew:  it.sort_kind = LpSortKind::Homebrew;  break;
            case IconCategory::Extras:    it.sort_kind = LpSortKind::Extras;    break;
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

    // Pre-size per-slot icon texture vectors to items_.size() with nullptr / false.
    const size_t sz = items_.size();
    icon_tex_.assign(sz, nullptr);
    icon_loaded_.assign(sz, false);
    // v1.8.23 Option C: paint_logged_ removed (diagnostic served its purpose).

    // v1.8.24 F-1: pre-size name/glyph texture vectors parallel to icon_tex_.
    // Textures are rendered on-demand in PaintCell() and retained until
    // FreeSlotTextures() / FreeAllTextures() is called.
    name_tex_.assign(sz, nullptr);
    glyph_tex_.assign(sz, nullptr);
    // v2.0.3-A4: parallel dim caches; zero-initialised in lockstep.
    name_tex_w_.assign(sz, 0);
    name_tex_h_.assign(sz, 0);
    glyph_tex_w_.assign(sz, 0);
    glyph_tex_h_.assign(sz, 0);

    // v1.8.24 F-3: reset search bar cache on every Open() so stale textures from
    // a previous open cycle are not reused (query_ was cleared above).
    if (search_bar_tex_) {
        pu::ui::render::DeleteTexture(search_bar_tex_);
        search_bar_tex_ = nullptr;
    }
    search_bar_cached_text_.clear();
    search_bar_caret_visible_ = false;

    // A5-OPT-1: build the stable-ID cache AFTER the sort is final so the index
    // mapping is stable for the entire Open/Close cycle.  RebuildFilter() reads
    // this vector instead of calling StableIdForItem() per-item per-call.
    items_stable_ids_.clear();
    items_stable_ids_.reserve(sz);
    for (size_t i = 0u; i < sz; ++i) {
        items_stable_ids_.push_back(StableIdForItem(items_[i]));
    }

    // Build the initial (unfiltered) filtered index list.
    // NOTE: RebuildFilter() also populates status_counts_[] (F-2).
    RebuildFilter();

    // v1.8.24 F-4: render the hot-corner "Q" glyph once, reuse each frame.
    // The SDL renderer is available by the time Open() runs (Plutonium is live).
    // Free any prior texture from a previous open cycle (idempotent guard above).
    if (q_glyph_tex_) {
        pu::ui::render::DeleteTexture(q_glyph_tex_);
        q_glyph_tex_ = nullptr;
        q_glyph_tex_w_ = 0;
        q_glyph_tex_h_ = 0;
    }
    // v3.7: free the prior-cycle Q image (TryFindLoadImage-owned → SDL_DestroyTexture).
    if (q_glyph_img_) {
        SDL_DestroyTexture(q_glyph_img_);
        q_glyph_img_ = nullptr;
    }
    {
        const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        q_glyph_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            "Q", wh);
        // v2.0.3-A4: cache dims once at create — texture size immutable.
        if (q_glyph_tex_) {
            SDL_QueryTexture(q_glyph_tex_, nullptr, nullptr,
                             &q_glyph_tex_w_, &q_glyph_tex_h_);
        }
        // v3.7: per-theme Q image — preferred over the text "Q" when present.
        // Same asset + lookup path the desktop hot-corner widget uses, so the
        // start-menu Q is visually identical and re-themes on each Open().
        q_glyph_img_ = ::ul::menu::ui::TryFindLoadImage("ui/Main/EntryIcon/HotCornerQ");
    }

    // v2.0.3-A5: render the launchpad star "★" once at Open(); blit per-frame
    // in PaintCell.  Mirrors qd_DesktopIcons.cpp:2787 pattern.  Free any
    // prior-cycle texture first.  RenderText-LRU-owned → DeleteTexture.
    if (star_tex_) {
        pu::ui::render::DeleteTexture(star_tex_);
        star_tex_ = nullptr;
        star_tex_w_ = 0;
        star_tex_h_ = 0;
    }
    {
        const pu::ui::Color star_white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        star_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            "\xe2\x98\x85", star_white);  // U+2605 BLACK STAR
        if (star_tex_) {
            SDL_QueryTexture(star_tex_, nullptr, nullptr, &star_tex_w_, &star_tex_h_);
        }
    }
    // v2.0.3-A5: drop any stale status-line cache from a prior open cycle.
    if (status_line_tex_) {
        pu::ui::render::DeleteTexture(status_line_tex_);
        status_line_tex_ = nullptr;
        status_line_tex_w_ = 0;
        status_line_tex_h_ = 0;
    }
    status_line_cached_counts_[0] = 0;
    status_line_cached_counts_[1] = 0;
    status_line_cached_counts_[2] = 0;
    status_line_cached_counts_[3] = 0;

    // v1.8.23 Option C: pre-warm the icon cache for first-page items on a
    // background thread instead of synchronously in Open().  The synchronous
    // loop blocked Open() for ~2000 ms wall-clock (HW evidence) — the user
    // saw a frozen menu.  The thread now runs concurrently with the first
    // frames; PaintCell sees gray fallbacks for at most a few frames before
    // the cache is populated, instead of a frozen window.  See
    // PrewarmLaunchpadIcons (this file) for the relocated body and
    // SpawnLpPrewarmThread for the spawn site (called below after
    // RebuildFilter).

    is_open_ = true;
    closed_  = false;  // B68 (v1.8.27): re-arm idempotent guard for this cycle.

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

    // v1.8.23 Option C: spawn the background prewarm thread AFTER items_,
    // icon_tex_, icon_loaded_, and filtered_idxs_ are fully built (and after
    // is_open_ flips true).  The thread reads items_ + desktop_icons_ptr_ and
    // calls into the desktop's load helpers; cache writes are guarded by
    // GetSharedIconCacheMutex() inside those helpers.  Idempotency: the prior
    // cycle's thread (if any) was already reaped at the top of this Open().
    SpawnLpPrewarmThread();
}

// ── Close ─────────────────────────────────────────────────────────────────────

void QdLaunchpadElement::Close() {
    // B68 (v1.8.27): idempotent guard — second call during Finalize is a no-op.
    if (closed_) return;
    closed_ = true;

    // v1.8.23 Option C: reap the background prewarm thread BEFORE
    // FreeAllTextures() / items_.clear() — the thread reads icon_tex_ and
    // items_, so destroying those out from under it would be UB.
    // StopLpPrewarmThread is idempotent (joinable() guard) and a no-op if
    // the thread already exited normally.
    StopLpPrewarmThread();

    // v1.8.24 F-3/F-4: free per-session scalar cached textures before
    // FreeAllTextures() frees the per-slot vectors.
    if (search_bar_tex_) {
        pu::ui::render::DeleteTexture(search_bar_tex_);
        search_bar_tex_ = nullptr;
    }
    search_bar_cached_text_.clear();
    if (q_glyph_tex_) {
        pu::ui::render::DeleteTexture(q_glyph_tex_);
        q_glyph_tex_ = nullptr;
    }
    // v3.7: Q image is TryFindLoadImage-owned → raw SDL_DestroyTexture.
    if (q_glyph_img_) {
        SDL_DestroyTexture(q_glyph_img_);
        q_glyph_img_ = nullptr;
    }
    // v2.0.3-A5: free status_line_tex_ + star_tex_ on Close.
    // RenderText-LRU-owned — use DeleteTexture (P-B cluster).
    if (status_line_tex_) {
        pu::ui::render::DeleteTexture(status_line_tex_);
        status_line_tex_ = nullptr;
        status_line_tex_w_ = 0;
        status_line_tex_h_ = 0;
    }
    if (star_tex_) {
        pu::ui::render::DeleteTexture(star_tex_);
        star_tex_ = nullptr;
        star_tex_w_ = 0;
        star_tex_h_ = 0;
    }

    // Free every cached SDL texture before clearing items_; the vectors must
    // still be alive while FreeAllTextures walks them.
    FreeAllTextures();

    items_.clear();
    items_stable_ids_.clear();  // A5-OPT-1: clear parallel ID cache
    filtered_idxs_.clear();
    query_.clear();
    dpad_focus_index_          = 0;
    mouse_hover_index_         = SIZE_MAX;
    tab_focus_idx_             = SIZE_MAX;  // v1.8.29 Slice 1: reset on close
    search_focus_active_       = false;     // v1.8.33: reset search focus on close
    pending_launch_            = false;
    pending_launch_from_mouse_ = false;
    filter_dirty_              = false;
    active_folder_             = AutoFolderIdx::None;  // Fix D (v1.6.12)
    // v1.7.0-stabilize-2: clear edge-trigger latch on close so a re-Open later
    // starts from a known state. The latch is reset to true again at Open()
    // so the still-down finger does not retrigger the close handler.
    lp_was_touch_active_last_frame_ = false;
    // v1.8.18: icon_cache_ removed; no reset needed (using GetSharedIconCache() singleton).
    desktop_icons_ptr_         = nullptr;
    is_open_                   = false;
    lp_scroll_y_               = 0;  // W11-SCROLL: reset internal scroll on close
    // W12-FIX Bug 1: reset drag-scroll state on close so a carry-over gesture
    // cannot accidentally set lp_swipe_fired_ or lp_scroll_y_ on re-open.
    lp_drag_scroll_engaged_       = false;
    lp_drag_scroll_origin_y_      = 0;
    lp_drag_scroll_origin_offset_ = 0;

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
    // W11-SCROLL: reset scroll to top on query change.
    lp_scroll_y_ = 0;
    page_index_  = 0;
    if (lp_scroll_cb_) { lp_scroll_cb_(0); }
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
        // W11-SCROLL: reset scroll to top on query change.
        lp_scroll_y_ = 0;
        page_index_  = 0;
        if (lp_scroll_cb_) { lp_scroll_cb_(0); }
    }
}

void QdLaunchpadElement::ClearQuery() {
    query_.clear();
    filter_dirty_ = true;
    RebuildFilter();
    dpad_focus_index_ = 0u;
    // W11-SCROLL: reset scroll to top on query clear.
    lp_scroll_y_ = 0;
    page_index_  = 0;
    if (lp_scroll_cb_) { lp_scroll_cb_(0); }
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

    // ── QoL-T2: context menu — drain confirmed selection before anything else ──
    // Mirrors qd_DesktopIcons.cpp context_menu_ gate: if open, route all input
    // to the menu; dispatch on close.  Context menu owns input exclusively.
    if (lp_ctx_menu_.IsOpen()) {
        const s32 cm_cx = 0;  // no software cursor in launchpad; touch only
        const s32 cm_cy = 0;
        const s32 cm_tx = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.x);
        const s32 cm_ty = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.y);
        lp_ctx_menu_.HandleInput(keys_down, /*keys_up=*/0u, cm_cx, cm_cy, cm_tx, cm_ty);

        if (!lp_ctx_menu_.IsOpen()) {
            // Menu closed this frame — dispatch if a row was confirmed.
            const auto sel = lp_ctx_menu_.GetSelection();
            if (sel.parent_index >= 0 && lp_ctx_target_lp_idx_ < items_.size()) {
                const LpItem &it = items_[lp_ctx_target_lp_idx_];
                if (sel.parent_index == lp_ctx_opt_open_) {
                    // Open: use the same dispatch path as A-key launch.
                    if (lp_ctx_target_lp_idx_ < filtered_idxs_.size()) {
                        // Resolve to filtered position first, then dispatch.
                        // Use desktop_idx directly so we don't need to search filtered.
                        if (desktop_icons_ptr_ && it.desktop_idx < static_cast<size_t>(-1)) {
                            UL_LOG_INFO("lp ctx-menu: Open lp_idx=%zu desktop_idx=%zu",
                                        lp_ctx_target_lp_idx_, it.desktop_idx);
                            desktop_icons_ptr_->LaunchIcon(it.desktop_idx);
                        }
                    }
                } else if (sel.parent_index == lp_ctx_opt_fav_toggle_) {
                    UL_LOG_INFO("lp ctx-menu: Fav toggle lp_idx=%zu name='%s'",
                                lp_ctx_target_lp_idx_, it.name);
                    // QOS-AUDIO-DEAD-SFX (2026-06-19): capture result for SFX dispatch.
                    const bool ctx_now_fav = ToggleFavoriteByLpItem(it);
                    if(ctx_now_fav) {
                        LP_PLAY_SFX(PlayFavoriteOnSfx);
                    } else {
                        LP_PLAY_SFX(PlayFavoriteOffSfx);
                    }
                    filter_dirty_ = true;
                } else if (sel.parent_index == lp_ctx_opt_hide_) {
                    const std::string sid = StableIdForItem(it);
                    if (!sid.empty()) {
                        UL_LOG_INFO("lp ctx-menu: Hide lp_idx=%zu sid='%s'",
                                    lp_ctx_target_lp_idx_, sid.c_str());
                        QdVisibility::Get().SetHidden(sid, true);
                        filter_dirty_ = true;
                    }
                } else if (sel.parent_index == lp_ctx_opt_move_folder_
                           && sel.sub_index >= 0
                           && static_cast<size_t>(sel.sub_index) < kFolderCount) {
                    const std::string sid = StableIdForItem(it);
                    const FolderIdx target_fidx = kFolderSpecs[sel.sub_index].idx;
                    if (!sid.empty()) {
                        UL_LOG_INFO("lp ctx-menu: MoveFolder lp_idx=%zu sid='%s' folder=%d",
                                    lp_ctx_target_lp_idx_, sid.c_str(),
                                    static_cast<int>(target_fidx));
                        QdFolderClassifier::Get().SetUserOverride(sid, target_fidx);
                        filter_dirty_ = true;
                    }
                }
                // Cancel and unrecognised indices: no-op.
            }
            lp_ctx_target_lp_idx_ = SIZE_MAX;
        }
        // Context menu owns all input while open; return when it was (still) open
        // on entry regardless of dispatch above.
        lp_was_touch_active_last_frame_ = !touch_pos.IsEmpty();
        return;
    }

    // ── B / Plus: close ───────────────────────────────────────────────────────
    if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
        Close();
        SetVisible(false);
        return;
    }

    // A-8 (v1.7.2): capture the previous-frame touch latch BEFORE the
    // hot-corner block updates lp_was_touch_active_last_frame_.  Used below
    // to edge-trigger the folder tile strip so it fires only on touch-down,
    // not every frame the finger is held (level-trigger bug).
    const bool lp_was_touch_prev = lp_was_touch_active_last_frame_;

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
        const bool touch_active_now = !touch_pos.IsEmpty();  // F1 (stabilize-5): RC-A sentinel fix
        const s32  tx               = static_cast<s32>(touch_pos.x);
        const s32  ty               = static_cast<s32>(touch_pos.y);
        // v2.0.3.2: in windowed mode the hot-corner is suppressed at render
        // time (qd_Launchpad.cpp section 2 above).  The hit-test must also be
        // suppressed — otherwise a touch in natural (0,0)–(96,72) inside the
        // window (where the user expects nothing because no glyph is drawn)
        // calls Close() + LoadMenu(Main), which yanks the user out of the
        // folder window into a Main-menu reload.  This is the most likely
        // cause of "tap an icon, nothing reacts" when the window happens to
        // be near the screen origin: touch translation can put the local
        // coord into the hot-corner zone if the user taps just inside the
        // top-left of the visible launchpad area.
        const bool touch_corner_now = (!windowed_mode_)
                                      && touch_active_now
                                      && tx >= 0 && tx < LP_HOTCORNER_W
                                      && ty >= 0 && ty < LP_HOTCORNER_H;
        const bool touch_corner_edge = touch_corner_now
                                       && !lp_was_touch_active_last_frame_;
        if (touch_corner_edge) {
            UL_LOG_INFO("qdesktop: Launchpad hot-corner CLOSE tap edge tx=%d ty=%d", tx, ty);
            Close();
            SetVisible(true);
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            return;
        }
        // Update the latch every frame so subsequent Open/Close calls see a
        // consistent edge boundary.
        lp_was_touch_active_last_frame_ = touch_active_now;
    }

    // ── v2.0.3.4: search-bar touch handler ───────────────────────────────────
    // Tap inside the search bar rect activates `search_focus_active_` so the
    // bar gets a focus ring and downstream key input goes to the query.  Edge-
    // triggered via lp_was_touch_prev so a held finger doesn't re-fire every
    // frame.  Works in both full-screen and windowed launchpads — search is
    // useful in both contexts.
    {
        const bool has_touch_search = !touch_pos.IsEmpty();
        const bool search_touch_edge = has_touch_search && !lp_was_touch_prev;
        if (search_touch_edge) {
            const s32 stx = static_cast<s32>(touch_pos.x);
            const s32 sty = static_cast<s32>(touch_pos.y);
            if (stx >= LP_SEARCH_BAR_X && stx < LP_SEARCH_BAR_X + LP_SEARCH_BAR_W &&
                sty >= LP_SEARCH_BAR_Y && sty < LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H) {
                if (!search_focus_active_) {
                    search_focus_active_ = true;
                    tab_focus_idx_       = SIZE_MAX;
                    active_input_source_ = InputSource::MOUSE;
                    UL_LOG_INFO("qdesktop: launchpad search-bar tap → search focus on");
                }
            }
        }
    }

    const size_t n = FilteredCount();

    // ── StickL: backspace on query ────────────────────────────────────────────
    if (keys_down & HidNpadButton_StickL) {
        PopQueryChar();
        // After filter change, re-read n for navigation below.
        return;
    }

    // F8 (stabilize-5): P3 — auto-folder strip OnInput re-enabled.
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
    // v2.0.3.2: in windowed mode the tab strip is suppressed at render time;
    // its hit-test must also be suppressed.  Folder windows are already
    // category-filtered via SetFolderFilter — letting a tap in the (now
    // invisible) tab band silently flip the filter would be unexpected, and
    // the tab band's natural y range (138–174) is BELOW the search bar but
    // ABOVE the icon grid, where the user might tap expecting nothing.
    if (!windowed_mode_) {
        // touch_pos.IsEmpty() / valid tap is signalled by keys_down containing
        // the Plutonium touch-tap flag. Use the x/y fields when the point is
        // non-zero (the framework sets {0,0} when there is no active touch).
        const bool has_touch = !touch_pos.IsEmpty();  // F1 (stabilize-5): RC-A sentinel fix
        // A-8 (v1.7.2): edge-trigger — only process folder strip on the
        // FIRST frame of a touch-down (touch active now AND not active last
        // frame).  Prevents the active_folder_ / filter_dirty_ flip from
        // re-firing every frame while the user holds a finger on a tile.
        const bool is_touch_edge = has_touch && !lp_was_touch_prev;
        if (is_touch_edge) {
            const s32 tx = static_cast<s32>(touch_pos.x);
            const s32 ty = static_cast<s32>(touch_pos.y);

            // v1.8 Input-source latch: any touch-down is MOUSE/touch mode.
            active_input_source_ = InputSource::MOUSE;

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
                        LP_PLAY_SFX(PlayFolderFilterSfx);  // v1.8.1 Task D
                    }
                } else {
                    // Walk the spec tiles in the same order as OnRender.
                    // Count bucket occupancy to skip empty tiles (which are not
                    // rendered and thus have no hit area).
                    s32 spec_tile_x = LP_SEARCH_BAR_X;
                    for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
                        // A-5 (v1.7.2): use pre-computed bucket count from
                        // folder_bucket_count_[] instead of re-walking items_.
                        if (folder_bucket_count_[fi] == 0u) {
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
                                LP_PLAY_SFX(PlayFolderFilterSfx);  // v1.8.1 Task D
                            }
                            break;
                        }
                        spec_tile_x += FTILE_W + FTILE_GAP;
                    }
                }
            }
        }
    }

    if (n == 0u) {
        return;  // Nothing to navigate.
    }

    // ── A-9 (v1.7.2.1) + W3 Bug L2: deferred tile launch on touch LIFT ──────
    // Provides MNR §6 / §33 single-fire touch-launch on the tile grid.
    //
    // History
    // ──────────────────────────────────────────────────────────────────────
    //   v1.7.2.1 fired the launch on the touch-DOWN edge (the moment the
    //   finger landed on a cell).  That pre-empted the 500 ms long-press
    //   timer below: every finger-down on an icon launched the app before
    //   the long-press detector ever ran, so the ZL ctx menu was unreachable
    //   from touch — creator HW report (Bug L2): "fingers touches just start
    //   it no matter what".  Especially visible in folder windows where the
    //   ZL ctx menu was the ONLY way to surface per-app options.
    //
    // W3 Bug L2 fix
    // ──────────────────────────────────────────────────────────────────────
    //   Touch-DOWN now CAPTURES the touched cell into `lp_touch_down_vpos_`
    //   without dispatching.  The long-press and swipe detectors (below)
    //   then have their full 500 ms / 80 px windows to fire.  Touch-LIFT
    //   finally dispatches the launch from the captured vpos — but only
    //   when neither long-press nor swipe fired during the gesture.
    //
    // Mutual exclusion contract
    //   • long-press fired   → ctx menu opens; `lp_long_press_fired_=true`
    //                          suppresses the lift-launch.
    //   • swipe fired        → page navigated; `lp_swipe_fired_=true`
    //                          suppresses the lift-launch.
    //   • finger drift > 12 px before 500 ms but < 80 px (no swipe yet)
    //                        → long-press cancels (start_tick→0); if the
    //                          finger never reaches the swipe threshold
    //                          before lifting we still launch the original
    //                          vpos.  This matches the desktop dock tap
    //                          tolerance (qd_DesktopIcons.cpp QoL-T1).
    //
    // Hit-test inverts CellXY: (tx, ty) → (col, row) → vpos.
    // Gap rejection prevents fingers landing in the inter-cell space from
    // launching a neighbouring tile.
    {
        const bool has_touch_tile     = !touch_pos.IsEmpty();
        const bool is_touch_tile_edge = has_touch_tile && !lp_was_touch_prev;
        const bool is_touch_lift_edge = !has_touch_tile && lp_was_touch_prev;

        if (is_touch_tile_edge) {
            // Touch-DOWN edge: hit-test the cell and CAPTURE the vpos.
            // No launch — the long-press / swipe detectors get their window.
            // W11-SCROLL: ty is adjusted by lp_scroll_y_ (fullscreen) so the
            // hit-test works against the full canvas instead of page-local coords.
            lp_touch_down_vpos_ = SIZE_MAX;  // default: not on a tile
            const s32 tx = static_cast<s32>(touch_pos.x);
            const s32 ty = static_cast<s32>(touch_pos.y);
            // In windowed mode, touch_pos is already in content-local (natural)
            // coordinates (QdWindow pre-translates including scroll).
            // In fullscreen mode, add lp_scroll_y_ to map screen y → canvas y.
            const s32 canvas_y = windowed_mode_ ? ty : ty + lp_scroll_y_;
            // Grid bounds in full-canvas space.
            const s32 grid_x = LP_GRID_X;
            const s32 grid_y = LP_GRID_Y;
            if (tx >= grid_x && canvas_y >= grid_y) {
                const s32 dx           = tx - grid_x;
                const s32 dy           = canvas_y - grid_y;
                const s32 col          = dx / (LP_CELL_W + LP_GAP_X);
                const s32 row          = dy / (LP_CELL_H + LP_GAP_Y);
                const s32 cell_local_x = dx - col * (LP_CELL_W + LP_GAP_X);
                const s32 cell_local_y = dy - row * (LP_CELL_H + LP_GAP_Y);
                if (cell_local_x < LP_CELL_W && cell_local_y < LP_CELL_H &&
                    col < LP_COLS) {
                    const size_t vpos = static_cast<size_t>(row) * static_cast<size_t>(LP_COLS)
                                      + static_cast<size_t>(col);
                    if (vpos < n) {
                        UL_LOG_INFO("qdesktop: Launchpad tile touch-DOWN tx=%d ty=%d vpos=%zu (defer)",
                                    tx, ty, vpos);
                        // Update hover/focus immediately so the ctx menu
                        // (if a long-press fires) targets the same tile,
                        // and the focus ring tracks the finger.
                        active_input_source_ = InputSource::MOUSE;
                        mouse_hover_index_   = vpos;
                        dpad_focus_index_    = vpos;
                        lp_touch_down_vpos_  = vpos;
                    }
                }
            }
        } else if (is_touch_lift_edge) {
            // Touch-LIFT edge: dispatch the captured launch unless a
            // long-press or swipe already consumed this gesture.
            const size_t vpos = lp_touch_down_vpos_;
            if (vpos != SIZE_MAX
                    && vpos < n
                    && !lp_long_press_fired_
                    && !lp_swipe_fired_) {
                UL_LOG_INFO("qdesktop: Launchpad tile touch-LIFT launch vpos=%zu", vpos);
                active_input_source_       = InputSource::MOUSE;
                mouse_hover_index_         = vpos;
                dpad_focus_index_          = vpos;
                pending_launch_            = true;
                pending_launch_from_mouse_ = true;
            }
            lp_touch_down_vpos_ = SIZE_MAX;
        }
    }

    // D-pad navigation: clamp at edges, no wrapping, per spec. ──────────────
    // v1.8 Input-source latch: any directional D-pad key switches to DPAD mode.
    // We check the directional mask before individual keys so the source flip
    // happens once, before any nav side-effect.
    if (keys_down & (HidNpadButton_Up | HidNpadButton_Down
                    | HidNpadButton_Left | HidNpadButton_Right)) {
        active_input_source_ = InputSource::DPAD;
    }

    // v1.8.29 Slice 1: count visible tab tiles so tab_focus_idx_ stays in range.
    // visible_tab_count = 1 ("All") + number of non-empty category buckets.
    // "All" = index 0; category tiles = indices 1..visible_tab_count-1.
    size_t visible_tab_count = 1u; // always includes "All"
    for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
        if (folder_bucket_count_[fi] > 0u) {
            ++visible_tab_count;
        }
    }

    // v1.8.32: capture tab-mode-at-frame-start so the A-launch handler later
    // can skip when an A press was already consumed by a tab activation. The
    // tab-A handler clears tab_focus_idx_ to SIZE_MAX, which would otherwise
    // make the grid-mode A launch fire on the same keys_down and double-
    // dispatch (causing the black-screen-stuck symptom).
    const bool was_in_tab_mode_     = (tab_focus_idx_ != SIZE_MAX);
    bool       tab_a_consumed       = false;

    // ── v1.8.33: search-bar focus mode ────────────────────────────────────────
    // Three focus zones now: search bar (top), tab strip (middle), grid (bottom).
    // search_focus_active_ takes precedence over tab/grid input.
    //
    // v2.0.3.5: tap-outside-bar exits search focus.  The earlier search-bar
    // touch handler sets search_focus_active_=true on tap; without this gate,
    // the early-return at the bottom of this block traps OnInput forever and
    // blocks every subsequent icon-launch tap until B is pressed (no D-pad on
    // touch-only sessions).  When the user taps outside the search bar, we
    // clear the focus and fall through so the tap continues to the icon-grid
    // handler at line ~840.
    if (search_focus_active_) {
        const bool has_touch_now = !touch_pos.IsEmpty();
        const bool touch_edge_now = has_touch_now && !lp_was_touch_prev;
        if (touch_edge_now) {
            const s32 stx = static_cast<s32>(touch_pos.x);
            const s32 sty = static_cast<s32>(touch_pos.y);
            const bool inside_search =
                (stx >= LP_SEARCH_BAR_X && stx < LP_SEARCH_BAR_X + LP_SEARCH_BAR_W &&
                 sty >= LP_SEARCH_BAR_Y && sty < LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H);
            if (!inside_search) {
                search_focus_active_ = false;
                UL_LOG_INFO("qdesktop: launchpad tap outside search bar → search focus off");
                // Fall through; the tap will be handled by the icon-grid touch
                // block further down in this OnInput.
            }
        }
    }

    if (search_focus_active_) {
        if (keys_down & HidNpadButton_Down) {
            // Return to tab focus on the "All" tab.
            search_focus_active_ = false;
            tab_focus_idx_       = 0u;
            UL_LOG_INFO("qdesktop: launchpad dpad down (search→tab)");
        }
        if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
            // B/+ in search focus exits to tab focus instead of closing the
            // Launchpad — matches the "search above tabs" mental model.
            search_focus_active_ = false;
            tab_focus_idx_       = 0u;
            UL_LOG_INFO("qdesktop: launchpad B/+ in search focus → tab focus");
        }
        if (keys_down & HidNpadButton_A) {
            // Open the system on-screen keyboard, write result into query_
            // via existing ClearQuery() + PushQueryChar() loop.  filter rebuild
            // happens implicitly per push; final clamp in PushQueryChar handles
            // out-of-range focus cleanup.
            char buf[256];
            buf[0] = '\0';
            SwkbdConfig kbd;
            if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
                swkbdConfigMakePresetDefault(&kbd);
                swkbdConfigSetInitialText(&kbd, query_.c_str());
                swkbdConfigSetGuideText(&kbd, "Search apps");
                swkbdConfigSetStringLenMax(&kbd, 64u);
                if (R_SUCCEEDED(swkbdShow(&kbd, buf, sizeof(buf)))) {
                    ClearQuery();
                    for (const char *p = buf; *p != '\0'; ++p) {
                        PushQueryChar(*p);
                    }
                    UL_LOG_INFO("qdesktop: launchpad swkbd query='%s'", buf);
                }
                swkbdClose(&kbd);
            }
            // Stay in search focus after swkbd return so the user can press A
            // again to refine, or DOWN to enter tab focus, or B to exit.
        }
        // Skip the rest of OnInput (tab/grid handling) while in search focus.
        return;
    }

    if (tab_focus_idx_ != SIZE_MAX) {
        // ── Tab-strip mode ─────────────────────────────────────────────────────
        // D-pad LEFT/RIGHT cycles among visible tab tiles (no wrapping).
        // D-pad DOWN returns to grid at (0, 0).
        // v1.8.33: D-pad UP enters search-bar focus.
        if (keys_down & HidNpadButton_Up) {
            search_focus_active_ = true;
            tab_focus_idx_       = SIZE_MAX;
            UL_LOG_INFO("qdesktop: launchpad dpad up (tab→search) — search focus on");
        }
        if (keys_down & HidNpadButton_Left) {
            if (tab_focus_idx_ > 0u) {
                --tab_focus_idx_;
            }
        }
        if (keys_down & HidNpadButton_Right) {
            if (tab_focus_idx_ + 1u < visible_tab_count) {
                ++tab_focus_idx_;
            }
        }
        if (keys_down & HidNpadButton_Down) {
            // Return to grid, first cell.
            tab_focus_idx_    = SIZE_MAX;
            dpad_focus_index_ = 0u;
        }
        // A in tab mode activates the focused filter and returns to grid.
        if (keys_down & HidNpadButton_A) {
            if (tab_focus_idx_ == 0u) {
                // "All" tab selected.
                active_folder_ = AutoFolderIdx::None;
            } else {
                // Map tab_focus_idx_ (1-based) to the corresponding non-empty
                // kTopLevelFolders entry.
                size_t match = 0u;
                for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
                    if (folder_bucket_count_[fi] > 0u) {
                        ++match;
                        if (match == tab_focus_idx_) {
                            active_folder_ = kTopLevelFolders[fi].idx;
                            break;
                        }
                    }
                }
            }
            filter_dirty_     = true;
            tab_focus_idx_    = SIZE_MAX;
            dpad_focus_index_ = 0u;
            tab_a_consumed    = true;  // v1.8.32: prevent grid-mode A from re-firing
            // W11-SCROLL: reset scroll to top when a tab filter is activated.
            lp_scroll_y_      = 0;
            page_index_       = 0;
            if (lp_scroll_cb_) { lp_scroll_cb_(0); }
        }
    } else {
        // ── Grid mode ─────────────────────────────────────────────────────────
        bool focus_moved = false;
        if (keys_down & HidNpadButton_Up) {
            if (dpad_focus_index_ >= static_cast<size_t>(LP_COLS)) {
                dpad_focus_index_ -= static_cast<size_t>(LP_COLS);
                focus_moved = true;
            } else {
                // Row 0: enter tab-strip mode. Set tab_focus_idx_ to the tab
                // that corresponds to the currently active filter so focus lands
                // on the right tile rather than always jumping to "All".
                if (active_folder_ == AutoFolderIdx::None) {
                    tab_focus_idx_ = 0u;
                } else {
                    // Find which rendered tab slot holds active_folder_.
                    size_t match = 0u;
                    bool found   = false;
                    for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
                        if (folder_bucket_count_[fi] > 0u) {
                            ++match;
                            if (kTopLevelFolders[fi].idx == active_folder_) {
                                tab_focus_idx_ = match;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        tab_focus_idx_ = 0u;  // fallback to "All"
                    }
                }
            }
        }
        if (keys_down & HidNpadButton_Down) {
            const size_t stepped = dpad_focus_index_ + static_cast<size_t>(LP_COLS);
            dpad_focus_index_ = (stepped < n) ? stepped : (n - 1u);
            focus_moved = true;
        }
        if (keys_down & HidNpadButton_Left) {
            if (dpad_focus_index_ > 0u) {
                dpad_focus_index_ -= 1u;
                focus_moved = true;
            }
        }
        if (keys_down & HidNpadButton_Right) {
            if (dpad_focus_index_ + 1u < n) {
                dpad_focus_index_ += 1u;
                focus_moved = true;
            }
        }
        // W11-SCROLL: after any D-pad move in the grid, ensure the focused cell
        // is visible.  EnsureFocusVisible adjusts lp_scroll_y_ (fullscreen) or
        // fires lp_scroll_cb_ (windowed) as needed.
        if (focus_moved && tab_focus_idx_ == SIZE_MAX) {
            EnsureFocusVisible();
        }

        // Fix B (v1.6.12): A and ZR are independent input sources.
        // A launches the D-pad focused item; ZR launches the mouse-hovered item.
        // pending_launch_from_mouse_ tells DispatchPendingLaunch() which index to use.
    } // end grid mode

    // ── A: launch based on current input source (grid mode only) ─────────────
    // v1.8 Input-source latch: in DPAD mode, A launches the D-pad focused tile
    // (pending_launch_from_mouse_=false → DispatchPendingLaunch uses
    // dpad_focus_index_).  In MOUSE mode, A launches the cursor-hovered tile
    // (pending_launch_from_mouse_=true → uses mouse_hover_index_).
    // A does NOT change the active_input_source_ itself (spec requirement).
    //
    // v1.8.32: Suppress this launch when the SAME A press was already consumed
    // by a tab-strip activation this frame.  Without the guard the tab-A
    // handler clears tab_focus_idx_ to SIZE_MAX, then the grid-mode launch
    // fires on the still-pressed A and double-dispatches into the freshly
    // filtered grid's first tile — stuck black screen if the launch path
    // hasn't fully wired up yet for the new filter.
    if (!was_in_tab_mode_ && !tab_a_consumed
            && (tab_focus_idx_ == SIZE_MAX)
            && (keys_down & HidNpadButton_A)) {
        if (dpad_focus_index_ < n) {
            pending_launch_            = true;
            pending_launch_from_mouse_ = (active_input_source_ == InputSource::MOUSE);
        }
    }

    // ── ZR: launch mouse-hovered item ────────────────────────────────────────
    // v1.8 Input-source latch: ZR is a mouse/controller button that switches
    // source to MOUSE and launches the cursor-hovered tile.
    if (keys_down & HidNpadButton_ZR) {
        // ZR → MOUSE mode
        active_input_source_ = InputSource::MOUSE;
        if (mouse_hover_index_ < n) {
            pending_launch_            = true;
            pending_launch_from_mouse_ = true;
        }
    }

    // ── v1.7.0-stabilize-7 Slice 5 (O-F Patch 2): Y toggles favorite ─────────
    // v3.1.2 (BUG-10d 2026-05-19) — Y semantics in Launchpad updated to
    // match the desktop dock behavior the user explicitly requested
    // ("Y doesn't work anywhere"): when an icon is focused (cursor or
    // D-pad), Y toggles that icon's favorite state.  Without a focused
    // icon, Y falls back to the v1.9 behavior of jumping to the
    // Favorites tab.
    //
    // Priority order: mouse_hover_index_ first (cursor wins when present),
    // then dpad_focus_index_.  Each resolves through filtered_idxs_ →
    // items_[lp_idx] which is the LpItem that ToggleFavoriteByLpItem
    // expects.
    if (keys_down & HidNpadButton_Y) {
        size_t y_target = SIZE_MAX;
        if (mouse_hover_index_ < filtered_idxs_.size()) {
            y_target = filtered_idxs_[mouse_hover_index_];
        } else if (dpad_focus_index_ < filtered_idxs_.size()) {
            y_target = filtered_idxs_[dpad_focus_index_];
        }
        if (y_target < items_.size()) {
            const LpItem &it = items_[y_target];
            const bool now_fav = ToggleFavoriteByLpItem(it);
            UL_LOG_INFO("lp: Y toggle-favorite lp_idx=%zu name='%s' now_fav=%d",
                        y_target, it.name, now_fav ? 1 : 0);
            // QOS-AUDIO-DEAD-SFX (2026-06-19): FavoriteOn / FavoriteOff wired here.
            if(now_fav) {
                LP_PLAY_SFX(PlayFavoriteOnSfx);
            } else {
                LP_PLAY_SFX(PlayFavoriteOffSfx);
            }
            // v3.1.3 (BUG-Y): show a toast so the user knows what happened.
            // Without this, removing a favorite from the Favorites tab makes
            // the icon "disappear" silently — confusing on HW.
            if (g_MenuApplication != nullptr) {
                g_MenuApplication->ShowNotification(
                    now_fav
                        ? (std::string("Added '") + it.name + "' to Favorites.")
                        : (std::string("Removed '") + it.name + "' from Favorites."));
            }
            // Refresh filtered list so a removed favorite disappears when
            // the user is currently on the Favorites tab.
            filter_dirty_ = true;
        } else if (active_tab_kind_ == LaunchpadTabKind::Favorites) {
            // No focused item — fall back to v1.9 behavior: toggle back
            // to "All" tab.
            active_folder_fi_ = FolderIdx::None;
            active_tab_kind_  = LaunchpadTabKind::Folder;
            filter_dirty_     = true;
            dpad_focus_index_ = 0u;
        } else {
            // No focused item — jump to Favorites tab if present.
            for (size_t ti = 0u; ti < active_tabs_.size(); ++ti) {
                if (active_tabs_[ti].kind == LaunchpadTabKind::Favorites) {
                    active_folder_fi_ = FolderIdx::None;
                    active_tab_kind_  = LaunchpadTabKind::Favorites;
                    filter_dirty_     = true;
                    tab_focus_idx_    = SIZE_MAX;
                    dpad_focus_index_ = 0u;
                    break;
                }
            }
        }
    }

    // ── W11-SCROLL: L / R — scroll by one viewport of rows ───────────────────
    // L scrolls up one screen of rows (LP_ITEMS_PER_PAGE items / LP_COLS rows).
    // R scrolls down one screen of rows.
    // Focus moves to the first item of the newly visible screen; EnsureFocusVisible
    // adjusts the scroll offset so the focused item is visible.
    // mouse_hover_index_ is reset to SIZE_MAX (hover is position-relative,
    // not meaningful after a scroll jump).
    if (keys_down & HidNpadButton_L) {
        if (page_index_ > 0u) {
            --page_index_;
            LP_PLAY_SFX(PlayPageTurnSfx);  // QOS-AUDIO-DEAD-SFX (2026-06-19)
        } else {
            LP_PLAY_SFX(PlayErrorToneSfx); // already at first page
        }
        // Jump focus back by one screen (LP_ITEMS_PER_PAGE items).
        if (dpad_focus_index_ >= LP_ITEMS_PER_PAGE) {
            dpad_focus_index_ -= LP_ITEMS_PER_PAGE;
        } else {
            dpad_focus_index_ = 0u;
        }
        mouse_hover_index_ = SIZE_MAX;
        EnsureFocusVisible();
    }
    if (keys_down & HidNpadButton_R) {
        if (page_index_ + 1u < page_count_) {
            ++page_index_;
            LP_PLAY_SFX(PlayPageTurnSfx);  // QOS-AUDIO-DEAD-SFX (2026-06-19)
        } else {
            LP_PLAY_SFX(PlayErrorToneSfx); // already at last page
        }
        // Jump focus forward by one screen (LP_ITEMS_PER_PAGE items).
        const size_t stepped = dpad_focus_index_ + LP_ITEMS_PER_PAGE;
        dpad_focus_index_ = (stepped < n) ? stepped : (n > 0u ? n - 1u : 0u);
        mouse_hover_index_ = SIZE_MAX;
        EnsureFocusVisible();
    }

    // ── QoL-T3: long-press touch → synthetic ZL ──────────────────────────────
    // Mirrors qd_DesktopIcons.cpp QoL-T1 exactly.  A 500 ms held touch with
    // < 12 px jitter synthesises a ZL press (opens the context menu at the
    // touch position).  On fire: long_press_fire_this_frame feeds the ZL gate
    // below; SetSkipFirstLift() is called so the lift that ends the press does
    // NOT immediately confirm a row.
    bool lp_long_press_fire_this_frame = false;
    {
        constexpr s32 kLpLongPressJitterPx    = 12;
        constexpr u64 kLpLongPressThresholdNs = 500'000'000ULL;  // 500 ms
        const bool touch_now  = !touch_pos.IsEmpty();
        // BUG-FIX (post-W3): use the snapshot captured at line ~833 BEFORE the
        // hot-corner block overwrote `lp_was_touch_active_last_frame_` to the
        // current frame's value.  Reading the field directly here returned
        // `touch_active_now` (true on a touch-down frame), making
        // `touch_now && !touch_prev` always false → long-press never armed
        // inside the launchpad/folder.  Long-press worked everywhere else
        // because the other state machines (desktop dock, hot-corner) capture
        // their own snapshots before mutation.
        const bool touch_prev = lp_was_touch_prev;
        if (touch_now && !touch_prev) {
            // Touch just started.
            lp_long_press_start_tick_ = armGetSystemTick();
            lp_long_press_start_x_    = static_cast<s32>(touch_pos.x);
            lp_long_press_start_y_    = static_cast<s32>(touch_pos.y);
            lp_long_press_fired_      = false;
        } else if (!touch_now) {
            // No touch this frame — reset.
            lp_long_press_start_tick_ = 0;
            lp_long_press_start_x_    = -1;
            lp_long_press_start_y_    = -1;
            lp_long_press_fired_      = false;
        } else if (touch_now && touch_prev && !lp_long_press_fired_
                   && lp_long_press_start_tick_ > 0) {
            // Touch continuing — check drift + elapsed time.
            const s32 dx  = static_cast<s32>(touch_pos.x) - lp_long_press_start_x_;
            const s32 dy  = static_cast<s32>(touch_pos.y) - lp_long_press_start_y_;
            const s32 adx = dx < 0 ? -dx : dx;
            const s32 ady = dy < 0 ? -dy : dy;
            if (adx > kLpLongPressJitterPx || ady > kLpLongPressJitterPx) {
                // Finger drifted — cancel.
                lp_long_press_start_tick_ = 0;
            } else {
                const u64 now_tick   = armGetSystemTick();
                const u64 elapsed_ns = armTicksToNs(now_tick - lp_long_press_start_tick_);
                if (elapsed_ns >= kLpLongPressThresholdNs) {
                    lp_long_press_fired_           = true;
                    lp_long_press_fire_this_frame  = true;
                    UL_LOG_INFO("lp: long-press fired @ (%d,%d) elapsed_ms=%llu",
                                lp_long_press_start_x_, lp_long_press_start_y_,
                                static_cast<unsigned long long>(elapsed_ns / 1'000'000ULL));
                }
            }
        }
    }

    // W8-FIX Bug 3: swipe-to-page REMOVED.  Creator wants continuous scroll,
    // not discrete page flips.  lp_swipe_fired_ is preserved as a drag-vs-
    // launch guard (≥80 px horizontal drag suppresses icon launch on lift),
    // but the page-flip side-effect is gone.  QdWindow's T2 drag-scroll
    // handles the actual viewport movement when the launchpad is windowed.
    // For the full-screen launchpad, D-pad L/R + the page index from
    // RebuildFilter remain as a fallback until the full single-canvas
    // refactor lands in a future wave.
    {
        constexpr s32 kLpSwipeThreshPx    = 80;
        constexpr s32 kLpSwipeVertGuardPx = 60;
        const bool touch_now  = !touch_pos.IsEmpty();
        const bool touch_prev = lp_was_touch_prev;

        if (touch_now && !touch_prev) {
            // Touch just started — record swipe origin for drag detection.
            lp_swipe_start_x_ = static_cast<s32>(touch_pos.x);
            lp_swipe_start_y_ = static_cast<s32>(touch_pos.y);
            lp_swipe_fired_   = false;
        } else if (touch_now && touch_prev && !lp_swipe_fired_
                   && lp_swipe_start_x_ >= 0) {
            const s32 dx  = static_cast<s32>(touch_pos.x) - lp_swipe_start_x_;
            const s32 dy  = static_cast<s32>(touch_pos.y) - lp_swipe_start_y_;
            const s32 adx = dx < 0 ? -dx : dx;
            const s32 ady = dy < 0 ? -dy : dy;
            if (adx >= kLpSwipeThreshPx && ady < kLpSwipeVertGuardPx) {
                // Arm the drag guard so the lifting finger doesn't launch.
                lp_swipe_fired_ = true;
                // Page-flip logic intentionally removed (Bug 3).
            }
        } else if (!touch_now && !touch_prev) {
            lp_swipe_start_x_ = -1;
            lp_swipe_start_y_ = -1;
            lp_swipe_fired_   = false;
        }
    }

    // ── W12-FIX Bug 1: fullscreen vertical drag-scroll ───────────────────────
    // Mirrors QdWindow's T2 universal drag-scroll for the FULLSCREEN launchpad.
    // In windowed mode QdWindow already owns the scroll via render_origin_y_;
    // this block is skipped entirely with an early guard.
    //
    // Mutual exclusion: when the deadband (8 px) is crossed we set BOTH
    // lp_drag_scroll_engaged_ AND lp_swipe_fired_ so every existing guard
    // site that checks lp_swipe_fired_ (tile launch, long-press dispatch)
    // correctly suppresses those actions for the remainder of the gesture.
    //
    // Interaction with long-press: the long-press cancels after 12 px drift
    // (kLpLongPressJitterPx = 12 px, above), which is > 8 px drag deadband,
    // so a drag-scroll gesture naturally cancels the long-press timer before
    // the scroll engages; both trackers cancel cleanly without interference.
    if (!windowed_mode_) {
        constexpr s32 kDragDeadbandPx = 8;
        const bool touch_now  = !touch_pos.IsEmpty();
        const bool touch_prev = lp_was_touch_prev;

        if (touch_now && !touch_prev) {
            // Touch-DOWN: record origin for drag-scroll.
            lp_drag_scroll_origin_y_      = static_cast<s32>(touch_pos.y);
            lp_drag_scroll_origin_offset_ = lp_scroll_y_;
            lp_drag_scroll_engaged_       = false;
        } else if (touch_now && touch_prev) {
            // Touch-HELD: measure vertical displacement from origin.
            const s32 dy  = static_cast<s32>(touch_pos.y) - lp_drag_scroll_origin_y_;
            const s32 ady = dy < 0 ? -dy : dy;
            if (!lp_drag_scroll_engaged_ && ady > kDragDeadbandPx) {
                lp_drag_scroll_engaged_ = true;
                // Arm the launch-suppress guard used throughout this function
                // so the lifting finger does NOT trigger a tile launch.
                lp_swipe_fired_ = true;
            }
            if (lp_drag_scroll_engaged_) {
                // Compute viewport height: screen height minus the header area
                // (LP_GRID_Y) and footer (LP_FOOTER_H).
                const s32 viewport_h = 1080 - LP_GRID_Y - LP_FOOTER_H;
                const s32 max_scroll = (lp_natural_h_ > viewport_h)
                                       ? (lp_natural_h_ - viewport_h) : 0;
                // Dragging DOWN (positive dy) scrolls UP (reduces lp_scroll_y_).
                s32 new_scroll = lp_drag_scroll_origin_offset_ - dy;
                if (new_scroll < 0)          { new_scroll = 0; }
                if (new_scroll > max_scroll) { new_scroll = max_scroll; }
                lp_scroll_y_ = new_scroll;
            }
        } else if (!touch_now) {
            // Touch-LIFT or no-touch: reset drag-scroll state.
            // lp_swipe_fired_ is already set if we engaged; the main
            // lift-launch block above will skip launch correctly.
            lp_drag_scroll_engaged_ = false;
        }
    }

    // ── QoL-T2: ZL — open context menu for focused/hovered launchpad item ────
    // Open trigger: ZL button OR long-press synthesises ZL.
    // Target resolution: mouse_hover_index_ first (cursor mode), then
    // dpad_focus_index_ (D-pad mode).  Position: cursor or long-press origin.
    if ((keys_down & HidNpadButton_ZL) || lp_long_press_fire_this_frame) {
        size_t target = SIZE_MAX;
        if (mouse_hover_index_ < filtered_idxs_.size()) {
            target = filtered_idxs_[mouse_hover_index_];
        } else if (dpad_focus_index_ < filtered_idxs_.size()) {
            target = filtered_idxs_[dpad_focus_index_];
        }

        if (target < items_.size()) {
            const LpItem &it = items_[target];
            lp_ctx_target_lp_idx_ = target;

            // Build menu items (mirrors qd_DesktopIcons dock context-menu).
            std::vector<QdContextMenuItem> opts;

            lp_ctx_opt_open_ = static_cast<int>(opts.size());
            opts.push_back({ "Open", {}, false, "[A]" });

            // Favorite toggle — show direction based on current state.
            const bool is_fav = IsFavoriteByLpItem(it);
            lp_ctx_opt_fav_toggle_ = static_cast<int>(opts.size());
            opts.push_back({ is_fav ? "Remove from Favorites"
                                    : "Add to Favorites",
                             {}, false, "[Y]" });

            lp_ctx_opt_hide_ = static_cast<int>(opts.size());
            opts.push_back({ "Hide", {}, false });

            // "Move to Folder ▸" submenu with 8 folder names.
            std::vector<std::string> folder_names;
            folder_names.reserve(kFolderCount);
            for (size_t fi = 0u; fi < kFolderCount; ++fi) {
                folder_names.emplace_back(kFolderSpecs[fi].display_name);
            }
            lp_ctx_opt_move_folder_ = static_cast<int>(opts.size());
            opts.push_back({ "Move to Folder \xe2\x96\xb8", std::move(folder_names), false });

            lp_ctx_opt_cancel_ = static_cast<int>(opts.size());
            opts.push_back({ "Cancel", {}, false, "[B]" });

            // ── W3 Bug L1: anchor ctx menu at the focused icon's tile ───────
            // History: prior anchor used (960, 540) screen-centre when no
            // active touch was registered (cursor mode + D-pad mode), so the
            // ZL ctx menu appeared in the middle of the screen instead of
            // next to the launchpad icon the user was actually looking at.
            // Creator HW report: "it works but its in the middle of the
            // screen" (full-screen launchpad) and the same applies inside
            // folder windows (Bug 5).
            //
            // Fix: anchor at the top-right of the FOCUSED tile, computed
            // from the visible index → page-local cell position → CellXY.
            // Coordinates are in launchpad-natural space (1920×1080); for
            // windowed launchpads QdWindow's active SDL_RenderSetScale
            // applies the window scale at render time so the menu lands
            // next to the icon visually.  An 8 px inset from the right
            // edge of the icon prevents the menu from covering the icon
            // it describes; the menu's own on-screen clamp (qd_ContextMenu.cpp
            // lines 137-144) handles right-edge overflow.
            //
            // Resolution priority mirrors the target resolution above:
            //   (1) long-press fire — use the actual finger origin
            //   (2) cursor mode    — use the cursor-hovered tile
            //   (3) D-pad mode     — use the D-pad focused tile
            // In all three cases the on-screen result is "menu next to the
            // icon being acted on".
            constexpr s32 kLpCtxAnchorInsetPx = 8;
            s32 open_x = 960;
            s32 open_y = 540;
            if (lp_long_press_fire_this_frame) {
                // Long-press: anchor at the touch origin so the menu pops up
                // exactly where the finger was — the user already sees the
                // tile they pressed.
                open_x = lp_long_press_start_x_;
                open_y = lp_long_press_start_y_;
            } else {
                // ZL button path: derive cell from the FILTERED (visible)
                // index of whichever input source is active.  mouse_hover_index_
                // wins when set; otherwise fall back to dpad_focus_index_.
                size_t visible_idx = SIZE_MAX;
                if (mouse_hover_index_ < filtered_idxs_.size()) {
                    visible_idx = mouse_hover_index_;
                } else if (dpad_focus_index_ < filtered_idxs_.size()) {
                    visible_idx = dpad_focus_index_;
                }
                if (visible_idx != SIZE_MAX) {
                    // W11-SCROLL: CellXY now takes raw vpos (global canvas position).
                    // Anchor at the icon's right edge + render origin, adjusted
                    // for fullscreen scroll (lp_scroll_y_) so the menu appears
                    // next to the visible cell even when scrolled.
                    s32 cx = 0, cy = 0;
                    CellXY(visible_idx, cx, cy);
                    // For fullscreen, subtract lp_scroll_y_ to get screen-space y.
                    const s32 cy_screen = windowed_mode_ ? cy : cy - lp_scroll_y_;
                    open_x = render_origin_x_ + cx + LP_CELL_W - kLpCtxAnchorInsetPx;
                    open_y = render_origin_y_ + cy_screen;
                }
            }

            SDL_Renderer *rcm = pu::ui::render::GetMainRenderer();
            if (rcm != nullptr) {
                lp_ctx_menu_.Open(rcm, opts, open_x, open_y);
                // QoL-T3: long-press path pre-arms skip-first-lift so the lift
                // that ends the opening touch is consumed (menu stays open).
                if (lp_long_press_fire_this_frame) {
                    lp_ctx_menu_.SetSkipFirstLift();
                }
            }
            UL_LOG_INFO("lp: ZL ctx-menu opened target=%zu name='%s' fav=%d",
                        target, it.name, is_fav ? 1 : 0);
        }
        lp_was_touch_active_last_frame_ = !touch_pos.IsEmpty();
        return;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────
//
// v2.0.3 origin propagation: (x, y) are pre-scale natural-coord offsets from
// the host (zero for full-screen overlay; window-position offsets when hosted
// inside QdFolderLaunchpadElement / QdWindow).  Cache to members so all paint
// helpers (PaintFolderTile, PaintCell, PaintStatusLine, PaintPageDots) can
// add the offset to their SDL_Rect literals without signature churn.

void QdLaunchpadElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                   s32 x, s32 y)
{
    if (!is_open_) {
        return;
    }

    render_origin_x_ = x;
    render_origin_y_ = y;

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
    SDL_Rect full { render_origin_x_, render_origin_y_, 1920, 1080 };
    SDL_RenderFillRect(r, &full);

    // ── 2. Hot-corner widget (top-left 60×48 px launcher button) ─────────────
    // Draws a slightly lighter rectangle so the user can see the tap target.
    // v2.0.3.1: suppressed in windowed mode — the hot-corner is the "open
    // launchpad" affordance, redundant when the launchpad is already open in
    // a folder window.
    if (!windowed_mode_) {
        SDL_SetRenderDrawColor(r,
            static_cast<u8>(std::min(255, (int)theme_.topbar_bg.r + 0x18)),
            static_cast<u8>(std::min(255, (int)theme_.topbar_bg.g + 0x18)),
            static_cast<u8>(std::min(255, (int)theme_.topbar_bg.b + 0x18)),
            0xFFu);
        SDL_Rect hc { render_origin_x_, render_origin_y_, LP_HOTCORNER_W, LP_HOTCORNER_H };
        SDL_RenderFillRect(r, &hc);
        // 1px accent border on the right and bottom of the hot-corner.
        SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
        SDL_Rect hcbr { render_origin_x_ + LP_HOTCORNER_W - 1, render_origin_y_, 1, LP_HOTCORNER_H };
        SDL_Rect hcbb { render_origin_x_, render_origin_y_ + LP_HOTCORNER_H - 1, LP_HOTCORNER_W, 1 };
        SDL_RenderFillRect(r, &hcbr);
        SDL_RenderFillRect(r, &hcbb);
        // F7 (stabilize-5): Block A re-enabled — "Q" glyph in hot-corner.
        // v1.8.24 F-4: q_glyph_tex_ rendered once at Open(); reused here each frame.
        // v2.0.3-A4: dims cached at Open(); no per-frame SDL_QueryTexture.
        // v3.7: prefer the per-theme Q IMAGE (matches the desktop hot-corner
        // widget + dock icons — changes SHAPE per theme).  Fall back to the
        // accent-tinted text "Q" only when the theme ships no HotCornerQ.png.
        if (q_glyph_img_) {
            // PNG is 192×192 with the Q centred + transparent margins; draw a
            // 60×60 box centred in the hot-corner cell (mirrors the desktop
            // widget's kQGlyphPx=60).  White colour-mod preserves the PNG's
            // own per-theme colours.
            constexpr int kQImgPx = 60;
            SDL_Rect td { render_origin_x_ + (LP_HOTCORNER_W - kQImgPx) / 2,
                          render_origin_y_ + (LP_HOTCORNER_H - kQImgPx) / 2,
                          kQImgPx, kQImgPx };
            SDL_SetTextureBlendMode(q_glyph_img_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(q_glyph_img_, 0xFFu, 0xFFu, 0xFFu);
            SDL_SetTextureAlphaMod(q_glyph_img_, 0xFFu);
            SDL_RenderCopy(r, q_glyph_img_, nullptr, &td);
        } else if (q_glyph_tex_) {
            const int tw = q_glyph_tex_w_;
            const int th = q_glyph_tex_h_;
            SDL_Rect td { render_origin_x_ + (LP_HOTCORNER_W - tw) / 2,
                          render_origin_y_ + (LP_HOTCORNER_H - th) / 2, tw, th };
            // v2.6.0 — tint the hot-corner Q glyph with the active theme's
            // accent color. The texture was rendered white in Open(); SDL
            // color-mod multiplies that white by the accent, producing a
            // glyph in the theme's accent hue (cyan for Q OS, magenta for
            // Neon, amber for Retro, NES yellow for Pixel, etc.).
            const auto &ac = ::ul::menu::qdesktop::g_QdTheme.accent;
            SDL_SetTextureColorMod(q_glyph_tex_, ac.r, ac.g, ac.b);
            SDL_RenderCopy(r, q_glyph_tex_, nullptr, &td);
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
    SDL_Rect search_bg { render_origin_x_ + LP_SEARCH_BAR_X,
                         render_origin_y_ + LP_SEARCH_BAR_Y,
                         LP_SEARCH_BAR_W, LP_SEARCH_BAR_H };
    SDL_RenderFillRect(r, &search_bg);

    // 1px accent border around the search bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_Rect search_ring { render_origin_x_ + LP_SEARCH_BAR_X - 1,
                           render_origin_y_ + LP_SEARCH_BAR_Y - 1,
                           LP_SEARCH_BAR_W + 2, LP_SEARCH_BAR_H + 2 };
    SDL_RenderDrawRect(r, &search_ring);

    // v1.8.24 F-3: search bar texture cache.
    // Compute the canonical display string for the current frame (3 states):
    //   a) non-empty query with caret:  "<query>|"
    //   b) empty query with caret:       "|"
    //   c) empty query, caret hidden:    "Search..."  (placeholder)
    // When display_text matches search_bar_cached_text_ AND caret visibility
    // matches, reuse search_bar_tex_. Otherwise re-render and cache.
    {
        const bool caret_visible = ((frame_tick_ / 30) % 2) == 0;

        // Build canonical display key string.
        std::string display_text;
        if (!query_.empty()) {
            display_text = query_;
            if (caret_visible) { display_text += '|'; }
        } else if (caret_visible) {
            display_text = "|";
        } else {
            display_text = "Search...";
        }

        // Invalidate cached texture when the display key changes.
        if (display_text != search_bar_cached_text_) {
            if (search_bar_tex_) {
                pu::ui::render::DeleteTexture(search_bar_tex_);
                search_bar_tex_ = nullptr;
                search_bar_tex_w_ = 0;
                search_bar_tex_h_ = 0;
            }
            search_bar_cached_text_ = display_text;

            // Re-render into the cache slot.
            if (!query_.empty() || caret_visible) {
                // Active text or caret: use the normal text colour.
                const pu::ui::Color tc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
                search_bar_tex_ = pu::ui::render::RenderText(
                    pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                    display_text, tc,
                    static_cast<u32>(LP_SEARCH_BAR_W - 16));
            } else {
                // Placeholder ("Search..."): dimmed hint colour.
                const pu::ui::Color hint_col { 0x88u, 0x88u, 0xAAu, 0xFFu };
                search_bar_tex_ = pu::ui::render::RenderText(
                    pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                    display_text, hint_col);
            }
            // v2.0.3-A4: cache dims at create — re-issued only on display change.
            if (search_bar_tex_) {
                SDL_QueryTexture(search_bar_tex_, nullptr, nullptr,
                                 &search_bar_tex_w_, &search_bar_tex_h_);
            }
        }

        // Blit the cached texture (may be nullptr if RenderText returned null).
        // v2.0.3-A4: dims cached above — no per-frame SDL_QueryTexture.
        if (search_bar_tex_) {
            const int tw = search_bar_tex_w_;
            const int th = search_bar_tex_h_;
            const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
            SDL_Rect td { render_origin_x_ + LP_SEARCH_BAR_X + 8,
                          render_origin_y_ + ty, tw, th };
            SDL_RenderCopy(r, search_bar_tex_, nullptr, &td);
        }

        // v1.8.33: focus ring around the search bar when search focus active.
        if (search_focus_active_) {
            // v3.7 unified selection ring — drops the old hardcoded #0080AA
            // inner ring; uses the per-theme focus_ring + soft glow.
            DrawFocusRing(r,
                render_origin_x_ + LP_SEARCH_BAR_X - 3,
                render_origin_y_ + LP_SEARCH_BAR_Y - 3,
                LP_SEARCH_BAR_W + 6, LP_SEARCH_BAR_H + 6,
                8, 3, ::ul::menu::qdesktop::g_QdTheme.focus_ring);
        }
    }

    // F8 (stabilize-5): P3 — auto-folder strip OnRender re-enabled.
    // ── 3.5. Fix D (v1.6.12): Auto-folder tile strip ─────────────────────────
    // Render up to kTopLevelFolderCount tiles in a horizontal strip between the
    // search bar and the icon grid.  Only tiles for non-empty buckets are drawn;
    // the active bucket (active_folder_) gets an accent border.
    // Tile geometry: strip top = LP_SEARCH_BAR_Y + LP_SEARCH_BAR_H + 6 px gap.
    // Each tile: 200 px wide, 36 px tall, 8 px horizontal gap between tiles.
    // The strip is left-aligned at LP_SEARCH_BAR_X so it aligns with the search bar.
    //
    // v2.0.3.1: suppressed in windowed mode — folder windows already apply a
    // category filter via SetFolderFilter, so the tab strip is redundant chrome.
    if (!windowed_mode_) {
        // A-4 (v1.7.2): bucket counts are now pre-computed in RebuildFilter()
        // into folder_bucket_count_[].  No per-frame items_ walk needed here.
        const size_t (&bucket_count)[kTopLevelFolderCount] = folder_bucket_count_;

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
            // W5-PERF-HOTSPOTS #2 (P0-B): slot 1..kTopLevelFolderCount = category tiles.
            PaintFolderTile(r, static_cast<int>(fi) + 1,
                            tile_x, FTILE_STRIP_Y, FTILE_W, FTILE_H,
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
            // W5-PERF-HOTSPOTS #2 (P0-B): slot 0 = "All" tile.
            PaintFolderTile(r, 0,
                            LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP,
                            FTILE_STRIP_Y,
                            FTILE_W, FTILE_H,
                            "All",
                            items_.size(),
                            all_active);
        }

        // v1.8.29 Slice 1: draw D-pad focus ring around the focused tab tile.
        // tab_focus_idx_ == SIZE_MAX means grid mode (no tab ring).
        // tab_focus_idx_ == 0 means "All" tile; 1..M are category tiles.
        if (tab_focus_idx_ != SIZE_MAX) {
            s32 ring_x;
            if (tab_focus_idx_ == 0u) {
                ring_x = LP_SEARCH_BAR_X - FTILE_W - FTILE_GAP;
            } else {
                // Walk non-empty buckets to find the tab_focus_idx_-th rendered tile.
                size_t match = 0u;
                ring_x = LP_SEARCH_BAR_X;
                for (size_t fi = 0u; fi < kTopLevelFolderCount; ++fi) {
                    if (bucket_count[fi] == 0u) continue;
                    ++match;
                    if (match == tab_focus_idx_) {
                        break;
                    }
                    ring_x += FTILE_W + FTILE_GAP;
                }
            }
            // v3.7 unified selection ring (rounded + soft glow, per-theme
            // focus_ring) — replaces the hard outer/darker-accent-inner pair.
            DrawFocusRing(r,
                render_origin_x_ + ring_x - 3,
                render_origin_y_ + FTILE_STRIP_Y - 3,
                FTILE_W + 6, FTILE_H + 6,
                8, 3, ::ul::menu::qdesktop::g_QdTheme.focus_ring);
        }
    }

    // v1.8.23 Option C: F1 section-headers deferred-block removed.  The
    // original code (RenderText per visible section per frame, uncached)
    // was permanently disabled via #if 0 in stabilize-4 due to GPU pool
    // exhaustion on Switch; the static-cached label strategy referenced in
    // the original comment was never implemented and there is no plan to
    // resurrect this code path.  Auditor R2 flagged it as pure dead code.

    // ── 5. Icon grid (W11-SCROLL: full canvas, all items) ────────────────────
    // All filtered items are painted at their natural canvas coordinates.
    // QdWindow's clip rect (windowed mode) handles what is actually visible.
    // For fullscreen, cells outside the visible area are culled below.
    const size_t nf = filtered_idxs_.size();
    for (size_t vpos = 0u; vpos < nf; ++vpos) {
        const size_t item_idx = filtered_idxs_[vpos];
        if (item_idx >= items_.size()) { continue; }

        // CellXY returns natural canvas position (global, not page-local).
        s32 cx = 0, cy = 0;
        CellXY(vpos, cx, cy);

        // For fullscreen: cull cells outside the visible scroll window.
        // cy_screen is the cell's Y position in screen space.
        const s32 cy_screen = cy - lp_scroll_y_;
        if (cy_screen + LP_CELL_H <= 0) { continue; }   // above viewport
        if (cy_screen > 1080 - LP_FOOTER_H) { continue; } // below status line

        // Fix B (v1.6.12): highlight if D-pad focused OR mouse-hovered.
        // v1.8 Input-source latch: only one highlight source is active at a time.
        //   DPAD mode → dpad_focus_index_ wins; mouse_hover_index_ is suppressed
        //               (cursor is hidden in DPAD mode, so no hover confusion).
        //   MOUSE mode → mouse_hover_index_ wins; dpad_focus_index_ is suppressed
        //               (D-pad focus ring would be misleading while cursor drives).
        // v1.8.31: when tab focus is active, suppress the grid focus ring so the
        // user sees only the tab ring (mirrors the v1.8.28 favorites focus-clear
        // fix). Without this guard, both rings render and the grid ring looks
        // "stuck" — the user can't tell they entered tab mode.
        // v1.8.33: same goes for search-bar focus.
        const bool dpad_active     = (active_input_source_ == InputSource::DPAD);
        const bool grid_owns_focus = (tab_focus_idx_ == SIZE_MAX) && !search_focus_active_;
        const bool cell_highlighted = grid_owns_focus && (dpad_active
            ? (vpos == dpad_focus_index_)
            : (vpos == mouse_hover_index_));
        // For fullscreen, pass cy_screen (scroll-adjusted) instead of cy so
        // PaintCell draws at the correct screen position.
        const s32 cy_paint = windowed_mode_ ? cy : cy_screen;
        PaintCell(r, items_[item_idx], item_idx, cx, cy_paint, cell_highlighted);
    }

    // W11-SCROLL: page dots removed — single canvas has no pages to indicate.
    // (page_count_ / page_index_ are retained only for D-pad L/R scroll steps.)

    // ── 6. Status line ────────────────────────────────────────────────────────
    // v1.8.24 F-2: status_counts_[] is pre-populated by RebuildFilter() and
    // updated whenever the filter changes.  O(1) read replaces O(n) items_ walk.
    // [0]=Nintendo, [1]=Homebrew, [2]=Extras, [3]=Builtin (LpSortKind ordinals).
    //
    // v2.0.3.1: suppressed in windowed mode.  The host QdWindow exposes a
    // SetHintText() API that renders into the window's bottom bar — see
    // QdFolderLaunchpadElement, which now drives the bottom-bar text from
    // status_counts_ on filter rebuild.  Rendering both would double the info.
    if (!windowed_mode_) {
        PaintStatusLine(r, status_counts_[0], status_counts_[1],
                           status_counts_[2], status_counts_[3]);
    }

    // ── 7. QoL-T2: context menu overlay ──────────────────────────────────────
    // Rendered last so it occludes everything else (icons, tabs, status line).
    // No-op when not open.
    lp_ctx_menu_.Render(r);
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

    // v1.8.10: IconCategory::Payloads removed from the enum.  Payload entries
    // are now IconCategory::Extras but are distinguished by having an empty
    // nro_path (ScanPayloads sets e.nro_path[0] = '\0') combined with a
    // non-empty icon_path that carries the resolved payload filename.
    // The stable ID is "payload:<basename-of-icon_path>" which aligns with the
    // "payload:<fname>" form registered in qd_DesktopIcons.cpp ScanPayloads.
    if (item.nro_path[0] == '\0' && !item.is_builtin && item.app_id == 0u) {
        // Extract basename from icon_path (may be empty if no icon was resolved;
        // that yields "payload:" which is still the same degenerate stable ID the
        // previous Payloads branch produced when nro_path was empty).
        const char *p = item.icon_path;
        const char *slash = nullptr;
        for (const char *q = p; *q != '\0'; ++q) {
            if (*q == '/') {
                slash = q;
            }
        }
        const char *base = (slash != nullptr) ? (slash + 1) : p;
        std::string sid;
        sid.reserve(8u + strnlen(base, sizeof(item.icon_path)));
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
                                          int slot_idx,
                                          s32 tx, s32 ty,
                                          s32 tile_w, s32 tile_h,
                                          const char *label,
                                          size_t item_count,
                                          bool is_active)
{
    // v2.0.3: caller passes natural-coord (tx, ty); fold in the windowed-render
    // origin once so all SDL_Rects below shift uniformly with the host window.
    tx += render_origin_x_;
    ty += render_origin_y_;

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
    // W5-PERF-HOTSPOTS #2 (P0-B): cache one SDL_Texture* per tab slot keyed by
    // (label, item_count, is_active).  Eliminates ~480 alloc/destroy/s at 60 Hz × 8 tabs.
    // Build "<label> (N)" string. Max label buffer: 64 + 12 = 76 chars.
    char label_buf[LP_FOLDER_LABEL_BUF];
    snprintf(label_buf, sizeof(label_buf), "%s (%zu)", label, item_count);

    const pu::ui::Color text_col =
        is_active
            ? pu::ui::Color{ 0xFFu, 0xFFu, 0xFFu, 0xFFu }
            : pu::ui::Color{ theme_.text_secondary.r,
                             theme_.text_secondary.g,
                             theme_.text_secondary.b,
                             0xFFu };

    // Clamp slot index to valid cache range.
    const int si = (slot_idx >= 0 && slot_idx < LP_FOLDER_TILE_SLOTS) ? slot_idx : 0;
    // Invalidate the slot if key (label_str, count, active) changed.
    const bool key_changed = (strncmp(folder_tile_last_label_[si], label_buf, LP_FOLDER_LABEL_BUF) != 0)
                          || (folder_tile_last_count_[si]  != item_count)
                          || (folder_tile_last_active_[si] != is_active);
    if (key_changed) {
        if (folder_tile_tex_[si] != nullptr) {
            // W6-LEDGER: untrack old texture.
            UL_LEDGER_UNTRACK(folder_tile_lh_[si]);
            folder_tile_lh_[si] = 0;
            pu::ui::render::DeleteTexture(folder_tile_tex_[si]);
            folder_tile_tex_[si] = nullptr;
        }
        folder_tile_tex_[si] = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            label_buf, text_col,
            static_cast<u32>(tile_w - 12));
        // W6-LEDGER: track new texture.
        if (folder_tile_tex_[si] != nullptr) {
            char lh_tag[32];
            snprintf(lh_tag, sizeof(lh_tag), "lp:folder_tab_%d", si);
            folder_tile_lh_[si] = UL_LEDGER_TRACK(
                QdResKind::Texture, lh_tag, 0);
        }
        // memcpy avoids -Wstringop-truncation; snprintf guarantees null-termination
        // within LP_FOLDER_LABEL_BUF so the full copy is safe.
        memcpy(folder_tile_last_label_[si], label_buf, LP_FOLDER_LABEL_BUF);
        folder_tile_last_label_[si][LP_FOLDER_LABEL_BUF - 1] = '\0';
        folder_tile_last_count_[si]  = item_count;
        folder_tile_last_active_[si] = is_active;
    }

    SDL_Texture *lt = folder_tile_tex_[si];
    if (lt) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(lt, nullptr, nullptr, &lw, &lh);
        const s32 lx = tx + 6;
        const s32 ly = ty + (tile_h - lh) / 2;
        SDL_Rect ldst { lx, ly, lw, lh };
        SDL_RenderCopy(r, lt, nullptr, &ldst);
    }
}

// ── RebuildFilter ─────────────────────────────────────────────────────────────

void QdLaunchpadElement::RebuildFilter() {
    filtered_idxs_.clear();

    // A-4 (v1.7.2): populate per-bucket counts FIRST (before any early return)
    // so the folder tile strip always reflects the full items_ list regardless
    // of which filter path runs below.
    std::fill(std::begin(folder_bucket_count_), std::end(folder_bucket_count_), 0u);
    // v1.8.24 F-2: populate status_counts_[] in the same pass.
    // [0]=Nintendo, [1]=Homebrew, [2]=Extras, [3]=Builtin (matches LpSortKind enum).
    std::fill(std::begin(status_counts_), std::end(status_counts_), 0u);
    for (size_t bi = 0u; bi < items_.size(); ++bi) {
        const LpItem &it = items_[bi];
        // A5-OPT-1: use cached stable ID; falls back to computing if cache not yet built.
        const std::string &sid = (bi < items_stable_ids_.size())
            ? items_stable_ids_[bi]
            : StableIdForItem(it);
        const AutoFolderIdx fidx = LookupFolderIdx(sid);
        const u8 raw = static_cast<u8>(fidx);
        if (raw >= 1u && raw <= static_cast<u8>(kTopLevelFolderCount)) {
            folder_bucket_count_[raw - 1u] += 1u;  // kTopLevelFolders[0] = NxGames (idx=1)
        }
        // F-2: accumulate by sort kind (index matches LpSortKind ordinal).
        const u8 sk = static_cast<u8>(it.sort_kind);
        if (sk < 4u) {
            status_counts_[sk] += 1u;
        }
    }

    // Fix D (v1.6.12): build the query-lowercased string once, used below.
    // If query is empty AND no folder filter is active, fast-path all items.
    const bool folder_filter = (active_folder_ != AutoFolderIdx::None);

    if (query_.empty() && !folder_filter) {
        // No query, no folder filter: all non-hidden items visible.
        // QoL-T4: exclude items whose stable ID is in the QdVisibility hidden set.
        filtered_idxs_.reserve(items_.size());
        for (size_t i = 0u; i < items_.size(); ++i) {
            // A5-OPT-1: indexed read from cached stable IDs.
            const std::string &sid = (i < items_stable_ids_.size())
                ? items_stable_ids_[i]
                : StableIdForItem(items_[i]);
            if (QdVisibility::Get().IsHidden(sid)) {
                continue;
            }
            filtered_idxs_.push_back(i);
        }
        // W11-SCROLL: recalculate page_count_ and canvas height in the fast-path.
        const size_t nf_all = filtered_idxs_.size();
        if (nf_all == 0u) {
            page_count_ = 1u;
        } else {
            page_count_ = (nf_all + LP_ITEMS_PER_PAGE - 1u) / LP_ITEMS_PER_PAGE;
        }
        if (page_index_ >= page_count_) {
            page_index_ = page_count_ - 1u;
        }
        {
            const size_t rows = (nf_all == 0u) ? 1u
                : (nf_all + static_cast<size_t>(LP_COLS) - 1u) / static_cast<size_t>(LP_COLS);
            const s32 new_h = LP_GRID_Y
                            + static_cast<s32>(rows) * (LP_CELL_H + LP_GAP_Y)
                            + LP_FOOTER_H;
            if (new_h != lp_natural_h_) {
                lp_natural_h_       = new_h;
                lp_natural_h_dirty_ = true;
            }
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

        // QoL-T4: skip hidden items regardless of query/folder filter.
        // A5-OPT-1: indexed read from cached stable IDs.
        const std::string &sid_slow = (i < items_stable_ids_.size())
            ? items_stable_ids_[i]
            : StableIdForItem(it);
        if (QdVisibility::Get().IsHidden(sid_slow)) {
            continue;
        }

        // Fix D (v1.6.12): apply folder filter (cheapest check, reuses sid_slow).
        if (folder_filter) {
            const AutoFolderIdx fidx = LookupFolderIdx(sid_slow);
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

    // W11-SCROLL: recalculate page_count_ (used as D-pad scroll-step count).
    const size_t nf = filtered_idxs_.size();
    if (nf == 0u) {
        page_count_ = 1u;
    } else {
        page_count_ = (nf + LP_ITEMS_PER_PAGE - 1u) / LP_ITEMS_PER_PAGE;
    }
    // Clamp page_index_ so it stays within [0, page_count_ - 1].
    if (page_index_ >= page_count_) {
        page_index_ = page_count_ - 1u;
    }

    // W11-SCROLL: compute full canvas height for all filtered items.
    // Formula: HEADER_H + rows_needed * (LP_CELL_H + LP_GAP_Y) + LP_FOOTER_H
    // rows_needed = ceil(nf / LP_COLS), minimum 1.
    {
        const size_t rows_needed = (nf == 0u) ? 1u
            : (nf + static_cast<size_t>(LP_COLS) - 1u) / static_cast<size_t>(LP_COLS);
        const s32 new_h = LP_GRID_Y
                        + static_cast<s32>(rows_needed) * (LP_CELL_H + LP_GAP_Y)
                        + LP_FOOTER_H;
        if (new_h != lp_natural_h_) {
            lp_natural_h_       = new_h;
            lp_natural_h_dirty_ = true;
        }
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
    // v1.8.23 Option C: v1.8.22f per-slot diagnostic removed.  The cumulative
    // HW logs proved the v1.8.22d 2a-romfs branch state, so the once-per-slot
    // log line + paint_logged_ vector are no longer needed.

    // v2.0.3: caller passes natural-coord (cx, cy); fold in the windowed-render
    // origin once so every SDL_Rect derived from cx/cy/icon_x/icon_y inherits
    // the offset.
    cx += render_origin_x_;
    cy += render_origin_y_;

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
    if (cache_key) {
        // v1.8.18: shared singleton + shared mutex.  Both Desktop's background prewarm
        // thread and this render-thread PaintCell now share the same QdIconCache and
        // the same std::mutex, eliminating the duplicate-extraction on Launchpad open.
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        bgra = GetSharedIconCache().Get(cache_key);
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

    // ── 2a-romfs. v1.8.22d B66: payload entries with romfs-backed icon_path ──
    // ResolvePayloadIcon returns "romfs:/default/ui/Main/PayloadIcon/<name>.png"
    // for matched payload stems. These cannot route through the BGRA shared
    // cache because LoadJpegIconToCache opens fsdevGetDeviceFileSystem("sdmc")
    // and fails rc=0x2EEA02 for romfs paths, then writes a gray fallback BGRA
    // into the cache keyed by the romfs path. Load via LoadImageFromFile here
    // so IMG_Load can route through libnx fsdev to the romfs mount. Mirrors
    // the qd_DesktopIcons.cpp 2a-romfs branch (qd_DesktopIcons.cpp:2184-2217).
    if (bgra == nullptr
            && item.icon_path[0] == 'r' && item.icon_path[1] == 'o'
            && item.icon_path[2] == 'm' && item.icon_path[3] == 'f'
            && item.icon_path[4] == 's' && item.icon_path[5] == ':'
            && item_idx < icon_tex_.size()) {
        if (icon_tex_[item_idx] == nullptr) {
            icon_tex_[item_idx] =
                ::pu::ui::render::LoadImageFromFile(item.icon_path);
            icon_loaded_[item_idx] = (icon_tex_[item_idx] != nullptr);
            // v1.8.22e B66 proof-of-fire: log first-load result so HW logs
            // confirm the 2a-romfs branch is reachable + working.
            if (icon_tex_[item_idx] != nullptr) {
                UL_LOG_INFO("launchpad: 2a-romfs payload icon loaded path=%s",
                            item.icon_path);
            } else {
                UL_LOG_WARN("launchpad: 2a-romfs LoadImageFromFile FAILED "
                            "path=%s (romfs not mounted or asset missing?)",
                            item.icon_path);
            }
        }
        if (icon_tex_[item_idx] != nullptr) {
            SDL_Rect dst { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
            SDL_RenderCopy(r, icon_tex_[item_idx], nullptr, &dst);
        }
    }

    // ── 2b. F9 (stabilize-5): Builtin icon lazy-load via TryFindLoadImage ───────
    // When the BGRA cache misses for a Builtin entry, attempt to load the
    // per-slot PNG from romfs (e.g. "ui/Main/EntryIcon/DockVault.png").
    // Mirrors the qd_DesktopIcons.cpp IconKind::Builtin branch.
    // Only fires once per slot: icon_tex_[item_idx] is set on first hit and
    // reused on subsequent frames. Falls through to glyph if PNG is absent.
    if (bgra == nullptr && item.sort_kind == LpSortKind::Builtin
            && item_idx < icon_tex_.size()) {
        if (icon_tex_[item_idx] == nullptr) {
            // First render of this slot: try to load from romfs.
            static char dock_path_lp[128];
            snprintf(dock_path_lp, sizeof(dock_path_lp),
                     "ui/Main/EntryIcon/Dock%s", item.name);
            icon_tex_[item_idx] = ::ul::menu::ui::TryFindLoadImage(dock_path_lp);
        }
        if (icon_tex_[item_idx] != nullptr) {
            SDL_Rect dst { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
            SDL_RenderCopy(r, icon_tex_[item_idx], nullptr, &dst);
        }
    }

    // ── 3. Glyph fallback (when no icon art) ─────────────────────────────────
    // Only render glyph if BOTH the bgra cache and the Builtin tex are absent.
    // v1.8.2: render per-frame into local (LRU TextCacheClear invalidates stored ptrs).
    // v1.8.22d: also suppress glyph when the 2a-romfs path loaded a payload PNG.
    // v1.8.24 F-1: cache into glyph_tex_[item_idx]; render only on first paint.
    const bool has_builtin_tex = (item.sort_kind == LpSortKind::Builtin
                                  && item_idx < icon_tex_.size()
                                  && icon_tex_[item_idx] != nullptr);
    const bool has_romfs_tex = (item.icon_path[0] == 'r' && item.icon_path[1] == 'o'
                                && item.icon_path[2] == 'm' && item.icon_path[3] == 'f'
                                && item.icon_path[4] == 's' && item.icon_path[5] == ':'
                                && item_idx < icon_tex_.size()
                                && icon_tex_[item_idx] != nullptr);
    if (bgra == nullptr && !has_builtin_tex && !has_romfs_tex && item.glyph != '\0') {
        // Render once and cache; reuse on subsequent frames.
        if (item_idx < glyph_tex_.size() && glyph_tex_[item_idx] == nullptr) {
            const std::string gs(1, item.glyph);
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            glyph_tex_[item_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                gs, wh);
            // NOTE: do NOT call DeleteTexture here — texture is retained in cache.
            // v2.0.3-A4: cache dims at create.
            if (glyph_tex_[item_idx] != nullptr
                && item_idx < glyph_tex_w_.size()) {
                SDL_QueryTexture(glyph_tex_[item_idx], nullptr, nullptr,
                                 &glyph_tex_w_[item_idx], &glyph_tex_h_[item_idx]);
            }
        }
        SDL_Texture *glyph_tex = (item_idx < glyph_tex_.size())
                                 ? glyph_tex_[item_idx]
                                 : nullptr;
        if (glyph_tex != nullptr && item_idx < glyph_tex_w_.size()) {
            // v2.0.3-A4: dims cached above — no per-frame SDL_QueryTexture.
            const int gw = glyph_tex_w_[item_idx];
            const int gh = glyph_tex_h_[item_idx];
            SDL_Rect gdst {
                icon_x + (LP_ICON_W - gw) / 2,
                icon_y + (LP_ICON_H - gh) / 2,
                gw, gh
            };
            SDL_RenderCopy(r, glyph_tex, nullptr, &gdst);
        }
    }

    // ── 4. Name label ─────────────────────────────────────────────────────────
    // v1.8.2: render per-frame into local (LRU TextCacheClear invalidates stored ptrs).
    // v1.8.24 F-1: cache into name_tex_[item_idx]; render only on first paint.
    if (item.name[0] != '\0') {
        // Render once and cache; reuse on subsequent frames.
        if (item_idx < name_tex_.size() && name_tex_[item_idx] == nullptr) {
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
            // NOTE: do NOT call DeleteTexture here — texture is retained in cache.
            // v2.0.3-A4: cache dims at create.
            if (name_tex_[item_idx] != nullptr
                && item_idx < name_tex_w_.size()) {
                SDL_QueryTexture(name_tex_[item_idx], nullptr, nullptr,
                                 &name_tex_w_[item_idx], &name_tex_h_[item_idx]);
            }
        }
        SDL_Texture *name_tex = (item_idx < name_tex_.size())
                                ? name_tex_[item_idx]
                                : nullptr;
        if (name_tex != nullptr && item_idx < name_tex_w_.size()) {
            // v2.0.3-A4: dims cached above — no per-frame SDL_QueryTexture.
            const int nw = name_tex_w_[item_idx];
            const int nh = name_tex_h_[item_idx];
            SDL_Rect ndst {
                cx + (LP_CELL_W - nw) / 2,
                icon_y + LP_ICON_H + 4,
                nw, nh
            };
            SDL_RenderCopy(r, name_tex, nullptr, &ndst);
        }
    }

    // ── 5. Focus ring ─────────────────────────────────────────────────────────
    if (is_focused) {
        // v3.7 unified selection language: rounded 3px ring + soft glow in the
        // per-theme focus_ring (replaces the hard 2×1px double rectangle), sitting
        // just outside the icon art (radius/sm = 8px corners).
        DrawFocusRing(r, icon_x - 3, icon_y - 3, LP_ICON_W + 6, LP_ICON_H + 6,
                      8, 3, theme_.focus_ring);
    }

    // ── v1.7.0-stabilize-7 Slice 5 (O-F Patch 2): star overlay ────────────────
    // U+2605 BLACK STAR ★ rendered top-right of the icon when favorited.
    // v2.0.3-A5: star_tex_ rendered once at Open(); blit per-frame here.  The
    // per-frame RenderText + DeleteTexture (B57 cluster history) is gone.
    if (IsFavoriteByLpItem(item) && star_tex_ != nullptr) {
        const int sw = star_tex_w_;
        const int sh = star_tex_h_;
        SDL_Rect sdst {
            icon_x + LP_ICON_W - sw - 4,
            icon_y + 4,
            sw, sh
        };
        SDL_RenderCopy(r, star_tex_, nullptr, &sdst);
    }
}

// ── FreeSlotTextures ──────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeSlotTextures(size_t item_idx) {
    // icon_tex_ slots are SDL_CreateTexture-owned; use SDL_DestroyTexture.
    auto free_sdl = [](SDL_Texture *&t) {
        if (t != nullptr) {
            SDL_DestroyTexture(t);
            t = nullptr;
        }
    };
    // name_tex_ and glyph_tex_ slots are RenderText-cache-owned; use DeleteTexture.
    auto free_pu = [](SDL_Texture *&t) {
        if (t != nullptr) {
            pu::ui::render::DeleteTexture(t);
            t = nullptr;
        }
    };
    if (item_idx < icon_tex_.size())    { free_sdl(icon_tex_[item_idx]);   }
    if (item_idx < icon_loaded_.size()) { icon_loaded_[item_idx] = false;  }
    // v1.8.24 F-1: free cached name and glyph textures for this slot.
    if (item_idx < name_tex_.size())    { free_pu(name_tex_[item_idx]);    }
    if (item_idx < glyph_tex_.size())   { free_pu(glyph_tex_[item_idx]);   }
    // v2.0.3-A4: clear parallel dim caches.
    if (item_idx < name_tex_w_.size())  { name_tex_w_[item_idx]  = 0; name_tex_h_[item_idx]  = 0; }
    if (item_idx < glyph_tex_w_.size()) { glyph_tex_w_[item_idx] = 0; glyph_tex_h_[item_idx] = 0; }
}

// ── FreeAllTextures ───────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeAllTextures() {
    // A5-OPT-1: clear stable-ID cache; will be rebuilt on next Open().
    items_stable_ids_.clear();

    // icon_tex_ slots: SDL_CreateTexture-owned — use SDL_DestroyTexture.
    for (size_t i = 0u; i < icon_tex_.size(); ++i)  {
        if (icon_tex_[i])  { SDL_DestroyTexture(icon_tex_[i]);  icon_tex_[i]  = nullptr; }
    }
    icon_tex_.clear();
    icon_loaded_.clear();

    // v1.8.24 F-1: free cached name and glyph textures.
    // These are RenderText-cache-owned — use DeleteTexture (NOT SDL_DestroyTexture).
    for (size_t i = 0u; i < name_tex_.size(); ++i) {
        if (name_tex_[i])  { pu::ui::render::DeleteTexture(name_tex_[i]);  name_tex_[i]  = nullptr; }
    }
    name_tex_.clear();
    for (size_t i = 0u; i < glyph_tex_.size(); ++i) {
        if (glyph_tex_[i]) { pu::ui::render::DeleteTexture(glyph_tex_[i]); glyph_tex_[i] = nullptr; }
    }
    glyph_tex_.clear();
    // v2.0.3-A4: clear parallel dim caches in lockstep.
    name_tex_w_.clear();
    name_tex_h_.clear();
    glyph_tex_w_.clear();
    glyph_tex_h_.clear();

    // W5-PERF-HOTSPOTS #2 (P0-B): free cached folder-tab label textures.
    for (int i = 0; i < LP_FOLDER_TILE_SLOTS; ++i) {
        if (folder_tile_tex_[i] != nullptr) {
            // W6-LEDGER: untrack before free.
            UL_LEDGER_UNTRACK(folder_tile_lh_[i]);
            folder_tile_lh_[i] = 0;
            pu::ui::render::DeleteTexture(folder_tile_tex_[i]);
            folder_tile_tex_[i] = nullptr;
        }
        folder_tile_last_label_[i][0] = '\0';
        folder_tile_last_count_[i]    = static_cast<size_t>(-1);  // sentinel: force re-render
        folder_tile_last_active_[i]   = false;
    }
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

// v2.0.3-A5: PaintStatusLine no longer renders/frees per frame.  It caches
// the texture in status_line_tex_, invalidated by status_line_cached_counts_[]
// snapshot mismatch (RebuildFilter changes the counts; everywhere else they
// are stable).  The const qualifier was dropped — method now mutates member
// cache state.
void QdLaunchpadElement::PaintStatusLine(SDL_Renderer *r,
                                          size_t total_nintendo,
                                          size_t total_homebrew,
                                          size_t total_extras,
                                          size_t total_builtins)
{
    const u32 c0 = static_cast<u32>(total_nintendo);
    const u32 c1 = static_cast<u32>(total_homebrew);
    const u32 c2 = static_cast<u32>(total_extras);
    const u32 c3 = static_cast<u32>(total_builtins);
    const bool counts_changed =
        (c0 != status_line_cached_counts_[0]) ||
        (c1 != status_line_cached_counts_[1]) ||
        (c2 != status_line_cached_counts_[2]) ||
        (c3 != status_line_cached_counts_[3]);

    if (counts_changed || status_line_tex_ == nullptr) {
        // Free prior texture — RenderText LRU-owned, MUST use DeleteTexture
        // per AGENT-REGRESSION-RISK §4 rule 2 (P-B cluster, 7 incidents incl.
        // v1.8.25 PaintStatusLine itself).
        if (status_line_tex_) {
            pu::ui::render::DeleteTexture(status_line_tex_);
            status_line_tex_ = nullptr;
            status_line_tex_w_ = 0;
            status_line_tex_h_ = 0;
        }
        char buf[160];
        // W11-SCROLL: hint text updated — "L/R Page" replaced with scroll nav.
        snprintf(buf, sizeof(buf),
                 "%zu nintendo  %zu homebrew  %zu extras  %zu built-in"
                 "  |  B Close  \xe2\x86\x91\xe2\x86\x93\xe2\x86\x90\xe2\x86\x92 Navigate  A Launch  ZL Menu",
                 total_nintendo, total_homebrew, total_extras, total_builtins);
        const pu::ui::Color sc { theme_.text_secondary.r, theme_.text_secondary.g,
                                  theme_.text_secondary.b, 0xFFu };
        status_line_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            buf, sc);
        if (status_line_tex_) {
            SDL_QueryTexture(status_line_tex_, nullptr, nullptr,
                             &status_line_tex_w_, &status_line_tex_h_);
        }
        status_line_cached_counts_[0] = c0;
        status_line_cached_counts_[1] = c1;
        status_line_cached_counts_[2] = c2;
        status_line_cached_counts_[3] = c3;
    }

    if (status_line_tex_) {
        const int sw = status_line_tex_w_;
        const int sh = status_line_tex_h_;
        // Centre horizontally; 8 px above the bottom edge.
        // v2.0.3: shift by render_origin_ for windowed launchpad.
        const s32 sx = render_origin_x_ + (1920 - sw) / 2;
        const s32 sy = render_origin_y_ + 1080 - sh - 8;
        SDL_Rect sd { sx, sy, sw, sh };
        SDL_RenderCopy(r, status_line_tex_, nullptr, &sd);
    }
}

// ── PaintPageDots ─────────────────────────────────────────────────────────────
// F10 (stabilize-5): draw a row of small filled squares centred horizontally
// at y == 1040, in the gap between the icon grid and the status line.
// Active page dot: accent colour, full alpha.
// Inactive page dots: text_secondary colour, full alpha.
// Only called when page_count_ > 1.
void QdLaunchpadElement::PaintPageDots(SDL_Renderer *r) const {
    static constexpr s32 DOT_SIZE = 12;  // Bug #1: was 8, too small
    static constexpr s32 DOT_GAP  = 8;   // Bug #1: was 4, too tight

    const s32 n = static_cast<s32>(page_count_);
    const s32 total_w = n * DOT_SIZE + (n - 1) * DOT_GAP;
    // v2.0.3: shift by render_origin_ for windowed launchpad.
    s32 dot_x = render_origin_x_ + (1920 - total_w) / 2;
    static constexpr s32 DOT_Y_NATURAL = 1015;  // Bug #1: was 1040, overlapped "1/2" text
    const s32 DOT_Y = render_origin_y_ + DOT_Y_NATURAL;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (size_t i = 0u; i < page_count_; ++i) {
        if (i == page_index_) {
            SDL_SetRenderDrawColor(r,
                theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
        } else {
            SDL_SetRenderDrawColor(r,
                theme_.text_secondary.r,
                theme_.text_secondary.g,
                theme_.text_secondary.b,
                0xFFu);
        }
        const SDL_Rect dot { dot_x, DOT_Y, DOT_SIZE, DOT_SIZE };
        SDL_RenderFillRect(r, &dot);
        dot_x += DOT_SIZE + DOT_GAP;
    }
}

// ── EnsureFocusVisible (W11-SCROLL) ──────────────────────────────────────────
//
// Adjusts the scroll offset so the currently D-pad-focused cell is visible
// in the viewport.  Called after every D-pad navigation event.
//
// Viewport height estimate:
//   For fullscreen: 1080 - LP_GRID_Y - LP_FOOTER_H pixels of grid area,
//   i.e. the number of pixels between the grid top and the status line.
//   This is the same as LP_ROWS * (LP_CELL_H + LP_GAP_Y) for the 4-row layout.
//
// For windowed launchpad with lp_scroll_cb_ set, fires the callback with
// the target scroll_y value (natural pixels from top of canvas).
//
// For fullscreen launchpad (lp_scroll_cb_ not set), updates lp_scroll_y_.

void QdLaunchpadElement::EnsureFocusVisible() {
    if (dpad_focus_index_ >= filtered_idxs_.size()) {
        return;
    }

    // Compute the natural-canvas Y of the focused row top.
    s32 cx = 0, cy = 0;
    CellXY(dpad_focus_index_, cx, cy);
    (void)cx;

    // Viewport in natural pixels (grid area, top-to-bottom).
    // For fullscreen this is the screen height minus header and footer.
    // For windowed mode the host window knows its own viewport height;
    // we use the same estimate since QdWindow will clamp the scroll anyway.
    constexpr s32 kViewportH = 1080 - LP_GRID_Y - LP_FOOTER_H;

    if (lp_scroll_cb_) {
        // Windowed mode: fire the callback so the host can call SetScrollOffset.
        // Best effort: scroll so the focused row is at the top of the viewport.
        // If it's already on-screen (QdWindow clips) this is a no-op in practice.
        const s32 target_sy = cy - LP_GRID_Y;
        lp_scroll_cb_(target_sy < 0 ? 0 : target_sy);
        return;
    }

    // Fullscreen mode: adjust lp_scroll_y_ so the focused row is visible.
    // cy_screen = cy - lp_scroll_y_.
    // Visible range: [0, kViewportH) relative to LP_GRID_Y.
    const s32 cy_relative = cy - LP_GRID_Y;  // focused row offset from grid top
    const s32 cell_bottom = cy_relative + LP_CELL_H;

    // Scroll up if the cell top is above the current viewport top.
    if (cy_relative < lp_scroll_y_) {
        lp_scroll_y_ = cy_relative;
    }
    // Scroll down if the cell bottom is below the current viewport bottom.
    else if (cell_bottom > lp_scroll_y_ + kViewportH) {
        lp_scroll_y_ = cell_bottom - kViewportH;
    }

    // Clamp lp_scroll_y_ to valid range [0, max_scroll].
    const s32 canvas_grid_h = lp_natural_h_ - LP_GRID_Y - LP_FOOTER_H;
    const s32 max_scroll = (canvas_grid_h > kViewportH)
                           ? canvas_grid_h - kViewportH
                           : 0;
    if (lp_scroll_y_ < 0) {
        lp_scroll_y_ = 0;
    }
    if (lp_scroll_y_ > max_scroll) {
        lp_scroll_y_ = max_scroll;
    }
}

} // namespace ul::menu::qdesktop
