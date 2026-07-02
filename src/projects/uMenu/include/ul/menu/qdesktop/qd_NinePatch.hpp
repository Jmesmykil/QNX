// qd_NinePatch.hpp — Stateless nine-patch (9-slice) SDL_Texture blit.
// Used by QdFrame to blit the window SVG master at any size without stretching corners.
#pragma once
#ifdef QDESKTOP_MODE
#include <SDL2/SDL.h>
#include <switch.h>  // u8

namespace ul::menu::qdesktop {

struct NinePatchInsets { int left, right, top, bottom; };

// Blit `src` (src_w×src_h source pixels) into `dst` using 9-slice.
// Corners blit 1:1; top/bottom edges stretch horizontally; left/right edges
// stretch vertically; center stretches in both axes.
// Mid cell widths/heights are clamped to >= 0 so undersized rects are safe.
// `alpha` is applied via SDL_SetTextureAlphaMod (restored to 255 after the call).
void DrawNinePatch(SDL_Renderer* r, SDL_Texture* src,
                   int src_w, int src_h,
                   const NinePatchInsets& ins,
                   const SDL_Rect& dst, u8 alpha = 255);

} // namespace ul::menu::qdesktop
#endif // QDESKTOP_MODE
