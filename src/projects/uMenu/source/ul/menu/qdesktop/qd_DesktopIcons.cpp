// qd_DesktopIcons.cpp — Auto-grid desktop icon element for uMenu C++ SP3 (v1.3.0).
// Ported from tools/mock-nro-desktop-gui/src/desktop_icons.rs + wm.rs.
// Scans sdmc:/switch/*.nro once at construction; paints icon cells every frame.
// SP2 additions: dock magnify animation via UpdateDockMagnify + two-pass OnRender.
// SP3 additions: SetApplicationEntries() ingests installed Switch apps; JPEG icon loading
//   via LoadJpegIconToCache(); LaunchIcon() dispatches smi::LaunchApplication for apps.

#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_IconCategory.hpp>
#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/menu/ui/ui_Common.hpp>  // ShowSettingsMenu/ShowAlbum/etc. for Special launch
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Pull in SDL2 directly (sdl2_Types.hpp aliases Renderer = SDL_Renderer*).
#include <SDL2/SDL.h>
// SDL2_image: used for JPEG decoding of Application icon files.
#include <SDL2/SDL_image.h>

namespace ul::menu::qdesktop {

// ── Built-in dock icon table ──────────────────────────────────────────────────
// 6 entries: { display_name, glyph_char, r, g, b }
// Verbatim from desktop_icons.rs BUILTIN_ICONS.
struct BuiltinIconDef {
    const char *name;
    char        glyph;
    u8          r, g, b;
};

static constexpr BuiltinIconDef BUILTIN_ICON_DEFS[BUILTIN_ICON_COUNT] = {
    { "Vault",      'V', 0x7D, 0xD3, 0xFC },
    { "Terminal",   'T', 0xE0, 0xE0, 0xF0 },
    { "Monitor",    'M', 0x4A, 0xDE, 0x80 },
    { "Control",    'C', 0x4A, 0xDE, 0x80 },
    { "About",      'A', 0xF8, 0x71, 0x71 },
    { "VaultSplit", 'S', 0xFB, 0xBF, 0x24 },
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdDesktopIconsElement::QdDesktopIconsElement(const QdTheme &theme)
    : theme_(theme), icon_count_(0), focused_idx_(0),
      prev_magnify_center_(-1), magnify_center_(-1), frame_tick_(0),
      app_entry_start_idx_(0)
{
    UL_LOG_INFO("qdesktop: QdDesktopIconsElement ctor entry");

    // Ensure icon cache dir exists before any icon I/O.
    const bool cache_dir_ok = cache_.EnsureDir();
    UL_LOG_INFO("qdesktop: icon cache dir ensure result = %d", cache_dir_ok ? 1 : 0);

    // Pre-populate built-in dock icons, then scan NRO files.
    PopulateBuiltins();
    const size_t after_builtins = icon_count_;
    ScanNros();
    const size_t after_scan = icon_count_;

    // Record where Application entries will begin.  SetApplicationEntries() truncates
    // icon_count_ back to this index and re-appends on every call.
    app_entry_start_idx_ = icon_count_;

    UL_LOG_INFO("qdesktop: builtins=%zu scan_added=%zu app_slot_start=%zu total_static=%zu",
                after_builtins, after_scan - after_builtins,
                app_entry_start_idx_, after_scan);
}

QdDesktopIconsElement::~QdDesktopIconsElement() {
    // All members are value-typed — nothing to free.
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdDesktopIconsElement::AdvanceTick() {
    static bool logged_once = false;
    if (!logged_once) {
        UL_LOG_INFO("qdesktop: AdvanceTick first call (F-06 tick site active)");
        logged_once = true;
    }
    cache_.AdvanceTick();
    ++frame_tick_;
}

// ── UpdateDockMagnify ─────────────────────────────────────────────────────────
// SP2-F12: dock proximity zone uses 30px (×1.5 from Rust 20px).
// SP2-F13: magnify_center_ = -1 when cursor is outside the dock zone.
//
// Dock layout constants (×1.5 from Rust):
//   SCREEN_H = 1080, DOCK_H = 108 → dock_nominal_top = 1080 - 108 = 972
//   DOCK_SLOT_SIZE = 84, DOCK_SLOT_GAP = 18
//   Proximity threshold: DOCK_SLOT_SIZE + DOCK_SLOT_GAP = 102
//
// Magnify scale table (×100 fixed-point, mirrors wm.rs):
//   dist 0 → 140, dist 1 → 120, dist 2 → 105, dist ≥3 → 100

static constexpr int32_t DOCK_H           = 108;    // ×1.5 from Rust 72
static constexpr int32_t DOCK_NOMINAL_TOP = 1080 - DOCK_H;  // 972
static constexpr int32_t DOCK_SLOT_SIZE   = 84;     // ×1.5 from Rust 56
static constexpr int32_t DOCK_SLOT_GAP    = 18;     // ×1.5 from Rust 12
static constexpr int32_t DOCK_PROX_ZONE   = 30;     // SP2-F12: ×1.5 from Rust 20
static constexpr int32_t DOCK_PROX_THRESH = DOCK_SLOT_SIZE + DOCK_SLOT_GAP; // 102

void QdDesktopIconsElement::UpdateDockMagnify(int32_t cursor_y) {
    prev_magnify_center_ = magnify_center_;

    // Cursor must be in or near the dock zone.
    if (cursor_y < DOCK_NOMINAL_TOP - DOCK_PROX_ZONE) {
        magnify_center_ = -1;
        return;
    }

    // Find the nearest builtin dock slot by horizontal centre.
    // Built-in slots 0..BUILTIN_ICON_COUNT are the dock icons.
    // For simplicity we treat them as evenly spaced starting from ICON_GRID_LEFT.
    // The dock centre of slot i = ICON_GRID_LEFT + i * (DOCK_SLOT_SIZE + DOCK_SLOT_GAP)
    //                             + DOCK_SLOT_SIZE / 2.
    // (We do not need cursor_x here — SP2 magnify is proximity-by-slot-index only,
    //  matching the Rust wm.rs cursor_y-only proximity check for dock zone entry.)

    // No cursor_x available in AdvanceTick (input-driven context), so we use the
    // nearest slot determination based purely on dock-zone entry for SP2.
    // The Rust wm.rs magnify logic picks the nearest slot to cursor_x when in zone.
    // Since UpdateDockMagnify is called without cursor_x in SP2, we keep the last
    // focused dock slot as the magnify center, defaulting to slot 0 on first entry.
    // Full cursor_x wiring is deferred to SP3 (input method refactor).

    if (magnify_center_ == -1) {
        // Entering dock zone: start at slot 0.
        magnify_center_ = 0;
    }
    // magnify_center_ persists across frames while cursor_y is in zone.
}

// Returns magnify scale ×100 for dock slot `slot_idx` given current magnify center.
// dist 0→140, 1→120, 2→105, else→100.
static int32_t dock_magnify_scale_x100(int32_t slot_idx, int32_t magnify_center) {
    if (magnify_center < 0) {
        return 100;
    }
    const int32_t dist = (slot_idx >= magnify_center)
                       ? (slot_idx - magnify_center)
                       : (magnify_center - slot_idx);
    switch (dist) {
        case 0: return 140;
        case 1: return 120;
        case 2: return 105;
        default: return 100;
    }
}

// ── PopulateBuiltins ─────────────────────────────────────────────────────────

void QdDesktopIconsElement::PopulateBuiltins() {
    for (size_t i = 0; i < BUILTIN_ICON_COUNT; ++i) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        NroEntry &e = icons_[icon_count_];
        // Copy display name (guaranteed to fit in 64 bytes).
        strncpy(e.name, BUILTIN_ICON_DEFS[i].name, sizeof(e.name) - 1u);
        e.name[sizeof(e.name) - 1u] = '\0';
        e.glyph        = BUILTIN_ICON_DEFS[i].glyph;
        e.bg_r         = BUILTIN_ICON_DEFS[i].r;
        e.bg_g         = BUILTIN_ICON_DEFS[i].g;
        e.bg_b         = BUILTIN_ICON_DEFS[i].b;
        e.nro_path[0]  = '\0';
        e.icon_path[0] = '\0';
        e.is_builtin   = true;
        e.dock_slot    = static_cast<u8>(i);
        e.category     = NroCategory::QosApp;
        e.icon_loaded  = false;
        e.kind         = IconKind::Builtin;
        e.app_id       = 0;
        ++icon_count_;
    }
}

// ── ScanNros ─────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::ScanNros() {
    DIR *d = opendir("sdmc:/switch/");
    if (!d) {
        return; // SD not mounted or directory absent — silently skip.
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        const char *fname = ent->d_name;

        // Skip hidden files (leading dot).
        if (fname[0] == '.') {
            continue;
        }

        // Skip non-.nro files.
        const size_t flen = strlen(fname);
        if (flen < 5u) { // ".nro" + at least 1 char
            continue;
        }
        if (strcmp(fname + flen - 4u, ".nro") != 0) {
            continue;
        }

        // Build the full path "sdmc:/switch/<fname>".
        char nro_path[768];
        int written = snprintf(nro_path, sizeof(nro_path), "sdmc:/switch/%s", fname);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(nro_path)) {
            continue; // Path too long — skip.
        }

        // Build the display name: stem = fname without ".nro".
        char stem[64];
        const size_t stem_len = flen - 4u; // strip ".nro"
        if (stem_len == 0u || stem_len >= sizeof(stem)) {
            continue;
        }
        memcpy(stem, fname, stem_len);
        stem[stem_len] = '\0';

        // Classify: no NACP name available at scan time — pass empty string.
        CategoryResult cat = Classify("", stem);

        NroEntry &e = icons_[icon_count_];
        // snprintf always null-terminates and silences -Wstringop-truncation.
        snprintf(e.name,     sizeof(e.name),     "%s", stem);
        e.glyph        = cat.glyph;
        e.bg_r         = cat.r;
        e.bg_g         = cat.g;
        e.bg_b         = cat.b;
        snprintf(e.nro_path, sizeof(e.nro_path), "%s", nro_path);
        e.icon_path[0] = '\0';
        e.is_builtin   = false;
        e.dock_slot    = 0;
        e.category     = cat.category;
        e.icon_loaded  = false;
        e.kind         = IconKind::Nro;
        e.app_id       = 0;
        ++icon_count_;
    }

    closedir(d);
}

// ── HashToColor ───────────────────────────────────────────────────────────────
// u32 DJB2 on first 16 bytes of name → HSL(hue, 0.55, 0.40) → RGB.
// Mirrors desktop_icons.rs hash_to_color + hsl_to_rgb exactly.

// static
void QdDesktopIconsElement::HashToColor(const char *name,
                                         u8 &out_r, u8 &out_g, u8 &out_b)
{
    u32 h = 5381u;
    const u8 *p = reinterpret_cast<const u8 *>(name);
    for (size_t i = 0u; i < 16u && p[i] != '\0'; ++i) {
        h = h * 33u + static_cast<u32>(p[i]);
    }
    const u32 hue_deg = h % 360u;

    // Delegate to qd_NroAsset.hpp HSL→RGB which uses the identical formula.
    HslToRgb(hue_deg, 0.55f, 0.40f, out_r, out_g, out_b);
}

// ── CellRect ─────────────────────────────────────────────────────────────────

bool QdDesktopIconsElement::CellRect(size_t i, s32 &out_x, s32 &out_y) const {
    if (i >= icon_count_) {
        return false;
    }
    const size_t col = i % static_cast<size_t>(ICON_GRID_COLS);
    const size_t row = i / static_cast<size_t>(ICON_GRID_COLS);
    if (row >= static_cast<size_t>(ICON_GRID_MAX_ROWS)) {
        return false;
    }
    out_x = ICON_GRID_LEFT
            + static_cast<s32>(col) * (ICON_CELL_W + ICON_GRID_GAP_X);
    out_y = ICON_GRID_TOP
            + static_cast<s32>(row) * ICON_CELL_H;
    return true;
}

// ── HitTest ───────────────────────────────────────────────────────────────────

size_t QdDesktopIconsElement::HitTest(s32 tx, s32 ty) const {
    for (size_t i = 0u; i < icon_count_; ++i) {
        s32 cx, cy;
        if (!CellRect(i, cx, cy)) {
            continue;
        }
        if (tx >= cx && ty >= cy && tx < cx + ICON_CELL_W && ty < cy + ICON_CELL_H) {
            return i;
        }
    }
    return MAX_ICONS; // sentinel: no hit
}

// ── PaintIconCell ─────────────────────────────────────────────────────────────

// Renders one icon cell at grid-pixel position (x, y).
// Layout (C++ ×1.5 from Rust):
//   background rect: (x + ICON_BG_INSET, y + 6, ICON_BG_W, ICON_BG_H)
//   icon texture:    same rect, 64×64 BGRA scaled via SDL
//   focus ring:      stroke rect 1px outside background
//
// Hover: saturate-add 40 to each R/G/B channel.
// Text labels are out-of-scope for SP1 (no TTF font through raw SDL path).

void QdDesktopIconsElement::PaintIconCell(SDL_Renderer *r,
                                           const NroEntry &entry,
                                           s32 x, s32 y,
                                           bool is_focused)
{
    // The background rect origin.
    // Rust reference: bg_x = x + ICON_BG_INSET; bg_y = y + 4 (at 1280×720).
    // C++ (×1.5): bg_y = y + 6.
    const s32 bg_x = x + ICON_BG_INSET;
    const s32 bg_y = y + 6;

    // Determine whether this cell is under the touch cursor this frame.
    // For SP1 we derive hover from focus identity only (no pointer device).
    const bool is_hovered = is_focused;

    // Compute the fill colour with optional hover highlight.
    const u8 base_r = entry.bg_r;
    const u8 base_g = entry.bg_g;
    const u8 base_b = entry.bg_b;

    // saturating_add(40) per Rust desktop_icons.rs hover path.
    const u8 fill_r = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_r) + 40))
        : base_r;
    const u8 fill_g = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_g) + 40))
        : base_g;
    const u8 fill_b = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_b) + 40))
        : base_b;

    // ── 1. Background filled rectangle ────────────────────────────────────
    SDL_SetRenderDrawColor(r, fill_r, fill_g, fill_b, 0xFFu);
    SDL_Rect bg_rect { bg_x, bg_y, ICON_BG_W, ICON_BG_H };
    SDL_RenderFillRect(r, &bg_rect);

    // ── 2. Cached icon texture blit ───────────────────────────────────────
    // Determine which path string to use as the cache key:
    //   - NRO entries    → nro_path  (ASET JPEG encoded per NRO spec)
    //   - Application    → icon_path (SD-card JPEG, may be empty)
    //   - Builtin        → no icon (nro_path and icon_path are both empty)
    const u8 *bgra = nullptr;
    if (entry.kind == IconKind::Application && entry.icon_path[0] != '\0') {
        bgra = cache_.Get(entry.icon_path);
    } else if (entry.nro_path[0] != '\0') {
        bgra = cache_.Get(entry.nro_path);
    }

    if (bgra != nullptr) {
        // Upload BGRA pixels to a temporary streaming texture and blit.
        // SDL_PIXELFORMAT_ABGR8888 on ARM little-endian → byte order [R,G,B,A],
        // which matches our CACHE_ENTRY_BYTES buffer layout.
        SDL_Texture *tex = SDL_CreateTexture(r,
                                             SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             static_cast<int>(CACHE_ICON_W),
                                             static_cast<int>(CACHE_ICON_H));
        if (tex != nullptr) {
            SDL_UpdateTexture(tex, nullptr, bgra,
                              static_cast<int>(CACHE_ICON_W) * 4);
            SDL_Rect dst { bg_x, bg_y, ICON_BG_W, ICON_BG_H };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
    }

    // ── 3. Focus ring ─────────────────────────────────────────────────────
    if (is_focused) {
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
        SDL_Rect ring { bg_x - 1, bg_y - 1, ICON_BG_W + 2, ICON_BG_H + 2 };
        SDL_RenderDrawRect(r, &ring);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                      const s32 x, const s32 y)
{
    // F-06 fix: LRU tick is owned by the public AdvanceTick() method.
    // OnRender does NOT call cache_.AdvanceTick() here to avoid double-counting
    // if the layout calls AdvanceTick() once per frame externally.
    // The QDESKTOP_MODE integration block in ui_MainMenuLayout.cpp does NOT call
    // AdvanceTick() on children — so tick advancement currently relies solely on
    // QdDesktopIconsElement::AdvanceTick() being called once per frame by the owner.

    // Obtain the raw SDL_Renderer* through Plutonium's accessor.
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    {
        static bool logged_once = false;
        if (!logged_once) {
            UL_LOG_INFO("qdesktop: DesktopIcons OnRender first call renderer=%p icons=%zu at x=%d y=%d",
                        static_cast<void*>(r), icon_count_, x, y);
            logged_once = true;
        }
    }
    if (!r) {
        return;
    }

    // ── SP2: Two-pass dock magnify layout for builtin dock slots ─────────────
    // Pass 1: compute magnify-scaled widths for each builtin slot.
    // Pass 2: walk left-to-right accumulating actual x positions.
    // Non-builtin icons (NRO grid) are rendered at nominal CellRect positions.

    // Magnify-scaled sizes for builtin slots [0, BUILTIN_ICON_COUNT).
    s32 magnify_w[BUILTIN_ICON_COUNT];
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        const s32 scale_x100 = dock_magnify_scale_x100(static_cast<s32>(i), magnify_center_);
        // Scaled width = ICON_BG_W * scale / 100, bounded to [ICON_BG_W, ICON_BG_W*2].
        const s32 sw = static_cast<s32>(
            static_cast<int64_t>(ICON_BG_W) * scale_x100 / 100LL);
        magnify_w[i] = sw;
    }

    // Compute total expanded dock width for re-centering.
    s32 total_expanded_w = 0;
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        total_expanded_w += magnify_w[i];
        if (i + 1u < BUILTIN_ICON_COUNT) {
            total_expanded_w += ICON_GRID_GAP_X;
        }
    }

    // Centre the expanded dock band within the screen (1920px).
    s32 expanded_start_x = (1920 - total_expanded_w) / 2;

    // Pre-compute each builtin slot's actual left edge in Pass 2.
    s32 builtin_slot_x[BUILTIN_ICON_COUNT];
    {
        s32 walk = expanded_start_x;
        for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
            builtin_slot_x[i] = walk;
            walk += magnify_w[i] + ICON_GRID_GAP_X;
        }
    }

    // ── Render all icons ─────────────────────────────────────────────────────

    for (size_t i = 0u; i < icon_count_; ++i) {
        s32 cell_x, cell_y;

        if (i < BUILTIN_ICON_COUNT) {
            // Dock slot: use two-pass magnify position.
            // cell_y: dock is placed at DOCK_NOMINAL_TOP row.
            cell_x = builtin_slot_x[i] + x;
            cell_y = DOCK_NOMINAL_TOP + y;
        } else {
            // NRO grid icon: use nominal CellRect.
            if (!CellRect(i, cell_x, cell_y)) {
                continue;
            }
            cell_x += x;
            cell_y += y;
        }

        NroEntry &entry = icons_[i];

        // ── Lazy icon load ────────────────────────────────────────────────
        // On the first frame for each non-builtin, non-loaded icon, load the icon
        // into the cache.  Strategy depends on entry kind:
        //   Nro         → extract JPEG from NRO ASET section (existing path).
        //   Application → decode on-disk JPEG at icon_path (new SP3 path).
        //                 If icon_path is empty, generate hash-derived fallback.
        if (!entry.icon_loaded && entry.kind != IconKind::Builtin) {
            if (entry.kind == IconKind::Nro && entry.nro_path[0] != '\0') {
                const u8 *cached = cache_.Get(entry.nro_path);
                if (cached == nullptr) {
                    // Not on disk either — load from NRO.
                    NroIconResult res = ExtractNroIcon(entry.nro_path);
                    if (res.valid && res.pixels != nullptr
                            && res.width > 0 && res.height > 0) {
                        cache_.Put(entry.nro_path, res.pixels, res.width, res.height);
                        FreeNroIcon(res);
                    } else {
                        // Extraction failed — generate a fallback 64×64 RGBA block.
                        // ExtractNroIcon always allocates res.pixels (even on failure);
                        // free it here before it goes out of scope (F-03 fix).
                        FreeNroIcon(res);
                        u8 *fallback = MakeFallbackIcon(entry.nro_path);
                        if (fallback != nullptr) {
                            cache_.Put(entry.nro_path, fallback,
                                       static_cast<s32>(CACHE_ICON_W),
                                       static_cast<s32>(CACHE_ICON_H));
                            delete[] fallback;
                        }
                    }
                }
            } else if (entry.kind == IconKind::Application) {
                if (entry.icon_path[0] != '\0') {
                    // Application has a custom JPEG icon on the SD card.
                    // Check cache first; load from disk on miss.
                    const u8 *cached = cache_.Get(entry.icon_path);
                    if (cached == nullptr) {
                        LoadJpegIconToCache(entry.icon_path, entry.icon_path);
                    }
                } else {
                    // No icon path available — insert a hash-derived fallback keyed
                    // on the application name so it is stable across reboots.
                    // We key by name because app_id does not fit the char* path API.
                    // Fabricate a pseudo-path: "app:<hex app_id>" as the cache key.
                    char app_cache_key[32];
                    snprintf(app_cache_key, sizeof(app_cache_key),
                             "app:%016llx",
                             static_cast<unsigned long long>(entry.app_id));
                    const u8 *cached = cache_.Get(app_cache_key);
                    if (cached == nullptr) {
                        u8 *fallback = MakeFallbackIcon(entry.name);
                        if (fallback != nullptr) {
                            cache_.Put(app_cache_key, fallback,
                                       static_cast<s32>(CACHE_ICON_W),
                                       static_cast<s32>(CACHE_ICON_H));
                            delete[] fallback;
                        }
                    }
                    // Store the pseudo-path into icon_path so PaintIconCell can find
                    // the cached texture.  We copy into icon_path (not nro_path)
                    // since the kind discriminant already routes to icon_path.
                    snprintf(entry.icon_path, sizeof(entry.icon_path),
                             "%s", app_cache_key);
                }
            }
            entry.icon_loaded = true;
        }

        const bool focused = (i == focused_idx_);
        PaintIconCell(r, entry, cell_x, cell_y, focused);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::OnInput(const u64 keys_down,
                                     const u64 keys_up,
                                     const u64 keys_held,
                                     const pu::ui::TouchPoint touch_pos)
{
    (void)keys_up;
    (void)keys_held;

    // A-button: launch currently focused icon.
    if (keys_down & HidNpadButton_A) {
        LaunchIcon(focused_idx_);
    }

    // Touch: hit-test and launch.
    if (!touch_pos.IsEmpty()) {
        const size_t hit = HitTest(touch_pos.x, touch_pos.y);
        if (hit < icon_count_) {
            LaunchIcon(hit);
        }
    }
}

// ── LaunchIcon ────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::LaunchIcon(size_t i) {
    if (i >= icon_count_) {
        return;
    }
    const NroEntry &entry = icons_[i];

    switch (entry.kind) {
        case IconKind::Builtin:
            // Built-in dock shortcuts: no launch action in SP3 (SP1 scope-out preserved).
            return;

        case IconKind::Nro:
            if (entry.nro_path[0] != '\0') {
                smi::LaunchHomebrewLibraryApplet(
                    std::string(entry.nro_path), std::string(""));
            }
            return;

        case IconKind::Application:
            if (entry.app_id != 0) {
                const Result rc = smi::LaunchApplication(entry.app_id);
                if (R_FAILED(rc)) {
                    UL_LOG_WARN("qdesktop: LaunchApplication(0x%016llx) failed rc=0x%X",
                                static_cast<unsigned long long>(entry.app_id),
                                static_cast<unsigned>(rc));
                }
            }
            return;

        case IconKind::Special: {
            // Dispatch to the matching ShowXxx() helper from ui_Common.
            // The mapping mirrors the upstream MainMenuLayout switch-case for
            // SpecialEntry types — see ui_MainMenuLayout.cpp:265-305.
            using ET = ul::menu::EntryType;
            const auto et = static_cast<ET>(entry.special_subtype);
            switch (et) {
                case ET::SpecialEntrySettings:    ::ul::menu::ui::ShowSettingsMenu(); break;
                case ET::SpecialEntryAlbum:       ::ul::menu::ui::ShowAlbum();        break;
                case ET::SpecialEntryThemes:      ::ul::menu::ui::ShowThemesMenu();   break;
                case ET::SpecialEntryControllers: ::ul::menu::ui::ShowController();   break;
                case ET::SpecialEntryMiiEdit:     ::ul::menu::ui::ShowMiiEdit();      break;
                case ET::SpecialEntryWebBrowser:  ::ul::menu::ui::ShowWebPage();      break;
                case ET::SpecialEntryUserPage:    ::ul::menu::ui::ShowUserPage();     break;
                case ET::SpecialEntryAmiibo:      ::ul::menu::ui::ShowCabinet();      break;
                default:
                    UL_LOG_WARN("qdesktop: Special launch unknown subtype=%u",
                                static_cast<unsigned>(entry.special_subtype));
                    break;
            }
            return;
        }
    }
    // Unreachable: exhaustive switch above covers all IconKind values.
}

// ── SetApplicationEntries ─────────────────────────────────────────────────────
// Replaces the Application section of the icon grid with freshly provided entries.
// Truncates icon_count_ back to app_entry_start_idx_ to discard stale apps from the
// previous call, then re-appends matching entries up to MAX_ICONS.
//
// Only entries satisfying ALL of the following are added:
//   - entry.Is<EntryType::Application>()
//   - entry.app_info.CanBeLaunched()     (has contents, not awaiting update)
//   - entry.app_info.app_id != 0
//
// The display name is taken from entry.control.name (non-empty) or synthesised as
// "App 0x<hex_app_id>" for titles with empty NACP.
//
// Fallback icon colour is DJB2-derived from the display name (same as NRO path).
// If entry.control.icon_path is non-empty (custom_icon_path), it is stored in
// NroEntry::icon_path so OnRender can load the JPEG on the first paint.

void QdDesktopIconsElement::SetApplicationEntries(
        const std::vector<ul::menu::Entry> &entries)
{
    // Truncate back to the Application slot boundary (idempotent reload).
    icon_count_ = app_entry_start_idx_;

    size_t added = 0;
    for (const auto &entry : entries) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }

        // Filter: must be a launchable Application.
        if (!entry.Is<ul::menu::EntryType::Application>()) {
            continue;
        }
        if (!entry.app_info.CanBeLaunched()) {
            continue;
        }
        if (entry.app_info.app_id == 0) {
            continue;
        }

        NroEntry &e = icons_[icon_count_];

        // ── Display name ─────────────────────────────────────────────────
        if (!entry.control.name.empty()) {
            snprintf(e.name, sizeof(e.name), "%s", entry.control.name.c_str());
        } else {
            snprintf(e.name, sizeof(e.name), "App %016llx",
                     static_cast<unsigned long long>(entry.app_info.app_id));
        }

        // ── Glyph and fallback colour ────────────────────────────────────
        // Application entries use Unknown category (glyph '?', grey) unless
        // the NACP name happens to match a known category keyword.  We try the
        // classifier on the NACP name and an empty file stem (no NRO path).
        {
            CategoryResult cat = Classify(e.name, "");
            e.glyph = cat.glyph;
            e.bg_r  = cat.r;
            e.bg_g  = cat.g;
            e.bg_b  = cat.b;
        }

        // ── Paths ─────────────────────────────────────────────────────────
        e.nro_path[0] = '\0'; // Applications have no NRO path.

        if (entry.control.custom_icon_path && !entry.control.icon_path.empty()) {
            snprintf(e.icon_path, sizeof(e.icon_path), "%s",
                     entry.control.icon_path.c_str());
        } else {
            e.icon_path[0] = '\0';
        }

        // ── Metadata ─────────────────────────────────────────────────────
        e.is_builtin  = false;
        e.dock_slot   = 0;
        e.category    = NroCategory::Unknown;
        e.icon_loaded = false; // forces lazy load on first OnRender pass
        e.kind        = IconKind::Application;
        e.app_id      = entry.app_info.app_id;

        ++icon_count_;
        ++added;
    }

    UL_LOG_INFO("qdesktop: SetApplicationEntries: in=%zu added=%zu total=%zu",
                entries.size(), added, icon_count_);
}

// ── LoadJpegIconToCache ───────────────────────────────────────────────────────
// Reads the file at jpeg_path and decodes it as a JPEG (or any SDL2_image-supported
// format) into a 32-bit RGBA surface, then calls cache_.Put() with the RGBA pixels.
// cache_key is passed to Put() as the path hash key; it need not equal jpeg_path.
//
// Returns true if the JPEG was decoded and the cache was populated with real image
// data.  Returns false (and still populates the cache with a fallback solid-colour
// block keyed by cache_key) on any of:
//   - File not found / not readable
//   - SDL2_image decode failure
//   - Surface conversion to RGBA failure
//
// The fallback is derived from cache_key via MakeFallbackIcon so the colour is stable.

bool QdDesktopIconsElement::LoadJpegIconToCache(const char *jpeg_path,
                                                 const char *cache_key)
{
    // Attempt to load and decode the file.
    SDL_Surface *raw = IMG_Load(jpeg_path);
    if (raw == nullptr) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: IMG_Load(%s) failed: %s",
                    jpeg_path, IMG_GetError());
        // Fall through to generate a colour-coded fallback.
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // Convert to RGBA8888 so our ScaleToBgra64 path receives the expected layout.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);

    if (rgba == nullptr) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: SDL_ConvertSurface failed: %s",
                    SDL_GetError());
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // Lock the surface to access pixel data (required for non-RLE surfaces).
    if (SDL_LockSurface(rgba) != 0) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: SDL_LockSurface failed: %s",
                    SDL_GetError());
        SDL_FreeSurface(rgba);
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    const s32 src_w = rgba->w;
    const s32 src_h = rgba->h;

    // SDL2 RGBA8888 on little-endian ARM is stored as [A, B, G, R] per byte in
    // memory (i.e., 0xRRGGBBAA stored little-endian = bytes [AA BB GG RR]).
    // Our ScaleToBgra64 expects standard RGBA layout [R, G, B, A].  The pixel
    // format constant SDL_PIXELFORMAT_RGBA8888 packs channels as R8G8B8A8 in
    // *big-endian* notation.  On the Switch (AArch64, little-endian), the in-memory
    // byte order of a pixel is [A, B, G, R] (least-significant byte first in a u32).
    // That is actually ABGR in byte order — identical to what SDL2_image ordinarily
    // calls SDL_PIXELFORMAT_ABGR8888.
    //
    // We convert to the explicit ABGR format to pass the correct byte-order into
    // ScaleToBgra64 which expects true RGBA (bytes: R G B A).

    // Re-convert to a format whose byte order matches our caller's assumption.
    SDL_UnlockSurface(rgba);
    SDL_Surface *rgba_le = SDL_ConvertSurfaceFormat(rgba,
                                                     SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rgba);

    if (rgba_le == nullptr) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: ABGR re-convert failed: %s",
                    SDL_GetError());
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    if (SDL_LockSurface(rgba_le) != 0) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: SDL_LockSurface(le) failed: %s",
                    SDL_GetError());
        SDL_FreeSurface(rgba_le);
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // src_w / src_h still valid from the first conversion (dimensions unchanged).
    const u8 *le_pixels = static_cast<const u8 *>(rgba_le->pixels);

    // ScaleToBgra64 expects RGBA byte layout [R G B A], which is what ABGR8888
    // little-endian gives us for the byte sequence.  The cache Put() call scales to
    // 64×64, swaps R↔B, and writes 64×64 BGRA to the disk cache.
    cache_.Put(cache_key, le_pixels, src_w, src_h);

    SDL_UnlockSurface(rgba_le);
    SDL_FreeSurface(rgba_le);

    UL_LOG_INFO("qdesktop: LoadJpegIconToCache: loaded %s (%d×%d) → cache key %s",
                jpeg_path, src_w, src_h, cache_key);
    return true;
}

} // namespace ul::menu::qdesktop
