// qd_ImageWallpaperElement.hpp — PNG/JPEG wallpaper element for Q OS qdesktop.
// Phase A of the theming-absorption arc (v2.2.x / v2.3.0).
//
// Loads a static image from a runtime-configurable path (typically
// sdmc:/ulaunch/cache/active/ui/Background.png after CacheActiveTheme()).
// Blits the image full-screen (0, 0, 1920, 1080) every frame.
//
// Texture lifecycle:
//   - Loaded lazily on first OnRender() call.
//   - Source image is scaled DOWN to WP_W×WP_H (1280×720) at load time via
//     SDL_BlitScaled before texture upload.  This keeps VRAM at ~3.5 MB —
//     the same budget as QdWallpaperElement — instead of ~7.9 MB for a raw
//     1920×1080 source.
//   - Freed in the destructor via SDL_DestroyTexture.
//
// Coexistence: this element is a SIBLING of QdWallpaperElement (procedural).
// Layouts opt in by instantiating this class instead of (or in addition to)
// QdWallpaperElement.  Do NOT delete QdWallpaperElement.
//
// Phase B wire-up (future): StartupMenuLayout will swap the procedural element
// for this one when Background.png is present in the active theme cache.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Wallpaper.hpp>  // WP_W, WP_H, WP_BLIT_W, WP_BLIT_H
#include <string>

namespace ul::menu::qdesktop {

class QdImageWallpaperElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdImageWallpaperElement>;

    // Construct with the path to the wallpaper image.
    // Typical value: ul::cfg::GetActiveThemeResource("ui/Background.png")
    //   which resolves to "sdmc:/ulaunch/cache/active/ui/Background.png".
    // Any SDL_image-readable format is accepted (PNG, JPEG, BMP, WebP).
    static Ref New(const std::string &image_path) {
        return std::make_shared<QdImageWallpaperElement>(image_path);
    }

    explicit QdImageWallpaperElement(const std::string &image_path);
    ~QdImageWallpaperElement();

    // ── pu::ui::elm::Element interface ──────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return static_cast<s32>(WP_BLIT_W); }
    s32 GetHeight() override { return static_cast<s32>(WP_BLIT_H); }

    // First call: load image, scale to 1280×720, upload to SDL_Texture.
    // Subsequent calls: blit cached texture full-screen.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    // No interactive input — wallpaper is a passive background element.
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}

    // Whether the image loaded successfully.  False before first OnRender,
    // or if the file was missing / could not be decoded.
    bool IsLoaded() const { return loaded_ok_; }

private:
    std::string       image_path_;
    SDL_Texture      *cached_tex_;   // nullptr until first render
    bool              rendered_;     // true after the first OnRender attempt
    bool              loaded_ok_;    // true if the texture was created
};

} // namespace ul::menu::qdesktop
