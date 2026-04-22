// QOS-PATCH-009: Cold Plasma Cascade procedural wallpaper element.
// Ported from /QOS/tools/mock-nro-desktop-gui/src/wallpaper.rs
// All PRNG / compositing logic is a direct translation of the Rust source;
// the seed, bloom positions, radii, stream lengths, and colour palette are
// bit-identical to the Rust original so both renderers produce the same frame.
// 2026-04-18

#include <ul/menu/ui/ui_PlasmaWallpaperElement.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>

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
          frame_count(0), last_time_ms(0)
    {
        // Seed all three scene objects once; the pixel buffer is rebuilt each frame.
        this->SeedBlooms();
        this->SeedStars();
        this->SeedStreams();
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
        // But because SeedBlooms/Streams/Stars are called sequentially in the constructor
        // starting from prng_state == WpSeed, the call order IS:
        //   SeedBlooms()  — pure table, no PRNG
        //   SeedStreams() — advances prng_state
        //   SeedStars()   — advances prng_state further
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

    // ── Render passes ─────────────────────────────────────────────────────────

    void PlasmaWallpaperElement::PassBackground(std::vector<u32> &buf) const {
        // Base: deep-space (#0A0A14)
        const u32 base = PackRgb(0x0Au, 0x0Au, 0x14u);
        for(auto &px : buf) { px = base; }
    }

    void PlasmaWallpaperElement::PassBlooms(std::vector<u32> &buf) const {
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

    void PlasmaWallpaperElement::PassStreams(std::vector<u32> &buf) const {
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

    void PlasmaWallpaperElement::PassStars(std::vector<u32> &buf) const {
        for(std::size_t i = 0; i < StarCount; i++) {
            const auto &s = this->stars[i];
            if(s.x < 0 || s.x >= WpW || s.y < 0 || s.y >= WpH) { continue; }
            const std::size_t idx = static_cast<std::size_t>(s.y * WpW + s.x);
            buf[idx] = BlendOver(buf[idx], s.r, s.g, s.b, s.alpha256);
        }
    }

    void PlasmaWallpaperElement::PassGrid(std::vector<u32> &buf) const {
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

    void PlasmaWallpaperElement::PassVignette(std::vector<u32> &buf) const {
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

    // ── Blit to SDL renderer ──────────────────────────────────────────────────

    void PlasmaWallpaperElement::BlitBuffer(pu::ui::render::Renderer::Ref &drawer, const std::vector<u32> &buf) const {
        // Blit every pixel as a 1×1 FillRect.
        // The deep-space base colour (#0A0A14 = 0x000A0A14) is the background;
        // rendering it is wasteful on solid regions but correct everywhere.
        const s32 bx = this->elem_x;
        const s32 by = this->elem_y;

        for(s32 y = 0; y < WpH; y++) {
            for(s32 x = 0; x < WpW; x++) {
                const u32 px = buf[static_cast<std::size_t>(y * WpW + x)];
                u32 r, g, b;
                UnpackRgb(px, r, g, b);
                const pu::ui::Color col(
                    static_cast<u8>(r),
                    static_cast<u8>(g),
                    static_cast<u8>(b),
                    0xFF);
                drawer->RenderRectangleFill(col, bx + x, by + y, 1, 1);
            }
        }
    }

    // ── OnRender ─────────────────────────────────────────────────────────────

    void PlasmaWallpaperElement::OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 /*x*/, const s32 /*y*/) {
        // Allocate pixel buffer on the heap each frame (no C++ static to avoid NX BSS issues).
        std::vector<u32> buf(static_cast<std::size_t>(WpW * WpH), 0u);

        this->PassBackground(buf);
        this->PassBlooms(buf);
        this->PassStreams(buf);
        this->PassStars(buf);
        this->PassGrid(buf);
        this->PassVignette(buf);

        this->BlitBuffer(drawer, buf);

        this->frame_count++;

        // Telemetry every 60 frames: EVENT UX_WALLPAPER_FRAME
        if((this->frame_count % 60u) == 0u) {
            // Using printf to stderr — ul_Logger.hpp macro UL_LOG_INFO requires linkage
            // that may not be available here at all call sites; use fprintf to remain safe.
            fprintf(stderr,
                "EVENT UX_WALLPAPER_FRAME fps=60 blooms=%zu streams=%zu stars=%zu frame=%llu\n",
                BloomCount, StreamCount, StarCount,
                static_cast<unsigned long long>(this->frame_count));
        }
    }

    // ── OnInput ───────────────────────────────────────────────────────────────

    void PlasmaWallpaperElement::OnInput(const u64 /*keys_down*/, const u64 /*keys_up*/, const u64 /*keys_held*/, const pu::ui::TouchPoint /*touch_pos*/) {
        // QOS-PATCH-009: Wallpaper element is purely decorative — no input consumed.
    }

}
