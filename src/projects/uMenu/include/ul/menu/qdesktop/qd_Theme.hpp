// qd_Theme.hpp — Q OS desktop color token system for uMenu C++ port.
// Originally ported from tools/mock-nro-desktop-gui/src/theme.rs (v1.1.12).
//
// v2.4.0: extended to 10 in-binary QdTheme factories, each paired with a
//         matching wallpaper algorithm (qd_Wallpaper.cpp) and folder-icon
//         pack (qd_FolderTheme.cpp) under the same name. The 10 names
//         (Glass / Neon / Minimal / Retro / Cards / Pastel / Dark / Gradient
//          / Blueprint / Pixel) are the same indices used by the
//         FolderThemePack registry — selecting "Cards" picks the Cards
//         palette + Cards wallpaper + Cards folder pack atomically.
//
// All RGB values are authoritative; do not derive from doc 33 JSON defaults.
#pragma once
#include <pu/Plutonium>
#include <string>

// Forward decl — DrawThemeTransitionFrame takes SDL_Renderer*.  Including
// SDL2/SDL.h from this header would pull SDL globally; keep it opaque here
// and let the implementation .cpp include the full SDL header.
struct SDL_Renderer;

namespace ul::menu::qdesktop {

// ── Color helpers ──────────────────────────────────────────────────────────

// Convenience: create a fully-opaque Color.
static inline constexpr pu::ui::Color Rgb(u8 r, u8 g, u8 b) {
    return pu::ui::Color(r, g, b, 0xFF);
}

// Create a Color with explicit alpha.
static inline constexpr pu::ui::Color Rgba(u8 r, u8 g, u8 b, u8 a) {
    return pu::ui::Color(r, g, b, a);
}

// ── QdTheme ────────────────────────────────────────────────────────────────

// 17 color tokens. Field names follow the Rust token names exactly (snake_case).
struct QdTheme {
    pu::ui::Color desktop_bg;
    pu::ui::Color surface_glass;
    pu::ui::Color topbar_bg;
    pu::ui::Color dock_bg;
    pu::ui::Color accent;
    pu::ui::Color text_primary;
    pu::ui::Color text_secondary;
    pu::ui::Color focus_ring;
    pu::ui::Color button_close;
    pu::ui::Color button_minimize;
    pu::ui::Color button_maximize;
    pu::ui::Color cursor_fill;
    pu::ui::Color cursor_outline;
    pu::ui::Color cursor_right_click;
    pu::ui::Color titlebar_inactive;
    pu::ui::Color button_restore;
    pu::ui::Color grid_line;

    // ── 10 in-binary factories (matched 1:1 to wallpaper + folder-pack names) ──

    // 0 — Glass — translucent dark + cyan ring + lavender pops (canonical default; alias of DarkLiquidGlass)
    static QdTheme Glass() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x0A, 0x0A, 0x14);
        t.surface_glass      = Rgb(0x12, 0x12, 0x2A);
        t.topbar_bg          = Rgb(0x0C, 0x0C, 0x20);
        t.dock_bg            = Rgb(0x10, 0x10, 0x2A);
        t.accent             = Rgb(0x7D, 0xD3, 0xFC);
        t.text_primary       = Rgb(0xE0, 0xE0, 0xF0);
        t.text_secondary     = Rgb(0x88, 0x88, 0xAA);
        t.focus_ring         = Rgb(0x7C, 0xC5, 0xFF);
        t.button_close       = Rgb(0xF8, 0x71, 0x71);
        t.button_minimize    = Rgb(0xFB, 0xBF, 0x24);
        t.button_maximize    = Rgb(0x4A, 0xDE, 0x80);
        t.cursor_fill        = Rgb(0xF5, 0xF5, 0xFF);
        t.cursor_outline     = Rgb(0x05, 0x05, 0x10);
        t.cursor_right_click = Rgb(0xE5, 0x4B, 0x4B);
        t.titlebar_inactive  = Rgb(0x18, 0x18, 0x30);
        t.button_restore     = Rgb(0x4A, 0xDE, 0x80);
        t.grid_line          = Rgb(0x24, 0x24, 0x4A);
        return t;
    }

    // 1 — Neon — black base + hot pink + lime + electric cyan glow
    static QdTheme Neon() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x05, 0x00, 0x10);
        t.surface_glass      = Rgb(0x14, 0x05, 0x22);
        t.topbar_bg          = Rgb(0x0A, 0x00, 0x18);
        t.dock_bg            = Rgb(0x10, 0x05, 0x20);
        t.accent             = Rgb(0xFF, 0x2A, 0xD0);  // hot magenta
        t.text_primary       = Rgb(0xF0, 0xFF, 0xF0);
        t.text_secondary     = Rgb(0xA8, 0x70, 0xC8);
        t.focus_ring         = Rgb(0x2A, 0xFF, 0xE5);  // electric cyan
        t.button_close       = Rgb(0xFF, 0x32, 0x6E);
        t.button_minimize    = Rgb(0xF5, 0xE0, 0x2A);
        t.button_maximize    = Rgb(0x6A, 0xFF, 0x50);
        t.cursor_fill        = Rgb(0xFF, 0xF0, 0xFF);
        t.cursor_outline     = Rgb(0x10, 0x00, 0x18);
        t.cursor_right_click = Rgb(0xFF, 0x2A, 0xD0);
        t.titlebar_inactive  = Rgb(0x22, 0x0A, 0x32);
        t.button_restore     = Rgb(0x6A, 0xFF, 0x50);
        t.grid_line          = Rgb(0x2A, 0x10, 0x3A);
        return t;
    }

    // 2 — Minimal — warm stone gray + dusty amber accent (v2.6.0: was sharing
    // Glass's #7DD3FC cyan accent — distinctness audit flagged the two as
    // indistinguishable in chrome. Minimal now reads as "warm minimalism"
    // while Glass reads as "cold cyan." No accent overlap with any other
    // pack in the set.)
    static QdTheme Minimal() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x1A, 0x18, 0x16);  // warm gray (was cold gray)
        t.surface_glass      = Rgb(0x26, 0x24, 0x22);
        t.topbar_bg          = Rgb(0x1E, 0x1C, 0x1A);
        t.dock_bg            = Rgb(0x22, 0x20, 0x1E);
        t.accent             = Rgb(0xD4, 0xC8, 0xB4);  // warm off-white (NEW)
        t.text_primary       = Rgb(0xF2, 0xEE, 0xE6);
        t.text_secondary     = Rgb(0x90, 0x88, 0x7C);
        t.focus_ring         = Rgb(0xE0, 0xB8, 0x6A);  // dusty amber (NEW)
        t.button_close       = Rgb(0xE0, 0x60, 0x60);
        t.button_minimize    = Rgb(0xE0, 0xC0, 0x40);
        t.button_maximize    = Rgb(0x40, 0xC8, 0x70);
        t.cursor_fill        = Rgb(0xFA, 0xF6, 0xEE);
        t.cursor_outline     = Rgb(0x1A, 0x18, 0x16);
        t.cursor_right_click = Rgb(0xE0, 0x60, 0x60);
        t.titlebar_inactive  = Rgb(0x2E, 0x2A, 0x26);
        t.button_restore     = Rgb(0x40, 0xC8, 0x70);
        t.grid_line          = Rgb(0x34, 0x30, 0x2A);
        return t;
    }

    // 3 — Retro — deep navy + amber/orange + CRT green
    static QdTheme Retro() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x0A, 0x14, 0x20);
        t.surface_glass      = Rgb(0x14, 0x1C, 0x2A);
        t.topbar_bg          = Rgb(0x10, 0x18, 0x26);
        t.dock_bg            = Rgb(0x14, 0x1C, 0x2C);
        t.accent             = Rgb(0xFF, 0xA8, 0x3A);  // amber
        t.text_primary       = Rgb(0xE8, 0xF0, 0xE0);  // legible near-white (v3.7 retune)
        t.text_secondary     = Rgb(0x6A, 0xD0, 0x80);  // phosphor green — the CRT signature
        t.focus_ring         = Rgb(0xFF, 0xC8, 0x60);
        t.button_close       = Rgb(0xE8, 0x5A, 0x4C);
        t.button_minimize    = Rgb(0xFF, 0xD2, 0x4A);
        t.button_maximize    = Rgb(0x6A, 0xFF, 0x82);
        t.cursor_fill        = Rgb(0x6A, 0xFF, 0x82);
        t.cursor_outline     = Rgb(0x0A, 0x14, 0x20);
        t.cursor_right_click = Rgb(0xE8, 0x5A, 0x4C);
        t.titlebar_inactive  = Rgb(0x1A, 0x24, 0x36);
        t.button_restore     = Rgb(0x6A, 0xFF, 0x82);
        t.grid_line          = Rgb(0x22, 0x2E, 0x42);
        return t;
    }

    // 4 — Cards — warm amber on blue slate + magenta drop-shadow pop
    // (v2.6.0: shifted from cyan accent → warm amber so Cards no longer
    // collides with Glass / Blueprint / Dark in the cyan family. Only theme
    // in the set with warm amber chrome.)
    static QdTheme Cards() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x1C, 0x20, 0x30);  // lighter blue-slate
        t.surface_glass      = Rgb(0x26, 0x2C, 0x40);
        t.topbar_bg          = Rgb(0x20, 0x24, 0x36);
        t.dock_bg            = Rgb(0x24, 0x28, 0x3A);
        t.accent             = Rgb(0xFF, 0x9A, 0x3C);  // warm amber (NEW)
        t.text_primary       = Rgb(0xF0, 0xEC, 0xE4);
        t.text_secondary     = Rgb(0xB0, 0xA8, 0x96);
        t.focus_ring         = Rgb(0xFF, 0x7A, 0xD0);  // magenta pop
        t.button_close       = Rgb(0xFF, 0x7A, 0x82);
        t.button_minimize    = Rgb(0xFF, 0xCC, 0x4A);
        t.button_maximize    = Rgb(0x60, 0xE8, 0x9C);
        t.cursor_fill        = Rgb(0xFF, 0xF5, 0xE6);
        t.cursor_outline     = Rgb(0x10, 0x14, 0x1E);
        t.cursor_right_click = Rgb(0xFF, 0x7A, 0xD0);
        t.titlebar_inactive  = Rgb(0x2A, 0x30, 0x44);
        t.button_restore     = Rgb(0x60, 0xE8, 0x9C);
        t.grid_line          = Rgb(0x2E, 0x34, 0x48);
        return t;
    }

    // 5 — Pastel — soft slate + powder pink + mint + lavender
    static QdTheme Pastel() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x1E, 0x1E, 0x28);
        t.surface_glass      = Rgb(0x2A, 0x2A, 0x38);
        t.topbar_bg          = Rgb(0x24, 0x22, 0x2E);
        t.dock_bg            = Rgb(0x26, 0x24, 0x32);
        t.accent             = Rgb(0xFB, 0xC6, 0xE4);  // powder pink
        t.text_primary       = Rgb(0xF4, 0xEC, 0xF4);
        t.text_secondary     = Rgb(0xB8, 0xA8, 0xC2);
        t.focus_ring         = Rgb(0xD4, 0xC7, 0xF5);  // lavender
        t.button_close       = Rgb(0xF5, 0xA8, 0xAE);
        t.button_minimize    = Rgb(0xFA, 0xE3, 0xA4);
        t.button_maximize    = Rgb(0xB0, 0xE8, 0xC0);  // mint
        t.cursor_fill        = Rgb(0xFD, 0xF2, 0xF5);
        t.cursor_outline     = Rgb(0x1E, 0x18, 0x28);
        t.cursor_right_click = Rgb(0xF5, 0xA8, 0xAE);
        t.titlebar_inactive  = Rgb(0x32, 0x2C, 0x3C);
        t.button_restore     = Rgb(0xB0, 0xE8, 0xC0);
        t.grid_line          = Rgb(0x3E, 0x36, 0x48);
        return t;
    }

    // 6 — Dark — embers in a void: pure black + ember orange accent
    // (v2.6.0: was sharing the cyan accent family. Dark now reads as
    // "burning embers in deep void" with the only orange chrome in the set,
    // unmistakably different from Glass's cyan-on-navy.)
    static QdTheme Dark() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x00, 0x00, 0x04);  // pure void with hint of blue
        t.surface_glass      = Rgb(0x12, 0x0A, 0x06);  // dark ember undertone
        t.topbar_bg          = Rgb(0x08, 0x04, 0x02);
        t.dock_bg            = Rgb(0x0C, 0x06, 0x04);
        t.accent             = Rgb(0xFF, 0x60, 0x40);  // ember orange (NEW)
        t.text_primary       = Rgb(0xEC, 0xE6, 0xE0);
        t.text_secondary     = Rgb(0x80, 0x70, 0x66);
        t.focus_ring         = Rgb(0xFF, 0x80, 0x50);  // brighter ember
        t.button_close       = Rgb(0xE8, 0x40, 0x40);
        t.button_minimize    = Rgb(0xE8, 0xA0, 0x30);
        t.button_maximize    = Rgb(0x60, 0xC0, 0x70);
        t.cursor_fill        = Rgb(0xFF, 0xE6, 0xD8);
        t.cursor_outline     = Rgb(0x00, 0x00, 0x00);
        t.cursor_right_click = Rgb(0xE8, 0x40, 0x40);
        t.titlebar_inactive  = Rgb(0x1A, 0x0E, 0x08);
        t.button_restore     = Rgb(0x60, 0xC0, 0x70);
        t.grid_line          = Rgb(0x26, 0x14, 0x10);
        return t;
    }

    // 7 — Gradient — deep indigo + smooth violet→cyan transition
    static QdTheme Gradient() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x10, 0x05, 0x22);
        t.surface_glass      = Rgb(0x1A, 0x12, 0x32);
        t.topbar_bg          = Rgb(0x14, 0x08, 0x28);
        t.dock_bg            = Rgb(0x18, 0x0C, 0x30);
        t.accent             = Rgb(0xA0, 0x70, 0xFF);  // violet
        t.text_primary       = Rgb(0xEC, 0xE8, 0xFA);
        t.text_secondary     = Rgb(0x9A, 0x88, 0xC0);
        t.focus_ring         = Rgb(0x7A, 0xE0, 0xFF);  // cyan end
        t.button_close       = Rgb(0xF8, 0x6A, 0x9A);
        t.button_minimize    = Rgb(0xFF, 0xC4, 0x70);
        t.button_maximize    = Rgb(0x70, 0xE0, 0xB8);
        t.cursor_fill        = Rgb(0xF5, 0xEF, 0xFF);
        t.cursor_outline     = Rgb(0x0A, 0x05, 0x18);
        t.cursor_right_click = Rgb(0xF8, 0x6A, 0x9A);
        t.titlebar_inactive  = Rgb(0x22, 0x18, 0x38);
        t.button_restore     = Rgb(0x70, 0xE0, 0xB8);
        t.grid_line          = Rgb(0x2E, 0x22, 0x47);
        return t;
    }

    // 8 — Blueprint — deep blueprint blue + cyan accents + white technical text
    static QdTheme Blueprint() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x05, 0x18, 0x32);
        t.surface_glass      = Rgb(0x0A, 0x22, 0x40);
        t.topbar_bg          = Rgb(0x06, 0x1C, 0x36);
        t.dock_bg            = Rgb(0x08, 0x1F, 0x3A);
        t.accent             = Rgb(0x7A, 0xE0, 0xFF);
        t.text_primary       = Rgb(0xE0, 0xF0, 0xFF);
        t.text_secondary     = Rgb(0x70, 0xA8, 0xD0);
        t.focus_ring         = Rgb(0xA8, 0xE8, 0xFF);
        t.button_close       = Rgb(0xE8, 0x7A, 0x7A);
        t.button_minimize    = Rgb(0xF0, 0xC8, 0x6A);
        t.button_maximize    = Rgb(0x6A, 0xE8, 0xB0);
        t.cursor_fill        = Rgb(0xF0, 0xF8, 0xFF);
        t.cursor_outline     = Rgb(0x05, 0x12, 0x24);
        t.cursor_right_click = Rgb(0xE8, 0x7A, 0x7A);
        t.titlebar_inactive  = Rgb(0x0C, 0x26, 0x46);
        t.button_restore     = Rgb(0x6A, 0xE8, 0xB0);
        t.grid_line          = Rgb(0x12, 0x32, 0x52);  // visible grid — drafting paper
        return t;
    }

    // 9 — Pixel — limited 8-bit palette + bold primaries on dark
    static QdTheme Pixel() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x00, 0x00, 0x18);
        t.surface_glass      = Rgb(0x10, 0x10, 0x30);
        t.topbar_bg          = Rgb(0x08, 0x08, 0x20);
        t.dock_bg            = Rgb(0x0C, 0x0C, 0x28);
        t.accent             = Rgb(0xFF, 0xCC, 0x00);  // NES yellow
        t.text_primary       = Rgb(0xF8, 0xF8, 0xF8);
        t.text_secondary     = Rgb(0x80, 0x80, 0xC0);
        t.focus_ring         = Rgb(0x00, 0xE8, 0x00);  // bold green
        t.button_close       = Rgb(0xE8, 0x00, 0x00);
        t.button_minimize    = Rgb(0xFF, 0x98, 0x00);
        t.button_maximize    = Rgb(0x00, 0xE8, 0x00);
        t.cursor_fill        = Rgb(0xFF, 0xFF, 0xFF);
        t.cursor_outline     = Rgb(0x00, 0x00, 0x00);
        t.cursor_right_click = Rgb(0xE8, 0x00, 0x00);
        t.titlebar_inactive  = Rgb(0x18, 0x18, 0x38);
        t.button_restore     = Rgb(0x00, 0xE8, 0x00);
        t.grid_line          = Rgb(0x20, 0x20, 0x40);
        return t;
    }

    // Legacy alias — DarkLiquidGlass() is the original v2.1.0 name for Glass().
    // Kept as a static method so existing call sites in libs (Plutonium-side
    // adapters, theme cache loaders, the upstream-compatibility shim) continue
    // to compile unchanged. New code should call Glass().
    static QdTheme DarkLiquidGlass() {
        return Glass();
    }
};

// ── Theme registry — paired 1:1 with FolderThemePack indices 0..9 ─────────
//
// kThemePackCount is the canonical count. kThemeNames is the display
// roster shown in Settings → Theme. kThemeFactories[i]() returns the
// QdTheme for index i. Indices match qd_FolderTheme.cpp::kPacks[] and
// qd_Wallpaper.cpp::kWallpaperVariants[], so selecting index 4 swaps
// the palette, the wallpaper, and the folder-icon pack atomically.

constexpr size_t kThemePackCount = 10;

extern const char *const kThemeNames[kThemePackCount];
QdTheme MakeThemeByIndex(size_t idx);

// W12-LAZYTHEME: current active pack index (0..9). Maintained by
// LoadThemeAssets() so the cycle path can read it without going through
// the TOML file.  Initialised to 0 (Glass) at process start.
extern size_t g_active_theme_pack_idx;

// ── Runtime global ─────────────────────────────────────────────────────────
//
// g_QdTheme is the single live palette used by all qdesktop elements.
// Initialized to Glass() at process start (qd_Theme.cpp).
// Mutated by:
//   - LoadThemeFromCache() — reads .ultheme QdPalette.json override
//   - SetActiveThemePack() — picks one of the 10 in-binary factories
// Phase B migration target — element constructors should hold const-ref
// to this global instead of copying by value at construction time.
extern QdTheme g_QdTheme;

// Set g_QdTheme to factory index idx (0..9). No-op if idx out of range.
// Caller is responsible for triggering a layout rebuild
// (RestartMenu or new layout instantiation) for the change to render.
void SetActiveThemePack(size_t idx);

// v3.1.1 — Decoupled setters so the empty-desktop "Change Wallpaper" and
// the desktop-folder "Choose Folder Theme" menu actions can mutate
// independent concerns.  SetActiveThemePack remains as the "set both"
// shortcut used at boot to apply the persisted state.
//
// SetActiveWallpaperPack: writes g_active_wallpaper_pack + raises
// g_wallpaper_dirty so QdWallpaperElement re-bakes its cached texture.
// Palette (g_QdTheme) is NOT touched.
//
// SetActivePalettePack: writes g_QdTheme (palette + folder-theme companion)
// only.  Wallpaper texture / g_active_wallpaper_pack are NOT touched.
void SetActiveWallpaperPack(size_t idx);
void SetActivePalettePack(size_t idx);

// v3.1.1 — Bundled .ultheme filename for each pack index 0..9.  Used by
// the desktop-folder "Change Icons" menu action to switch the active
// theme bundle (which is the only mechanism that actually changes the
// per-category folder icon PNGs — palette/wallpaper packs by themselves
// don't repopulate the active-theme cache).  The strings are owned by
// the .rodata of qd_Theme.cpp; do not free.
const char *GetBundledThemeFilename(size_t idx);

// Read currently-persisted theme pack index from disk (0..9).
// Returns 0 (Glass) if no persistence file or unparseable.
size_t LoadActiveThemePack();

// Persist theme pack index (0..9) to disk so the choice survives reboot.
// Delegates to SaveFolderThemePack — writes sdmc:/ulaunch/qos-folder-theme.toml
// atomically (tmp + rename + fsdevCommitDevice).
void SaveActiveThemePack(size_t idx);

// Load palette tokens from <active_cache_dir>/ui/QdPalette.json into
// g_QdTheme.  Any key absent from the JSON keeps the current theme's
// defaults (not Glass — uses whatever theme was last selected).
// Silent no-op if the file does not exist.
//
// active_cache_dir: path to the extracted .ultheme cache, e.g.
//   "sdmc:/ulaunch/cache/active"   (ul::ActiveThemeCachePath)
// Call after CacheActiveTheme() has extracted the zip.
void LoadThemeFromCache(const std::string &active_cache_dir);

// v2.7.2 — Paint a one-shot "transitioning to new theme" frame using
// the CURRENT g_QdTheme values (caller is expected to have already
// applied the destination theme via LoadThemeFromCache() / SetActiveThemePack()).
//
// Full-screen `desktop_bg` fill + centered Q glyph in `accent` color, ~200×200.
// Followed by an immediate SDL_RenderPresent so the frame is on-screen
// before the caller fires smi::RestartMenu() and Finalize().
//
// Purpose: the uSystem RestartMenu→TerminateMenu queue dance leaves the
// last rendered frame on-screen for ~500–1500 ms while the applet is
// terminated and uMenu relaunches.  Previously that frame was Plutonium's
// cyan/lavender brand fade texture (hardcoded in MenuApplication::SetBackgroundFade)
// which reads as a "wrong theme" flash for non-cyan palettes.  Painting
// over the brand frame with the destination theme makes the defer window
// look like an intentional transition.
//
// Deadlock-safe — no IPC, no la::Terminate, no blocking calls. Just SDL.
void DrawThemeTransitionFrame(SDL_Renderer *r);

} // namespace ul::menu::qdesktop
