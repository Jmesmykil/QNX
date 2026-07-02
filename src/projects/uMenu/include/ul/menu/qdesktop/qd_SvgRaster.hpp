// qd_SvgRaster.hpp — runtime SVG → SDL_Texture rasterizer (nanosvg).
//
// v3.7: the window chrome is rendered from the per-theme SVG masters
// (romfs:/window/q-os-{idx}.svg) so it is PIXEL-EXACT to Claude Design's
// vectors and never drifts from a code-draw approximation.  The Switch engine
// has no SVG support, so we rasterize on-device at the target size with the
// vendored single-header nanosvg + nanosvgrast (libs/nanosvg).
#pragma once

#ifdef QDESKTOP_MODE

#include <pu/Plutonium>
#include <SDL2/SDL.h>
#include <string>

namespace ul::menu::qdesktop {

// Rasterize an SVG file at the given pixel size into a fresh ABGR8888 texture
// (BLEND mode, caller owns → SDL_DestroyTexture).  The SVG viewBox is uniformly
// scaled to the destination width; for chrome authored at the window aspect
// (640×400 = 1.6) this fills w×h exactly.  Returns nullptr on any failure
// (missing file, parse error, OOM) so callers can fall back to code-draw.
SDL_Texture *RasterizeSvgFile(SDL_Renderer *r, const std::string &path,
                              int w, int h);

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
