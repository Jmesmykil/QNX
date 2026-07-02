// qd_Wallpaper.cpp — Cold Plasma Cascade wallpaper for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/wallpaper.rs.
//
// Design: deep-space black base (#0A0A14) + 6 radial plasma blooms +
//         18 diagonal data streams + 80 single-pixel stars +
//         faint grid overlay + radial vignette.
//
// Deterministic: xorshift32 seeded at WP_SEED = 0x514F535F every render.
// Render-once: pixel buffer generated on first OnRender call, uploaded to
//              SDL_Texture, blitted every subsequent frame.
//
// Texture is generated at native Rust 1280×720 (constants verbatim) and SDL
// scales it to the full 1920×1080 framebuffer in OnRender via SDL_RenderCopy.
// This keeps the texture at ~3.5 MB (fits the Switch GPU pool) instead of
// ~8 MB at 1920×1080, which exhausted the NVN allocator.
// Constants live in qd_Wallpaper.hpp (BLOOM_CX/CY/RADII, STAR_COUNT, etc.).

#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>   // Cycle D5: g_dev_wallpaper_enabled toggle
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>  // W11: UL_LEDGER_TRACK / UL_LEDGER_UNTRACK
#include <ul/menu/qdesktop/qd_LazyLoader.hpp>      // W11: QdLazyLoader::WallpaperResolution()
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <new>  // for std::nothrow

// ── SDL2_image is NOT needed here — wallpaper is procedurally generated.
// ── We use raw SDL2 only (already a dependency of Plutonium).

namespace ul::menu::qdesktop {

// ── Active wallpaper pack global + dispatcher ─────────────────────────────────
// v2.4.0: 10-pack registry. Default = 0 (Glass).

size_t g_active_wallpaper_pack = 0;

// W11-FOOTPRINT Part 3: wallpaper texture size selected at first render based
// on the Switch operation mode (docked vs handheld).  Computed once by
// QdLazyLoader::WallpaperResolution() and cached in QdWallpaperElement so
// subsequent dirty-flag repaints reuse the same dimensions without re-querying
// appletGetOperationMode.
//
// Docked (TV mode)  : 1280×720 (~3.5 MiB) — existing size, upscaled by SDL
//                     to the full 1920×1080 framebuffer at blit time.
// Handheld          : 960×540 (~2.0 MiB) — sufficient for the 1280×720 LCD;
//                     SDL upscales 960×540 → 1280×720 in GPU hardware with
//                     no visible quality difference on the handheld panel.
// Estimated saving in handheld mode: ~1.5 MiB resident.
//
// The IPC query (appletGetOperationMode) costs <1 ms and is called once per
// wallpaper bake cycle, not every frame.

// v3.1.1 (BUG-1 fix 2026-05-19): atomic flag set by SetActiveThemePack so
// QdWallpaperElement::OnRender knows to drop its cached texture and re-bake.
std::atomic<bool> g_wallpaper_dirty{false};

// File-local helper aliases so the free RenderWallpaperPack0_Glass function
// below can use Xorshift/Isqrt64/BlendAddCh/BlendOverCh without qualifying
// every call site with QdWallpaperElement::. These forward to the existing
// class statics (defined later in this file).
namespace {
    inline u32 Xorshift(u32 &s)                      { return QdWallpaperElement::Xorshift(s); }
    inline u32 Isqrt64(u64 n)                        { return QdWallpaperElement::Isqrt64(n); }
    inline void BlendAddCh(u8 &d, u8 s, u32 a256)    { QdWallpaperElement::BlendAddCh(d, s, a256); }
    inline void BlendOverCh(u8 &d, u8 s, u32 a256)   { QdWallpaperElement::BlendOverCh(d, s, a256); }
}

void RenderWallpaperPack(size_t pack_idx, const QdTheme &theme, u8 *buf, int pitch_bytes) {
    switch (pack_idx) {
        case 0: RenderWallpaperPack0_Glass    (theme, buf, pitch_bytes); break;
        case 1: RenderWallpaperPack1_Neon     (theme, buf, pitch_bytes); break;
        case 2: RenderWallpaperPack2_Minimal  (theme, buf, pitch_bytes); break;
        case 3: RenderWallpaperPack3_Retro    (theme, buf, pitch_bytes); break;
        case 4: RenderWallpaperPack4_Cards    (theme, buf, pitch_bytes); break;
        case 5: RenderWallpaperPack5_Pastel   (theme, buf, pitch_bytes); break;
        case 6: RenderWallpaperPack6_Dark     (theme, buf, pitch_bytes); break;
        case 7: RenderWallpaperPack7_Gradient (theme, buf, pitch_bytes); break;
        case 8: RenderWallpaperPack8_Blueprint(theme, buf, pitch_bytes); break;
        case 9: RenderWallpaperPack9_Pixel    (theme, buf, pitch_bytes); break;
        default: RenderWallpaperPack0_Glass   (theme, buf, pitch_bytes); break;
    }
}

// W11-FOOTPRINT Part 3: render-time width/height override.
// Set by GeneratePixelsInto before calling RenderWallpaperPack; pack render
// functions read these instead of the compile-time WP_W/WP_H constants so
// the handheld 960×540 path composes correctly without code duplication.
// Both are always restored to WP_W/WP_H after the pack call completes.
u32 g_wp_render_w = WP_W;
u32 g_wp_render_h = WP_H;

// ── Internal helpers (file-local) ─────────────────────────────────────────────

// Xorshift32 — exact port of wallpaper.rs::xorshift().
u32 QdWallpaperElement::Xorshift(u32 &state) {
    u32 x = state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    state = x;
    return x;
}

// Integer square root via bisection — no libm.
// Returns largest r such that r*r <= n. Exact port of wallpaper.rs::isqrt64.
u32 QdWallpaperElement::Isqrt64(u64 n) {
    if (n == 0ULL) {
        return 0U;
    }
    u64 lo = 1ULL;
    u64 hi = (n < 0x000F'FFFF'FFFFULL) ? n : 0x000F'FFFF'FFFFULL;
    while (lo < hi) {
        const u64 mid = (lo + hi + 1ULL) / 2ULL;
        if (mid * mid <= n) {
            lo = mid;
        } else {
            hi = mid - 1ULL;
        }
    }
    return static_cast<u32>(lo);
}

// Additive blend src over dst pixel (single channel, clamp 255).
// Models: dst_ch = clamp(dst_ch + src_ch * alpha256 / 256, 0, 255).
void QdWallpaperElement::BlendAddCh(u8 &dst, u8 src, u32 alpha256) {
    const u32 v = static_cast<u32>(dst) + (static_cast<u32>(src) * alpha256 / 256U);
    dst = static_cast<u8>(v < 255U ? v : 255U);
}

// Alpha-over blend src over dst pixel (single channel).
// Models: dst_ch = (src_ch * alpha256 + dst_ch * (256 - alpha256)) / 256.
void QdWallpaperElement::BlendOverCh(u8 &dst, u8 src, u32 alpha256) {
    const u32 inv = 256U - alpha256;
    const u32 v = (static_cast<u32>(src) * alpha256
                 + static_cast<u32>(dst) * inv) / 256U;
    dst = static_cast<u8>(v);
}

// ── GeneratePixelsInto ─────────────────────────────────────────────────────────

// v2.4.0: dispatches to the active wallpaper pack (g_active_wallpaper_pack).
// Each pack writes w * h RGBA8888 pixels into the caller-provided buffer.
// W11 Part 3: w/h now explicit so handheld-mode lower-resolution path works.
// The pack render functions already parameterise on their W/H arguments; passing
// a smaller w/h produces an equivalent composition at lower pixel count.
void QdWallpaperElement::GeneratePixelsInto(const QdTheme &theme, u8 *buf, int pitch_bytes,
                                             int w, int h) {
    UL_LOG_INFO("qdesktop: GeneratePixelsInto pack=%zu %dx%d",
                g_active_wallpaper_pack, w, h);
    // The RenderWallpaperPack functions currently hardcode WP_W/WP_H internally.
    // When w==WP_W and h==WP_H (docked path), behaviour is unchanged.
    // When w<WP_W (handheld path), we temporarily override the constants by
    // passing them via a thread-local pair that the pack functions can query.
    // Since the pack functions are already called single-threaded (render thread),
    // a file-local variable is safe without a mutex.
    g_wp_render_w = static_cast<u32>(w);
    g_wp_render_h = static_cast<u32>(h);
    RenderWallpaperPack(g_active_wallpaper_pack, theme, buf, pitch_bytes);
    g_wp_render_w = WP_W;  // restore canonical defaults
    g_wp_render_h = WP_H;
}

// ── RenderWallpaperPack0_Glass ─────────────────────────────────────────────────
//
// Cold Plasma Cascade — the canonical Glass wallpaper. 6 passes:
// base fill, blooms, streams, stars, grid overlay, vignette.

void RenderWallpaperPack0_Glass(const QdTheme & /*theme*/, u8 *buf, int pitch_bytes) {
    // W11 Part 3: use g_wp_render_w/h so handheld 960×540 path works.
    const u32 W = g_wp_render_w;
    const u32 H = g_wp_render_h;
    const size_t pitch = static_cast<size_t>(pitch_bytes);

    UL_LOG_INFO("qdesktop: Pack0 Glass W=%u H=%u pitch=%d buf=%p",
                W, H, pitch_bytes, static_cast<void*>(buf));

    // ── Pass 0: base fill (#0A0A14, full opacity) ─────────────────────────
    // Row-by-row to honour SDL pitch (may exceed W*4 when texture is padded).
    for (u32 y = 0; y < H; ++y) {
        u8 *row = buf + static_cast<size_t>(y) * pitch;
        for (u32 x = 0; x < W; ++x) {
            row[x * 4u + 0] = 0x0A; // R
            row[x * 4u + 1] = 0x0A; // G
            row[x * 4u + 2] = 0x14; // B
            row[x * 4u + 3] = 0xFF; // A
        }
    }

    u32 rng = WP_SEED;

    // Convenience lambda: get RGBA byte pointers for pixel at (x, y).
    // Returns pointer to buf + (y*W + x)*4.
    auto px = [&](u32 x, u32 y) -> u8 * {
        return buf + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4u;
    };

    // ── Pass 1: plasma blooms ─────────────────────────────────────────────
    for (u32 bi = 0; bi < BLOOM_COUNT; ++bi) {
        const u32 cx     = BLOOM_CX[bi];
        const u32 cy     = BLOOM_CY[bi];
        const u32 radius = BLOOM_RADII_CPP[bi];
        const u8  br = BLOOM_PAL_R[bi];
        const u8  bg_c = BLOOM_PAL_G[bi];
        const u8  bb = BLOOM_PAL_B[bi];
        const s32 r  = static_cast<s32>(radius);

        const u32 x0 = (static_cast<s32>(cx) - r > 0)        ? static_cast<u32>(static_cast<s32>(cx) - r) : 0u;
        const u32 x1 = (static_cast<s32>(cx) + r < static_cast<s32>(W)) ? static_cast<u32>(static_cast<s32>(cx) + r) : W - 1u;
        const u32 y0 = (static_cast<s32>(cy) - r > 0)        ? static_cast<u32>(static_cast<s32>(cy) - r) : 0u;
        const u32 y1 = (static_cast<s32>(cy) + r < static_cast<s32>(H)) ? static_cast<u32>(static_cast<s32>(cy) + r) : H - 1u;

        for (u32 y = y0; y <= y1; ++y) {
            for (u32 x = x0; x <= x1; ++x) {
                const s32 dx = static_cast<s32>(x) - static_cast<s32>(cx);
                const s32 dy = static_cast<s32>(y) - static_cast<s32>(cy);
                const u64 dist_sq = static_cast<u64>(dx * dx + dy * dy);
                const u64 r_sq   = static_cast<u64>(r) * static_cast<u64>(r);
                if (dist_sq >= r_sq) {
                    continue;
                }
                const u32 dist = Isqrt64(dist_sq);

                // Cycle C4: alpha cap dropped 180→110 and falloff 120→80
                // so the blooms read as background ambience, not as
                // foreground objects. Combined with the radii reduction in
                // qd_Wallpaper.hpp BLOOM_RADII_CPP this kills the
                // three-giant-glow-blob symptom the user reported on hw.
                u32 alpha256;
                if (dist < static_cast<u32>(r / 3)) {
                    alpha256 = 110U;
                } else {
                    const u32 fade  = static_cast<u32>(r) - dist;
                    const u32 range = static_cast<u32>(r) - static_cast<u32>(r / 3);
                    alpha256 = (range > 0u) ? (fade * 80u / range) : 0u;
                    if (alpha256 > 110u) { alpha256 = 110u; }
                }

                u8 *p = px(x, y);
                BlendAddCh(p[0], br,   alpha256);
                BlendAddCh(p[1], bg_c, alpha256);
                BlendAddCh(p[2], bb,   alpha256);
                // alpha channel stays 0xFF
            }
        }
    }

    // ── Pass 2: diagonal data streams ─────────────────────────────────────
    for (u32 si = 0; si < STREAM_COUNT; ++si) {
        const bool on_top = (Xorshift(rng) & 1u) == 0u;
        s32 sx_pos, sy_pos;
        if (on_top) {
            sx_pos = static_cast<s32>(Xorshift(rng) % W);
            sy_pos = 0;
        } else {
            sx_pos = 0;
            sy_pos = static_cast<s32>(Xorshift(rng) % H);
        }

        const s32 dx = 1 + static_cast<s32>(Xorshift(rng) % 2u); // 1 or 2
        const s32 dy = 1 + static_cast<s32>(Xorshift(rng) % 2u); // 1 or 2
        const s32 length = 80 + static_cast<s32>(Xorshift(rng) % 320u); // 80–400 (×1.5 from Rust)

        // Stream colour — cold-blue / teal / arctic only.
        static constexpr u8 STREAM_R[3] = { 0x7D, 0x34, 0xA5 };
        static constexpr u8 STREAM_G[3] = { 0xD3, 0xD3, 0xF3 };
        static constexpr u8 STREAM_B[3] = { 0xFC, 0x99, 0xFC };
        const u32 ci = Xorshift(rng) % 3u;
        const u8 sr = STREAM_R[ci];
        const u8 sg = STREAM_G[ci];
        const u8 sb_v = STREAM_B[ci];

        s32 step = 0;
        s32 cx_s = sx_pos, cy_s = sy_pos;
        while (step < length
               && cx_s >= 0 && cx_s < static_cast<s32>(W)
               && cy_s >= 0 && cy_s < static_cast<s32>(H)) {
            // Triangle envelope — ramp up then down.
            const u32 t256 = static_cast<u32>(step * 256 / (length > 0 ? length : 1));
            u32 alpha256;
            if (t256 < 128u) {
                alpha256 = t256 * 2u;
            } else {
                alpha256 = (255u - t256) * 2u + 2u;
            }
            alpha256 = (alpha256 * 60u / 256u);
            if (alpha256 < 8u) { alpha256 = 8u; }

            u8 *p = px(static_cast<u32>(cx_s), static_cast<u32>(cy_s));
            BlendAddCh(p[0], sr,   alpha256);
            BlendAddCh(p[1], sg,   alpha256);
            BlendAddCh(p[2], sb_v, alpha256);

            // Soft fringe: one pixel below with 1/3 intensity.
            if (cy_s + 1 < static_cast<s32>(H)) {
                u8 *p2 = px(static_cast<u32>(cx_s), static_cast<u32>(cy_s + 1));
                BlendAddCh(p2[0], sr,   alpha256 / 3u);
                BlendAddCh(p2[1], sg,   alpha256 / 3u);
                BlendAddCh(p2[2], sb_v, alpha256 / 3u);
            }

            cx_s += dx;
            cy_s += dy;
            ++step;
        }
    }

    // ── Pass 3: stars ─────────────────────────────────────────────────────
    static constexpr u8 STAR_R[4] = { 0xFF, 0xA5, 0xE0, 0x7D };
    static constexpr u8 STAR_G[4] = { 0xFF, 0xF3, 0xE0, 0xD3 };
    static constexpr u8 STAR_B[4] = { 0xFF, 0xFC, 0xF0, 0xFC };

    for (u32 star = 0; star < STAR_COUNT; ++star) {
        const u32 star_x = Xorshift(rng) % W;
        const u32 star_y = Xorshift(rng) % H;
        const u32 t      = Xorshift(rng) % 4u;
        const u32 alpha256 = 100u + (Xorshift(rng) % 156u); // 100–255

        u8 *p = px(star_x, star_y);
        BlendOverCh(p[0], STAR_R[t], alpha256);
        BlendOverCh(p[1], STAR_G[t], alpha256);
        BlendOverCh(p[2], STAR_B[t], alpha256);
    }

    // ── Pass 4: faint grid overlay ────────────────────────────────────────
    // GRID_CELL = 96 (×1.5 from Rust 64), GRID_ALPHA = 22.
    static constexpr u8 GRID_R_CH = 0x18;
    static constexpr u8 GRID_G_CH = 0x18;
    static constexpr u8 GRID_B_CH = 0x32;

    // Horizontal lines every GRID_CELL rows.
    for (u32 row = 0; row < H; row += GRID_CELL) {
        for (u32 x = 0; x < W; ++x) {
            u8 *p = px(x, row);
            BlendOverCh(p[0], GRID_R_CH, GRID_ALPHA);
            BlendOverCh(p[1], GRID_G_CH, GRID_ALPHA);
            BlendOverCh(p[2], GRID_B_CH, GRID_ALPHA);
        }
    }
    // Vertical lines every GRID_CELL columns.
    for (u32 col = 0; col < W; col += GRID_CELL) {
        for (u32 y = 0; y < H; ++y) {
            u8 *p = px(col, y);
            BlendOverCh(p[0], GRID_R_CH, GRID_ALPHA);
            BlendOverCh(p[1], GRID_G_CH, GRID_ALPHA);
            BlendOverCh(p[2], GRID_B_CH, GRID_ALPHA);
        }
    }

    // ── Pass 5: radial vignette ───────────────────────────────────────────
    // Darkens toward corners. Strength = dist_sq * 154 / max_dist_sq.
    // scale = 256 - strength; each channel *= scale / 256.
    {
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
                    strength_256 = 154U;
                } else {
                    strength_256 = static_cast<u32>(dist_sq * 154ULL / max_dist_sq);
                }

                const u32 scale = 256U - strength_256;
                u8 *p = px(x, y);
                p[0] = static_cast<u8>(static_cast<u32>(p[0]) * scale / 256U);
                p[1] = static_cast<u8>(static_cast<u32>(p[1]) * scale / 256U);
                p[2] = static_cast<u8>(static_cast<u32>(p[2]) * scale / 256U);
                // Alpha channel stays 0xFF throughout all passes.
            }
        }
    }
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdWallpaperElement::QdWallpaperElement(const QdTheme &theme)
    : theme_(theme), cached_tex_(nullptr), rendered_(false),
      ledger_handle_(0), tex_w_(0), tex_h_(0) {
}

QdWallpaperElement::~QdWallpaperElement() {
    if (cached_tex_ != nullptr) {
        // W11-FOOTPRINT Part 1: untrack wallpaper texture from ledger before
        // freeing so the Monitor Resources view shows the correct live byte count.
        UL_LEDGER_UNTRACK(ledger_handle_);
        ledger_handle_ = 0;
        SDL_DestroyTexture(cached_tex_);
        cached_tex_ = nullptr;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdWallpaperElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                  const s32 /*x*/, const s32 /*y*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    {
        static bool logged_once = false;
        if (!logged_once) {
            UL_LOG_INFO("qdesktop: Wallpaper OnRender first call renderer=%p rendered=%d",
                        static_cast<void*>(r), rendered_ ? 1 : 0);
            logged_once = true;
        }
    }
    if (r == nullptr) {
        UL_LOG_WARN("qdesktop: Wallpaper OnRender got NULL main renderer");
        return;
    }

    // v3.1.1 (BUG-1 fix): consume the wallpaper-dirty flag.  Set by
    // SetActiveThemePack() in qd_Theme.cpp when the user picks a different
    // theme pack via the Z2.1 Change-Wallpaper or Z2.2 Choose-Folder-Theme
    // submenus.  We drop the cached texture and reset rendered_ so the
    // existing first-render block below re-bakes the pixels with the new
    // pack + theme.
    if (g_wallpaper_dirty.exchange(false, std::memory_order_acq_rel)) {
        UL_LOG_INFO("qdesktop: Wallpaper dirty flag consumed — re-baking texture for pack=%zu",
                    g_active_wallpaper_pack);
        if (cached_tex_ != nullptr) {
            // W11-FOOTPRINT Part 1: untrack before freeing so ledger byte count
            // drops immediately when the old texture is freed (theme switch).
            UL_LEDGER_UNTRACK(ledger_handle_);
            ledger_handle_ = 0;
            SDL_DestroyTexture(cached_tex_);
            cached_tex_ = nullptr;
        }
        rendered_ = false;
    }

    if (!rendered_) {
        UL_LOG_INFO("qdesktop: Wallpaper render-once block entry");

        // v3.7 visual overhaul: prefer a shipped art-directed image wallpaper
        // for the active WALLPAPER pack (decoupled from the icon/palette pack).
        // The 10 images live loose at romfs:/wallpapers/q-os-{idx}.png so they
        // can be picked by g_active_wallpaper_pack directly (no .ultheme
        // extraction).  This element already drops cached_tex_ and re-bakes on
        // g_wallpaper_dirty, so a live "Change Wallpaper" swaps the image too.
        // LoadImageFromFile returns a native-res (1280×720) texture; the blit
        // below scales it to 1920×1080 in hardware, exactly like the procedural
        // texture.  If the PNG is absent/fails we fall through to the procedural
        // generator below so a wallpaper ALWAYS renders.
        {
            char wp_img_path[64];
            snprintf(wp_img_path, sizeof(wp_img_path),
                     "romfs:/wallpapers/q-os-%zu.png", g_active_wallpaper_pack);
            cached_tex_ = pu::ui::render::LoadImageFromFile(wp_img_path);
            if (cached_tex_ != nullptr) {
                SDL_QueryTexture(cached_tex_, nullptr, nullptr, &tex_w_, &tex_h_);
                const size_t img_bytes = static_cast<size_t>(tex_w_)
                                       * static_cast<size_t>(tex_h_) * 4u;
                char itag[40];
                snprintf(itag, sizeof(itag), "wp:img_pack%zu_%dx%d",
                         g_active_wallpaper_pack, tex_w_, tex_h_);
                ledger_handle_ = UL_LEDGER_TRACK(QdResKind::Texture, itag, img_bytes);
                rendered_ = true;
                UL_LOG_INFO("qdesktop: Wallpaper using IMAGE %s (%dx%d)",
                            wp_img_path, tex_w_, tex_h_);
            }
        }
    }

    // Procedural fallback — runs only when no image wallpaper loaded above.
    if (!rendered_) {
        // W11-FOOTPRINT Part 3: select texture resolution based on Switch
        // operation mode.  In handheld mode we render at 960×540 (~2.0 MiB)
        // instead of 1280×720 (~3.5 MiB); SDL upscales to the full framebuffer
        // in GPU hardware on both paths.  Saves ~1.5 MiB in handheld mode.
        const QdLazyLoader::WpRes res = QdLazyLoader::WallpaperResolution();
        tex_w_ = static_cast<int>(res.w);
        tex_h_ = static_cast<int>(res.h);
        UL_LOG_INFO("qdesktop: W11 wallpaper resolution=%dx%d (handheld=%d)",
                    tex_w_, tex_h_, res.handheld ? 1 : 0);

        // Create a STREAMING SDL_Texture; we'll write pixels directly via
        // SDL_LockTexture, avoiding any host-side 8 MB intermediate alloc.
        // ABGR8888 = bytes [R,G,B,A] in RAM on little-endian (AArch64).
        UL_LOG_INFO("qdesktop: SDL_CreateTexture %dx%d ABGR8888 STREAMING",
                    tex_w_, tex_h_);
        cached_tex_ = SDL_CreateTexture(r,
                                        SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        tex_w_,
                                        tex_h_);
        UL_LOG_INFO("qdesktop: SDL_CreateTexture returned %p", static_cast<void*>(cached_tex_));
        if (cached_tex_ == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_CreateTexture returned NULL: %s", SDL_GetError());
            rendered_ = true;  // graceful degrade — wallpaper absent, icons still render
            return;
        }

        void *locked_pixels = nullptr;
        int locked_pitch = 0;
        const int lock_rc = SDL_LockTexture(cached_tex_, nullptr, &locked_pixels, &locked_pitch);
        UL_LOG_INFO("qdesktop: SDL_LockTexture rc=%d pixels=%p pitch=%d",
                    lock_rc, locked_pixels, locked_pitch);
        if (lock_rc != 0 || locked_pixels == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_LockTexture failed: %s", SDL_GetError());
            SDL_DestroyTexture(cached_tex_);
            cached_tex_ = nullptr;
            rendered_ = true;
            return;
        }

        // Write directly into GPU-visible memory.
        // W11 Part 3: pass tex_w_/tex_h_ to GeneratePixelsInto so the pack
        // render functions use the correct pixel dimensions.
        GeneratePixelsInto(theme_, static_cast<u8*>(locked_pixels), locked_pitch,
                           tex_w_, tex_h_);
        SDL_UnlockTexture(cached_tex_);

        UL_LOG_INFO("qdesktop: Wallpaper texture written %dx%d (streaming)",
                    tex_w_, tex_h_);

        // W11-FOOTPRINT Part 1: track the new wallpaper texture in the ledger
        // so the Monitor Resources view shows the correct byte count.
        const size_t tex_bytes = static_cast<size_t>(tex_w_)
                               * static_cast<size_t>(tex_h_) * 4u;
        char tag[32];
        snprintf(tag, sizeof(tag), "wp:pack%zu_%dx%d",
                 g_active_wallpaper_pack, tex_w_, tex_h_);
        ledger_handle_ = UL_LEDGER_TRACK(QdResKind::Texture, tag, tex_bytes);

        rendered_ = true;
    }

    if (cached_tex_ == nullptr) {
        return;
    }

    // Cycle D5: dev toggle — when disabled the wallpaper is suppressed and
    // the layout's bg color (qd_Theme PANEL_BG_DEEP) shows through.  We DO
    // NOT release the cached_tex_ here: the toggle is meant to be flipped
    // back on without paying the ~3.5 MB regeneration cost.
    if (!::ul::menu::qdesktop::g_dev_wallpaper_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    // Blit the native-resolution (1280×720) cached texture scaled up to the
    // full screen (1920×1080) via SDL_RenderCopy. The Switch GPU does this
    // bilinear scale in hardware — no per-frame CPU cost.
    SDL_Rect dst;
    dst.x = 0;
    dst.y = 0;
    dst.w = static_cast<int>(WP_BLIT_W);
    dst.h = static_cast<int>(WP_BLIT_H);
    SDL_RenderCopy(r, cached_tex_, nullptr, &dst);
}

} // namespace ul::menu::qdesktop
