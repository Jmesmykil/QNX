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
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>  // Cycle D5: dev toggles
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
// Plutonium text rendering — RenderText, GetDefaultFont, DefaultFontSize.
#include <pu/ui/ui_Types.hpp>
// MenuApplication — needed for FadeOutToNonLibraryApplet + Finalize on game launch.
#include <ul/menu/ui/ui_MenuApplication.hpp>

// Global menu application instance (defined in main.cpp).  Used in LaunchIcon
// for IconKind::Application to gracefully suspend uMenu before the game takes
// the foreground; without this the system applet state is wrong and Home
// won't return to the desktop after the game runs.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

// Cycle G1 (SP4.15): read suspended-app state so LaunchIcon can route
// Application launches through Resume / close-and-relaunch instead of always
// firing a fresh smi::LaunchApplication.  Defined in main.cpp.
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── Click tolerance ───────────────────────────────────────────────────────────
// Maximum Euclidean pixel displacement (squared comparison to avoid sqrt) from
// TouchDown to TouchUp that still classifies as a click rather than a drag.
// 24 px in 1920×1080 layout space ≈ 16 px on the physical 1280×720 panel.
static constexpr s32 CLICK_TOLERANCE_PX = 24;

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
      app_entry_start_idx_(0),
      pressed_(false), down_x_(0), down_y_(0),
      last_touch_x_(0), last_touch_y_(0),
      down_idx_(MAX_ICONS), was_touch_active_last_frame_(false),
      cursor_ref_(nullptr)
{
    UL_LOG_INFO("qdesktop: QdDesktopIconsElement ctor entry");

    // Initialise per-slot cached textures.  std::array<T*, N> default-
    // constructs to indeterminate pointer values, so we explicitly null them.
    for (size_t i = 0; i < MAX_ICONS; ++i) {
        name_text_tex_[i]  = nullptr;
        glyph_text_tex_[i] = nullptr;
        icon_tex_[i]        = nullptr;
    }

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
    // Free cached name/glyph text textures (created lazily in PaintIconCell).
    // SDL_DestroyTexture is null-safe, but we guard explicitly for clarity.
    for (size_t i = 0; i < MAX_ICONS; ++i) {
        FreeCachedText(i);
    }
}

// ── FreeCachedText ─────────────────────────────────────────────────────────────
// Releases the cached name + glyph text textures for one icon slot and resets
// the slot pointers to nullptr so a subsequent paint will re-rasterise.
void QdDesktopIconsElement::FreeCachedText(size_t entry_idx) {
    if (entry_idx >= MAX_ICONS) {
        return;
    }
    if (name_text_tex_[entry_idx] != nullptr) {
        SDL_DestroyTexture(name_text_tex_[entry_idx]);
        name_text_tex_[entry_idx] = nullptr;
    }
    if (glyph_text_tex_[entry_idx] != nullptr) {
        SDL_DestroyTexture(glyph_text_tex_[entry_idx]);
        glyph_text_tex_[entry_idx] = nullptr;
    }
    // Also free the cached icon BGRA texture for this slot.
    // This is the texture that was previously allocated every frame; it is
    // now allocated once here (lazily in PaintIconCell) and freed here on slot
    // reset, ensuring the GPU pool sees only one allocation per active icon.
    if (icon_tex_[entry_idx] != nullptr) {
        SDL_DestroyTexture(icon_tex_[entry_idx]);
        icon_tex_[entry_idx] = nullptr;
    }
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
        e.special_subtype = 0;
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
        e.special_subtype = 0;
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
                                           size_t entry_idx,
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

    if (bgra != nullptr && entry_idx < MAX_ICONS) {
        // Lazily build the per-slot icon texture on first paint; reuse every
        // subsequent frame.  Previously the code created and destroyed a new
        // SDL_Texture here each frame (~1 200 GPU allocs/sec at 20 icons × 60 fps),
        // fragmenting the Switch's 8 MB GPU pool and causing progressive lag.
        //
        // Rebuild if the texture has not been created yet for this slot.
        // Invalidation (when icon_loaded flips to false and FreeCachedText is
        // called on slot reset) already sets icon_tex_[entry_idx] = nullptr, so
        // the condition below will re-create the texture on the next render.
        if (icon_tex_[entry_idx] == nullptr) {
            // The cache buffer is BGRA byte-order in memory: bytes [B,G,R,A]
            // (qd_IconCache::ScaleToBgra64 explicitly writes dst[0]=B, dst[1]=G,
            // dst[2]=R, dst[3]=A — see qd_IconCache.cpp:146-149).
            //
            // SDL pixel-format constants use big-endian word notation, so on
            // AArch64 LE the memory byte order is reversed from the constant
            // name:
            //   SDL_PIXELFORMAT_ARGB8888 → memory bytes [B,G,R,A]   ← we want this
            //   SDL_PIXELFORMAT_ABGR8888 → memory bytes [R,G,B,A]   ← OLD value, wrong
            //   SDL_PIXELFORMAT_RGBA8888 → memory bytes [A,B,G,R]
            //   SDL_PIXELFORMAT_BGRA8888 → memory bytes [A,R,G,B]
            //
            // Using ABGR8888 here previously caused a persistent R↔B swap on
            // every cached icon (skin tones purple, sky orange) because the
            // texture interpreted byte[0] as R while the cache wrote B there.
            icon_tex_[entry_idx] = SDL_CreateTexture(r,
                                                      SDL_PIXELFORMAT_ARGB8888,
                                                      SDL_TEXTUREACCESS_STREAMING,
                                                      static_cast<int>(CACHE_ICON_W),
                                                      static_cast<int>(CACHE_ICON_H));
            if (icon_tex_[entry_idx] != nullptr) {
                SDL_UpdateTexture(icon_tex_[entry_idx], nullptr, bgra,
                                  static_cast<int>(CACHE_ICON_W) * 4);
            }
        }
        if (icon_tex_[entry_idx] != nullptr) {
            SDL_Rect dst { bg_x, bg_y, ICON_BG_W, ICON_BG_H };
            SDL_RenderCopy(r, icon_tex_[entry_idx], nullptr, &dst);
        }
    }

    // ── 3. Glyph (only when no JPEG art was blitted) ─────────────────────
    // Lazy-render once per slot, then reuse the cached SDL_Texture every
    // frame.  Glyph uses Medium font for visibility on the colored block.
    if (bgra == nullptr && entry_idx < MAX_ICONS) {
        if (glyph_text_tex_[entry_idx] == nullptr && entry.glyph != '\0') {
            const std::string glyph_str(1, entry.glyph);
            // Render in white; the background block already provides contrast.
            const pu::ui::Color glyph_color { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            glyph_text_tex_[entry_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                glyph_str, glyph_color);
        }
        if (glyph_text_tex_[entry_idx] != nullptr) {
            int gw = 0, gh = 0;
            SDL_QueryTexture(glyph_text_tex_[entry_idx], nullptr, nullptr, &gw, &gh);
            SDL_Rect gdst {
                bg_x + (ICON_BG_W - gw) / 2,
                bg_y + (ICON_BG_H - gh) / 2,
                gw, gh
            };
            SDL_RenderCopy(r, glyph_text_tex_[entry_idx], nullptr, &gdst);
        }
    }

    // ── 4. Name label (below the icon block) ──────────────────────────────
    // Truncate to a max display length (16 chars) so we don't blow out the
    // cell horizontally; longer NACP names render with an ellipsis suffix.
    if (entry_idx < MAX_ICONS && entry.name[0] != '\0') {
        if (name_text_tex_[entry_idx] == nullptr) {
            // Build the displayed string (truncate + ellipsis).
            char display[24];
            const size_t name_len = strnlen(entry.name, sizeof(entry.name));
            if (name_len > 16) {
                memcpy(display, entry.name, 13);
                display[13] = '.'; display[14] = '.'; display[15] = '.';
                display[16] = '\0';
            } else {
                memcpy(display, entry.name, name_len);
                display[name_len] = '\0';
            }
            const pu::ui::Color name_color { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            name_text_tex_[entry_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(display), name_color,
                static_cast<u32>(ICON_BG_W));  // soft-cap width
        }
        if (name_text_tex_[entry_idx] != nullptr) {
            int nw = 0, nh = 0;
            SDL_QueryTexture(name_text_tex_[entry_idx], nullptr, nullptr, &nw, &nh);
            // Centre horizontally under the bg rect; pad 4 px below.
            SDL_Rect ndst {
                bg_x + (ICON_BG_W - nw) / 2,
                bg_y + ICON_BG_H + 4,
                nw, nh
            };
            SDL_RenderCopy(r, name_text_tex_[entry_idx], nullptr, &ndst);
        }
    }

    // ── 5. Focus ring ─────────────────────────────────────────────────────
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

    // ── Top bar background (translucent dark strip behind status widgets) ────
    // The Plutonium time/date/battery/connection elements are instantiated in
    // ui_MainMenuLayout.cpp and rendered by Plutonium on top of whatever the
    // canvas shows.  Without a background strip they are invisible over the
    // animated wallpaper.  Height = 48 px (qd_WmConstants.hpp TOPBAR_H ×1.5).
    // Drawn here so it is always behind all icon and dock layers.
    //
    // Cycle D5 dev toggle: when g_dev_topbar_enabled is false the strip is
    // suppressed.  The Plutonium time/battery widgets that float above this
    // strip are owned by ui_MainMenuLayout and continue to render — their
    // visibility is intentionally NOT gated here because the user's primary
    // need for the toggle is "show me the wallpaper without the dark band
    // along the top".
    static constexpr int32_t TOPBAR_H_PX = 48;
    if (::ul::menu::qdesktop::g_dev_topbar_enabled.load(std::memory_order_relaxed)) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xB0u);
        SDL_Rect topbar_bg { 0, y, 1920, TOPBAR_H_PX };
        SDL_RenderFillRect(r, &topbar_bg);
        // 1px bottom border for visual separation from the icon grid.
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x30u);
        SDL_Rect topbar_border { 0, y + TOPBAR_H_PX - 1, 1920, 1 };
        SDL_RenderFillRect(r, &topbar_border);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
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

    // ── Dock panel (translucent backdrop behind builtin slots) ───────────────
    // Visually delineates the dock zone (y = DOCK_NOMINAL_TOP..1080) from the
    // icon grid above it.  Drawn BEFORE the icon loop so dock builtins paint
    // on top.  Alpha-blended black at ~38% opacity.  Width = full screen.
    //
    // Cycle D5 dev toggle: when g_dev_dock_enabled is false the dock panel
    // backdrop is suppressed AND the BUILTIN_ICON_COUNT slots in the icon
    // loop below are skipped (see the in-loop guard).
    const bool dev_dock_on = ::ul::menu::qdesktop::g_dev_dock_enabled.load(
        std::memory_order_relaxed);
    const bool dev_icons_on = ::ul::menu::qdesktop::g_dev_icons_enabled.load(
        std::memory_order_relaxed);
    if (dev_dock_on) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0x60u);
        SDL_Rect dock_panel { 0, DOCK_NOMINAL_TOP + y, 1920, DOCK_H };
        SDL_RenderFillRect(r, &dock_panel);
        // Thin top border line for definition (1px white at 25% alpha).
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x40u);
        SDL_Rect dock_border { 0, DOCK_NOMINAL_TOP + y, 1920, 1 };
        SDL_RenderFillRect(r, &dock_border);
        // Restore opaque draw colour for downstream filled rects.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── Render all icons ─────────────────────────────────────────────────────

    for (size_t i = 0u; i < icon_count_; ++i) {
        s32 cell_x, cell_y;

        if (i < BUILTIN_ICON_COUNT) {
            // Cycle D5 dev toggle: skip dock builtin slots when disabled.
            if (!dev_dock_on) { continue; }
            // Dock slot: use two-pass magnify position.
            // cell_y: dock is placed at DOCK_NOMINAL_TOP row.
            cell_x = builtin_slot_x[i] + x;
            cell_y = DOCK_NOMINAL_TOP + y;
        } else {
            // Cycle D5 dev toggle: skip NRO/Application grid icons when
            // desktop-icons is disabled.
            if (!dev_icons_on) { continue; }
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
                    LoadNroIconToCache(entry.nro_path, entry.nro_path);
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
                    // No custom icon path — fetch the real cover-art JPEG directly
                    // from NS storage (NsApplicationControlData::icon) via the NS
                    // service call.  Fabricate a stable cache key from the app_id so
                    // subsequent frames skip the NS call.
                    char app_cache_key[32];
                    snprintf(app_cache_key, sizeof(app_cache_key),
                             "app:%016llx",
                             static_cast<unsigned long long>(entry.app_id));
                    const u8 *cached = cache_.Get(app_cache_key);
                    if (cached == nullptr) {
                        const bool loaded = LoadNsIconToCache(entry.app_id, app_cache_key);
                        UL_LOG_INFO("qdesktop: app icon load app_id=0x%016llx"
                                    " path='%s' result=%s",
                                    static_cast<unsigned long long>(entry.app_id),
                                    app_cache_key,
                                    loaded ? "ok" : "fallback");
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
        PaintIconCell(r, entry, i, cell_x, cell_y, focused);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────
//
// Touch state machine (click-vs-drag):
//   TouchDown  — record press origin + hit-test index; do NOT launch.
//   TouchMove  — update last position; do NOT launch.
//   TouchUp    — launch only when:
//                  (a) pressed_ is true
//                  (b) hit-test at lift position matches the down icon
//                  (c) Euclidean displacement from down to lift ≤ CLICK_TOLERANCE_PX
//
// A-button path — launch the icon under the cursor when cursor_ref_ is wired;
// fall back to focused_idx_ when it is not (the layout always wires it in
// QDESKTOP_MODE, but the fallback protects against null deref if order changes).

void QdDesktopIconsElement::OnInput(const u64 keys_down,
                                     const u64 keys_up,
                                     const u64 keys_held,
                                     const pu::ui::TouchPoint touch_pos)
{
    (void)keys_up;
    (void)keys_held;

    // ── A / ZR: launch icon under the cursor (or focused icon as fallback) ──
    // ZR is the primary trigger — it mirrors the A-button action so players can
    // use either the face button or the shoulder trigger to launch.
    if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
        if (cursor_ref_ != nullptr) {
            const s32 cx = cursor_ref_->GetCursorX();
            const s32 cy = cursor_ref_->GetCursorY();
            const size_t hit = HitTest(cx, cy);
            if (hit < icon_count_) {
                LaunchIcon(hit);
            }
        } else {
            // cursor_ref_ not yet wired — fall back to D-pad focused index.
            // This path is not reachable in normal QDESKTOP_MODE operation
            // because MainMenuLayout::Initialize() calls SetCursorRef().
            LaunchIcon(focused_idx_);
        }
    }

    // ── ZL / Y: right-click / context menu on the icon under the cursor ──
    // Cycle G2 (SP4.15): real popup wired.  Both ZL (shoulder) and Y (face)
    // open the same context menu so users discover the gesture either way.
    // Options shown depend on icon kind + suspended-app state:
    //   • Application + this icon's app suspended → Resume / Close / Cancel
    //   • Application + a DIFFERENT app suspended → Open (close current) /
    //                                                Close current / Cancel
    //   • Application, no app suspended           → Open / Cancel
    //   • Nro                                     → Open / Cancel
    //   • Special / Builtin                       → Open / Cancel
    // Real "Verify" / "Delete" / "Move to dock" actions land in a later
    // cycle once the corresponding SMI surface is wired (T1-4 punch list).
    if ((keys_down & HidNpadButton_ZL) || (keys_down & HidNpadButton_Y)) {
        size_t ctx_idx = MAX_ICONS;
        if (cursor_ref_ != nullptr) {
            const s32 cx = cursor_ref_->GetCursorX();
            const s32 cy = cursor_ref_->GetCursorY();
            ctx_idx = HitTest(cx, cy);
        }
        if (ctx_idx >= icon_count_) {
            ctx_idx = focused_idx_;
        }
        if (ctx_idx < icon_count_ && g_MenuApplication != nullptr) {
            const NroEntry &entry = icons_[ctx_idx];
            UL_LOG_INFO("qdesktop: context-menu open idx=%zu name='%s' kind=%d",
                        ctx_idx, entry.name, static_cast<int>(entry.kind));

            const u64 suspended_app_id = g_GlobalSettings.system_status.suspended_app_id;
            const bool this_is_suspended =
                entry.kind == IconKind::Application
                && entry.app_id != 0
                && suspended_app_id == entry.app_id;
            const bool other_is_suspended =
                suspended_app_id != 0 && !this_is_suspended;

            std::vector<std::string> opts;
            int opt_open    = -1;
            int opt_close   = -1;
            int opt_close_other = -1;

            if (this_is_suspended) {
                opt_open  = static_cast<int>(opts.size()); opts.emplace_back("Resume");
                opt_close = static_cast<int>(opts.size()); opts.emplace_back("Close");
            } else {
                opt_open = static_cast<int>(opts.size());
                opts.emplace_back(other_is_suspended ? "Open (close current first)"
                                                     : "Open");
                if (other_is_suspended) {
                    opt_close_other = static_cast<int>(opts.size());
                    opts.emplace_back("Close currently running game");
                }
            }
            opts.emplace_back("Cancel");
            const int cancel_idx = static_cast<int>(opts.size()) - 1;

            std::string body;
            if (this_is_suspended) {
                body = "This game is currently running.";
            } else if (other_is_suspended) {
                body = "A different game is currently running.";
            } else {
                body = "What would you like to do?";
            }

            const int choice = g_MenuApplication->DisplayDialog(
                std::string(entry.name),
                body,
                opts,
                true
            );
            if (choice < 0 || choice == cancel_idx) {
                UL_LOG_INFO("qdesktop: context-menu cancelled");
            } else if (choice == opt_open) {
                // Routes through LaunchIcon → G1 logic handles
                // resume/terminate-and-launch transparently.
                LaunchIcon(ctx_idx);
            } else if (choice == opt_close && this_is_suspended) {
                UL_LOG_INFO("qdesktop: context-menu Close → TerminateApplication 0x%016llx",
                            static_cast<unsigned long long>(entry.app_id));
                const auto trc = smi::TerminateApplication();
                if (R_SUCCEEDED(trc)) {
                    g_GlobalSettings.ResetSuspendedApplication();
                    g_MenuApplication->ShowNotification("Closed running game.");
                } else {
                    UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X",
                                static_cast<unsigned>(trc));
                    g_MenuApplication->ShowNotification("Close failed");
                }
            } else if (choice == opt_close_other) {
                UL_LOG_INFO("qdesktop: context-menu Close-current → TerminateApplication 0x%016llx",
                            static_cast<unsigned long long>(suspended_app_id));
                const auto trc = smi::TerminateApplication();
                if (R_SUCCEEDED(trc)) {
                    g_GlobalSettings.ResetSuspendedApplication();
                    g_MenuApplication->ShowNotification("Closed running game.");
                } else {
                    UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X",
                                static_cast<unsigned>(trc));
                    g_MenuApplication->ShowNotification("Close failed");
                }
            }
        }
    }

    // ── Touch click-vs-drag state machine ─────────────────────────────────────

    const bool touch_active_now = !touch_pos.IsEmpty();

    if (touch_active_now) {
        const s32 tx = touch_pos.x;
        const s32 ty = touch_pos.y;

        if (!was_touch_active_last_frame_) {
            // ── TouchDown ──────────────────────────────────────────────────
            pressed_    = true;
            down_x_     = tx;
            down_y_     = ty;
            last_touch_x_ = tx;
            last_touch_y_ = ty;
            down_idx_   = HitTest(tx, ty);
            UL_LOG_INFO("qdesktop: touch_down x=%d y=%d hit=%zu", tx, ty, down_idx_);
        } else {
            // ── TouchMove ──────────────────────────────────────────────────
            last_touch_x_ = tx;
            last_touch_y_ = ty;
        }
    } else if (was_touch_active_last_frame_) {
        // ── TouchUp ────────────────────────────────────────────────────────
        UL_LOG_INFO("qdesktop: touch_up x=%d y=%d down_idx=%zu pressed=%d",
                    last_touch_x_, last_touch_y_, down_idx_,
                    static_cast<int>(pressed_));

        if (pressed_) {
            const size_t lift_hit = HitTest(last_touch_x_, last_touch_y_);
            const s32 dx = last_touch_x_ - down_x_;
            const s32 dy = last_touch_y_ - down_y_;
            // Squared distance avoids sqrt; CLICK_TOLERANCE_PX^2 = 576.
            const s32 dist_sq = dx * dx + dy * dy;
            const s32 tol     = CLICK_TOLERANCE_PX;
            const s32 tol_sq  = tol * tol;

            if (lift_hit < icon_count_ && lift_hit == down_idx_ && dist_sq <= tol_sq) {
                UL_LOG_INFO("qdesktop: launch idx=%zu (touch click)", lift_hit);
                // Cycle E1: clear ALL touch state BEFORE LaunchIcon so the
                // nested OnInput calls fired by FadeOut's CallForRender
                // busy-loop see a clean slate (touch_active_now is false,
                // pressed_ is false, was_touch_active_last_frame_ matches).
                // This belts-and-suspenders the re-entry guard inside
                // LaunchIcon itself — they're independent defenses.
                pressed_                     = false;
                down_idx_                    = MAX_ICONS;
                was_touch_active_last_frame_ = touch_active_now;
                LaunchIcon(lift_hit);
                return;
            }
        }

        pressed_   = false;
        down_idx_  = MAX_ICONS;
    }

    was_touch_active_last_frame_ = touch_active_now;
}

// ── LaunchIcon ────────────────────────────────────────────────────────────────

namespace {
    // Cycle E1 (SP4.13): re-entry guard.
    //
    // BUG (manifested in SP4.12.1 hardware test):
    //   Tapping a Themes/Settings icon caused infinite recursion:
    //
    //     QdDesktopIconsElement::OnInput     [user touch_up]
    //       LaunchIcon(themes_idx)
    //         ShowThemesMenu()
    //           MenuApplication::LoadMenu(MenuType::Themes, fade=true)
    //             Application::FadeOut()       [busy-loops CallForRender]
    //               Application::OnRender()    [per-frame, fires layout OnInput]
    //                 QdDesktopIconsElement::OnInput  [pressed_ STILL true,
    //                                                  was_touch_active_last_frame_
    //                                                  STILL true → re-enters
    //                                                  TouchUp branch]
    //                   LaunchIcon(themes_idx)        ← RE-ENTERS
    //                     ShowThemesMenu()            ← infinite recursion
    //
    //   ~2 KB of stack per recursion level (UL_LOG_INFO → LogImpl → vsnprintf).
    //   With a 1 MB main-thread stack the recursion depth tops out at ~500
    //   levels, then SP overruns the stack region → null deref in vsnprintf
    //   scratch.  Crash report 01777148075 confirms this exact chain.
    //
    //   For Application/Nro launches (Pokemon, RetroArch, Tinwoo) the same
    //   re-entry path fired smi::LaunchApplication TWICE with the same app_id,
    //   confusing uSystem's launch state machine and bouncing the game back
    //   to the desktop.  That's the "tries to load → black → back to desktop"
    //   pattern the user reported.
    //
    // FIX:
    //   A static atomic guards the function body.  Any call that finds the
    //   guard already set (i.e. we're already inside LaunchIcon, presumably
    //   from a nested OnInput fired by FadeOut's CallForRender loop) is
    //   discarded.  The guard is reset when the original call returns.
    //
    //   This is safe because LaunchIcon is only ever called on the main UI
    //   thread (from OnInput / Plutonium's render loop).  No cross-thread
    //   races possible.  std::atomic is used purely for the relaxed memory
    //   ordering on the load/store; a plain bool with relaxed access would
    //   work but std::atomic documents the intent.
    std::atomic<bool> g_launch_icon_in_flight{false};
}

void QdDesktopIconsElement::LaunchIcon(size_t i) {
    if (g_launch_icon_in_flight.load(std::memory_order_relaxed)) {
        UL_LOG_WARN("qdesktop: LaunchIcon(%zu) re-entered while another launch "
                    "is in flight — discarding (cycle E1 guard)", i);
        return;
    }
    g_launch_icon_in_flight.store(true, std::memory_order_relaxed);
    // Reset on every exit path via this scope-exit helper.
    struct LaunchIconReentryGuard {
        ~LaunchIconReentryGuard() {
            g_launch_icon_in_flight.store(false, std::memory_order_relaxed);
        }
    } reentry_guard;

    if (i >= icon_count_) {
        return;
    }
    const NroEntry &entry = icons_[i];

    switch (entry.kind) {
        case IconKind::Builtin:
            // dock_slot 0 = Vault file browser.
            // dock_slot 3 = Control (Settings) panel.
            if (entry.dock_slot == 0) {
                if (g_MenuApplication) {
                    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Vault);
                }
            } else {
                UL_LOG_WARN("qdesktop: LaunchIcon: unhandled builtin dock_slot=%u", (unsigned)entry.dock_slot);
            }
            return;

        case IconKind::Nro:
            if (entry.nro_path[0] != '\0') {
                UL_LOG_INFO("qdesktop: LaunchHomebrewLibraryApplet '%s' — Finalize uMenu",
                            entry.nro_path);
                smi::LaunchHomebrewLibraryApplet(
                    std::string(entry.nro_path), std::string(""));
                // CRITICAL (cycle C1): mirror the upstream
                // MainMenuLayout::HandleHomebrewLaunch path
                // (ui_MainMenuLayout.cpp:1529-1531). Without these two calls
                // uMenu re-asserts foreground after smi::LaunchHomebrewLibraryApplet
                // returns, which silently kills hbloader before the NRO can
                // display. This is why Tinwoo (and every other NRO) appeared
                // to "do nothing" when launched from the desktop.
                if (g_MenuApplication) {
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                }
            } else {
                UL_LOG_WARN("qdesktop: LaunchIcon: Nro entry '%s' has empty nro_path — launch skipped",
                            entry.name);
            }
            return;

        case IconKind::Application:
            if (entry.app_id != 0) {
                if (g_MenuApplication == nullptr) {
                    UL_LOG_WARN("qdesktop: LaunchApplication: g_MenuApplication"
                                " null — skipping launch of 0x%016llx",
                                static_cast<unsigned long long>(entry.app_id));
                    return;
                }

                // ── Cycle G1 (SP4.15): suspended-app handling ──────────────
                // Upstream MainMenuLayout has rich resume/close-suspended
                // logic at ui_MainMenuLayout.cpp:175-189; that whole branch
                // was gated off in QDESKTOP_MODE awaiting QdContextMenu.
                // Now we wire the equivalent inline so the user can:
                //   • Press the SAME game's icon while it's suspended
                //     → smi::ResumeApplication() (no fresh launch)
                //   • Press a DIFFERENT game's icon while one is suspended
                //     → confirmation dialog → smi::TerminateApplication()
                //       then smi::LaunchApplication() the new one
                //
                // Without this, tapping any icon after a game has been
                // home-buttoned quietly does nothing because Horizon won't
                // create a second Application while one is already alive.
                // (Why D2 cycle's launch order is preserved below: see prior
                // commit history — 26fa3de E1 + 2d11260 D2 — the SP4.10
                // ordering Launch→fade→Finalize is hardware-validated and
                // we DO NOT reorder it.)
                const u64 suspended_app_id = g_GlobalSettings.system_status.suspended_app_id;
                if (suspended_app_id != 0) {
                    if (suspended_app_id == entry.app_id) {
                        UL_LOG_INFO("qdesktop: ResumeApplication 0x%016llx (matches suspended)",
                                    static_cast<unsigned long long>(entry.app_id));
                        const auto rrc = smi::ResumeApplication();
                        if (R_SUCCEEDED(rrc)) {
                            g_MenuApplication->FadeOutToNonLibraryApplet();
                            g_MenuApplication->Finalize();
                        } else {
                            UL_LOG_WARN("qdesktop: ResumeApplication failed rc=0x%X",
                                        static_cast<unsigned>(rrc));
                            g_MenuApplication->ShowNotification("Resume failed");
                        }
                        return;
                    }
                    // Different app already running — ask before terminating.
                    const auto opt = g_MenuApplication->DisplayDialog(
                        std::string(entry.name),
                        std::string("A different game is currently running.\n"
                                    "Close it to launch this one?"),
                        { std::string("Yes, close and launch"),
                          std::string("Cancel") },
                        true
                    );
                    if (opt != 0) {
                        UL_LOG_INFO("qdesktop: user declined close-suspended;"
                                    " keeping running app 0x%016llx",
                                    static_cast<unsigned long long>(suspended_app_id));
                        return;
                    }
                    const auto trc = smi::TerminateApplication();
                    if (R_FAILED(trc)) {
                        UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X"
                                    " — aborting new launch",
                                    static_cast<unsigned>(trc));
                        g_MenuApplication->ShowNotification("Close failed");
                        return;
                    }
                    g_GlobalSettings.ResetSuspendedApplication();
                    // fall through to fresh-launch path
                }

                // ── Fresh launch (no suspended app, or just terminated one).
                const Result rc = smi::LaunchApplication(entry.app_id);
                if (R_SUCCEEDED(rc)) {
                    UL_LOG_INFO("qdesktop: LaunchApplication ok 0x%016llx — fade + Finalize",
                                static_cast<unsigned long long>(entry.app_id));
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                } else {
                    UL_LOG_WARN("qdesktop: LaunchApplication(0x%016llx) failed rc=0x%X — desktop stays live",
                                static_cast<unsigned long long>(entry.app_id),
                                static_cast<unsigned>(rc));
                    g_MenuApplication->ShowNotification("Launch failed");
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
    // Free cached name/glyph text textures for the slots about to be reused
    // so the next paint re-rasterises with the new entry's name + glyph.
    for (size_t i = app_entry_start_idx_; i < icon_count_; ++i) {
        FreeCachedText(i);
    }
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
        e.special_subtype = 0;

        ++icon_count_;
        ++added;
    }

    UL_LOG_INFO("qdesktop: SetApplicationEntries: in=%zu added=%zu total=%zu",
                entries.size(), added, icon_count_);
}

// ── SetSpecialEntries ─────────────────────────────────────────────────────────
// Appends Switch system-applet shortcut icons (Settings, Album, Themes,
// Controllers, MiiEdit, WebBrowser, UserPage, Amiibo) to the icon grid.
// MUST be called AFTER SetApplicationEntries on the same Reload pass — this
// method does NOT truncate icon_count_, only appends.  Each Special entry is
// stored with kind=IconKind::Special and special_subtype=static_cast<u16>(EntryType).
//
// Display name policy:
//   - Use entry.control.name when non-empty (typically a localised label).
//   - Fall back to a hardcoded English label keyed off EntryType.
// Glyph + colour come from a fixed table per applet — NACP classifier does not
// know about system applets so we map them explicitly here.

void QdDesktopIconsElement::SetSpecialEntries(
        const std::vector<ul::menu::Entry> &entries)
{
    using ET = ul::menu::EntryType;
    struct SpecialDef {
        ET   type;
        const char *fallback_name;
        char glyph;
        u8   r, g, b;
    };
    static constexpr SpecialDef SPECIAL_DEFS[] = {
        { ET::SpecialEntrySettings,    "Settings",    'S', 0x60, 0xA5, 0xFA },
        { ET::SpecialEntryAlbum,       "Album",       'P', 0xF8, 0x71, 0x71 },  // 'P' for Photos
        { ET::SpecialEntryThemes,      "Themes",      'T', 0xC0, 0x84, 0xFC },
        { ET::SpecialEntryControllers, "Controllers", 'C', 0x4A, 0xDE, 0x80 },
        { ET::SpecialEntryMiiEdit,     "Mii",         'M', 0xFB, 0xBF, 0x24 },
        { ET::SpecialEntryWebBrowser,  "Browser",     'W', 0x38, 0xBD, 0xF8 },
        { ET::SpecialEntryUserPage,    "User",        'U', 0xE0, 0xE0, 0xF0 },
        { ET::SpecialEntryAmiibo,      "Amiibo",      'A', 0xF0, 0x70, 0xC0 },
    };

    // Lookup helper for the static table.
    auto find_def = [](ET t) -> const SpecialDef * {
        for (const auto &d : SPECIAL_DEFS) {
            if (d.type == t) {
                return &d;
            }
        }
        return nullptr;
    };

    size_t added = 0;
    for (const auto &entry : entries) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        if (!entry.IsSpecial()) {
            continue;
        }
        const SpecialDef *def = find_def(entry.type);
        if (def == nullptr) {
            continue;
        }

        NroEntry &e = icons_[icon_count_];

        // Display name: prefer the localised control name, fall back to def.
        if (!entry.control.name.empty()) {
            snprintf(e.name, sizeof(e.name), "%s", entry.control.name.c_str());
        } else {
            snprintf(e.name, sizeof(e.name), "%s", def->fallback_name);
        }

        e.glyph    = def->glyph;
        e.bg_r     = def->r;
        e.bg_g     = def->g;
        e.bg_b     = def->b;

        e.nro_path[0]  = '\0';
        e.icon_path[0] = '\0';
        e.is_builtin   = false;
        e.dock_slot    = 0;
        e.category     = NroCategory::Unknown;
        e.icon_loaded  = false;
        e.kind         = IconKind::Special;
        e.app_id       = 0;
        e.special_subtype = static_cast<u16>(entry.type);

        ++icon_count_;
        ++added;
    }

    UL_LOG_INFO("qdesktop: SetSpecialEntries: in=%zu added=%zu total=%zu",
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

// ── LoadNsIconToCache ─────────────────────────────────────────────────────────
//
// Fetches the JPEG icon bytes for app_id directly from NS storage
// (NsApplicationControlSource_Storage), decodes them via SDL2_image using
// SDL_RWFromConstMem + IMG_Load_RW (no temporary file on SD card), applies the
// same double-convert path as LoadJpegIconToCache (RGBA8888 → ABGR8888), and
// inserts the result into cache_ under cache_key.
//
// On any failure the function generates a MakeFallbackIcon() solid-colour block
// and inserts that instead, so the caller always has a usable cache entry.
// Returns true if a real JPEG was decoded; false if the fallback was used.
//
// NsApplicationControlData is ~393 KB — heap-allocated to avoid stack overflow.

bool QdDesktopIconsElement::LoadNsIconToCache(const u64 app_id,
                                               const char *cache_key)
{
    // Heap-allocate the control data struct (~393 KB — too large for stack).
    NsApplicationControlData *ctrl_data = new(std::nothrow) NsApplicationControlData;
    if (ctrl_data == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: OOM allocating NsApplicationControlData"
                    " for app_id=0x%016llx",
                    static_cast<unsigned long long>(app_id));
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    u64 icon_size = 0;
    // First try NsApplicationControlSource_Storage (reads from storage if not
    // cached by the system).  In applet mode this can fail with
    // 0x196002 (permission denied) because the system hasn't exposed
    // read-only storage access to library applets on some firmware.
    // When it fails, retry with NsApplicationControlSource_CacheOnly — the
    // icon thumbnail is almost always warm in the NS cache after the home menu
    // has displayed it once.
    Result rc = nsextGetApplicationControlData(
        NsApplicationControlSource_Storage,
        app_id,
        ctrl_data,
        sizeof(NsApplicationControlData),
        &icon_size);

    if (R_FAILED(rc) || icon_size == 0) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: Storage source failed"
                    " for app_id=0x%016llx rc=0x%08x icon_size=%llu"
                    " — retrying with CacheOnly",
                    static_cast<unsigned long long>(app_id),
                    static_cast<unsigned int>(rc),
                    static_cast<unsigned long long>(icon_size));
        icon_size = 0;
        rc = nsextGetApplicationControlData(
            NsApplicationControlSource_CacheOnly,
            app_id,
            ctrl_data,
            sizeof(NsApplicationControlData),
            &icon_size);
    }

    if (R_FAILED(rc) || icon_size == 0) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: both Storage and CacheOnly"
                    " failed for app_id=0x%016llx rc=0x%08x icon_size=%llu"
                    " — using fallback colour block",
                    static_cast<unsigned long long>(app_id),
                    static_cast<unsigned int>(rc),
                    static_cast<unsigned long long>(icon_size));
        delete ctrl_data;
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // Wrap the in-memory JPEG bytes in an SDL_RWops (no disk I/O).
    SDL_RWops *rw = SDL_RWFromConstMem(ctrl_data->icon, static_cast<int>(icon_size));
    if (rw == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_RWFromConstMem failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        delete ctrl_data;
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // IMG_Load_RW decodes the JPEG and frees rw (freesrc = 1).
    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);
    // rw is now freed regardless of success/failure — do not touch it again.
    delete ctrl_data;
    ctrl_data = nullptr;

    if (raw == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: IMG_Load_RW failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), IMG_GetError());
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // First conversion: normalize to RGBA8888.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);

    if (rgba == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_ConvertSurface(RGBA) failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // Second conversion: RGBA8888 → ABGR8888 to match the byte-order expected
    // by ScaleToBgra64 inside cache_.Put() (same reasoning as LoadJpegIconToCache).
    SDL_Surface *rgba_le = SDL_ConvertSurfaceFormat(rgba, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rgba);

    if (rgba_le == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_ConvertSurface(ABGR) failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
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
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_LockSurface failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
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

    const s32 src_w = rgba_le->w;
    const s32 src_h = rgba_le->h;
    const u8 *le_pixels = static_cast<const u8 *>(rgba_le->pixels);

    cache_.Put(cache_key, le_pixels, src_w, src_h);

    SDL_UnlockSurface(rgba_le);
    SDL_FreeSurface(rgba_le);

    UL_LOG_INFO("qdesktop: LoadNsIconToCache: loaded app_id=0x%016llx"
                " (%d×%d icon_size=%llu) → cache key %s",
                static_cast<unsigned long long>(app_id),
                src_w, src_h,
                static_cast<unsigned long long>(icon_size),
                cache_key);
    return true;
}

// ── LoadNroIconToCache ────────────────────────────────────────────────────────
//
// Extract the JPEG icon from the ASET section of the NRO at nro_path via
// ExtractNroIcon, decode it, and insert the result into the icon cache under
// cache_key.  Falls back to a MakeFallbackIcon solid-colour block on any
// parse/decode failure.  Returns true if a real JPEG was decoded; false if the
// fallback was used.
//
// FreeNroIcon semantics (from qd_NroAsset.hpp):
//   FreeNroIcon(res) calls delete[] res.pixels (if non-null) then zeroes the
//   pointer and sets res.valid = false.  It is safe to call on an invalid result
//   (valid==false) because pixels is still a valid fallback buffer allocated by
//   MakeFallbackIcon inside ExtractNroIcon.  FreeNroIcon must be called in BOTH
//   the success and failure branches — see F-05 fix comment in NroIconResult.
//
// pixel snapshot rule: capture res.width/res.height to locals BEFORE calling
// FreeNroIcon so the log line reads from stack not from a freed struct.

bool QdDesktopIconsElement::LoadNroIconToCache(const char *nro_path,
                                               const char *cache_key)
{
    // Guard: reject null or empty path.
    if (nro_path == nullptr || nro_path[0] == '\0') {
        UL_LOG_WARN("qdesktop: LoadNroIconToCache: invalid nro_path (null or empty)"
                    " for cache_key=%s — using fallback colour block",
                    cache_key != nullptr ? cache_key : "(null)");
        u8 *fallback = MakeFallbackIcon(cache_key != nullptr ? cache_key : "");
        if (fallback != nullptr) {
            cache_.Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    NroIconResult res = ExtractNroIcon(nro_path);

    // Sanity-check the result dimensions before we hand pixels to cache_.Put.
    // The 4096 upper bound guards against absurdly large JPEG blobs that
    // ExtractNroIcon accepted but that would overflow ScaleToBgra64's buffers.
    if (res.valid && res.pixels != nullptr
            && res.width > 0 && res.height > 0
            && res.width <= 4096 && res.height <= 4096) {
        // Snapshot dimensions to locals before FreeNroIcon zeroes the struct.
        const s32 snap_w = res.width;
        const s32 snap_h = res.height;
        // cache_.Put copies via ScaleToBgra64 — res.pixels is not read after this.
        cache_.Put(cache_key, res.pixels, snap_w, snap_h);
        FreeNroIcon(res);
        UL_LOG_INFO("qdesktop: LoadNroIconToCache: loaded %s (%d×%d) → cache key %s",
                    nro_path, snap_w, snap_h, cache_key);
        return true;
    }

    // Extraction failed or dimensions out of range — log diagnostics using
    // res.width/res.height directly here (safe: pixels not yet freed), then
    // free, then insert fallback.
    UL_LOG_WARN("qdesktop: LoadNroIconToCache: ExtractNroIcon failed or bad dims"
                " for %s valid=%d pixels=%p width=%d height=%d"
                " — using fallback colour block",
                nro_path,
                static_cast<int>(res.valid),
                static_cast<const void *>(res.pixels),
                res.width,
                res.height);
    FreeNroIcon(res);

    u8 *fallback = MakeFallbackIcon(nro_path);
    if (fallback != nullptr) {
        cache_.Put(cache_key, fallback,
                   static_cast<s32>(CACHE_ICON_W),
                   static_cast<s32>(CACHE_ICON_H));
        delete[] fallback;
    }
    return false;
}

} // namespace ul::menu::qdesktop
