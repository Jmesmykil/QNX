// qd_HotCornerOverlay.cpp — see qd_HotCornerOverlay.hpp for design notes.
//
// Widget paint geometry (same as the block removed from qd_DesktopIcons.cpp
// in v1.9.7):
//   Background:  REMOVED in v3.5 W17-BUG1 (was dark #101014 solid rect at (0,0) 96x72)
//   Accent right: 2px cyan #00E5FF A=0xA0 at (94,0) 2x72
//   Accent bottom: 2px cyan #00E5FF A=0xA0 at (0,70) 96x2
//   Q outline:   4 cyan #00E5FF solid rects forming an open square 36x36
//                centred in the 96x72 area, plus a 14x4 tail at bottom-right

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_HotCornerOverlay.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>  // v2.7.1 — accent + bg from g_QdTheme
#include <ul/menu/ui/ui_Common.hpp>       // v2.9.5 — TryFindLoadImage
#include <ul/ul_Result.hpp>               // v2.9.7 — UL_LOG_INFO for load diagnostic
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

namespace {
// v2.9.5 — per-theme hot-corner Q glyph.  One-shot lazy load through
// TryFindLoadImage so it picks up sdmc:/ulaunch/cache/active/ui/Main/EntryIcon/
// HotCornerQ.png (active theme) or romfs:/default/ui/Main/EntryIcon/HotCornerQ.png
// (Glass).  Themes that ship without this asset stay on the 5-rect procedural
// fallback below.  Texture is leaked deliberately at process exit (qlaunch
// replacement applets do not have a clean shutdown hook for global statics);
// SDL frees it when the renderer is destroyed.
SDL_Texture *g_hotcorner_q_tex = nullptr;
bool         g_hotcorner_q_load_attempted = false;
}

// v3.7 — live theme switch.  Drop the cached Q glyph and re-arm the one-shot
// loader so the next OnRender re-fetches HotCornerQ.png from the freshly
// extracted active theme (sdmc:/ulaunch/cache/active/...).  Without this the
// glyph is sticky for the process lifetime and the dock corner keeps the old
// theme's Q after a switch.
void InvalidateHotCornerQGlyph() {
    if (g_hotcorner_q_tex != nullptr) {
        SDL_DestroyTexture(g_hotcorner_q_tex);
        g_hotcorner_q_tex = nullptr;
    }
    g_hotcorner_q_load_attempted = false;
}

void QdHotCornerOverlay::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                  s32 /*x*/, s32 /*y*/) {
    // Suppressed when Launchpad search bar has focus.
    if (search_active_ref_ != nullptr && *search_active_ref_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const int32_t kW = LP_HOTCORNER_W;  // 96
    const int32_t kH = LP_HOTCORNER_H;  // 72

    // v2.7.1 — chrome from g_QdTheme (was hardcoded dark navy + cyan).
    const auto &sg = g_QdTheme.surface_glass;
    const auto &ac = g_QdTheme.accent;

    // Pass 1: solid theme-aware background.
    // v3.5 W17-BUG1: removed opaque backplate FillRect.  The 96×72 solid rect
    // at (0,0) was rendering as a visible grey/dark square over the wallpaper
    // on hardware.  The hot-corner widget identity is the Q glyph + accent
    // borders; the background fill is purely cosmetic and its loss is invisible
    // because the wallpaper already provides context behind the corner widget.
    // The sg variable is kept to avoid an unused-variable warning since it
    // is only referenced by the removed fill.
    (void)sg;

    // Pass 2: translucent accent borders.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, ac.r, ac.g, ac.b, 0xA0u);
    SDL_Rect hc_right  { kW - 2, 0,      2,  kH };
    SDL_Rect hc_bottom { 0,      kH - 2, kW, 2  };
    SDL_RenderFillRect(r, &hc_right);
    SDL_RenderFillRect(r, &hc_bottom);

    // Pass 3: Q glyph — prefer per-theme PNG, fall back to procedural rects.
    // v2.9.5: HotCornerQ.png is bundled per theme (Glass through Pixel) with a
    // theme-specific render style — pixel-art for Pixel, glow for Neon, etc.
    // Lazy-load once; sticky cache for the process lifetime.
    if (!g_hotcorner_q_load_attempted) {
        g_hotcorner_q_tex =
            ::ul::menu::ui::TryFindLoadImage("ui/Main/EntryIcon/HotCornerQ");
        g_hotcorner_q_load_attempted = true;
        // v2.9.7 diagnostic — folder tiles loaded fine in v2.9.6 HW test but
        // the hot-corner Q stayed on the procedural 5-rect fallback.  Both
        // call the SAME TryFindLoadImage with the SAME directory.  Log the
        // result so the next HW boot leaves a paper trail.
        UL_LOG_INFO("qdesktop: HotCornerOverlay TryFindLoadImage(ui/Main/EntryIcon/HotCornerQ) = %p",
                    static_cast<void*>(g_hotcorner_q_tex));
    }
    if (g_hotcorner_q_tex != nullptr) {
        // PNG is 192×192 with the Q glyph centred + transparent margins.  Draw
        // into a 60×60 box centred in the 96×72 cell — large enough that
        // per-theme styling (pixel chunks, neon glow, blueprint outlines)
        // remains visible after downscale.
        constexpr int32_t kQGlyphPx = 60;
        const int32_t gx = (kW - kQGlyphPx) / 2;          // 18
        const int32_t gy = (kH - kQGlyphPx) / 2;          //  6
        SDL_Rect q_dst { gx, gy, kQGlyphPx, kQGlyphPx };
        SDL_SetTextureBlendMode(g_hotcorner_q_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(g_hotcorner_q_tex, 0xFFu, 0xFFu, 0xFFu);
        SDL_SetTextureAlphaMod(g_hotcorner_q_tex, 0xFFu);
        SDL_RenderCopy(r, g_hotcorner_q_tex, nullptr, &q_dst);
    } else {
        // Procedural fallback — solid Q-glyph in accent (pre-v2.9.5 behavior).
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, ac.r, ac.g, ac.b, 0xFFu);
        const int32_t gx = (kW - 36) / 2;  // 30
        const int32_t gy = (kH - 36) / 2;  // 18
        SDL_Rect q_top   { gx,      gy,      36, 4  };
        SDL_Rect q_bot   { gx,      gy + 32, 36, 4  };
        SDL_Rect q_left  { gx,      gy,      4,  36 };
        SDL_Rect q_right { gx + 32, gy,      4,  36 };
        SDL_Rect q_tail  { gx + 26, gy + 26, 14, 4  };
        SDL_RenderFillRect(r, &q_top);
        SDL_RenderFillRect(r, &q_bot);
        SDL_RenderFillRect(r, &q_left);
        SDL_RenderFillRect(r, &q_right);
        SDL_RenderFillRect(r, &q_tail);
    }
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
