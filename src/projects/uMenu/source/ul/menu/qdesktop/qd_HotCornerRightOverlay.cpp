// qd_HotCornerRightOverlay.cpp — see qd_HotCornerRightOverlay.hpp for design notes.
//
// Widget paint geometry (mirror of LEFT overlay's right + bottom accents):
//   Hot zone: HC_VISUAL_W × HC_VISUAL_H at (HC_RIGHT_X, 0) = (1824, 0)
//   Accent left:   2px accent at (HC_RIGHT_X, 0)            2×HC_VISUAL_H
//   Accent bottom: 2px accent at (HC_RIGHT_X, HC_VISUAL_H−2) HC_VISUAL_W×2
//
// D8 fix: removed local `kScreenW = 1920` — use HC_RIGHT_X from qd_LayoutConstants.hpp.
//
// No dark fill, no centre glyph — Plutonium's system status icons (battery,
// time, network, volume) render in the top bar at y∈[0,48] within this 96-px
// horizontal zone.  Painting a fill would obscure them.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_HotCornerRightOverlay.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>   // HC_VISUAL_W/H, HC_RIGHT_X, HC_ACCENT_THICKNESS
#include <ul/menu/qdesktop/qd_Theme.hpp>  // v2.7.1 — accent from g_QdTheme
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

void QdHotCornerRightOverlay::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                       s32 /*x*/, s32 /*y*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    // D8 fix: HC_RIGHT_X (=SCR_W - HC_VISUAL_W = 1824) replaces local `kScreenW=1920`.
    constexpr int32_t kW   = HC_VISUAL_W;              // 96
    constexpr int32_t kH   = HC_VISUAL_H;              // 72
    constexpr int32_t kHcX = HC_RIGHT_X;               // 1824 (hot-zone left edge)
    constexpr int32_t kAcc = HC_ACCENT_THICKNESS;      // 2

    // v2.7.1 — translucent accent borders (theme-aware, was hardcoded cyan).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const auto &ac = g_QdTheme.accent;
    SDL_SetRenderDrawColor(r, ac.r, ac.g, ac.b, 0xA0u);

    // Left accent — kAcc-px vertical strip at the inner edge of the hot zone.
    SDL_Rect hc_left   { kHcX,     0,        kAcc, kH };
    // Bottom accent — kAcc-px horizontal strip along the bottom of the hot zone.
    SDL_Rect hc_bottom { kHcX,     kH - kAcc, kW,  kAcc };
    SDL_RenderFillRect(r, &hc_left);
    SDL_RenderFillRect(r, &hc_bottom);
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
