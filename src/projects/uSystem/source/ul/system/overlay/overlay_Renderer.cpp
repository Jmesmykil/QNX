// overlay_Renderer.cpp — implementation.

#include <ul/system/overlay/overlay_Renderer.hpp>
#include <algorithm>
#include <cstring>

namespace ul::system::overlay {

void Renderer::Begin(u8 *fb, u32 stride_bytes, u32 width, u32 height) {
    fb_     = fb;
    stride_ = stride_bytes;
    w_      = width;
    h_      = height;
}

void Renderer::Clear(Color4444 color) {
    if (fb_ == nullptr || stride_ == 0 || h_ == 0) {
        return;
    }
    // Hot path: full transparent / full opaque uniform-byte fill via memset.
    // Uniform-byte means high byte == low byte of the u16.
    const u8 hi = (u8)(color.packed >> 8);
    const u8 lo = (u8)(color.packed & 0xFF);
    if (hi == lo) {
        std::memset(fb_, hi, (size_t)stride_ * (size_t)h_);
        return;
    }
    // Slow path: per-pixel fill.  Only triggered for unusual chrome colors.
    for (u32 y = 0; y < h_; ++y) {
        u16 *row = reinterpret_cast<u16*>(fb_ + (size_t)y * stride_);
        for (u32 x = 0; x < w_; ++x) {
            row[x] = color.packed;
        }
    }
}

void Renderer::PutPixel(s32 x, s32 y, Color4444 color) {
    if (fb_ == nullptr) return;
    if (x < 0 || y < 0) return;
    if ((u32)x >= w_ || (u32)y >= h_) return;
    u16 *row = reinterpret_cast<u16*>(fb_ + (size_t)y * stride_);
    row[x] = color.packed;
}

void Renderer::FillRect(s32 x, s32 y, s32 w, s32 h, Color4444 color) {
    if (fb_ == nullptr) return;
    if (w <= 0 || h <= 0) return;

    // Clip to framebuffer bounds.
    s32 x0 = std::max<s32>(x, 0);
    s32 y0 = std::max<s32>(y, 0);
    s32 x1 = std::min<s32>(x + w, (s32)w_);
    s32 y1 = std::min<s32>(y + h, (s32)h_);
    if (x0 >= x1 || y0 >= y1) return;

    // Row-by-row fill.  Inner loop is a simple u16 write; the compiler
    // can vectorize this via NEON on devkitA64.
    for (s32 yy = y0; yy < y1; ++yy) {
        u16 *row = reinterpret_cast<u16*>(fb_ + (size_t)yy * stride_);
        for (s32 xx = x0; xx < x1; ++xx) {
            row[xx] = color.packed;
        }
    }
}

void Renderer::StrokeRect(s32 x, s32 y, s32 w, s32 h, s32 thickness, Color4444 color) {
    if (thickness <= 0 || w <= 0 || h <= 0) return;
    // Clip thickness so it can't exceed half the rect's smallest side.
    const s32 t = std::min<s32>(thickness, std::min<s32>(w / 2, h / 2));
    if (t <= 0) {
        // Rect too thin for inward stroke; fall back to filling the whole
        // rect to make sure SOMETHING renders (debug visibility).
        FillRect(x, y, w, h, color);
        return;
    }

    // Top strip: full width × thickness.
    FillRect(x,           y,             w, t, color);
    // Bottom strip: full width × thickness, offset down by h - t.
    FillRect(x,           y + h - t,     w, t, color);
    // Left strip: thickness × (h - 2t), avoiding double-fill of corners.
    FillRect(x,           y + t,         t, h - 2 * t, color);
    // Right strip: same offset by w - t.
    FillRect(x + w - t,   y + t,         t, h - 2 * t, color);
}

}  // namespace ul::system::overlay
