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
#include <ul/menu/qdesktop/qd_LaunchpadHostLayout.hpp> // v1.8.1: SFX dispatch via LP_PLAY_SFX
#include <ul/menu/ui/ui_MenuApplication.hpp>         // F3 (stabilize-4): g_MenuApplication + MenuType
#include <ul/menu/ui/ui_Common.hpp>                  // F9 (stabilize-5): TryFindLoadImage for Builtin icons
#include <ul/ul_Result.hpp>                         // UL_LOG_INFO
#include <pu/ui/render/render_Renderer.hpp>          // pu::ui::render::GetMainRenderer
#include <pu/ui/ui_Types.hpp>                        // pu::ui::GetDefaultFont / DefaultFontSize

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
      // v1.8 Input-source latch: Launchpad opens in DPAD mode on Switch (the
      // user triggers Open via hot-corner tap or Plus — both are controller/touch
      // actions; D-pad or touch-tile navigation follows immediately).
      active_input_source_(InputSource::DPAD),
      page_index_(0),    // F10 (stabilize-5): pagination
      page_count_(1),    // F10 (stabilize-5): pagination
      // v1.8.18: icon_cache_ removed; using GetSharedIconCache() singleton.
      folder_bucket_count_{},  // A-4 (v1.7.2): zero-init; populated in RebuildFilter()
      // v1.8.24 F-2: status-bar counters; zero-init; populated in RebuildFilter().
      status_counts_{},
      // v1.8.24 F-3: search bar texture cache; null until first render in Open().
      search_bar_tex_(nullptr),
      search_bar_cached_text_(),
      search_bar_caret_visible_(false),
      // v1.8.24 F-4: hot-corner Q glyph; rendered once in Open(), freed in Close()/dtor.
      q_glyph_tex_(nullptr)
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
    FreeAllTextures();
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdLaunchpadElement::AdvanceTick() {
    ++frame_tick_;
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
    page_index_                = 0;  // F10 (stabilize-5): always start at first page
    page_count_                = 1;  // F10 (stabilize-5): recalculated in RebuildFilter
    // v1.7.0-stabilize-2: reset edge-trigger latch so the same finger-down
    // that triggered Open() does not immediately fire the close handler on
    // the very next frame. The latch must be true while a still-down finger
    // is sliding off the corner, then drop to false when the finger lifts.
    lp_was_touch_active_last_frame_ = true;
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

    // v1.8.24 F-3: reset search bar cache on every Open() so stale textures from
    // a previous open cycle are not reused (query_ was cleared above).
    if (search_bar_tex_) {
        pu::ui::render::DeleteTexture(search_bar_tex_);
        search_bar_tex_ = nullptr;
    }
    search_bar_cached_text_.clear();
    search_bar_caret_visible_ = false;

    // Build the initial (unfiltered) filtered index list.
    // NOTE: RebuildFilter() also populates status_counts_[] (F-2).
    RebuildFilter();

    // v1.8.24 F-4: render the hot-corner "Q" glyph once, reuse each frame.
    // The SDL renderer is available by the time Open() runs (Plutonium is live).
    // Free any prior texture from a previous open cycle (idempotent guard above).
    if (q_glyph_tex_) {
        pu::ui::render::DeleteTexture(q_glyph_tex_);
        q_glyph_tex_ = nullptr;
    }
    {
        const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        q_glyph_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            "Q", wh);
    }

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
    // v1.8.18: icon_cache_ removed; no reset needed (using GetSharedIconCache() singleton).
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
        const bool touch_corner_now = touch_active_now
                                      && tx >= 0 && tx < LP_HOTCORNER_W
                                      && ty >= 0 && ty < LP_HOTCORNER_H;
        const bool touch_corner_edge = touch_corner_now
                                       && !lp_was_touch_active_last_frame_;
        if (touch_corner_edge) {
            UL_LOG_INFO("qdesktop: Launchpad hot-corner CLOSE tap edge tx=%d ty=%d", tx, ty);
            Close();
            // F11 (stabilize-6): mirror QdLaunchpadHostLayout::OnMenuInput's
            // B/Plus path — leave the element VISIBLE so the next
            // LoadMenu(MenuType::Launchpad) actually renders. SetVisible(false)
            // here orphans the element for every subsequent Open cycle because
            // Plutonium's per-element dispatch gates OnRender + OnInput on
            // IsVisible() and Open() does not toggle visibility. Counter-example
            // proof at qd_LaunchpadHostLayout.cpp:32-40 (B/Plus path).
            SetVisible(true);
            // F3 (stabilize-4): restore the Main desktop layout so the screen
            // is not left blank after closing.  Previously Close()+SetVisible(false)
            // hid the Launchpad but never switched the active MenuType back, so
            // the user saw a black screen on close.
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
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
    {
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

    // ── A-9 (v1.7.2.1): Touch tap on a Launchpad grid tile ───────────────────
    // Provides MNR §6 / §33 single-fire touch-launch on the tile grid. Prior
    // to v1.7.2.1 the grid was D-pad+A or ZR+mouse only; touch was wired only
    // for hot-corner close (above) and folder-strip filter (above). Inside an
    // active folder filter (active_folder_ != None) the existing dispatch path
    // already uses filtered_idxs_[dpad_focus_index_] via FocusedDesktopIdx(),
    // so setting dpad_focus_index_ to the touched cell + raising
    // pending_launch_ launches the correct item under any filter.
    //
    // Hit-test inverts CellXY: (tx, ty) → (col, row) → vpos. Edge-trigger via
    // lp_was_touch_prev (captured at the top of OnInput before the hot-corner
    // block updates the latch) prevents tap-and-hold from firing every frame.
    // Gap rejection prevents fingers landing in the inter-cell space from
    // launching a neighbouring tile.
    {
        const bool has_touch_tile     = !touch_pos.IsEmpty();
        const bool is_touch_tile_edge = has_touch_tile && !lp_was_touch_prev;
        if (is_touch_tile_edge) {
            const s32 tx     = static_cast<s32>(touch_pos.x);
            const s32 ty     = static_cast<s32>(touch_pos.y);
            const s32 grid_w = LP_COLS * (LP_CELL_W + LP_GAP_X);
            const s32 grid_h = LP_ROWS * (LP_CELL_H + LP_GAP_Y);
            if (tx >= LP_GRID_X && tx < LP_GRID_X + grid_w &&
                ty >= LP_GRID_Y && ty < LP_GRID_Y + grid_h) {
                const s32 dx           = tx - LP_GRID_X;
                const s32 dy           = ty - LP_GRID_Y;
                const s32 col          = dx / (LP_CELL_W + LP_GAP_X);
                const s32 row          = dy / (LP_CELL_H + LP_GAP_Y);
                const s32 cell_local_x = dx - col * (LP_CELL_W + LP_GAP_X);
                const s32 cell_local_y = dy - row * (LP_CELL_H + LP_GAP_Y);
                if (cell_local_x < LP_CELL_W && cell_local_y < LP_CELL_H &&
                    col < LP_COLS && row < LP_ROWS) {
                    const size_t cell_pos = static_cast<size_t>(row) * static_cast<size_t>(LP_COLS)
                                          + static_cast<size_t>(col);
                    const size_t vpos     = page_index_ * LP_ITEMS_PER_PAGE + cell_pos;
                    if (vpos < n) {
                        UL_LOG_INFO("qdesktop: Launchpad tile touch-launch tx=%d ty=%d vpos=%zu",
                                    tx, ty, vpos);
                        // v1.8 Input-source latch: touch tap is unambiguously
                        // MOUSE/touch mode.  Use pending_launch_from_mouse_=true
                        // so DispatchPendingLaunch picks mouse_hover_index_.
                        // We also set mouse_hover_index_ to the touched vpos so
                        // DispatchPendingLaunch resolves correctly (the cursor
                        // may not be hovering over the tapped cell on Switch
                        // because cursor position is updated by QdCursorElement
                        // which follows touch, but may lag one frame).
                        active_input_source_       = InputSource::MOUSE;
                        mouse_hover_index_         = vpos;
                        dpad_focus_index_          = vpos;
                        pending_launch_            = true;
                        pending_launch_from_mouse_ = true;
                    }
                }
            }
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

    // ── A: launch based on current input source ──────────────────────────────
    // v1.8 Input-source latch: in DPAD mode, A launches the D-pad focused tile
    // (pending_launch_from_mouse_=false → DispatchPendingLaunch uses
    // dpad_focus_index_).  In MOUSE mode, A launches the cursor-hovered tile
    // (pending_launch_from_mouse_=true → uses mouse_hover_index_).
    // A does NOT change the active_input_source_ itself (spec requirement).
    if (keys_down & HidNpadButton_A) {
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
    // Mirrors qd_DesktopIcons.cpp:Y handling at the focused-tile level.
    // Y on the dpad-focused item flips its favorite state. Star overlay (in
    // PaintCell) reflects the new state on the next paint via the lazy build.
    if (keys_down & HidNpadButton_Y) {
        if (dpad_focus_index_ < n) {
            const size_t item_idx = filtered_idxs_[dpad_focus_index_];
            if (item_idx < items_.size()) {
                ToggleFavoriteByLpItem(items_[item_idx]);
            }
        }
    }

    // ── F10 (stabilize-5): L / R — page navigation ───────────────────────────
    // L steps to the previous page; R steps to the next page.
    // After a page change, clamp dpad_focus_index_ to the items on the new page.
    // mouse_hover_index_ is reset to SIZE_MAX (cursor position is page-relative
    // so hovered item changes identity after a page turn).
    if (keys_down & HidNpadButton_L) {
        if (page_index_ > 0u) {
            --page_index_;
            // Move D-pad focus to the first item of the new page.
            dpad_focus_index_ = page_index_ * LP_ITEMS_PER_PAGE;
            mouse_hover_index_ = SIZE_MAX;
        }
    }
    if (keys_down & HidNpadButton_R) {
        if (page_index_ + 1u < page_count_) {
            ++page_index_;
            // Move D-pad focus to the first item of the new page.
            dpad_focus_index_ = page_index_ * LP_ITEMS_PER_PAGE;
            // Clamp to last item if the new page has fewer than LP_ITEMS_PER_PAGE.
            if (dpad_focus_index_ >= n) {
                dpad_focus_index_ = (n > 0u) ? (n - 1u) : 0u;
            }
            mouse_hover_index_ = SIZE_MAX;
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
    // F7 (stabilize-5): Block A re-enabled — "Q" glyph in hot-corner.
    // Previously wrapped in #if 0 (stabilize-4) due to Plutonium RenderText crash
    // on Erista.  Re-enabled for stabilize-5; if a crash recurs on HW the
    // root cause is in Plutonium font metrics, not this call site.
    // v1.8.2: render per-frame into local (LRU TextCacheClear invalidates stored ptrs).
    // v1.8.24 F-4: q_glyph_tex_ rendered once at Open(); reused here each frame.
    if (q_glyph_tex_) {
        int tw = 0, th = 0;
        SDL_QueryTexture(q_glyph_tex_, nullptr, nullptr, &tw, &th);
        SDL_Rect td { (LP_HOTCORNER_W - tw) / 2, (LP_HOTCORNER_H - th) / 2, tw, th };
        SDL_RenderCopy(r, q_glyph_tex_, nullptr, &td);
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
        }

        // Blit the cached texture (may be nullptr if RenderText returned null).
        if (search_bar_tex_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(search_bar_tex_, nullptr, nullptr, &tw, &th);
            const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
            SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
            SDL_RenderCopy(r, search_bar_tex_, nullptr, &td);
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
    {
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

    // v1.8.23 Option C: F1 section-headers deferred-block removed.  The
    // original code (RenderText per visible section per frame, uncached)
    // was permanently disabled via #if 0 in stabilize-4 due to GPU pool
    // exhaustion on Switch; the static-cached label strategy referenced in
    // the original comment was never implemented and there is no plan to
    // resurrect this code path.  Auditor R2 flagged it as pure dead code.

    // ── 5. Icon grid (F10: sliced to current page) ───────────────────────────
    const size_t nf         = filtered_idxs_.size();
    const size_t page_start = page_index_ * LP_ITEMS_PER_PAGE;
    const size_t page_end   = (page_start + LP_ITEMS_PER_PAGE < nf)
                              ? page_start + LP_ITEMS_PER_PAGE
                              : nf;
    for (size_t vpos = page_start; vpos < page_end; ++vpos) {
        const size_t item_idx = filtered_idxs_[vpos];
        if (item_idx >= items_.size()) { continue; }

        // cell_pos is page-local so CellXY maps row 0..LP_ROWS-1 correctly.
        const size_t cell_pos = vpos - page_start;
        s32 cx = 0, cy = 0;
        CellXY(cell_pos, cx, cy);

        // Cull cells that would render below the status line (1080 - 48 = 1032).
        if (cy + LP_CELL_H > 1032) { continue; }

        // Fix B (v1.6.12): highlight if D-pad focused OR mouse-hovered.
        // v1.8 Input-source latch: only one highlight source is active at a time.
        //   DPAD mode → dpad_focus_index_ wins; mouse_hover_index_ is suppressed
        //               (cursor is hidden in DPAD mode, so no hover confusion).
        //   MOUSE mode → mouse_hover_index_ wins; dpad_focus_index_ is suppressed
        //               (D-pad focus ring would be misleading while cursor drives).
        // dpad_focus_index_ and mouse_hover_index_ are global filtered indices.
        const bool dpad_active  = (active_input_source_ == InputSource::DPAD);
        const bool cell_highlighted = dpad_active
            ? (vpos == dpad_focus_index_)
            : (vpos == mouse_hover_index_);
        PaintCell(r, items_[item_idx], item_idx, cx, cy, cell_highlighted);
    }

    // F10: render page indicator dots when more than one page exists.
    if (page_count_ > 1u) {
        PaintPageDots(r);
    }

    // ── 6. Status line ────────────────────────────────────────────────────────
    // v1.8.24 F-2: status_counts_[] is pre-populated by RebuildFilter() and
    // updated whenever the filter changes.  O(1) read replaces O(n) items_ walk.
    // [0]=Nintendo, [1]=Homebrew, [2]=Extras, [3]=Builtin (LpSortKind ordinals).
    PaintStatusLine(r, status_counts_[0], status_counts_[1],
                       status_counts_[2], status_counts_[3]);
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
        pu::ui::render::DeleteTexture(lt);  // B61: lt is cache-owned (RenderText); DeleteTexture no-ops the free, sets nullptr
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
    for (const LpItem &it : items_) {
        const std::string sid = StableIdForItem(it);
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
        // No query, no folder filter: all items visible.
        filtered_idxs_.reserve(items_.size());
        for (size_t i = 0u; i < items_.size(); ++i) {
            filtered_idxs_.push_back(i);
        }
        // Fix T7 (v1.7.2.3): recalculate page_count_ in the fast-path.
        // Previously this block returned early without updating page_count_,
        // leaving it at 1 (set by Open()). Effect: only the first 40 items
        // were ever rendered in the "All" view, and page-nav dots never showed.
        // Mirror the same formula used in the slow-path at lines 1243-1252.
        const size_t nf_all = filtered_idxs_.size();
        if (nf_all == 0u) {
            page_count_ = 1u;
        } else {
            page_count_ = (nf_all + LP_ITEMS_PER_PAGE - 1u) / LP_ITEMS_PER_PAGE;
        }
        if (page_index_ >= page_count_) {
            page_index_ = page_count_ - 1u;
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

    // F10 (stabilize-5): recalculate page_count_ after filter is rebuilt.
    // page_count_ = ceil(filtered_idxs_.size() / LP_ITEMS_PER_PAGE), minimum 1.
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
        }
        SDL_Texture *glyph_tex = (item_idx < glyph_tex_.size())
                                 ? glyph_tex_[item_idx]
                                 : nullptr;
        if (glyph_tex != nullptr) {
            int gw = 0, gh = 0;
            SDL_QueryTexture(glyph_tex, nullptr, nullptr, &gw, &gh);
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
        }
        SDL_Texture *name_tex = (item_idx < name_tex_.size())
                                ? name_tex_[item_idx]
                                : nullptr;
        if (name_tex != nullptr) {
            int nw = 0, nh = 0;
            SDL_QueryTexture(name_tex, nullptr, nullptr, &nw, &nh);
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
        SDL_SetRenderDrawColor(r,
            theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0xFFu);
        SDL_Rect ring { icon_x - 2, icon_y - 2, LP_ICON_W + 4, LP_ICON_H + 4 };
        SDL_RenderDrawRect(r, &ring);
        // Second ring (thicker visual) one pixel inside.
        SDL_Rect ring2 { icon_x - 1, icon_y - 1, LP_ICON_W + 2, LP_ICON_H + 2 };
        SDL_RenderDrawRect(r, &ring2);
    }

    // ── v1.7.0-stabilize-7 Slice 5 (O-F Patch 2): star overlay ────────────────
    // U+2605 BLACK STAR ★ rendered top-right of the icon when favorited.
    // v1.8.2: render per-frame into local (LRU TextCacheClear invalidates stored ptrs).
    if (IsFavoriteByLpItem(item)) {
        const pu::ui::Color white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        SDL_Texture *star_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            "\xe2\x98\x85", white);  // U+2605 BLACK STAR ★
        if (star_tex != nullptr) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(star_tex, nullptr, nullptr, &sw, &sh);
            SDL_Rect sdst {
                icon_x + LP_ICON_W - sw - 4,
                icon_y + 4,
                sw, sh
            };
            SDL_RenderCopy(r, star_tex, nullptr, &sdst);
            pu::ui::render::DeleteTexture(star_tex);  // B57: free per-frame local
        }
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
}

// ── FreeAllTextures ───────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeAllTextures() {
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
    s32 dot_x = (1920 - total_w) / 2;
    static constexpr s32 DOT_Y = 1015;  // Bug #1: was 1040, overlapped "1/2" text

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

} // namespace ul::menu::qdesktop
