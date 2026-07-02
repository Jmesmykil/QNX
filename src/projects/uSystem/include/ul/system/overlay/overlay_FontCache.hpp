// overlay_FontCache.hpp — Q OS overlay text rendering.
//
// Recipe (Atmosphère fatal pattern — `stratosphere/fatal/source/fatal_font.cpp`, CC0):
//   1. plInitialize(PlServiceType_User)
//   2. plGetSharedFontByType(&font_data, PlSharedFontType_Standard)
//   3. stbtt_InitFont on the (libnx-decoded BFTTF) font buffer
//   4. Per codepoint: stbtt_GetCodepointBitmap → cache the alpha bitmap
//   5. drawString: walk codepoints, alpha-blit each cached glyph into the
//      overlay's RGBA_4444 framebuffer
//
// Improvements over fatal:
//   • Glyph cache (fatal rasterizes every frame).  Tesla also caches.
//   • Configurable point size (fatal is single-size).
//   • Unicode-clean (BFTTF has CJK glyphs).
//
// License:
//   • stbtt = public domain (no copyleft taint)
//   • Recipe = inspired by Atmosphère fatal (CC0/PD) + libtesla API surface
//     (we do not copy libtesla CODE; libtesla is GPL-2.0)
//   • Q OS code = whatever license the repo settles on
//
// See docs/50_v3.1_phase2_implementation_plan.md §T2.1.

#pragma once
#include <switch.h>
#include <ul/system/overlay/overlay_Renderer.hpp>
#include <cstddef>
#include <cstdint>

namespace ul::system::overlay {

    // ── Glyph rendering ──────────────────────────────────────────────────
    //
    // Lifecycle:
    //   - Construct once (or use the file-static g_FontCache below).
    //   - Call Initialize() AFTER plInitialize succeeded in the parent.
    //   - DrawString freely from the render thread.
    //   - Finalize() releases the cache + stbtt state.
    //
    // Thread safety: the render thread is the SOLE caller — no internal
    // locking.  If multiple threads ever need DrawString, add a mutex
    // around the cache map operations.
    class FontCache {
    public:
        FontCache();
        ~FontCache();

        // Acquire the Standard shared font and prepare stbtt.  Returns the
        // libnx Result.  Safe to call multiple times — only the first
        // succeeds; subsequent return a sentinel "already initialized" rc.
        Result Initialize();

        // Tear down.  Safe to call repeatedly; no-op if not initialized.
        void Finalize();

        bool IsReady() const { return ready_; }

        // Draw a UTF-8 string at (x, y) — baseline-anchored, NOT top-left.
        // (Tesla and stbtt convention: y is the baseline.  Add ascent to
        // get the top-of-pixels position.)
        //
        // pixel_height — font size in pixels.  Common values: 16 (body),
        // 20 (header), 28 (title bar text).
        //
        // Returns the x advance — pixel position to continue drawing at.
        // Useful for laying out adjacent strings on the same line.
        s32 DrawString(Renderer &renderer, s32 x, s32 y, s32 pixel_height,
                       const char *utf8, Color4444 color);

        // Measure-only variant: returns the pixel width without drawing.
        // Use to right-align or center text.
        s32 MeasureString(s32 pixel_height, const char *utf8);

        // Returns the ascent (top of pixels above baseline) for the given
        // pixel_height.  Convenience for callers that want top-left
        // anchoring: `DrawString(x, y + Ascent(h), h, ...)`.
        s32 Ascent(s32 pixel_height);

    private:
        // Opaque PIMPL — stbtt_fontinfo + std::unordered_map can't live
        // in the header cleanly (stbtt is a header-only impl included
        // ONLY by the .cpp).  We hide everything behind a void* to keep
        // this header lightweight.
        void *impl_   = nullptr;
        bool  ready_  = false;
    };

    // Process-singleton.  uSystem main initializes this once after
    // plInitialize; the render thread reads it freely.
    FontCache &SharedFontCache();

}  // namespace ul::system::overlay
