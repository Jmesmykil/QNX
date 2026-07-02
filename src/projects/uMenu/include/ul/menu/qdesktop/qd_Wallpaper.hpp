// qd_Wallpaper.hpp — Wallpaper element for uMenu C++.
// Pack 0 (Glass) = Cold Plasma Cascade ported from
// tools/mock-nro-desktop-gui/src/wallpaper.rs.
//
// v2.4.0: extended to 10 in-binary procedural wallpapers (one per QdTheme
// pack). Pack 0 lives in qd_Wallpaper.cpp; packs 1..9 in qd_WallpaperPacks.cpp.
// Render-once per pack: generates a 1280×720 SDL_Texture on first OnRender
// after a pack change, blits it scaled to 1920×1080 every frame.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>

namespace ul::menu::qdesktop {

// ── Wallpaper pack registry — paired 1:1 with QdTheme + FolderThemePack ────
//
// kWallpaperPackCount = kThemePackCount = kFolderThemePackCount = 10
// g_active_wallpaper_pack is read at QdWallpaperElement first-render time
// to pick which RenderPack<N>_<Name> draws into the texture buffer. A pack
// change requires a layout rebuild (RestartMenu) for the new wallpaper to
// take effect — same model as QdTheme palette swaps.

constexpr size_t kWallpaperPackCount = 10;
extern size_t g_active_wallpaper_pack;

// v3.1.1 (BUG-1 fix 2026-05-19): mid-session theme switches need to invalidate
// QdWallpaperElement::cached_tex_ so the next OnRender re-bakes the pixels.
// SetActiveThemePack() raises this flag; QdWallpaperElement::OnRender checks
// it at top, drops cached_tex_, lowers the flag, and falls through to the
// regenerate path.  Atomic (single-bit signal) so the qd_Theme.cpp setter and
// the UI render thread can touch it without locking.
//
// Why an atomic flag instead of a direct method call: QdWallpaperElement
// is owned by a UI Layout we don't have a stable handle to from qd_Theme.cpp,
// and the wallpaper invalidation needs to fire from anywhere SetActiveThemePack
// is invoked (context menu dispatches, future settings UI, etc.).
#include <atomic>
extern std::atomic<bool> g_wallpaper_dirty;

// W11-FOOTPRINT Part 3: render-time width/height override.
// Set by QdWallpaperElement::GeneratePixelsInto to the chosen resolution before
// calling RenderWallpaperPack; restored to WP_W/WP_H after.
// Pack 0 (qd_Wallpaper.cpp) and Packs 1..9 (qd_WallpaperPacks.cpp) read these
// instead of WP_W/WP_H so both paths support the handheld 960×540 resolution.
extern u32 g_wp_render_w;
extern u32 g_wp_render_h;

// Dispatch wallpaper render to the appropriate pack function.
// Writes WP_W * WP_H RGBA8888 pixels into the caller-provided buffer using
// the caller's row pitch (allows writing directly into SDL_LockTexture
// memory). Reads `pack_idx` to choose the algorithm; out-of-range falls
// back to pack 0 (Glass / Cold Plasma Cascade).
void RenderWallpaperPack(size_t pack_idx, const QdTheme &theme, u8 *buf, int pitch_bytes);

// Pack 0 — Glass (Cold Plasma Cascade); defined in qd_Wallpaper.cpp.
void RenderWallpaperPack0_Glass(const QdTheme &theme, u8 *buf, int pitch_bytes);

// Packs 1..9 — defined in qd_WallpaperPacks.cpp.
void RenderWallpaperPack1_Neon(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack2_Minimal(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack3_Retro(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack4_Cards(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack5_Pastel(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack6_Dark(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack7_Gradient(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack8_Blueprint(const QdTheme &theme, u8 *buf, int pitch_bytes);
void RenderWallpaperPack9_Pixel(const QdTheme &theme, u8 *buf, int pitch_bytes);

// ── Wallpaper algorithm constants ─────────────────────────────────────────
// All values from wallpaper.rs — do not change.
static constexpr u32  WP_SEED        = 0x514F535F;  // "QOS_"
// Wallpaper texture is rendered at native Rust resolution (1280×720) and
// scaled to the full screen (1920×1080) at blit time via SDL_RenderCopy.
// This keeps the texture memory at ~3.5 MB (1280*720*4) instead of ~8 MB,
// which the Switch GPU pool cannot fit alongside Plutonium's framebuffers.
static constexpr u32  WP_W           = 1280;
static constexpr u32  WP_H           = 720;
// Full-screen blit dimensions (used as SDL_Rect dst).
static constexpr u32  WP_BLIT_W      = 1920;
static constexpr u32  WP_BLIT_H      = 1080;
static constexpr u32  BLOOM_COUNT    = 6;            // from wallpaper.rs BLOOM_COUNT
static constexpr u32  STAR_COUNT     = 80;           // from wallpaper.rs STAR_COUNT
static constexpr u32  STREAM_COUNT   = 18;           // from wallpaper.rs STREAM_COUNT
static constexpr u32  GRID_CELL      = 64;           // wallpaper.rs GRID_CELL (1280×720 native)
static constexpr u8   GRID_ALPHA     = 22;           // from wallpaper.rs GRID_ALPHA

// Bloom palette (R,G,B) — 6 entries verbatim from wallpaper.rs BLOOM_PALETTES.
static constexpr u8 BLOOM_PAL_R[6] = { 0x7D, 0x34, 0x81, 0xA5, 0x38, 0xA7 };
static constexpr u8 BLOOM_PAL_G[6] = { 0xD3, 0xD3, 0x8C, 0xF3, 0xBD, 0x8B };
static constexpr u8 BLOOM_PAL_B[6] = { 0xFC, 0x99, 0xF8, 0xFC, 0xF8, 0xFA };

// Bloom centres (1280×720 native, identical to wallpaper.rs BLOOM_CENTRES).
// Texture is rendered at native res and SDL scales to 1920×1080 at blit time.
static constexpr u32 BLOOM_CX[6] = { 190, 740,  1120, 320, 900, 620 };
static constexpr u32 BLOOM_CY[6] = { 200, 110,  250,  540, 490, 360 };

// Bloom radii (1280×720 native).
//
// Cycle C4: reduced from the Rust-PoC values { 220, 190, 160, 200, 180, 140 }
// because at the C++ port's 1.5× SDL_RenderCopy upscale the blooms read as
// three giant translucent disks dominating the centre of the screen on
// hardware. The radii here are roughly 60% of the PoC values, which after
// the 1.5× upscale lands at ~the PoC's perceived size on a 720p screen.
// Combined with the alpha-cap drop from 180→110 in qd_Wallpaper.cpp, the
// wallpaper now reads as background ambience instead of a foreground
// element competing with the desktop icons.
static constexpr u32 BLOOM_RADII_CPP[6] = { 130, 110, 96, 120, 108, 84 };

// ── QdWallpaperElement ─────────────────────────────────────────────────────

// Pu Element that renders the Cold Plasma Cascade wallpaper.
// Covers full screen (1920×1080).  Renders exactly once to a cached SDL_Texture.
class QdWallpaperElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdWallpaperElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdWallpaperElement>(theme);
    }

    explicit QdWallpaperElement(const QdTheme &theme);
    ~QdWallpaperElement();

    s32 GetX() override { return 0; }
    s32 GetY() override { return 0; }
    // Layout dimensions = full screen (blit dst). The cached texture is
    // 1280×720 native and SDL scales it up to 1920×1080 in OnRender.
    s32 GetWidth() override  { return static_cast<s32>(WP_BLIT_W); }
    s32 GetHeight() override { return static_cast<s32>(WP_BLIT_H); }

    // First call: generates the wallpaper pixel buffer and uploads to SDL_Texture.
    // Subsequent calls: blit cached texture.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    // Wallpaper has no interactive input.
    // Parameter names omitted to satisfy -Wunused-parameter -Werror (F-02 fix).
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}

    // v2.4.0: helper statics made public so the free pack render functions in
    // qd_Wallpaper.cpp / qd_WallpaperPacks.cpp can use the same Xorshift seed +
    // blend math without code duplication. Behaviour unchanged.

    // Xorshift32 PRNG — exact port of wallpaper.rs xorshift().
    static u32 Xorshift(u32 &state);

    // Integer square root (no libm) — adapted from icon_cache.rs isqrt64.
    static u32 Isqrt64(u64 n);

    // Additive blend src over dst pixel (single channel, clamp 255).
    static void BlendAddCh(u8 &dst, u8 src, u32 alpha256);

    // Alpha-over blend src over dst pixel (single channel).
    static void BlendOverCh(u8 &dst, u8 src, u32 alpha256);

private:
    QdTheme theme_;
    pu::sdl2::Texture cached_tex_;  // nullptr until first render
    bool rendered_;
    // W11-FOOTPRINT Part 1: ledger handle for the wallpaper texture so the
    // Monitor Resources view tracks Texture bytes accurately.  0 = not tracked.
    uint64_t ledger_handle_;
    // W11-FOOTPRINT Part 3: texture dimensions selected at first-render time
    // based on appletGetOperationMode (docked=1280×720, handheld=960×540).
    int tex_w_;
    int tex_h_;

    // Generate pixels at `w`×`h` RGBA into a caller-provided buffer (v2.4.0:
    // dispatches to RenderWallpaperPack(g_active_wallpaper_pack)).
    // W11 Part 3: w/h are now explicit arguments instead of WP_W/WP_H constants
    // so the handheld-mode lower-resolution path can share the same render functions.
    // Pack render functions internally scale their geometry to (w,h).
    static void GeneratePixelsInto(const QdTheme &theme, u8 *buf, int pitch_bytes,
                                   int w = static_cast<int>(WP_W),
                                   int h = static_cast<int>(WP_H));
};

} // namespace ul::menu::qdesktop
