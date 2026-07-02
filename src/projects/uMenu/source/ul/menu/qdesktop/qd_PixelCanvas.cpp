// qd_PixelCanvas.cpp — Immediate-mode per-pixel drawing shim over SDL2.
// Ported for uMenu C++ SP1 (v1.1.12).

#include <ul/menu/qdesktop/qd_PixelCanvas.hpp>
#include <SDL2/SDL.h>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Begin / End ───────────────────────────────────────────────────────────────

void QdPixelCanvas::Begin(SDL_Renderer *r) {
    renderer_ = r;
}

void QdPixelCanvas::End() {
    renderer_ = nullptr;
}

// ── SetPixel ──────────────────────────────────────────────────────────────────

void QdPixelCanvas::SetPixel(s32 x, s32 y, u8 r, u8 g, u8 b, u8 a) {
    if (!renderer_) { return; }
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_RenderDrawPoint(renderer_, x, y);
}

// ── FillRect ──────────────────────────────────────────────────────────────────

void QdPixelCanvas::FillRect(s32 x, s32 y, s32 w, s32 h,
                               u8 r, u8 g, u8 b, u8 a)
{
    if (!renderer_) { return; }
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_Rect rect { x, y, w, h };
    SDL_RenderFillRect(renderer_, &rect);
}

// ── BlitRgba ──────────────────────────────────────────────────────────────────

// Nearest-neighbor blit of src_rgba (src_w × src_h, RGBA) to the renderer at
// (dst_x, dst_y, dst_w × dst_h) using SDL_RenderCopy via a streaming texture.
// The source buffer is RGBA8888; uploaded as SDL_PIXELFORMAT_ABGR8888 on ARM
// little-endian (byte layout [R,G,B,A] is what the hardware expects).
void QdPixelCanvas::BlitRgba(s32 dst_x, s32 dst_y, s32 dst_w, s32 dst_h,
                               const u8 *src_rgba, s32 src_w, s32 src_h)
{
    if (!renderer_ || !src_rgba || src_w <= 0 || src_h <= 0) { return; }

    SDL_Texture *tex = SDL_CreateTexture(renderer_,
                                         SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         src_w, src_h);
    if (!tex) { return; }

    // Upload the raw RGBA bytes.
    SDL_UpdateTexture(tex, nullptr, src_rgba, src_w * 4);

    SDL_Rect dst { dst_x, dst_y, dst_w, dst_h };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

// ── DrawLine ──────────────────────────────────────────────────────────────────

// Bresenham's line algorithm. For a single-pixel-wide horizontal or vertical
// line SDL_RenderDrawLine would suffice, but this matches the wallpaper.rs path
// that works pixel-by-pixel for diagonal stream lines.
void QdPixelCanvas::DrawLine(s32 x0, s32 y0, s32 x1, s32 y1,
                              u8 r, u8 g, u8 b, u8 a)
{
    if (!renderer_) { return; }

    SDL_SetRenderDrawColor(renderer_, r, g, b, a);

    const s32 dx = std::abs(x1 - x0);
    const s32 dy = std::abs(y1 - y0);
    const s32 sx = (x0 < x1) ? 1 : -1;
    const s32 sy = (y0 < y1) ? 1 : -1;
    s32 err = dx - dy;

    while (true) {
        SDL_RenderDrawPoint(renderer_, x0, y0);
        if (x0 == x1 && y0 == y1) { break; }
        const s32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

// ── BlendChannel ─────────────────────────────────────────────────────────────

// static
u8 QdPixelCanvas::BlendChannel(u8 src, u8 dst, u32 alpha256) {
    // Standard porter-duff "over" per-channel: out = (src*a + dst*(256-a)) / 256
    const u32 out = (static_cast<u32>(src) * alpha256
                   + static_cast<u32>(dst) * (256u - alpha256)) >> 8u;
    return static_cast<u8>(out > 255u ? 255u : out);
}

// ── BlendAdd ─────────────────────────────────────────────────────────────────

// static
void QdPixelCanvas::BlendAdd(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                              u8 src_r, u8 src_g, u8 src_b, u32 alpha256)
{
    // Additive: dst += src * alpha256/256 (clamp to 255).
    const auto add = [&](u8 &dst, u8 src) {
        const u32 v = static_cast<u32>(dst)
                    + (static_cast<u32>(src) * static_cast<u32>(alpha256) >> 8u);
        dst = static_cast<u8>(v > 255u ? 255u : v);
    };
    add(dst_r, src_r);
    add(dst_g, src_g);
    add(dst_b, src_b);
}

// ── BlendOver ────────────────────────────────────────────────────────────────

// static
void QdPixelCanvas::BlendOver(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                               u8 src_r, u8 src_g, u8 src_b, u32 alpha256)
{
    dst_r = BlendChannel(src_r, dst_r, static_cast<u32>(alpha256));
    dst_g = BlendChannel(src_g, dst_g, static_cast<u32>(alpha256));
    dst_b = BlendChannel(src_b, dst_b, static_cast<u32>(alpha256));
}

} // namespace ul::menu::qdesktop
