// qd_SvgRaster.cpp — see qd_SvgRaster.hpp.  This is the single TU that defines
// the nanosvg + nanosvgrast implementations (the IMPLEMENTATION macros must be
// defined in exactly one TU).

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_SvgRaster.hpp>
#include <ul/ul_Result.hpp>   // UL_LOG_*
#include <cstdlib>
#include <cstring>

// SDL2_image (2.0.4) statically bundles its OWN nanosvg, so building our vendored
// copy's IMPLEMENTATION here collides at link time (multiple definition).  Rename
// our copy's exported symbols to a qd_ prefix; our code below still calls the
// canonical names (the #defines redirect them to our renamed impl), and the
// linker keeps SDL2_image's untouched.
#define nsvg__colors          qd_nsvg__colors
#define nsvgParse             qd_nsvgParse
#define nsvgParseFromFile     qd_nsvgParseFromFile
#define nsvgDelete            qd_nsvgDelete
#define nsvgCreateRasterizer  qd_nsvgCreateRasterizer
#define nsvgDeleteRasterizer  qd_nsvgDeleteRasterizer
#define nsvgRasterize         qd_nsvgRasterize
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

namespace ul::menu::qdesktop {

SDL_Texture *RasterizeSvgFile(SDL_Renderer *r, const std::string &path,
                              int w, int h) {
    if (r == nullptr || w <= 0 || h <= 0) {
        return nullptr;
    }

    // nsvgParseFromFile mutates the buffer, so it loads the whole file; romfs:/
    // paths work through libnx's fopen.
    NSVGimage *img = nsvgParseFromFile(path.c_str(), "px", 96.0f);
    if (img == nullptr) {
        UL_LOG_WARN("qdesktop: RasterizeSvgFile: parse failed for %s", path.c_str());
        return nullptr;
    }
    if (img->width <= 0.0f || img->height <= 0.0f) {
        nsvgDelete(img);
        return nullptr;
    }

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (rast == nullptr) {
        nsvgDelete(img);
        return nullptr;
    }

    const size_t stride = static_cast<size_t>(w) * 4u;
    unsigned char *pixels = static_cast<unsigned char *>(std::malloc(stride * static_cast<size_t>(h)));
    if (pixels == nullptr) {
        nsvgDeleteRasterizer(rast);
        nsvgDelete(img);
        return nullptr;
    }
    std::memset(pixels, 0, stride * static_cast<size_t>(h));

    // Uniform scale to the destination width.  Window masters are authored at
    // the window aspect (640×400), so w-based scale fills w×h for those windows.
    const float scale = static_cast<float>(w) / img->width;
    nsvgRasterize(rast, img, 0.0f, 0.0f, scale,
                  pixels, w, h, static_cast<int>(stride));

    // nanosvg writes straight (non-premultiplied) RGBA, byte order [R,G,B,A].
    // SDL_PIXELFORMAT_ABGR8888 on little-endian AArch64 is memory order
    // [R,G,B,A] too (see qd_Cursor.cpp), so the buffer maps 1:1 — no swizzle.
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex != nullptr) {
        SDL_UpdateTexture(tex, nullptr, pixels, static_cast<int>(stride));
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    } else {
        UL_LOG_WARN("qdesktop: RasterizeSvgFile: SDL_CreateTexture failed: %s", SDL_GetError());
    }

    std::free(pixels);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return tex;
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
