// overlay_Renderer.hpp — Q OS overlay drawing primitives.
//
// Drawing target is a CPU-side RGBA_4444 framebuffer (16 bpp, 4 bits per
// channel).  Owned by overlay_TestLayer / overlay_ChromeLayer; we just get
// a borrowed pointer + dimensions via Begin(), then issue draw calls.
//
// Why RGBA_4444 and not RGBA_8888:
//   • Half the memory bandwidth per frame (1.84 MB vs 3.6 MB at 1280×720).
//   • Tesla / libtesla uses the same format for the same reason.
//   • 4-bit alpha is enough for chrome — solid borders/buttons want alpha=15
//     (opaque) or alpha=0 (transparent); we don't yet do partial-alpha blends.
//
// Why no blending in T2:
//   • Simpler primitives ship faster.
//   • Chrome elements (border, title bar, buttons) are opaque-or-transparent,
//     no semi-transparent overlap.
//   • Blend support is a T6 addition once chrome layout stabilizes.
//
// Coordinate system: pixel grid, (0,0) at top-left, X right, Y down.
// Bounds checking is on every primitive — out-of-bounds writes silently
// clipped (we do NOT abort).  This makes the API safe to call with
// constant-folded coords without per-call validation in callers.
//
// Design pattern source: composition-over-reinvention — Atmosphère fatal's
// software framebuffer recipe (`stratosphere/fatal/source/fatal_task_screen.cpp`,
// CC0/PD) + libtesla's gfx::Renderer API surface (license-clean — surface
// shapes only, not code copy).
//
// See docs/50_v3.1_phase2_implementation_plan.md §T2.

#pragma once
#include <switch.h>

namespace ul::system::overlay {

    // ── RGBA_4444 color (16 bpp packed) ──────────────────────────────────
    //
    // libnx's PIXEL_FORMAT_RGBA_4444 byte order on screen for a single
    // 16-bit pixel (little-endian u16):
    //
    //   bit:  15 14 13 12 | 11 10  9  8 |  7  6  5  4 |  3  2  1  0
    //         R3 R2 R1 R0   G3 G2 G1 G0   B3 B2 B1 B0   A3 A2 A1 A0
    //
    // So an opaque red = R=15, G=0, B=0, A=15 = 0xF00F.
    // Fully transparent = 0x0000.
    struct Color4444 {
        u16 packed;

        constexpr Color4444() : packed(0) {}
        constexpr explicit Color4444(u16 p) : packed(p) {}

        // Build from 8-bit RGBA (lossy — discards low 4 bits per channel).
        static constexpr Color4444 FromRGBA8(u8 r, u8 g, u8 b, u8 a) {
            const u16 R = (u16)(r & 0xF0) << 8;   // high 4 bits of r → bits 15-12
            const u16 G = (u16)(g & 0xF0) << 4;   // high 4 bits of g → bits 11-8
            const u16 B = (u16)(b & 0xF0);        // high 4 bits of b → bits 7-4
            const u16 A = (u16)(a & 0xF0) >> 4;   // high 4 bits of a → bits 3-0
            return Color4444{ (u16)(R | G | B | A) };
        }
    };

    // ── Common chrome colors ─────────────────────────────────────────────
    // These match qd_Theme.hpp's "Glass" default palette (the Q OS default).
    // Real chrome will read sdmc:/ulaunch/qos-theme.toml in T5; these are
    // sensible fallbacks if the file is missing.
    constexpr Color4444 kColorTransparent = Color4444{0x0000};
    constexpr Color4444 kColorBlack       = Color4444{0x000F};  // opaque black
    constexpr Color4444 kColorWhite       = Color4444{0xFFFF};
    // qd_Theme Glass.accent = #7DD3FC cyan: r=0x7D, g=0xD3, b=0xFC, a=0xFF
    // → 4444 packing: 0x7DFF (top nibble of 0x7D=7, 0xD3=D, 0xFC=F, 0xFF=F)
    constexpr Color4444 kColorAccentCyan  = Color4444{0x7DFF};
    // qd_Theme Glass.surface_glass = #12122A dark blue with full alpha
    constexpr Color4444 kColorSurfaceDark = Color4444{0x112F};

    // ── Renderer ─────────────────────────────────────────────────────────
    //
    // Single instance per framebuffer.  Stateless across frames — just a
    // borrowed pointer + dimensions.  Call Begin() each frame, issue draws,
    // no End() needed (the framebuffer's queue/commit is owned by the
    // overlay's render thread, which calls framebufferEnd separately).
    class Renderer {
    public:
        // Borrow the framebuffer for this frame.
        // fb            — pointer to the (linear) framebuffer base
        // stride_bytes  — bytes per row (may exceed width × 2 for alignment)
        // width, height — framebuffer dimensions in pixels
        void Begin(u8 *fb, u32 stride_bytes, u32 width, u32 height);

        // Fill the entire framebuffer with color.  Optimized memset path
        // when color is uniform (all-zeros = transparent).
        void Clear(Color4444 color);

        // Set a single pixel at (x, y).  Silently no-ops if out of bounds.
        void PutPixel(s32 x, s32 y, Color4444 color);

        // Fill axis-aligned rectangle (x, y, w, h).  Bounds-clipped.
        // x,y can be negative; w,h must be non-negative.
        void FillRect(s32 x, s32 y, s32 w, s32 h, Color4444 color);

        // Stroke the outline of (x, y, w, h) with given thickness.
        // Thickness is drawn INWARD from the rect boundary (so a 1280×720
        // stroke at thickness=4 keeps the visible content at 1272×712).
        void StrokeRect(s32 x, s32 y, s32 w, s32 h, s32 thickness, Color4444 color);

        // ── Accessors (for advanced clients that need to write pixels directly)
        u8  *Fb()           const { return fb_; }
        u32  StrideBytes()  const { return stride_; }
        u32  Width()        const { return w_; }
        u32  Height()       const { return h_; }

    private:
        u8  *fb_     = nullptr;
        u32  stride_ = 0;   // bytes per row
        u32  w_      = 0;
        u32  h_      = 0;
    };

}  // namespace ul::system::overlay
