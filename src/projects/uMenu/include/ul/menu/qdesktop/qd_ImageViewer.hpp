// qd_ImageViewer.hpp — Full-screen image viewer overlay for Q OS qdesktop.
// Supports any SDL_image-readable format: PNG, JPEG, BMP, GIF (first frame only).
// Memory bound: rejects source files > 16 MiB with a logged error (no texture load).
// Rendering: fit-to-screen with aspect ratio preserved, black letterbox.
//            Top-left overlay shows filename + WxH dimensions.
// Zoom:      ZR/+ zoom in (×1.5); ZL/− zoom out (×0.67).
//            D-pad pans when zoom > 1.0.
// B closes via Close().
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <string>

namespace ul::menu::qdesktop {

class QdImageViewer : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdImageViewer>;

    // Viewer dimensions — always full screen.
    static constexpr s32 VIEWER_W = 1920;
    static constexpr s32 VIEWER_H = 1080;

    // Source-file size cap in bytes.
    static constexpr size_t MAX_SOURCE_BYTES = 16u * 1024u * 1024u;  // 16 MiB

    // Zoom bounds.
    static constexpr float ZOOM_MIN = 0.10f;
    static constexpr float ZOOM_MAX = 8.0f;
    static constexpr float ZOOM_IN_FACTOR  = 1.5f;
    static constexpr float ZOOM_OUT_FACTOR = 0.67f;

    // Pan speed in pixels per button press (at zoom=1.0; scaled by zoom).
    static constexpr s32 PAN_STEP = 40;

    static Ref New(const QdTheme &theme);

    QdImageViewer(const QdTheme &theme);
    ~QdImageViewer();

    // Load image at path. Returns true on success. Frees previous image.
    // Rejects files larger than MAX_SOURCE_BYTES.
    bool LoadFile(const char *path);

    // Close the viewer — frees texture, resets state.
    void Close();

    // Whether the viewer is currently visible / open.
    bool IsOpen() const { return open_; }

    // ── pu::ui::elm::Element interface ───────────────────────────────────────
    s32 GetX() override { return 0; }
    s32 GetY() override { return 0; }
    s32 GetWidth()  override { return VIEWER_W; }
    s32 GetHeight() override { return VIEWER_H; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 origin_x, const s32 origin_y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

private:
    // Free img_tex_ if allocated.
    void FreeTexture();

    // Free info_tex_ if allocated.
    void FreeInfoTex();

    // Build the cached info overlay texture (filename + WxH).
    void EnsureInfoTex();

    // Compute the fit-to-screen base destination rect (aspect-preserved, centred).
    // Returns the rect at zoom=1.0 / pan=(0,0).
    SDL_Rect ComputeBaseFitRect() const;

    QdTheme     theme_;
    bool        open_        = false;
    std::string filename_;           // basename for display
    int         img_w_       = 0;    // decoded image width  (pixels)
    int         img_h_       = 0;    // decoded image height (pixels)
    SDL_Texture *img_tex_    = nullptr;  // decoded image texture (STATIC)
    SDL_Texture *info_tex_   = nullptr;  // filename + size overlay

    // View state.
    float zoom_   = 1.0f;
    s32   pan_x_  = 0;   // offset in screen pixels (positive = right)
    s32   pan_y_  = 0;   // offset in screen pixels (positive = down)
};

} // namespace ul::menu::qdesktop
