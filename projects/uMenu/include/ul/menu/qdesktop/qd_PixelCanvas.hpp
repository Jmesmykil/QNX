// qd_PixelCanvas.hpp — Immediate-mode per-pixel drawing shim over SDL2.
// Ported for uMenu C++ SP1 (v1.1.12).
// Uses pu::ui::render::GetMainRenderer() — the SDL_Renderer* is live during OnRender.
// (F-08 fix: stale comment referenced pu::sdl2::GetMainRenderer which does not exist.)
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>

namespace ul::menu::qdesktop {

// Immediate-mode canvas — wraps SDL_Renderer* for per-pixel operations.
// All operations are no-ops when the canvas is not begun (guard against misuse).
class QdPixelCanvas {
public:
    // Begin a frame — records the renderer pointer.
    // Must be called at the top of OnRender before any draw calls.
    void Begin(SDL_Renderer *r);

    // End a frame — clears the stored pointer (safety guard).
    void End();

    // Set a single pixel at (x, y) to (r,g,b,a).
    // from wallpaper.rs: SDL_SetRenderDrawColor + SDL_RenderDrawPoint
    void SetPixel(s32 x, s32 y, u8 r, u8 g, u8 b, u8 a);

    // Fill a rectangle with a solid color.
    // from wallpaper.rs: SDL_SetRenderDrawColor + SDL_RenderFillRect
    void FillRect(s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a);

    // Blit a raw RGBA pixel buffer (src_w × src_h pixels, 4 bytes each) to dst.
    // Nearest-neighbor scaled to (dst_w × dst_h).
    // Used by icon cache to blit 64×64 thumbnails.
    void BlitRgba(s32 dst_x, s32 dst_y, s32 dst_w, s32 dst_h,
                  const u8 *src_rgba, s32 src_w, s32 src_h);

    // Draw a single-pixel line from (x0,y0) to (x1,y1) using Bresenham's algorithm.
    void DrawLine(s32 x0, s32 y0, s32 x1, s32 y1,
                  u8 r, u8 g, u8 b, u8 a);

    // Alpha-blend src (r,g,b,alpha256) over dst_rgb — returns blended channel.
    // Used internally; exposed for tests.
    // blend = (src * alpha256 + dst * (256 - alpha256)) / 256
    static u8 BlendChannel(u8 src, u8 dst, u32 alpha256);

    // Additive blend src (r,g,b,alpha256) over dst (clamped to 255).
    // alpha256 is in [0,256] matching BlendChannel — 256 = full opacity.
    // Used by bloom/stream passes in wallpaper.
    static void BlendAdd(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                         u8 src_r, u8 src_g, u8 src_b, u32 alpha256);

    // Alpha-over blend src (r,g,b,alpha256) over dst.
    // alpha256 is in [0,256] matching BlendChannel — 256 = full opacity.
    // Used by star/grid/vignette passes in wallpaper.
    static void BlendOver(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                          u8 src_r, u8 src_g, u8 src_b, u32 alpha256);

private:
    SDL_Renderer *renderer_ = nullptr;
};

} // namespace ul::menu::qdesktop
