// qd_Wallpaper.hpp — Cold Plasma Cascade wallpaper element for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/wallpaper.rs.
// Render-once: generates a 1920×1080 SDL_Texture on first OnRender, blits it every frame.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>

namespace ul::menu::qdesktop {

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

// Bloom radii (1280×720 native) — from wallpaper.rs BLOOM_RADII.
static constexpr u32 BLOOM_RADII_CPP[6] = { 220, 190, 160, 200, 180, 140 };

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

private:
    QdTheme theme_;
    pu::sdl2::Texture cached_tex_;  // nullptr until first render
    bool rendered_;

    // Generate full 1920×1080 RGBA into a caller-provided buffer.
    // 6 passes: base fill, blooms, streams, stars, grid overlay, vignette.
    // `pitch_bytes` = bytes between consecutive rows (>= WP_W*4); allows
    // writing directly into SDL_LockTexture-provided GPU memory without a
    // host-side 8 MB alloc.
    static void GeneratePixelsInto(const QdTheme &theme, u8 *buf, int pitch_bytes);

    // Xorshift32 PRNG — exact port of wallpaper.rs xorshift().
    static u32 Xorshift(u32 &state);

    // Integer square root (no libm) — from icon_cache.rs isqrt64, adapted for u64.
    // Returns largest r such that r*r <= n.
    static u32 Isqrt64(u64 n);

    // Additive blend src over dst pixel (single channel, clamp 255).
    static void BlendAddCh(u8 &dst, u8 src, u32 alpha256);

    // Alpha-over blend src over dst pixel (single channel).
    static void BlendOverCh(u8 &dst, u8 src, u32 alpha256);
};

} // namespace ul::menu::qdesktop
