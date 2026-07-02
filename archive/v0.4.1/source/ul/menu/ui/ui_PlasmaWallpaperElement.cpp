// QOS-PATCH-009: Cold Plasma Cascade procedural wallpaper element.
// Ported from /QOS/tools/mock-nro-desktop-gui/src/wallpaper.rs
// All PRNG / compositing logic is a direct translation of the Rust source;
// the seed, bloom positions, radii, stream lengths, and colour palette are
// bit-identical to the Rust original so both renderers produce the same frame.
// 2026-04-18
//
// v0.4.1 OOM fix — single-allocation design:
//   pixel_buf  : std::unique_ptr<u32[]>(1280*720)  — allocated in ctor, reused every frame
//   plasma_tex : SDL_Texture* streaming ARGB8888   — created in ctor, updated each frame
//   OnRender() : run 5 passes into pixel_buf, call SDL_UpdateTexture, then
//                drawer->RenderTexture one time.  Zero heap churn per frame.
//   Fallback   : if plasma_tex == nullptr, one RenderRectangleFill (deep-space colour).

#include <ul/menu/ui/ui_PlasmaWallpaperElement.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>
#include <new>

namespace ul::menu::ui {

    // ── Bloom palette / geometry (mirrors Rust BLOOM_PALETTES / BLOOM_CENTRES / BLOOM_RADII) ─

    static constexpr struct { u8 r, g, b; } BloomPalette[BloomCount] = {
        { 0x7D, 0xD3, 0xFC }, // cold plasma blue   (#7dd3fc)
        { 0x34, 0xD3, 0x99 }, // ice teal           (#34d399)
        { 0x81, 0x8C, 0xF8 }, // indigo accent      (#818cf8)
        { 0xA5, 0xF3, 0xFC }, // arctic cyan        (#a5f3fc)
        { 0x38, 0xBD, 0xF8 }, // sky blue           (#38bdf8)
        { 0xA7, 0x8B, 0xFA }, // soft violet        (#a78bfa)
    };

    static constexpr struct { s32 cx, cy; } BloomCentres[BloomCount] = {
        { 190, 200 }, // upper-left cluster
        { 740, 110 }, // top-center
        {1120, 250 }, // upper-right
        { 320, 540 }, // lower-left
        { 900, 490 }, // lower-right cluster
        { 620, 360 }, // dead center
    };

    static constexpr s32 BloomRadii[BloomCount] = { 220, 190, 160, 200, 180, 140 };

    // ── Stream colour options (cold palette subset) ──────────────────────────

    static constexpr struct { u8 r, g, b; } StreamPalette[3] = {
        { 0x7D, 0xD3, 0xFC }, // cold blue
        { 0x34, 0xD3, 0x99 }, // teal
        { 0xA5, 0xF3, 0xFC }, // arctic
    };

    // ── Grid / vignette constants (mirrors Rust) ─────────────────────────────

    static constexpr u32 GridAlpha = 22u;
    static constexpr u32 GridR     = 0x18u;
    static constexpr u32 GridG     = 0x18u;
    static constexpr u32 GridB     = 0x32u;

    // ── Fallback base colour (deep-space #0A0A14) ─────────────────────────────

    static constexpr u8 FallbackR = 0x0Au;
    static constexpr u8 FallbackG = 0x0Au;
    static constexpr u8 FallbackB = 0x14u;

    // ── Isqrt64 ───────────────────────────────────────────────────────────────

    u64 PlasmaWallpaperElement::Isqrt64(const u64 n) {
        if(n == 0) { return 0; }
        u64 lo = 1;
        u64 hi = n < 0x000F'FFFF'FFFFull ? n : 0x000F'FFFF'FFFFull;
        while(lo < hi) {
            const u64 mid = (lo + hi + 1) / 2;
            if(mid * mid <= n) { lo = mid; } else { hi = mid - 1; }
        }
        return lo;
    }

    // ── Constructor ───────────────────────────────────────────────────────────

    PlasmaWallpaperElement::PlasmaWallpaperElement(const s32 x, const s32 y, const u32 w, const u32 h)
        : elem_x(x), elem_y(y), elem_w(w), elem_h(h),
          prng_state(WpSeed), blooms{}, stars{}, streams{},
          frame_count(0), last_time_ms(0),
          pixel_buf(nullptr), plasma_tex(nullptr)
    {
        // Allocate persistent pixel framebuffer — 1280*720*4 = 3,686,400 bytes.
        // uMenu is built with -fno-exceptions; new(nothrow) returns nullptr instead
        // of throwing on allocation failure.
        u32 *raw_buf = new(std::nothrow) u32[static_cast<std::size_t>(WpW * WpH)];
        pixel_buf.reset(raw_buf);

        // Create persistent SDL_Texture (streaming, ARGB8888, 1280×720).
        // pu::ui::render::GetMainRenderer() returns the SDL_Renderer* that the
        // Plutonium framework has already initialised.  We call SDL directly here
        // to create the texture once; Plutonium's RenderTexture() accepts SDL_Texture*.
        if(pixel_buf) {
            SDL_Renderer *sdl_renderer = pu::ui::render::GetMainRenderer();
            if(sdl_renderer != nullptr) {
                plasma_tex = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING,
                    WpW, WpH);
            }
        }

        fprintf(stderr,
            "[PLASMA] ctor tex=%p buf=%p\n",
            static_cast<void*>(plasma_tex),
            static_cast<void*>(pixel_buf.get()));

        if(plasma_tex == nullptr) {
            fprintf(stderr,
                "[PLASMA] WARNING: SDL_CreateTexture failed (%s) — solid-colour fallback active\n",
                SDL_GetError());
        }

        // Seed all three scene objects once.
        this->SeedBlooms();
        this->SeedStreams();
        this->SeedStars();
    }

    // ── Destructor ────────────────────────────────────────────────────────────

    PlasmaWallpaperElement::~PlasmaWallpaperElement() {
        fprintf(stderr,
            "[PLASMA] dtor tex=%p buf=%p\n",
            static_cast<void*>(plasma_tex),
            static_cast<void*>(pixel_buf.get()));
        if(plasma_tex != nullptr) {
            SDL_DestroyTexture(plasma_tex);
            plasma_tex = nullptr;
        }
        // pixel_buf is a unique_ptr — auto-freed.
    }

    // ── Seeding ───────────────────────────────────────────────────────────────

    void PlasmaWallpaperElement::SeedBlooms() {
        // Blooms are deterministic — no PRNG needed, use the compile-time tables.
        for(std::size_t i = 0; i < BloomCount; i++) {
            auto &b = this->blooms[i];
            b.cx     = BloomCentres[i].cx;
            b.cy     = BloomCentres[i].cy;
            b.radius = BloomRadii[i];
            b.r      = BloomPalette[i].r;
            b.g      = BloomPalette[i].g;
            b.b      = BloomPalette[i].b;
        }
    }

    void PlasmaWallpaperElement::SeedStars() {
        // Reset PRNG to a sub-seed derived from WpSeed so stars are deterministic.
        // Advance past the bloom RNG calls (blooms use no PRNG, so we start clean).
        // Rust processes: blooms (no rng), streams (advance rng), stars (advance rng).
        // We replicate by seeding streams first, then stars, in the same order.
        // Both match the Rust pass order (blooms, streams, stars).

        for(std::size_t i = 0; i < StarCount; i++) {
            auto &s   = this->stars[i];
            s.x       = static_cast<s32>(this->RandU32(static_cast<u32>(WpW)));
            s.y       = static_cast<s32>(this->RandU32(static_cast<u32>(WpH)));

            // Colour: 4 variants matching Rust exactly
            const u32 t = this->RandU32(4);
            switch(t) {
                case 0:  s.r = 0xFF; s.g = 0xFF; s.b = 0xFF; break; // pure white
                case 1:  s.r = 0xA5; s.g = 0xF3; s.b = 0xFC; break; // arctic
                case 2:  s.r = 0xE0; s.g = 0xE0; s.b = 0xF0; break; // off-white
                default: s.r = 0x7D; s.g = 0xD3; s.b = 0xFC; break; // cold blue
            }
            s.alpha256 = 100u + this->RandU32(156u); // 100..255
        }
    }

    void PlasmaWallpaperElement::SeedStreams() {
        for(std::size_t i = 0; i < StreamCount; i++) {
            auto &st = this->streams[i];

            const bool on_top = (this->Xorshift32() & 1u) == 0u;
            if(on_top) {
                st.ox = static_cast<s32>(this->RandU32(static_cast<u32>(WpW)));
                st.oy = 0;
            } else {
                st.ox = 0;
                st.oy = static_cast<s32>(this->RandU32(static_cast<u32>(WpH)));
            }
            st.dx = 1 + static_cast<s32>(this->RandU32(2)); // 1 or 2
            st.dy = 1 + static_cast<s32>(this->RandU32(2)); // 1 or 2
            st.length = 80 + static_cast<s32>(this->RandU32(320u)); // 80..400
            const std::size_t ci = static_cast<std::size_t>(this->RandU32(3));
            st.r = StreamPalette[ci].r;
            st.g = StreamPalette[ci].g;
            st.b = StreamPalette[ci].b;
        }
    }

    // ── Render passes (raw pointer overloads) ────────────────────────────────

    void PlasmaWallpaperElement::PassBackground(u32 *buf) const {
        // Base: deep-space (#0A0A14)
        const u32 base = PackRgb(0x0Au, 0x0Au, 0x14u);
        const std::size_t total = static_cast<std::size_t>(WpW * WpH);
        for(std::size_t i = 0; i < total; i++) { buf[i] = base; }
    }

    void PlasmaWallpaperElement::PassBlooms(u32 *buf) const {
        for(std::size_t bi = 0; bi < BloomCount; bi++) {
            const auto &bloom = this->blooms[bi];
            const s32 r = bloom.radius;
            const s32 x0 = (bloom.cx - r) > 0 ? (bloom.cx - r) : 0;
            const s32 x1 = (bloom.cx + r) < WpW ? (bloom.cx + r) : WpW - 1;
            const s32 y0 = (bloom.cy - r) > 0 ? (bloom.cy - r) : 0;
            const s32 y1 = (bloom.cy + r) < WpH ? (bloom.cy + r) : WpH - 1;

            for(s32 y = y0; y <= y1; y++) {
                for(s32 x = x0; x <= x1; x++) {
                    const s32 dx = x - bloom.cx;
                    const s32 dy = y - bloom.cy;
                    const u64 dist_sq = static_cast<u64>(dx * dx + dy * dy);
                    const u64 r_sq    = static_cast<u64>(r) * static_cast<u64>(r);
                    if(dist_sq >= r_sq) { continue; }

                    const s32 dist = static_cast<s32>(Isqrt64(dist_sq));
                    u32 alpha256;
                    if(dist < r / 3) {
                        alpha256 = 180u;
                    } else {
                        const u32 fade  = static_cast<u32>(r - dist);
                        const u32 range = static_cast<u32>(r - r / 3);
                        alpha256 = (fade * 120u / (range > 0u ? range : 1u));
                        if(alpha256 > 180u) { alpha256 = 180u; }
                    }
                    const std::size_t idx = static_cast<std::size_t>(y * WpW + x);
                    buf[idx] = BlendAdd(buf[idx], bloom.r, bloom.g, bloom.b, alpha256);
                }
            }
        }
    }

    void PlasmaWallpaperElement::PassStreams(u32 *buf) const {
        for(std::size_t i = 0; i < StreamCount; i++) {
            const auto &st = this->streams[i];
            s32 x = st.ox, y = st.oy;
            const s32 len = st.length;
            for(s32 step = 0; step < len; step++) {
                if(x < 0 || x >= WpW || y < 0 || y >= WpH) { break; }

                const u32 t256 = static_cast<u32>(step * 256 / (len > 0 ? len : 1));
                u32 a;
                if(t256 < 128u) {
                    a = t256 * 2u;
                } else {
                    a = (255u - t256) * 2u + 2u;
                }
                a = a * 60u / 256u;
                if(a < 8u) { a = 8u; }

                const std::size_t idx = static_cast<std::size_t>(y * WpW + x);
                buf[idx] = BlendAdd(buf[idx], st.r, st.g, st.b, a);

                if(y + 1 < WpH) {
                    const std::size_t idx2 = static_cast<std::size_t>((y + 1) * WpW + x);
                    buf[idx2] = BlendAdd(buf[idx2], st.r, st.g, st.b, a / 3u);
                }

                x += st.dx;
                y += st.dy;
            }
        }
    }

    void PlasmaWallpaperElement::PassStars(u32 *buf) const {
        for(std::size_t i = 0; i < StarCount; i++) {
            const auto &s = this->stars[i];
            if(s.x < 0 || s.x >= WpW || s.y < 0 || s.y >= WpH) { continue; }
            const std::size_t idx = static_cast<std::size_t>(s.y * WpW + s.x);
            buf[idx] = BlendOver(buf[idx], s.r, s.g, s.b, s.alpha256);
        }
    }

    void PlasmaWallpaperElement::PassGrid(u32 *buf) const {
        // Horizontal lines
        for(s32 row = 0; row < WpH; row += GridCell) {
            for(s32 x = 0; x < WpW; x++) {
                const std::size_t idx = static_cast<std::size_t>(row * WpW + x);
                buf[idx] = BlendOver(buf[idx], GridR, GridG, GridB, GridAlpha);
            }
        }
        // Vertical lines
        for(s32 col = 0; col < WpW; col += GridCell) {
            for(s32 y = 0; y < WpH; y++) {
                const std::size_t idx = static_cast<std::size_t>(y * WpW + col);
                buf[idx] = BlendOver(buf[idx], GridR, GridG, GridB, GridAlpha);
            }
        }
    }

    void PlasmaWallpaperElement::PassVignette(u32 *buf) const {
        const s64 cx = WpW / 2;
        const s64 cy = WpH / 2;
        const u64 max_dist_sq = static_cast<u64>(cx * cx + cy * cy);

        for(s32 y = 0; y < WpH; y++) {
            for(s32 x = 0; x < WpW; x++) {
                const s64 dx = x - cx;
                const s64 dy = y - cy;
                const u64 dist_sq = static_cast<u64>(dx * dx + dy * dy);

                u32 strength256;
                if(dist_sq >= max_dist_sq) {
                    strength256 = 154u;
                } else {
                    strength256 = static_cast<u32>(dist_sq * 154u / max_dist_sq);
                }
                const u32 scale = 256u - strength256;
                const std::size_t idx = static_cast<std::size_t>(y * WpW + x);
                u32 r, g, b;
                UnpackRgb(buf[idx], r, g, b);
                buf[idx] = PackRgb(r * scale / 256u, g * scale / 256u, b * scale / 256u);
            }
        }
    }

    // ── OnRender ─────────────────────────────────────────────────────────────

#if PLASMA_WALLPAPER_ENABLED

    void PlasmaWallpaperElement::OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 /*x*/, const s32 /*y*/) {

        // ── Fallback path: no texture or no buffer ────────────────────────────
        if(plasma_tex == nullptr || pixel_buf == nullptr) {
            // One solid-colour fill — safe and cheap.
            drawer->RenderRectangleFill(
                pu::ui::Color(FallbackR, FallbackG, FallbackB, 0xFF),
                this->elem_x, this->elem_y,
                static_cast<s32>(this->elem_w), static_cast<s32>(this->elem_h));

            this->frame_count++;
            return;
        }

        // ── Normal path: update pixel_buf, push to texture, blit once ─────────

        u32 *buf = pixel_buf.get();

        this->PassBackground(buf);
        this->PassBlooms(buf);
        this->PassStreams(buf);
        this->PassStars(buf);
        this->PassGrid(buf);
        this->PassVignette(buf);

        // Push pixel_buf into the streaming SDL_Texture.
        // Pitch = WpW * sizeof(u32) bytes per row.
        // SDL_PIXELFORMAT_ARGB8888 matches our PackRgb layout (0x00RRGGBB).
        // The alpha channel byte is always 0 from PackRgb — SDL treats the texture
        // as fully-opaque because SDL_BLENDMODE_NONE is the default for new textures.
        const int pitch = WpW * static_cast<int>(sizeof(u32));
        SDL_UpdateTexture(plasma_tex, nullptr, buf, pitch);

        // Single blit via Plutonium's RenderTexture.
        // sdl2::Texture is a typedef for SDL_Texture*, so no cast needed.
        drawer->RenderTexture(plasma_tex, this->elem_x, this->elem_y);

        this->frame_count++;

        // Telemetry every 60 frames: EVENT UX_WALLPAPER_FRAME
        if((this->frame_count % 60u) == 0u) {
            fprintf(stderr,
                "EVENT UX_WALLPAPER_FRAME fps=60 blooms=%zu streams=%zu stars=%zu frame=%llu tex=%p\n",
                BloomCount, StreamCount, StarCount,
                static_cast<unsigned long long>(this->frame_count),
                static_cast<void*>(plasma_tex));
        }
    }

#else // PLASMA_WALLPAPER_ENABLED == 0

    void PlasmaWallpaperElement::OnRender(pu::ui::render::Renderer::Ref &/*drawer*/, const s32 /*x*/, const s32 /*y*/) {
        // Disabled at compile time — nothing to draw.
    }

#endif // PLASMA_WALLPAPER_ENABLED

    // ── OnInput ───────────────────────────────────────────────────────────────

    void PlasmaWallpaperElement::OnInput(const u64 /*keys_down*/, const u64 /*keys_up*/, const u64 /*keys_held*/, const pu::ui::TouchPoint /*touch_pos*/) {
        // QOS-PATCH-009: Wallpaper element is purely decorative — no input consumed.
    }

}
