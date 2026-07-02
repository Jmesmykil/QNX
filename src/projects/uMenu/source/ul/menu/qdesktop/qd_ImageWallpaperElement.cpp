// qd_ImageWallpaperElement.cpp — PNG/JPEG wallpaper element for Q OS qdesktop.
// Phase A of the theming-absorption arc (v2.2.x / v2.3.0).
//
// Load pipeline (first OnRender):
//   1. IMG_Load(path) → SDL_Surface (any SDL_image-readable format).
//   2. SDL_CreateRGBSurface(WP_W, WP_H) → scaled_surf.
//   3. SDL_BlitScaled(src → scaled_surf) — downscale to 1280×720 to keep
//      VRAM at ~3.5 MB (same budget as QdWallpaperElement).
//   4. SDL_CreateTexture(STATIC, WP_W, WP_H).
//   5. SDL_UpdateTexture from scaled_surf pixel data.
//   6. SDL_FreeSurface both surfaces.
//   7. On every subsequent frame: SDL_RenderCopy texture to full-screen rect.
//
// Error handling: any step failure is logged and renders a blank frame.
// The element never panics — absence of a wallpaper image is graceful.

#include <ul/menu/qdesktop/qd_ImageWallpaperElement.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdImageWallpaperElement::QdImageWallpaperElement(const std::string &image_path)
    : image_path_(image_path),
      cached_tex_(nullptr),
      rendered_(false),
      loaded_ok_(false)
{
    UL_LOG_INFO("qdesktop: QdImageWallpaperElement ctor path='%s'", image_path_.c_str());
}

QdImageWallpaperElement::~QdImageWallpaperElement() {
    if (cached_tex_ != nullptr) {
        SDL_DestroyTexture(cached_tex_);
        cached_tex_ = nullptr;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdImageWallpaperElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                       const s32 /*x*/, const s32 /*y*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        UL_LOG_WARN("qdesktop: QdImageWallpaperElement: NULL renderer");
        return;
    }

    if (!rendered_) {
        rendered_ = true;  // set early so a failure path doesn't retry every frame

        // ── 1. Load source image ─────────────────────────────────────────────
        SDL_Surface *src_surf = IMG_Load(image_path_.c_str());
        if (src_surf == nullptr) {
            UL_LOG_WARN("qdesktop: QdImageWallpaperElement: IMG_Load failed for '%s': %s",
                        image_path_.c_str(), IMG_GetError());
            return;
        }
        UL_LOG_INFO("qdesktop: QdImageWallpaperElement: loaded src %dx%d fmt=%u",
                    src_surf->w, src_surf->h, src_surf->format->format);

        // ── 2. Create a 1280×720 ABGR8888 intermediate surface ──────────────
        // We target ABGR8888 (byte order R,G,B,A on AArch64 LE) to match the
        // format used by QdWallpaperElement and qd_UserCard.
        SDL_Surface *scaled_surf = SDL_CreateRGBSurfaceWithFormat(
            0,
            static_cast<int>(WP_W),
            static_cast<int>(WP_H),
            32,
            SDL_PIXELFORMAT_ABGR8888
        );
        if (scaled_surf == nullptr) {
            UL_LOG_WARN("qdesktop: QdImageWallpaperElement: SDL_CreateRGBSurfaceWithFormat failed: %s",
                        SDL_GetError());
            SDL_FreeSurface(src_surf);
            return;
        }

        // ── 3. Scale source into 1280×720 surface ───────────────────────────
        SDL_Rect dst_rect = { 0, 0, static_cast<int>(WP_W), static_cast<int>(WP_H) };
        if (SDL_BlitScaled(src_surf, nullptr, scaled_surf, &dst_rect) != 0) {
            UL_LOG_WARN("qdesktop: QdImageWallpaperElement: SDL_BlitScaled failed: %s",
                        SDL_GetError());
            SDL_FreeSurface(src_surf);
            SDL_FreeSurface(scaled_surf);
            return;
        }
        SDL_FreeSurface(src_surf);
        src_surf = nullptr;

        // ── 4. Create a STATIC SDL_Texture at 1280×720 ──────────────────────
        cached_tex_ = SDL_CreateTexture(r,
                                        SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STATIC,
                                        static_cast<int>(WP_W),
                                        static_cast<int>(WP_H));
        if (cached_tex_ == nullptr) {
            UL_LOG_WARN("qdesktop: QdImageWallpaperElement: SDL_CreateTexture failed: %s",
                        SDL_GetError());
            SDL_FreeSurface(scaled_surf);
            return;
        }

        // ── 5. Upload pixel data ─────────────────────────────────────────────
        if (SDL_UpdateTexture(cached_tex_,
                              nullptr,
                              scaled_surf->pixels,
                              scaled_surf->pitch) != 0) {
            UL_LOG_WARN("qdesktop: QdImageWallpaperElement: SDL_UpdateTexture failed: %s",
                        SDL_GetError());
            SDL_FreeSurface(scaled_surf);
            SDL_DestroyTexture(cached_tex_);
            cached_tex_ = nullptr;
            return;
        }
        SDL_FreeSurface(scaled_surf);
        scaled_surf = nullptr;

        loaded_ok_ = true;
        UL_LOG_INFO("qdesktop: QdImageWallpaperElement: texture ready %dx%d -> blit %dx%d",
                    static_cast<int>(WP_W), static_cast<int>(WP_H),
                    static_cast<int>(WP_BLIT_W), static_cast<int>(WP_BLIT_H));
    }

    if (cached_tex_ == nullptr) {
        return;
    }

    // ── Blit 1280×720 texture scaled to 1920×1080 full-screen ───────────────
    // Identical pattern to QdWallpaperElement::OnRender — hardware bilinear
    // scale, no per-frame CPU cost.
    SDL_Rect dst;
    dst.x = 0;
    dst.y = 0;
    dst.w = static_cast<int>(WP_BLIT_W);
    dst.h = static_cast<int>(WP_BLIT_H);
    SDL_RenderCopy(r, cached_tex_, nullptr, &dst);
}

} // namespace ul::menu::qdesktop
