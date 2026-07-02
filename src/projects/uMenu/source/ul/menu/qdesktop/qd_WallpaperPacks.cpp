// qd_WallpaperPacks.cpp — Procedural wallpapers for packs 1..9.
//
// Pack 0 (Glass / Cold Plasma Cascade) lives in qd_Wallpaper.cpp.
// Each function here paints 1280×720 RGBA8888 into the caller-provided buffer
// (caller is SDL_LockTexture on a streaming texture inside QdWallpaperElement).
// All draws are seeded with WP_SEED for determinism across reboots.
//
// Design principles:
//   - Each pack is visually distinct from every other pack at thumbnail size.
//   - Each pack matches its QdTheme palette (qd_Theme.hpp same-index).
//   - Render budget: complete in <100 ms on Switch Erista (no per-frame work —
//     wallpaper renders once per pack-change then blits).
//   - Pure SDL pixel fills — no SDL2_image, no fonts, no textures loaded.

#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/ul_Result.hpp>  // UL_LOG_INFO
#include <cstdint>

namespace ul::menu::qdesktop {

// ── File-local helpers (duplicated from QdWallpaperElement for use here) ──

namespace {

// Xorshift32 PRNG — identical to QdWallpaperElement::Xorshift.
inline u32 Xorshift(u32 &state) {
    u32 x = state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    state = x;
    return x;
}

// Additive blend src over dst pixel (single channel, clamp 255).
inline void BlendAddCh(u8 &dst, u8 src, u32 alpha256) {
    const u32 v = static_cast<u32>(dst) + (static_cast<u32>(src) * alpha256 / 256U);
    dst = static_cast<u8>(v < 255U ? v : 255U);
}

// Alpha-over blend src over dst pixel (single channel).
inline void BlendOverCh(u8 &dst, u8 src, u32 alpha256) {
    const u32 inv = 256U - alpha256;
    const u32 v = (static_cast<u32>(src) * alpha256
                 + static_cast<u32>(dst) * inv) / 256U;
    dst = static_cast<u8>(v);
}

// Integer square root.
u32 Isqrt64(u64 n) {
    if (n == 0ULL) return 0U;
    u64 lo = 1ULL;
    u64 hi = (n < 0x000F'FFFF'FFFFULL) ? n : 0x000F'FFFF'FFFFULL;
    while (lo < hi) {
        const u64 mid = (lo + hi + 1ULL) / 2ULL;
        if (mid * mid <= n) lo = mid;
        else                hi = mid - 1ULL;
    }
    return static_cast<u32>(lo);
}

// Pixel accessor.
inline u8 *Px(u8 *buf, size_t pitch, u32 x, u32 y) {
    return buf + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4u;
}

// Fill the buffer with a solid colour.
void FillSolid(u8 *buf, int pitch_bytes, u8 r, u8 g, u8 b) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    for (u32 y = 0; y < H; ++y) {
        u8 *row = buf + static_cast<size_t>(y) * pitch;
        for (u32 x = 0; x < W; ++x) {
            row[x * 4u + 0] = r;
            row[x * 4u + 1] = g;
            row[x * 4u + 2] = b;
            row[x * 4u + 3] = 0xFF;
        }
    }
}

// Apply a radial vignette darkening (same algorithm as Pack 0 vignette pass).
void ApplyVignette(u8 *buf, int pitch_bytes, u32 strength_max256 = 154U) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    const u64 cx_v = W / 2u;
    const u64 cy_v = H / 2u;
    const u64 max_dist_sq = cx_v * cx_v + cy_v * cy_v;

    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            const u64 dx = (x >= cx_v) ? (x - cx_v) : (cx_v - x);
            const u64 dy = (y >= cy_v) ? (y - cy_v) : (cy_v - y);
            const u64 dist_sq = dx * dx + dy * dy;

            u32 strength_256;
            if (dist_sq >= max_dist_sq) {
                strength_256 = strength_max256;
            } else {
                strength_256 = static_cast<u32>(dist_sq * strength_max256 / max_dist_sq);
            }
            const u32 scale = 256U - strength_256;
            u8 *p = Px(buf, pitch, x, y);
            p[0] = static_cast<u8>(static_cast<u32>(p[0]) * scale / 256U);
            p[1] = static_cast<u8>(static_cast<u32>(p[1]) * scale / 256U);
            p[2] = static_cast<u8>(static_cast<u32>(p[2]) * scale / 256U);
        }
    }
}

// Draw a radial blob (additive blend) centred at (cx,cy) with radius r.
// alpha_peak is the centre intensity (0..255).
void DrawRadialBlob(u8 *buf, int pitch_bytes, u32 cx, u32 cy, u32 radius,
                    u8 r, u8 g, u8 b, u32 alpha_peak) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    const s32 R = static_cast<s32>(radius);
    const s32 x0 = (static_cast<s32>(cx) - R > 0) ? static_cast<s32>(cx) - R : 0;
    const s32 x1 = (static_cast<s32>(cx) + R < static_cast<s32>(W)) ? static_cast<s32>(cx) + R : static_cast<s32>(W) - 1;
    const s32 y0 = (static_cast<s32>(cy) - R > 0) ? static_cast<s32>(cy) - R : 0;
    const s32 y1 = (static_cast<s32>(cy) + R < static_cast<s32>(H)) ? static_cast<s32>(cy) + R : static_cast<s32>(H) - 1;

    for (s32 y = y0; y <= y1; ++y) {
        for (s32 x = x0; x <= x1; ++x) {
            const s32 dx = x - static_cast<s32>(cx);
            const s32 dy = y - static_cast<s32>(cy);
            const u64 dist_sq = static_cast<u64>(dx * dx + dy * dy);
            const u64 r_sq = static_cast<u64>(R) * static_cast<u64>(R);
            if (dist_sq >= r_sq) continue;
            const u32 dist = Isqrt64(dist_sq);
            const u32 fade = static_cast<u32>(R) - dist;
            u32 alpha256 = (fade * alpha_peak) / static_cast<u32>(R);
            if (alpha256 > alpha_peak) alpha256 = alpha_peak;
            u8 *p = Px(buf, pitch, static_cast<u32>(x), static_cast<u32>(y));
            BlendAddCh(p[0], r, alpha256);
            BlendAddCh(p[1], g, alpha256);
            BlendAddCh(p[2], b, alpha256);
        }
    }
}

// Draw a 1-pixel-wide outline rectangle (replaces, no blend).
void DrawRectOutline(u8 *buf, int pitch_bytes, u32 x0, u32 y0, u32 w, u32 h,
                     u8 r, u8 g, u8 b, u32 alpha256 = 256u) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    if (x0 >= W || y0 >= H || w == 0 || h == 0) return;
    const u32 x1 = (x0 + w - 1 < W) ? x0 + w - 1 : W - 1;
    const u32 y1 = (y0 + h - 1 < H) ? y0 + h - 1 : H - 1;

    auto plot = [&](u32 x, u32 y) {
        u8 *p = Px(buf, pitch, x, y);
        BlendOverCh(p[0], r, alpha256);
        BlendOverCh(p[1], g, alpha256);
        BlendOverCh(p[2], b, alpha256);
    };
    // Top + bottom rows
    for (u32 x = x0; x <= x1; ++x) {
        plot(x, y0);
        plot(x, y1);
    }
    // Left + right columns
    for (u32 y = y0; y <= y1; ++y) {
        plot(x0, y);
        plot(x1, y);
    }
}

// Fill a rectangle with alpha blending.
void FillRectBlend(u8 *buf, int pitch_bytes, u32 x0, u32 y0, u32 w, u32 h,
                   u8 r, u8 g, u8 b, u32 alpha256) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    if (x0 >= W || y0 >= H || w == 0 || h == 0) return;
    const u32 x1 = (x0 + w - 1 < W) ? x0 + w - 1 : W - 1;
    const u32 y1 = (y0 + h - 1 < H) ? y0 + h - 1 : H - 1;

    for (u32 y = y0; y <= y1; ++y) {
        for (u32 x = x0; x <= x1; ++x) {
            u8 *p = Px(buf, pitch, x, y);
            BlendOverCh(p[0], r, alpha256);
            BlendOverCh(p[1], g, alpha256);
            BlendOverCh(p[2], b, alpha256);
        }
    }
}

// Draw a hollow circle by sampling angles.
void DrawCircleOutline(u8 *buf, int pitch_bytes, s32 cx, s32 cy, s32 radius,
                       u8 r, u8 g, u8 b, u32 alpha256) {
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    // Midpoint circle algorithm.
    s32 x = radius;
    s32 y = 0;
    s32 err = 0;
    auto plot = [&](s32 px, s32 py) {
        if (px < 0 || py < 0 || static_cast<u32>(px) >= W || static_cast<u32>(py) >= H) return;
        u8 *p = Px(buf, pitch, static_cast<u32>(px), static_cast<u32>(py));
        BlendOverCh(p[0], r, alpha256);
        BlendOverCh(p[1], g, alpha256);
        BlendOverCh(p[2], b, alpha256);
    };
    while (x >= y) {
        plot(cx + x, cy + y); plot(cx + y, cy + x);
        plot(cx - y, cy + x); plot(cx - x, cy + y);
        plot(cx - x, cy - y); plot(cx - y, cy - x);
        plot(cx + y, cy - x); plot(cx + x, cy - y);
        if (err <= 0) { y += 1; err += 2 * y + 1; }
        if (err > 0)  { x -= 1; err -= 2 * x + 1; }
    }
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// Pack 1 — Neon
// Black base + 4 horizontal neon bands (cyan, magenta, lime, yellow) +
// central magenta radial glow + 40 bright neon dots + faint scanlines.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack1_Neon(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack1 Neon");
    FillSolid(buf, pitch_bytes, 0x05, 0x00, 0x10);

    // 4 horizontal neon bands, alpha-blended at low intensity (background glow).
    struct Band { u32 y; u8 r, g, b; };
    constexpr Band kBands[4] = {
        { 120, 0x2A, 0xFF, 0xE5 },  // cyan
        { 290, 0xFF, 0x2A, 0xD0 },  // magenta
        { 460, 0x6A, 0xFF, 0x50 },  // lime
        { 600, 0xF5, 0xE0, 0x2A },  // yellow
    };
    for (const auto &b : kBands) {
        for (s32 dy = -30; dy <= 30; ++dy) {
            const s32 y = static_cast<s32>(b.y) + dy;
            if (y < 0 || y >= static_cast<s32>(g_wp_render_h)) continue;
            const u32 alpha = static_cast<u32>(60 - (dy < 0 ? -dy : dy) * 2);
            FillRectBlend(buf, pitch_bytes, 0, static_cast<u32>(y), g_wp_render_w, 1,
                          b.r, b.g, b.b, alpha);
        }
    }

    // Central magenta glow.
    DrawRadialBlob(buf, pitch_bytes, g_wp_render_w / 2, g_wp_render_h / 2, 280,
                   0xFF, 0x2A, 0xD0, 90);

    // 40 neon dots.
    u32 rng = WP_SEED;
    constexpr u8 DOT_R[4] = { 0xFF, 0x6A, 0x2A, 0xFF };
    constexpr u8 DOT_G[4] = { 0x2A, 0xFF, 0xFF, 0xCC };
    constexpr u8 DOT_B[4] = { 0xD0, 0x50, 0xE5, 0x00 };
    for (u32 i = 0; i < 40; ++i) {
        const u32 x = Xorshift(rng) % g_wp_render_w;
        const u32 y = Xorshift(rng) % g_wp_render_h;
        const u32 c = Xorshift(rng) % 4;
        u8 *p = Px(buf, static_cast<size_t>(pitch_bytes), x, y);
        BlendOverCh(p[0], DOT_R[c], 255);
        BlendOverCh(p[1], DOT_G[c], 255);
        BlendOverCh(p[2], DOT_B[c], 255);
    }

    // Faint horizontal scanlines.
    for (u32 y = 0; y < g_wp_render_h; y += 3) {
        FillRectBlend(buf, pitch_bytes, 0, y, g_wp_render_w, 1, 0xFF, 0xFF, 0xFF, 8);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 2 — Minimal
// Solid dark gray + single thin diagonal accent line + corner dot.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack2_Minimal(const QdTheme &theme, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack2 Minimal");
    FillSolid(buf, pitch_bytes, 0x18, 0x18, 0x1C);

    // Diagonal accent line from BL to TR (1 px wide).
    const size_t pitch = static_cast<size_t>(pitch_bytes);
    const u8 ar = theme.accent.r;
    const u8 ag = theme.accent.g;
    const u8 ab = theme.accent.b;
    const u32 W2 = g_wp_render_w;
    const u32 H2 = g_wp_render_h;
    for (u32 i = 0; i < W2; ++i) {
        const u32 x = i;
        const u32 y = H2 - 1 - (i * (H2 - 1) / W2);
        if (y >= H2) continue;
        u8 *p = Px(buf, pitch, x, y);
        BlendOverCh(p[0], ar, 120);
        BlendOverCh(p[1], ag, 120);
        BlendOverCh(p[2], ab, 120);
    }

    // TR corner dot (24x24 accent fill).
    FillRectBlend(buf, pitch_bytes, W2 - 60, 36, 24, 24, ar, ag, ab, 230);

    // Subtle 4-row band at bottom.
    FillRectBlend(buf, pitch_bytes, 0, H2 - 8, W2, 4, ar, ag, ab, 40);
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 3 — Retro
// Deep navy + CRT scanlines + amber top band + green pixel grid bottom.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack3_Retro(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack3 Retro");
    FillSolid(buf, pitch_bytes, 0x0A, 0x14, 0x20);

    // Amber band at top (40px tall).
    {
    const u32 W3 = g_wp_render_w;
    const u32 H3 = g_wp_render_h;
    FillRectBlend(buf, pitch_bytes, 0, 0, W3, 40, 0xFF, 0xA8, 0x3A, 60);
    FillRectBlend(buf, pitch_bytes, 0, 38, W3, 2, 0xFF, 0xA8, 0x3A, 200);

    // CRT scanlines — every other row at low alpha.
    for (u32 y = 0; y < H3; y += 2) {
        FillRectBlend(buf, pitch_bytes, 0, y, W3, 1, 0x00, 0x00, 0x00, 50);
    }

    // Pixel grid at bottom 1/4 — green dots in 16px lattice.
    for (u32 y = H3 * 3 / 4; y < H3; y += 16) {
        for (u32 x = 0; x < W3; x += 16) {
            FillRectBlend(buf, pitch_bytes, x, y, 2, 2, 0x6A, 0xFF, 0x82, 180);
        }
    }

    // Slight cyan ambience top center.
    DrawRadialBlob(buf, pitch_bytes, W3 / 2, 100, 200, 0x40, 0x80, 0xC0, 50);
    }

    ApplyVignette(buf, pitch_bytes, 100);
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 4 — Cards
// Slate base + 6 overlapping translucent card rectangles offset by 20px each.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack4_Cards(const QdTheme &theme, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack4 Cards");
    FillSolid(buf, pitch_bytes, 0x14, 0x16, 0x1E);

    // 6 overlapping cards, centred, each offset and slightly smaller.
    constexpr u32 BASE_W = 540;
    constexpr u32 BASE_H = 360;
    const u32 cx = g_wp_render_w / 2;
    const u32 cy = g_wp_render_h / 2;
    const u8 sr = theme.surface_glass.r;
    const u8 sg = theme.surface_glass.g;
    const u8 sb = theme.surface_glass.b;
    const u8 ar = theme.accent.r;
    const u8 ag = theme.accent.g;
    const u8 ab = theme.accent.b;
    const u8 fr = theme.focus_ring.r;
    const u8 fg = theme.focus_ring.g;
    const u8 fb = theme.focus_ring.b;

    for (s32 i = 5; i >= 0; --i) {
        const u32 cw = BASE_W - static_cast<u32>(i) * 40;
        const u32 ch = BASE_H - static_cast<u32>(i) * 30;
        const u32 x0 = cx - cw / 2 + static_cast<u32>(i) * 16;
        const u32 y0 = cy - ch / 2 + static_cast<u32>(i) * 16;
        // Magenta drop-shadow (offset +4 +4)
        FillRectBlend(buf, pitch_bytes, x0 + 6, y0 + 6, cw, ch, fr, fg, fb, 60);
        // Card body
        FillRectBlend(buf, pitch_bytes, x0, y0, cw, ch, sr + 8u > 255u ? 255u : sr + 8u,
                      sg + 8u > 255u ? 255u : sg + 8u, sb + 12u > 255u ? 255u : sb + 12u, 220);
        // Card border
        DrawRectOutline(buf, pitch_bytes, x0, y0, cw, ch, ar, ag, ab, 160);
    }

    ApplyVignette(buf, pitch_bytes, 80);
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 5 — Pastel
// Soft slate base + 4 large soft blurred circles (pink, peach, mint, lavender).
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack5_Pastel(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack5 Pastel");
    FillSolid(buf, pitch_bytes, 0x1E, 0x1E, 0x28);

    // 4 large soft circles. Powder pink, peach, mint, lavender.
    struct Blob { u32 cx, cy, r; u8 cr, cg, cb; };
    constexpr Blob kBlobs[4] = {
        { 280,  220, 360, 0xFB, 0xC6, 0xE4 },  // powder pink TL
        { 980,  180, 320, 0xFA, 0xE3, 0xA4 },  // peach TR
        { 320,  560, 340, 0xB0, 0xE8, 0xC0 },  // mint BL
        { 980,  580, 380, 0xC9, 0xBB, 0xF0 },  // lavender BR
    };
    for (const auto &b : kBlobs) {
        DrawRadialBlob(buf, pitch_bytes, b.cx, b.cy, b.r, b.cr, b.cg, b.cb, 100);
    }

    // No vignette — pastel should feel open.
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 6 — Dark
// Pure black + 120 single-pixel stars + radial vignette.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack6_Dark(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack6 Dark");
    FillSolid(buf, pitch_bytes, 0x00, 0x00, 0x00);

    u32 rng = WP_SEED;
    for (u32 i = 0; i < 120; ++i) {
        const u32 x = Xorshift(rng) % g_wp_render_w;
        const u32 y = Xorshift(rng) % g_wp_render_h;
        const u32 alpha = 60u + (Xorshift(rng) % 196u);
        u8 *p = Px(buf, static_cast<size_t>(pitch_bytes), x, y);
        BlendOverCh(p[0], 0xE8, alpha);
        BlendOverCh(p[1], 0xE8, alpha);
        BlendOverCh(p[2], 0xEC, alpha);
    }

    // Subtle cyan single thread of accent at the top.
    DrawRadialBlob(buf, pitch_bytes, g_wp_render_w / 2, 60, 240, 0x5A, 0xC8, 0xF0, 40);

    ApplyVignette(buf, pitch_bytes, 80);
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 7 — Gradient
// Vertical 3-stop gradient: indigo (top) → violet (mid) → cyan (bottom).
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack7_Gradient(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack7 Gradient");
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);

    // Three stops: deep indigo (10,05,22) → violet (A0,70,FF) → cyan-aqua (7A,E0,FF).
    const u32 MID = H / 2;  // W11: runtime h instead of compile-time WP_H
    for (u32 y = 0; y < H; ++y) {
        u8 r, g, b;
        if (y < MID) {
            // Top half: indigo → violet
            const u32 t = y * 256 / MID;
            r = static_cast<u8>((0x10 * (256 - t) + 0xA0 * t) / 256);
            g = static_cast<u8>((0x05 * (256 - t) + 0x70 * t) / 256);
            b = static_cast<u8>((0x22 * (256 - t) + 0xFF * t) / 256);
        } else {
            // Bottom half: violet → cyan
            const u32 t = (y - MID) * 256 / (H - MID);
            r = static_cast<u8>((0xA0 * (256 - t) + 0x7A * t) / 256);
            g = static_cast<u8>((0x70 * (256 - t) + 0xE0 * t) / 256);
            b = static_cast<u8>((0xFF * (256 - t) + 0xFF * t) / 256);
        }
        u8 *row = buf + static_cast<size_t>(y) * pitch;
        for (u32 x = 0; x < W; ++x) {
            row[x * 4u + 0] = r;
            row[x * 4u + 1] = g;
            row[x * 4u + 2] = b;
            row[x * 4u + 3] = 0xFF;
        }
    }

    // Subtle stars scattered for depth.
    u32 rng = WP_SEED;
    for (u32 i = 0; i < 60; ++i) {
        const u32 x = Xorshift(rng) % W;
        const u32 y = Xorshift(rng) % H;
        u8 *p = Px(buf, pitch, x, y);
        BlendOverCh(p[0], 0xFF, 200);
        BlendOverCh(p[1], 0xFF, 200);
        BlendOverCh(p[2], 0xFF, 200);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 8 — Blueprint
// Deep blueprint blue + visible 32x32 grid + 3 large technical drawing shapes.
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack8_Blueprint(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack8 Blueprint");
    FillSolid(buf, pitch_bytes, 0x05, 0x18, 0x32);

    {
    const u32 W8 = g_wp_render_w;
    const u32 H8 = g_wp_render_h;
    // Major grid lines every 64px (brighter).
    for (u32 x = 0; x < W8; x += 64) {
        FillRectBlend(buf, pitch_bytes, x, 0, 1, H8, 0x7A, 0xE0, 0xFF, 70);
    }
    for (u32 y = 0; y < H8; y += 64) {
        FillRectBlend(buf, pitch_bytes, 0, y, W8, 1, 0x7A, 0xE0, 0xFF, 70);
    }
    // Minor grid lines every 16px (faint).
    for (u32 x = 0; x < W8; x += 16) {
        FillRectBlend(buf, pitch_bytes, x, 0, 1, H8, 0x7A, 0xE0, 0xFF, 22);
    }
    for (u32 y = 0; y < H8; y += 16) {
        FillRectBlend(buf, pitch_bytes, 0, y, W8, 1, 0x7A, 0xE0, 0xFF, 22);
    }
    }

    // Big circle outline (architectural compass-mark) at upper left.
    DrawCircleOutline(buf, pitch_bytes, 300, 240, 180, 0xA8, 0xE8, 0xFF, 200);
    DrawCircleOutline(buf, pitch_bytes, 300, 240, 178, 0xA8, 0xE8, 0xFF, 120);
    // Big square outline at right.
    DrawRectOutline(buf, pitch_bytes, 760, 140, 360, 360, 0xA8, 0xE8, 0xFF, 200);
    DrawRectOutline(buf, pitch_bytes, 762, 142, 356, 356, 0xA8, 0xE8, 0xFF, 120);
    // Small triangle (T-square style) at bottom centre — drawn as 2 lines.
    {
        const size_t pitch = static_cast<size_t>(pitch_bytes);
        const u32 ty_base = 600;
        for (u32 i = 0; i < 200; ++i) {
            // Left line from (480, 600) going down-right
            const u32 x = 480 + i;
            const u32 y = ty_base + i / 2;
            if (x < g_wp_render_w && y < g_wp_render_h) {
                u8 *p = Px(buf, pitch, x, y);
                BlendOverCh(p[0], 0xA8, 200);
                BlendOverCh(p[1], 0xE8, 200);
                BlendOverCh(p[2], 0xFF, 200);
            }
            // Right line going down-left
            const u32 x2 = 880 - i;
            const u32 y2 = ty_base + i / 2;
            if (x2 < g_wp_render_w && y2 < g_wp_render_h) {
                u8 *p2 = Px(buf, pitch, x2, y2);
                BlendOverCh(p2[0], 0xA8, 200);
                BlendOverCh(p2[1], 0xE8, 200);
                BlendOverCh(p2[2], 0xFF, 200);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Pack 9 — Pixel
// 32x32 colorful checkerboard with 8-color rotating palette (NES vibe).
// ──────────────────────────────────────────────────────────────────────────────
void RenderWallpaperPack9_Pixel(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    UL_LOG_INFO("qdesktop: Pack9 Pixel");
    constexpr u32 TILE = 32;
    constexpr u8 PR[8] = { 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x80, 0x40, 0xC0 };
    constexpr u8 PG[8] = { 0xCC, 0xE8, 0x00, 0x00, 0xFF, 0x00, 0x80, 0x40 };
    constexpr u8 PB[8] = { 0x00, 0x00, 0x00, 0xE8, 0xFF, 0xFF, 0xC0, 0xFF };

    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);

    for (u32 y = 0; y < H; ++y) {
        u8 *row = buf + static_cast<size_t>(y) * pitch;
        const u32 ty = y / TILE;
        for (u32 x = 0; x < W; ++x) {
            const u32 tx = x / TILE;
            const u32 idx = (tx + ty) % 8;
            // Half intensity for the dark variant of each tile (checker pattern).
            const bool dark = ((tx + ty) & 1u) == 0u;
            const u32 mul = dark ? 64u : 200u;
            row[x * 4u + 0] = static_cast<u8>(static_cast<u32>(PR[idx]) * mul / 256u);
            row[x * 4u + 1] = static_cast<u8>(static_cast<u32>(PG[idx]) * mul / 256u);
            row[x * 4u + 2] = static_cast<u8>(static_cast<u32>(PB[idx]) * mul / 256u);
            row[x * 4u + 3] = 0xFF;
        }
    }
}

} // namespace ul::menu::qdesktop
