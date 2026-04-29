// qd_HotCornerOverlay.cpp — see qd_HotCornerOverlay.hpp for design notes.
//
// Widget paint geometry (same as the block removed from qd_DesktopIcons.cpp
// in v1.9.7):
//   Background:  dark #101014 solid rect at (0,0) 96x72
//   Accent right: 2px cyan #00E5FF A=0xA0 at (94,0) 2x72
//   Accent bottom: 2px cyan #00E5FF A=0xA0 at (0,70) 96x2
//   Q outline:   4 cyan #00E5FF solid rects forming an open square 36x36
//                centred in the 96x72 area, plus a 14x4 tail at bottom-right

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_HotCornerOverlay.hpp>
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

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

    // Pass 1: solid dark background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0x10u, 0x10u, 0x14u, 0xFFu);
    SDL_Rect hc_bg { 0, 0, kW, kH };
    SDL_RenderFillRect(r, &hc_bg);

    // Pass 2: translucent cyan accent borders.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0xA0u);
    SDL_Rect hc_right  { kW - 2, 0,      2,  kH };
    SDL_Rect hc_bottom { 0,      kH - 2, kW, 2  };
    SDL_RenderFillRect(r, &hc_right);
    SDL_RenderFillRect(r, &hc_bottom);

    // Pass 3: solid Q-glyph (open square + tail).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0xFFu);
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

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
