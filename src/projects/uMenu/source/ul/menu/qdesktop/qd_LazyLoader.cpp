// qd_LazyLoader.cpp — W11-FOOTPRINT v3.3 / W12-LAZYTHEME v3.4.
// See qd_LazyLoader.hpp for design rationale.
#include <ul/menu/qdesktop/qd_LazyLoader.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_FolderTheme.hpp>
#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/ul_Result.hpp>

namespace ul::menu::qdesktop {

namespace QdLazyLoader {

// ── g_theme_meta definition ───────────────────────────────────────────────────
// Defined here (owned by qd_LazyLoader.cpp). Zero-initialised at process start.
ThemeMetadata g_theme_meta[10] = {};

// ── Part 3: WallpaperResolution ───────────────────────────────────────────────

WpRes WallpaperResolution() {
    const AppletOperationMode mode = appletGetOperationMode();

    if (mode == AppletOperationMode_Handheld) {
        // Handheld: 960×540 (~2.0 MiB RGBA). SDL blits to 1280×720 screen.
        return WpRes{ 960u, 540u, true };
    }
    // Docked (Console) or unknown: 1280×720 (~3.5 MiB RGBA).
    // SDL blits to 1920×1080 screen.
    return WpRes{ 1280u, 720u, false };
}

// ── Part 4: two-phase theme load helpers ─────────────────────────────────────

void LoadAllThemeMetadata() {
    // Populate from in-binary kThemeNames / kThemePackCount tables.
    // No I/O — pure pointer + integer assignment.
    for (size_t i = 0; i < kThemePackCount && i < 10u; ++i) {
        g_theme_meta[i].name   = kThemeNames[i];
        g_theme_meta[i].index  = i;
        g_theme_meta[i].loaded = true;
    }
    UL_LOG_INFO("qdesktop: W12 LoadAllThemeMetadata: %zu themes registered (metadata-only)",
                kThemePackCount);
}

void LoadThemeAssets(size_t idx) {
    if (idx >= kThemePackCount) {
        UL_LOG_WARN("qdesktop: W12 LoadThemeAssets(%zu) out of range — no-op", idx);
        return;
    }
    UL_LOG_INFO("qdesktop: W12 LoadThemeAssets(%zu) — palette + wallpaper dirty + png cache",
                idx);

    // 1. Update palette (g_QdTheme). Pure in-binary data, ~200 bytes.
    SetActivePalettePack(idx);

    // 2. Mark wallpaper dirty so QdWallpaperElement re-bakes its SDL_Texture
    //    on the next render call. The old texture is freed inside OnRender
    //    (UL_LEDGER_UNTRACK + SDL_DestroyTexture) before the new one is created.
    g_wallpaper_dirty.store(true, std::memory_order_release);

    // 3. Flush the PNG folder-icon cache. PNG textures for the old pack are
    //    destroyed + untracked inside InvalidatePngPackCache(). New textures
    //    are created lazily by DrawPngPack() on the first render of each folder
    //    icon in the new pack.
    InvalidatePngPackCache();
}

void UnloadThemeAssets(size_t idx) {
    if (idx >= kThemePackCount) {
        UL_LOG_WARN("qdesktop: W12 UnloadThemeAssets(%zu) out of range — no-op", idx);
        return;
    }
    UL_LOG_INFO("qdesktop: W12 UnloadThemeAssets(%zu) — wallpaper dirty + png cache invalidate",
                idx);

    // Signal QdWallpaperElement to drop its cached SDL_Texture on next render.
    // UL_LEDGER_UNTRACK for the wallpaper texture happens inside OnRender's
    // dirty-flag consumption path, ensuring ledger symmetry without requiring
    // a direct pointer to QdWallpaperElement here.
    g_wallpaper_dirty.store(true, std::memory_order_release);

    // Free all PNG folder textures for all packs. Untrack happens inside
    // InvalidatePngPackCache() for each live slot.
    InvalidatePngPackCache();
}

} // namespace QdLazyLoader

} // namespace ul::menu::qdesktop
