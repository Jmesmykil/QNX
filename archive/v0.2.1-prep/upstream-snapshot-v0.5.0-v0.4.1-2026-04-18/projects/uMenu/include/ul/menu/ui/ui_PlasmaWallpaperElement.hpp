// QOS-PATCH-009: Cold Plasma Cascade procedural wallpaper element.
// Ported from /QOS/tools/mock-nro-desktop-gui/src/wallpaper.rs (Rust xorshift32 PRNG,
// six passes identical to the Rust original).
// 2026-04-18
//
// v0.4.1 OOM fix: pixel framebuffer and SDL_Texture allocated once in ctor,
// reused every frame.  Zero heap churn during OnRender().

#pragma once
#include <pu/Plutonium>
#include <array>
#include <memory>

namespace ul::menu::ui {

    // ── Wallpaper compile-time constants ──────────────────────────────────────

    /// Enable/disable flag.  Set to 0 to fall back to Background.png behaviour.
    #ifndef PLASMA_WALLPAPER_ENABLED
    #define PLASMA_WALLPAPER_ENABLED 1
    #endif

    /// Screen dimensions (Switch docked 1080p canvas, same as WM constants).
    static constexpr s32 WpW = 1280;
    static constexpr s32 WpH = 720;

    /// Fixed xorshift32 seed: "QOS_" in ASCII (0x514F535F).
    static constexpr u32 WpSeed = 0x514F535Fu;

    static constexpr std::size_t BloomCount  = 6;
    static constexpr std::size_t StarCount   = 80;
    static constexpr std::size_t StreamCount = 18;

    /// Grid cell size in pixels (matches Rust GRID_CELL).
    static constexpr s32 GridCell = 64;

    // ── Data structures ────────────────────────────────────────────────────────

    struct PlasmaBloom {
        s32  cx, cy;       // centre x/y (pixels)
        s32  radius;       // outer radius (pixels)
        u8   r, g, b;      // bloom colour
    };

    struct WpStar {
        s32  x, y;
        u8   r, g, b;
        u32  alpha256;    // blending weight [0..256]
    };

    struct DataStream {
        s32  ox, oy;      // origin on top/left edge
        s32  dx, dy;      // step direction (+1 or +2 each axis)
        s32  length;      // total steps (80–400)
        u8   r, g, b;     // stream colour (cold palette)
    };

    // ── Element ───────────────────────────────────────────────────────────────

    /// PlasmaWallpaperElement — `pu::ui::elm::Element` subclass that draws the
    /// "Cold Plasma Cascade" wallpaper each frame via a single SDL_UpdateTexture
    /// + RenderTexture blit (one draw call per frame).
    ///
    /// v0.4.1: pixel buffer and SDL_Texture are allocated once in the ctor and
    /// reused every frame.  No heap allocation occurs during OnRender().
    /// If SDL_CreateTexture fails the element falls back to a single solid-colour
    /// RenderRectangleFill (safe, one call).
    ///
    /// Add at layer 0 (before DockElement, before icon grid) in MainMenuLayout.
    /// OnInput() is a no-op; all draw work happens in OnRender().
    class PlasmaWallpaperElement : public pu::ui::elm::Element {
        private:
            s32 elem_x;
            s32 elem_y;
            u32 elem_w;
            u32 elem_h;

            u32 prng_state;

            std::array<PlasmaBloom,  BloomCount>  blooms;
            std::array<WpStar,       StarCount>   stars;
            std::array<DataStream,   StreamCount> streams;

            u64 frame_count;
            u64 last_time_ms;

            /// Persistent pixel framebuffer — 1280*720*4 bytes, allocated once.
            /// NULL if allocation failed (ctor malloc returned nullptr).
            std::unique_ptr<u32[]> pixel_buf;

            /// Persistent streaming SDL_Texture — updated each frame via SDL_UpdateTexture.
            /// NULL if SDL_CreateTexture failed; triggers solid-colour fallback.
            SDL_Texture* plasma_tex;

            // ── PRNG helpers ──────────────────────────────────────────────────

            /// Xorshift32 — advances prng_state and returns the new value.
            inline u32 Xorshift32() {
                u32 x = this->prng_state;
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                this->prng_state = x;
                return x;
            }

            /// Uniform u32 in [0, range).
            inline u32 RandU32(const u32 range) {
                return this->Xorshift32() % range;
            }

            // ── Pixel math helpers (integer, no floating point) ──────────────

            static inline u32 PackRgb(const u32 r, const u32 g, const u32 b) {
                return ((r < 255u ? r : 255u) << 16)
                     | ((g < 255u ? g : 255u) << 8)
                     |  (b < 255u ? b : 255u);
            }

            static inline void UnpackRgb(const u32 px, u32 &r, u32 &g, u32 &b) {
                r = (px >> 16) & 0xFF;
                g = (px >>  8) & 0xFF;
                b =  px        & 0xFF;
            }

            /// Additive blend: dst += src * alpha / 256, clamped per channel.
            static inline u32 BlendAdd(const u32 dst, const u32 sr, const u32 sg, const u32 sb, const u32 alpha256) {
                u32 dr, dg, db;
                UnpackRgb(dst, dr, dg, db);
                const u32 nr = dr + sr * alpha256 / 256; const u32 cr = nr < 255u ? nr : 255u;
                const u32 ng = dg + sg * alpha256 / 256; const u32 cg = ng < 255u ? ng : 255u;
                const u32 nb = db + sb * alpha256 / 256; const u32 cb = nb < 255u ? nb : 255u;
                return PackRgb(cr, cg, cb);
            }

            /// Linear over: src*a + dst*(1-a), alpha in [0,256].
            static inline u32 BlendOver(const u32 dst, const u32 sr, const u32 sg, const u32 sb, const u32 alpha256) {
                u32 dr, dg, db;
                UnpackRgb(dst, dr, dg, db);
                const u32 inv = 256u - alpha256;
                return PackRgb((sr * alpha256 + dr * inv) / 256u,
                               (sg * alpha256 + dg * inv) / 256u,
                               (sb * alpha256 + db * inv) / 256u);
            }

            /// Integer square root (bisection, no libm).
            static u64 Isqrt64(const u64 n);

            // ── Seed / init helpers ────────────────────────────────────────────

            void SeedBlooms();
            void SeedStars();
            void SeedStreams();

            // ── Render passes (write into pixel_buf) ──────────────────────────

            /// Clear the pixel buffer to the deep-space base colour.
            void PassBackground(u32 *buf) const;

            /// Radial bloom glow (additive).
            void PassBlooms(u32 *buf) const;

            /// Diagonal data-stream streaks (additive).
            void PassStreams(u32 *buf) const;

            /// Single-pixel stars (alpha-over).
            void PassStars(u32 *buf) const;

            /// Faint grid overlay (alpha-over).
            void PassGrid(u32 *buf) const;

            /// Radial corner vignette (multiply-darken).
            void PassVignette(u32 *buf) const;

        public:
            PlasmaWallpaperElement(const s32 x, const s32 y, const u32 w, const u32 h);
            ~PlasmaWallpaperElement();
            PU_SMART_CTOR(PlasmaWallpaperElement)

            inline s32 GetX()      override { return this->elem_x; }
            inline s32 GetY()      override { return this->elem_y; }
            inline s32 GetWidth()  override { return static_cast<s32>(this->elem_w); }
            inline s32 GetHeight() override { return static_cast<s32>(this->elem_h); }

            void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 x, const s32 y) override;
            void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;
    };

}
