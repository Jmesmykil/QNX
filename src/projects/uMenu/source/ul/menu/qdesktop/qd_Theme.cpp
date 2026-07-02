// qd_Theme.cpp — Q OS desktop color token system for uMenu C++ port.
//
// Phase A (v2.2.5): g_QdTheme global + LoadThemeFromCache() to apply
// .ultheme QdPalette.json overrides.
//
// v2.4.0: extended to 10 in-binary QdTheme factories (Glass / Neon /
// Minimal / Retro / Cards / Pastel / Dark / Gradient / Blueprint /
// Pixel). Theme pack index 0..9 selects palette + wallpaper +
// folder-icon pack atomically (paired registries in qd_Wallpaper.cpp
// and qd_FolderTheme.cpp use the same indices).
//
// Originally ported from tools/mock-nro-desktop-gui/src/theme.rs.

#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/menu/qdesktop/qd_Cursor.hpp>   // g_cursor_dirty — rebuild cursor on palette change
#include <ul/menu/qdesktop/qd_FolderTheme.hpp>
#include <ul/util/util_Json.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>      // v2.7.2 — DrawThemeTransitionFrame uses SDL_Renderer
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ul::menu::qdesktop {

// ── Runtime globals ───────────────────────────────────────────────────────────
// Initialized once at startup to Glass() (the v2.1.0 default).
// Mutated by SetActiveThemePack() and LoadThemeFromCache().
QdTheme g_QdTheme = QdTheme::Glass();

// W12-LAZYTHEME: tracks the currently active pack index so callers can read
// it without a TOML round-trip. Updated by SetActivePalettePack().
size_t g_active_theme_pack_idx = 0;

// ── 10-pack registry — paired 1:1 with FolderThemePack + WallpaperVariant ─────

const char *const kThemeNames[kThemePackCount] = {
    "Q OS",       // 0 (the canonical Q OS default — palette/wallpaper unchanged from v2.1.0 DarkLiquidGlass)
    "Neon",       // 1
    "Minimal",    // 2
    "Retro",      // 3
    "Cards",      // 4
    "Pastel",     // 5
    "Dark",       // 6
    "Gradient",   // 7
    "Blueprint",  // 8
    "Pixel",      // 9
};

QdTheme MakeThemeByIndex(size_t idx) {
    switch (idx) {
        case 0: return QdTheme::Glass();
        case 1: return QdTheme::Neon();
        case 2: return QdTheme::Minimal();
        case 3: return QdTheme::Retro();
        case 4: return QdTheme::Cards();
        case 5: return QdTheme::Pastel();
        case 6: return QdTheme::Dark();
        case 7: return QdTheme::Gradient();
        case 8: return QdTheme::Blueprint();
        case 9: return QdTheme::Pixel();
        default: return QdTheme::Glass();
    }
}

void SetActiveThemePack(size_t idx) {
    if (idx >= kThemePackCount) {
        UL_LOG_WARN("qdesktop: SetActiveThemePack(%zu) out of range — keeping current", idx);
        return;
    }
    // Unified set: palette + wallpaper.  Used by boot init to apply persisted
    // state and by any caller that wants the legacy combined behavior.
    // Z2.1 "Change Wallpaper" and Z2.2 "Choose Folder Theme" use the
    // decoupled setters below.
    SetActivePalettePack(idx);
    SetActiveWallpaperPack(idx);
    UL_LOG_INFO("qdesktop: active theme pack = %zu (%s) [palette+wallpaper unified]",
                idx, kThemeNames[idx]);
}

void SetActiveWallpaperPack(size_t idx) {
    if (idx >= kThemePackCount) {
        UL_LOG_WARN("qdesktop: SetActiveWallpaperPack(%zu) out of range", idx);
        return;
    }
    g_active_wallpaper_pack = idx;
    g_wallpaper_dirty.store(true, std::memory_order_release);
    UL_LOG_INFO("qdesktop: SetActiveWallpaperPack = %zu (%s) [wallpaper only]",
                idx, kThemeNames[idx]);
}

void SetActivePalettePack(size_t idx) {
    if (idx >= kThemePackCount) {
        UL_LOG_WARN("qdesktop: SetActivePalettePack(%zu) out of range", idx);
        return;
    }
    g_QdTheme = MakeThemeByIndex(idx);
    g_active_theme_pack_idx = idx;  // W12-LAZYTHEME: keep in-memory index in sync
    // Cursor colours are palette-derived — flag a cursor rebuild too (consumed
    // in QdCursorElement::OnRender), mirroring the wallpaper-dirty mechanism.
    g_cursor_dirty.store(true, std::memory_order_release);
    UL_LOG_INFO("qdesktop: SetActivePalettePack = %zu (%s) [palette only]",
                idx, kThemeNames[idx]);
}

// v3.1.1 — Bundled .ultheme filename per pack index.  Mirrors
// kQosThemes in main.cpp's EnsureQosThemesOnSd (the source of truth for
// what files exist in sdmc:/ulaunch/themes/ after first boot).  Used by
// the "Change Icons" right-click action.
static constexpr const char *kBundledThemeFilenames[kThemePackCount] = {
    "q-os-0-q-os.ultheme",
    "q-os-1-neon.ultheme",
    "q-os-2-minimal.ultheme",
    "q-os-3-retro.ultheme",
    "q-os-4-cards.ultheme",
    "q-os-5-pastel.ultheme",
    "q-os-6-dark.ultheme",
    "q-os-7-gradient.ultheme",
    "q-os-8-blueprint.ultheme",
    "q-os-9-pixel.ultheme",
};

const char *GetBundledThemeFilename(size_t idx) {
    if (idx >= kThemePackCount) return kBundledThemeFilenames[0];
    return kBundledThemeFilenames[idx];
}

// ── Persistence — delegates to qd_FolderTheme's qos-folder-theme.toml ─────────
//
// v2.4.0 single-source-of-truth: the existing qos-folder-theme.toml stores
// the unified pack index. SetActiveThemePack drives palette + wallpaper +
// folder, so writing the folder TOML persists all three. No new file needed.

size_t LoadActiveThemePack() {
    // qd_FolderTheme already reads from sdmc:/ulaunch/qos-folder-theme.toml
    // and clamps the value into [0, kFolderThemePackCount). Since
    // kThemePackCount == kFolderThemePackCount, this is a 1:1 mapping.
    return LoadFolderThemePack();
}

void SaveActiveThemePack(size_t idx) {
    if (idx >= kThemePackCount) idx = 0;
    SaveFolderThemePack(idx);
    // W5-TRANSITIONS #2: flush stale PNG textures so new pack loads fresh.
    InvalidatePngPackCache();
}

// ── Hex colour parser ─────────────────────────────────────────────────────────
// Parses "#RRGGBB" or "#RRGGBBAA" hex strings (case-insensitive).
// Returns false and leaves `out` unchanged if the string is malformed.
static bool ParseHexColor(const std::string &hex, pu::ui::Color &out) {
    if (hex.empty() || hex[0] != '#') {
        return false;
    }
    const auto *s = hex.c_str() + 1;  // skip '#'
    const auto len = hex.size() - 1;

    unsigned int r = 0, g = 0, b = 0, a = 0xFF;
    if (len == 6) {
        // "#RRGGBB"
        if (std::sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3) {
            return false;
        }
    } else if (len == 8) {
        // "#RRGGBBAA"
        if (std::sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) {
            return false;
        }
    } else {
        return false;
    }

    out = pu::ui::Color(
        static_cast<u8>(r),
        static_cast<u8>(g),
        static_cast<u8>(b),
        static_cast<u8>(a)
    );
    return true;
}

// Convenience: try to read a single palette key from the JSON and apply it to
// a QdTheme field.  Logs a warning and skips on parse failure.
#define _QD_PALETTE_APPLY(json_obj, key, field) \
    if ((json_obj).count(#key)) { \
        pu::ui::Color _parsed_color; \
        if (ParseHexColor((json_obj)[#key].get<std::string>(), _parsed_color)) { \
            g_QdTheme.field = _parsed_color; \
        } else { \
            UL_LOG_WARN("qdesktop: QdPalette.json key '" #key "' has invalid hex value '%s' — ignored", \
                        (json_obj)[#key].get<std::string>().c_str()); \
        } \
    }

// ── LoadThemeFromCache ────────────────────────────────────────────────────────
//
// v2.4.0 semantics: applies .ultheme QdPalette.json overrides ON TOP OF the
// CURRENTLY SELECTED in-binary theme pack (not Glass). This means a user who
// has selected the Neon pack can still apply a .ultheme that tweaks just the
// accent colour — the rest of the Neon palette is preserved.

void LoadThemeFromCache(const std::string &active_cache_dir) {
    // Path: <active_cache_dir>/ui/QdPalette.json
    const std::string palette_path = active_cache_dir + "/ui/QdPalette.json";

    if (!ul::fs::ExistsFile(palette_path)) {
        // No QdPalette.json in this theme — keep current pack defaults.
        UL_LOG_INFO("qdesktop: No QdPalette.json at '%s' — keeping in-binary pack defaults",
                    palette_path.c_str());
        return;
    }

    ul::util::JSON palette_json;
    const auto rc = ul::util::LoadJSONFromFile(palette_json, palette_path);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: Failed to parse QdPalette.json at '%s' (rc=0x%X) — keeping defaults",
                    palette_path.c_str(), rc);
        return;
    }

    UL_LOG_INFO("qdesktop: Loading QdPalette.json from '%s' (on top of current pack)", palette_path.c_str());

    // v2.7.0 — Q OS extension: read the "wallpaper_pack" field if present
    // (e.g. "Glass", "Neon", "Pixel", etc.). Maps to the in-binary procedural
    // wallpaper index. This is the canonical mechanism for a .ultheme to
    // declare which Q OS procedural wallpaper it wants. Third-party themes
    // can omit this field and Q OS keeps whatever pack was previously active
    // (defaults to 0 = Glass).
    if (palette_json.count("wallpaper_pack")) {
        const std::string wp = palette_json["wallpaper_pack"].get<std::string>();
        size_t idx = 0;
        for (size_t i = 0; i < kThemePackCount; ++i) {
            if (wp == kThemeNames[i]) { idx = i; break; }
        }
        g_active_wallpaper_pack = idx;
        UL_LOG_INFO("qdesktop: QdPalette wallpaper_pack='%s' → g_active_wallpaper_pack=%zu",
                    wp.c_str(), idx);
    }

    // NOTE: v2.4.0 — do NOT reset g_QdTheme to Glass here. We layer the JSON
    // overrides on top of whatever pack was previously active (set either at
    // startup from qos-folder-theme.toml or by user via Settings).

    // Apply each token that is present in the JSON.  Unknown keys are ignored.
    _QD_PALETTE_APPLY(palette_json, desktop_bg,         desktop_bg)
    _QD_PALETTE_APPLY(palette_json, surface_glass,      surface_glass)
    _QD_PALETTE_APPLY(palette_json, topbar_bg,          topbar_bg)
    _QD_PALETTE_APPLY(palette_json, dock_bg,            dock_bg)
    _QD_PALETTE_APPLY(palette_json, accent,             accent)
    _QD_PALETTE_APPLY(palette_json, text_primary,       text_primary)
    _QD_PALETTE_APPLY(palette_json, text_secondary,     text_secondary)
    _QD_PALETTE_APPLY(palette_json, focus_ring,         focus_ring)
    _QD_PALETTE_APPLY(palette_json, button_close,       button_close)
    _QD_PALETTE_APPLY(palette_json, button_minimize,    button_minimize)
    _QD_PALETTE_APPLY(palette_json, button_maximize,    button_maximize)
    _QD_PALETTE_APPLY(palette_json, cursor_fill,        cursor_fill)
    _QD_PALETTE_APPLY(palette_json, cursor_outline,     cursor_outline)
    _QD_PALETTE_APPLY(palette_json, cursor_right_click, cursor_right_click)
    _QD_PALETTE_APPLY(palette_json, titlebar_inactive,  titlebar_inactive)
    _QD_PALETTE_APPLY(palette_json, button_restore,     button_restore)
    _QD_PALETTE_APPLY(palette_json, grid_line,          grid_line)

    UL_LOG_INFO("qdesktop: QdPalette.json applied to g_QdTheme successfully");
}

#undef _QD_PALETTE_APPLY

// ── DrawThemeTransitionFrame ────────────────────────────────────────────────
//
// v2.7.2 — paint a one-shot frame in the destination theme's identity to
// cover the uSystem RestartMenu→TerminateMenu defer window with something
// that reads as an intentional transition instead of a frozen frame of the
// hardcoded cyan/lavender brand fade texture (which doesn't match any
// non-Q-OS-pack-0 theme).
//
// Layout: full-screen `desktop_bg` + centered Q-glyph (~200×200) in `accent`.
// The Q-glyph geometry mirrors the hot-corner widget (qd_HotCornerOverlay.cpp)
// but scaled 5.5× — same primitive rects, same "open square plus tail"
// silhouette, so visually it's the same brand mark just bigger.
//
// Caller contract: must have already updated g_QdTheme to the destination
// theme (via LoadThemeFromCache() for .ultheme apply, or SetActiveThemePack()
// for reset-to-default). DrawThemeTransitionFrame reads g_QdTheme directly.
void DrawThemeTransitionFrame(SDL_Renderer *r) {
    if (r == nullptr) {
        UL_LOG_WARN("DrawThemeTransitionFrame: SDL_Renderer is null — skipping");
        return;
    }

    // 1280×720 screen (Switch native handheld; docked upscales).
    constexpr int kScreenW = 1280;
    constexpr int kScreenH = 720;

    // v3.6 — match the qd_Transition loading-splash look so a theme-apply
    // transition reads as the same intentional splash family (gradient +
    // Q emblem + loading dots) rather than a flat colour. This function is
    // font-less (qd_Theme.cpp does not pull in the Plutonium text path), so
    // the "Q OS" wordmark stays a primitive-drawn Q glyph — the SAME emblem
    // the hot-corner widget uses — instead of RenderText. Colours come from
    // g_QdTheme so it rebrands per theme. (Default content pending a specific
    // creator design for the loading splash.)

    // Themed vertical gradient: top = darkened desktop_bg (depth), bottom =
    // accent mixed back toward desktop_bg (in-palette, not blown out). Drawn
    // as 16-px horizontal strips — cheap and font-free. Mirrors
    // qd_Transition::ThemeGradientEndpoints / FillThemeGradient.
    const auto &bg     = g_QdTheme.desktop_bg;
    const auto &accent = g_QdTheme.accent;
    auto clamp_u8 = [](int v) -> u8 {
        if (v < 0)   return 0u;
        if (v > 255) return 255u;
        return static_cast<u8>(v);
    };
    auto lerp_u8 = [](u8 a, u8 b, int frac255) -> u8 {
        const int fa = 255 - frac255;
        return static_cast<u8>((static_cast<int>(a) * fa +
                                static_cast<int>(b) * frac255) / 255);
    };
    const u8 top_r = clamp_u8(static_cast<int>(bg.r) * 60 / 100);
    const u8 top_g = clamp_u8(static_cast<int>(bg.g) * 60 / 100);
    const u8 top_b = clamp_u8(static_cast<int>(bg.b) * 60 / 100);
    const u8 bot_r = clamp_u8((static_cast<int>(accent.r) * 60 + static_cast<int>(bg.r) * 40) / 100);
    const u8 bot_g = clamp_u8((static_cast<int>(accent.g) * 60 + static_cast<int>(bg.g) * 40) / 100);
    const u8 bot_b = clamp_u8((static_cast<int>(accent.b) * 60 + static_cast<int>(bg.b) * 40) / 100);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    constexpr int kBand = 16;
    for (int y = 0; y < kScreenH; y += kBand) {
        const int frac = (y * 255) / (kScreenH - 1);
        SDL_SetRenderDrawColor(r,
            lerp_u8(top_r, bot_r, frac),
            lerp_u8(top_g, bot_g, frac),
            lerp_u8(top_b, bot_b, frac),
            0xFFu);
        SDL_Rect band { 0, y, kScreenW, kBand };
        SDL_RenderFillRect(r, &band);
    }

    // Q-glyph emblem, ~180×180, sitting just above centre (dots go below).
    // Geometry scaled from qd_HotCornerOverlay (36×36 there) by ~5×.
    constexpr int kGlyphSz   = 180;
    constexpr int kGlyphStroke = 20;
    constexpr int kTailW     = 70;
    constexpr int kTailH     = 20;
    const int gx = (kScreenW - kGlyphSz) / 2;
    const int gy = (kScreenH - kGlyphSz) / 2 - 30;   // lift to leave room for dots

    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 0xFFu);
    SDL_Rect q_top   { gx,                          gy,                              kGlyphSz,    kGlyphStroke };
    SDL_Rect q_bot   { gx,                          gy + kGlyphSz - kGlyphStroke,    kGlyphSz,    kGlyphStroke };
    SDL_Rect q_left  { gx,                          gy,                              kGlyphStroke, kGlyphSz   };
    SDL_Rect q_right { gx + kGlyphSz - kGlyphStroke, gy,                             kGlyphStroke, kGlyphSz   };
    // Tail in the bottom-right — same offset ratio as the corner widget
    // (tail starts 26/36 = 0.72 from glyph origin in both X and Y).
    const int tx = gx + static_cast<int>(kGlyphSz * 0.72);
    const int ty = gy + static_cast<int>(kGlyphSz * 0.72);
    SDL_Rect q_tail  { tx, ty, kTailW, kTailH };
    SDL_RenderFillRect(r, &q_top);
    SDL_RenderFillRect(r, &q_bot);
    SDL_RenderFillRect(r, &q_left);
    SDL_RenderFillRect(r, &q_right);
    SDL_RenderFillRect(r, &q_tail);

    // Static loading-dot row below the emblem (this is a one-shot frame, so no
    // animation here — the animated version lives in
    // qd_Transition::DrawLoadingSplashFrame). 3 dots, first one "lit".
    constexpr int kDotCount = 3;
    constexpr int kDotSz    = 14;   // square stand-in for a dot (font-free)
    constexpr int kDotGap   = 34;
    const int dots_total = (kDotCount - 1) * kDotGap;
    const int dots_x0 = (kScreenW - dots_total - kDotSz) / 2;
    const int dots_y  = gy + kGlyphSz + 40;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < kDotCount; ++i) {
        const u8 a = (i == 0) ? 0xFFu : 0x66u;   // first dot lit, rest dim
        SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, a);
        SDL_Rect dot { dots_x0 + i * kDotGap, dots_y, kDotSz, kDotSz };
        SDL_RenderFillRect(r, &dot);
    }

    // Push to screen NOW — caller is about to fire smi::RestartMenu/Finalize
    // and we want this frame to be the last thing in the framebuffer.
    SDL_RenderPresent(r);

    UL_LOG_INFO("qdesktop: DrawThemeTransitionFrame painted splash "
                "(grad #%02X%02X%02X→#%02X%02X%02X, accent emblem+dots, v3.6)",
                top_r, top_g, top_b, bot_r, bot_g, bot_b);
}

} // namespace ul::menu::qdesktop
