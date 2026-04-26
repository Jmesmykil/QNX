// qd_Transition.cpp — implementation of qd_Transition.hpp.
//
// All rendering is done in a single SDL_LockTexture/UnlockTexture pass at
// 1280×720 native resolution (SDL_RenderCopy upscales to 1920×1080 at blit
// time, identical to qd_Wallpaper). No libm, no dynamic alloc, no font —
// the Q glyph is two pure-geometry shapes (annular ring + line segment).
//
// Pixel format: SDL_PIXELFORMAT_ABGR8888 == byte order [R,G,B,A] in RAM on
// AArch64 little-endian.  Same as qd_Wallpaper.

#include <ul/menu/qdesktop/qd_Transition.hpp>
#include <ul/ul_Result.hpp>          // UL_LOG_INFO / UL_LOG_WARN
#include <pu/ui/render/render_SDL2.hpp>
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

    namespace {

        // ── Texture geometry ───────────────────────────────────────────────
        // 1280×720 = 3.5 MB at RGBA8888; matches qd_Wallpaper, fits the
        // Switch GPU pool alongside Plutonium's framebuffers.
        constexpr u32 BRAND_W = 1280;
        constexpr u32 BRAND_H = 720;

        // ── Vertical gradient endpoints (R,G,B order — alpha is always FF) ─
        // PANEL_BG_DARK and PANEL_BG_DEEP from qd_Theme.hpp roughly; the
        // top is darker than C5's solid {0x0C,0x0C,0x20} so the brand glyph
        // reads with more contrast.
        constexpr u8 GRAD_TOP_R = 0x0A;
        constexpr u8 GRAD_TOP_G = 0x0E;
        constexpr u8 GRAD_TOP_B = 0x1E;
        constexpr u8 GRAD_BOT_R = 0x14;
        constexpr u8 GRAD_BOT_G = 0x16;
        constexpr u8 GRAD_BOT_B = 0x2E;

        // ── Q glyph geometry (in BRAND_W × BRAND_H native space) ──────────
        // The Q OS wordmark in the v1.1.12 reference uses a circle with a
        // diagonal tail at the lower-right.  These constants reproduce
        // that silhouette with no font dependency.
        constexpr s32 Q_CX        = 640;       // ring centre X
        constexpr s32 Q_CY        = 320;       // ring centre Y (above middle)
        constexpr s32 Q_R_OUTER   = 110;       // outer ring radius
        constexpr s32 Q_R_INNER   = 82;        // inner ring radius (=> 28 px stroke)
        constexpr s32 Q_TAIL_X0   = 690;       // tail start X
        constexpr s32 Q_TAIL_Y0   = 360;       // tail start Y
        constexpr s32 Q_TAIL_X1   = 760;       // tail end X
        constexpr s32 Q_TAIL_Y1   = 430;       // tail end Y
        constexpr s32 Q_TAIL_HALF = 14;        // half-thickness of the tail

        // ── Brand colour ramp (cyan → lavender, R/G/B order) ───────────────
        // #00E5FF and #A78BFA from the Filament brand palette.
        constexpr u8 BRAND_TOP_R = 0x00;
        constexpr u8 BRAND_TOP_G = 0xE5;
        constexpr u8 BRAND_TOP_B = 0xFF;
        constexpr u8 BRAND_BOT_R = 0xA7;
        constexpr u8 BRAND_BOT_G = 0x8B;
        constexpr u8 BRAND_BOT_B = 0xFA;

        // ── Cached handle (single-instance over Application lifetime) ──────
        pu::sdl2::TextureHandle::Ref g_brand_fade_tex {};

        // ── File-local helpers ────────────────────────────────────────────

        // Linear interp 0..255 with 0..255 fraction (frac=0 → a, frac=255 → b).
        u8 LerpU8(u8 a, u8 b, u32 frac255) {
            const u32 fa = 255U - frac255;
            return static_cast<u8>(
                (static_cast<u32>(a) * fa + static_cast<u32>(b) * frac255) / 255U
            );
        }

        // Integer square root via bisection (no libm — same pattern as
        // qd_Wallpaper::Isqrt64).  Returns largest r with r*r <= n.
        u32 Isqrt64(u64 n) {
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

        // Squared distance from (px,py) to (qx,qy).
        u64 SqDist(s32 px, s32 py, s32 qx, s32 qy) {
            const s64 dx = static_cast<s64>(px) - static_cast<s64>(qx);
            const s64 dy = static_cast<s64>(py) - static_cast<s64>(qy);
            return static_cast<u64>(dx * dx + dy * dy);
        }

        // Squared distance from point (px,py) to line segment [(x0,y0)-(x1,y1)].
        // Computed as: clamp t = (w·v)/(v·v) ∈ [0,1], dist² = |w − t·v|².
        // Result scaled into per-pixel² units (no libm divide — uses
        // |c2*p − c2*x0 − c1*v|² / c2²  to avoid floating point).
        u64 SqDistToSegment(s32 px, s32 py, s32 x0, s32 y0, s32 x1, s32 y1) {
            const s64 vx = static_cast<s64>(x1 - x0);
            const s64 vy = static_cast<s64>(y1 - y0);
            const s64 wx = static_cast<s64>(px - x0);
            const s64 wy = static_cast<s64>(py - y0);
            const s64 c1 = wx * vx + wy * vy;
            if (c1 <= 0) {
                return SqDist(px, py, x0, y0);
            }
            const s64 c2 = vx * vx + vy * vy;
            if (c1 >= c2) {
                return SqDist(px, py, x1, y1);
            }
            // Closest point: x0 + (c1/c2)*v.
            // We want |p − (x0 + (c1/c2)*v)|², written as
            //         | (c2*p − c2*x0 − c1*v) / c2 |²
            //       = ((c2*p_x − c2*x0 − c1*vx)² + (c2*p_y − c2*y0 − c1*vy)²) / c2²
            const s64 dx = static_cast<s64>(px) * c2
                         - static_cast<s64>(x0) * c2
                         - c1 * vx;
            const s64 dy = static_cast<s64>(py) * c2
                         - static_cast<s64>(y0) * c2
                         - c1 * vy;
            const s64 c2sq = c2 * c2;
            if (c2sq <= 0) {
                return 0;
            }
            return static_cast<u64>((dx * dx + dy * dy) / c2sq);
        }

        // Write one ABGR8888 pixel into &p[0..4] given its (x,y) position.
        // Composites three passes:
        //   1. Vertical gradient bg (top → bottom)
        //   2. Q ring  (annulus around (Q_CX, Q_CY))
        //   3. Q tail  (thick segment at the lower-right)
        // Brand colour for the glyph is a vertical lerp cyan→lavender across
        // the glyph's full Y extent so the ring and tail share one ramp.
        void WritePixel(u8 *p, s32 x, s32 y) {
            // Pass 1: vertical gradient
            const u32 v_frac = (BRAND_H > 1U)
                ? (static_cast<u32>(y) * 255U) / (BRAND_H - 1U)
                : 0U;
            u8 r = LerpU8(GRAD_TOP_R, GRAD_BOT_R, v_frac);
            u8 g = LerpU8(GRAD_TOP_G, GRAD_BOT_G, v_frac);
            u8 b = LerpU8(GRAD_TOP_B, GRAD_BOT_B, v_frac);

            // Pass 2 + 3: glyph
            const u64 d2_ring = SqDist(x, y, Q_CX, Q_CY);
            const u64 r_outer_sq = static_cast<u64>(Q_R_OUTER)
                                 * static_cast<u64>(Q_R_OUTER);
            const u64 r_inner_sq = static_cast<u64>(Q_R_INNER)
                                 * static_cast<u64>(Q_R_INNER);
            const bool in_ring = (d2_ring < r_outer_sq) && (d2_ring > r_inner_sq);

            const u64 d2_tail = SqDistToSegment(x, y,
                                                 Q_TAIL_X0, Q_TAIL_Y0,
                                                 Q_TAIL_X1, Q_TAIL_Y1);
            const u64 tail_thick_sq = static_cast<u64>(Q_TAIL_HALF)
                                    * static_cast<u64>(Q_TAIL_HALF);
            const bool in_tail = (d2_tail < tail_thick_sq);

            if (in_ring || in_tail) {
                // Vertical lerp of brand colour across the glyph's bounding box.
                const s32 glyph_top = Q_CY - Q_R_OUTER;
                const s32 glyph_bot = Q_TAIL_Y1 + Q_TAIL_HALF;
                const s32 glyph_h   = glyph_bot - glyph_top;
                u32 band_frac = 0U;
                if (glyph_h > 0) {
                    s32 ny = y - glyph_top;
                    if (ny < 0) ny = 0;
                    if (ny > glyph_h) ny = glyph_h;
                    band_frac = static_cast<u32>((ny * 255) / glyph_h);
                }
                const u8 br = LerpU8(BRAND_TOP_R, BRAND_BOT_R, band_frac);
                const u8 bg = LerpU8(BRAND_TOP_G, BRAND_BOT_G, band_frac);
                const u8 bb = LerpU8(BRAND_TOP_B, BRAND_BOT_B, band_frac);

                // Anti-aliased edge: alpha proportional to depth-into-stroke.
                // Default 90 % opaque so the glyph reads as solid; the last
                // ~2 px of stroke fade so the silhouette doesn't look pixelated.
                u32 a255 = 230U;
                if (in_ring) {
                    const u32 r_avg = static_cast<u32>(Q_R_OUTER + Q_R_INNER) / 2U;
                    const u32 r_half_thick = static_cast<u32>(Q_R_OUTER - Q_R_INNER) / 2U;
                    const u32 d = (d2_ring > 0ULL) ? Isqrt64(d2_ring) : 0U;
                    const u32 dist_from_centerline =
                        (d > r_avg) ? (d - r_avg) : (r_avg - d);
                    if (r_half_thick >= 2U) {
                        if (dist_from_centerline > (r_half_thick - 2U)) {
                            const u32 edge_remaining = r_half_thick
                                                     - dist_from_centerline;
                            a255 = (edge_remaining * 230U) / 2U;
                        }
                    }
                }
                // Tail keeps full 230 alpha — its bounding box is small enough
                // that anti-aliasing across 14 px doesn't matter visually.

                r = LerpU8(r, br, a255);
                g = LerpU8(g, bg, a255);
                b = LerpU8(b, bb, a255);
            }

            // ABGR8888 little-endian = bytes [R,G,B,A]
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 0xFF;
        }

    }  // namespace

    pu::sdl2::TextureHandle::Ref GetBrandFadeTexture() {
        if (g_brand_fade_tex != nullptr) {
            return g_brand_fade_tex;
        }

        SDL_Renderer *r = pu::ui::render::GetMainRenderer();
        if (r == nullptr) {
            UL_LOG_WARN("qdesktop: GetBrandFadeTexture: NULL main renderer "
                        "— caller will fall back to solid color");
            return nullptr;
        }

        UL_LOG_INFO("qdesktop: SDL_CreateTexture(BRAND_FADE) %ux%u ABGR8888 STREAMING",
                    static_cast<unsigned>(BRAND_W),
                    static_cast<unsigned>(BRAND_H));
        SDL_Texture *tex = SDL_CreateTexture(r,
                                              SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              static_cast<int>(BRAND_W),
                                              static_cast<int>(BRAND_H));
        if (tex == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_CreateTexture(BRAND_FADE) failed: %s",
                        SDL_GetError());
            return nullptr;
        }

        void *locked = nullptr;
        int locked_pitch = 0;
        const int lock_rc = SDL_LockTexture(tex, nullptr, &locked, &locked_pitch);
        if (lock_rc != 0 || locked == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_LockTexture(BRAND_FADE) rc=%d: %s",
                        lock_rc, SDL_GetError());
            SDL_DestroyTexture(tex);
            return nullptr;
        }

        u8 *buf = static_cast<u8*>(locked);
        for (s32 y = 0; y < static_cast<s32>(BRAND_H); ++y) {
            u8 *row = buf + (static_cast<s32>(locked_pitch) * y);
            for (s32 x = 0; x < static_cast<s32>(BRAND_W); ++x) {
                WritePixel(row + x * 4, x, y);
            }
        }

        SDL_UnlockTexture(tex);

        g_brand_fade_tex = pu::sdl2::TextureHandle::New(tex);
        UL_LOG_INFO("qdesktop: brand fade texture %ux%u uploaded — Q glyph "
                    "centered at (%d,%d) r=[%d,%d] tail=(%d,%d)→(%d,%d)",
                    static_cast<unsigned>(BRAND_W),
                    static_cast<unsigned>(BRAND_H),
                    Q_CX, Q_CY, Q_R_INNER, Q_R_OUTER,
                    Q_TAIL_X0, Q_TAIL_Y0, Q_TAIL_X1, Q_TAIL_Y1);
        return g_brand_fade_tex;
    }

    void ReleaseBrandFadeTexture() {
        // shared_ptr -> 0; ~TextureHandle calls SDL_DestroyTexture.
        g_brand_fade_tex.reset();
    }

}  // namespace ul::menu::qdesktop
