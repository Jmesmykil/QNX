// qd_ImageViewer.cpp — QdImageViewer implementation.
// Full-screen image viewer for Q OS qdesktop Stage 3.
//
// Load pipeline:
//   1. fopen + fseek/ftell to get file size.
//   2. Reject if size > MAX_SOURCE_BYTES (16 MiB).
//   3. fread all bytes into a heap buffer.
//   4. SDL_RWFromConstMem → IMG_Load_RW(rw, freesrc=1) → SDL_Surface.
//   5. SDL_ConvertSurfaceFormat(RGBA8888) → ABGR8888 (byte-order fix, same as
//      qd_UserCard pattern).
//   6. SDL_CreateTexture(STATIC) + SDL_UpdateTexture.
//   7. Store width/height; free surfaces.
//
// Rendering:
//   - Full black background.
//   - Fit-to-screen base rect (aspect-preserved, centred) at zoom=1.0.
//   - At zoom > 1.0 the dst rect grows and pan offset is applied.
//   - Top-left info overlay: filename + WxH in Small accent-coloured font.
//
// Input:
//   - B → Close.
//   - ZR / + → zoom in ×1.5 (clamped to ZOOM_MAX).
//   - ZL / − → zoom out ×0.67 (clamped to ZOOM_MIN).
//   - D-pad Up/Down/Left/Right → pan when zoom > 1.0.

#include <ul/menu/qdesktop/qd_ImageViewer.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ul::menu::qdesktop {

// ── Factory ───────────────────────────────────────────────────────────────────

/*static*/
QdImageViewer::Ref QdImageViewer::New(const QdTheme &theme) {
    return std::make_shared<QdImageViewer>(theme);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdImageViewer::QdImageViewer(const QdTheme &theme)
    : theme_(theme)
{
    UL_LOG_INFO("qdesktop: QdImageViewer ctor");
}

QdImageViewer::~QdImageViewer() {
    FreeTexture();
    FreeInfoTex();
}

// ── FreeTexture / FreeInfoTex ─────────────────────────────────────────────────

void QdImageViewer::FreeTexture() {
    if (img_tex_ != nullptr) {
        SDL_DestroyTexture(img_tex_);
        img_tex_ = nullptr;
    }
    img_w_ = 0;
    img_h_ = 0;
}

void QdImageViewer::FreeInfoTex() {
    if (info_tex_ != nullptr) {
        SDL_DestroyTexture(info_tex_);
        info_tex_ = nullptr;
    }
}

// ── Close ────────────────────────────────────────────────────────────────────

void QdImageViewer::Close() {
    open_     = false;
    zoom_     = 1.0f;
    pan_x_    = 0;
    pan_y_    = 0;
    FreeTexture();
    FreeInfoTex();
    filename_.clear();
    UL_LOG_INFO("qdesktop: QdImageViewer closed");
}

// ── LoadFile ─────────────────────────────────────────────────────────────────

bool QdImageViewer::LoadFile(const char *path) {
    // Tear down previous state.
    FreeTexture();
    FreeInfoTex();
    filename_.clear();
    open_  = false;
    zoom_  = 1.0f;
    pan_x_ = 0;
    pan_y_ = 0;

    if (path == nullptr || path[0] == '\0') {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: null/empty path");
        return false;
    }

    // Derive basename for the overlay.
    {
        const char *slash = strrchr(path, '/');
        filename_ = (slash != nullptr) ? (slash + 1) : path;
    }

    // ── Size check ────────────────────────────────────────────────────────────
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: fopen failed '%s'", path);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: fseek failed '%s'", path);
        fclose(f);
        return false;
    }
    const long file_size_l = ftell(f);
    if (file_size_l < 0) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: ftell failed '%s'", path);
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    const size_t file_size = static_cast<size_t>(file_size_l);
    if (file_size > MAX_SOURCE_BYTES) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: file too large (%zu > %zu) '%s'",
                    file_size, MAX_SOURCE_BYTES, path);
        fclose(f);
        // Not setting open_=true — caller should display a toast.
        return false;
    }

    // ── Read all bytes into heap buffer ───────────────────────────────────────
    std::vector<u8> buf(file_size);
    const size_t got = fread(buf.data(), 1, file_size, f);
    fclose(f);

    if (got != file_size) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: short read %zu/%zu '%s'",
                    got, file_size, path);
        return false;
    }

    // ── Decode via SDL_image ──────────────────────────────────────────────────
    SDL_RWops *rw = SDL_RWFromConstMem(buf.data(), static_cast<int>(got));
    if (rw == nullptr) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: SDL_RWFromConstMem failed: %s",
                    SDL_GetError());
        return false;
    }

    // IMG_Load_RW with freesrc=1 frees rw on return regardless of success/failure.
    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);
    if (raw == nullptr) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: IMG_Load_RW failed: %s",
                    IMG_GetError());
        return false;
    }

    const int src_w = raw->w;
    const int src_h = raw->h;

    // ── Normalise pixel format (RGBA8888 → ABGR8888) ─────────────────────────
    // Two-step convert: matches the qd_UserCard pattern for byte-order on AArch64 LE.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);
    if (rgba == nullptr) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: RGBA8888 convert failed: %s",
                    SDL_GetError());
        return false;
    }

    SDL_Surface *abgr = SDL_ConvertSurfaceFormat(rgba, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rgba);
    if (abgr == nullptr) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: ABGR8888 convert failed: %s",
                    SDL_GetError());
        return false;
    }

    if (SDL_LockSurface(abgr) != 0) {
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: SDL_LockSurface failed: %s",
                    SDL_GetError());
        SDL_FreeSurface(abgr);
        return false;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        SDL_UnlockSurface(abgr);
        SDL_FreeSurface(abgr);
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: no renderer");
        return false;
    }

    SDL_Texture *tex = SDL_CreateTexture(r,
                                          SDL_PIXELFORMAT_ABGR8888,
                                          SDL_TEXTUREACCESS_STATIC,
                                          src_w, src_h);
    if (tex == nullptr) {
        SDL_UnlockSurface(abgr);
        SDL_FreeSurface(abgr);
        UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: SDL_CreateTexture failed: %s",
                    SDL_GetError());
        return false;
    }

    SDL_UpdateTexture(tex, nullptr, abgr->pixels, abgr->pitch);
    SDL_UnlockSurface(abgr);
    SDL_FreeSurface(abgr);

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    img_tex_ = tex;
    img_w_   = src_w;
    img_h_   = src_h;
    open_    = true;

    UL_LOG_INFO("qdesktop: QdImageViewer::LoadFile: ok '%s' %dx%d %zu bytes",
                filename_.c_str(), img_w_, img_h_, got);
    return true;
}

// ── EnsureInfoTex ─────────────────────────────────────────────────────────────

void QdImageViewer::EnsureInfoTex() {
    if (info_tex_ != nullptr) {
        return;
    }
    if (filename_.empty()) {
        return;
    }

    char info_buf[256];
    snprintf(info_buf, sizeof(info_buf), "%s  %dx%d  [B] Close  [ZR/+] Zoom In  [ZL/-] Zoom Out  [D-pad] Pan",
             filename_.c_str(), img_w_, img_h_);

    info_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string(info_buf),
        theme_.accent,
        static_cast<u32>(VIEWER_W - 16));
}

// ── ComputeBaseFitRect ────────────────────────────────────────────────────────
// Returns a rect that fits the image into VIEWER_W × VIEWER_H with aspect
// ratio preserved, centred on the screen, at zoom=1.0 / pan=(0,0).

SDL_Rect QdImageViewer::ComputeBaseFitRect() const {
    if (img_w_ <= 0 || img_h_ <= 0) {
        return SDL_Rect { 0, 0, VIEWER_W, VIEWER_H };
    }

    const float scale_x = static_cast<float>(VIEWER_W) / static_cast<float>(img_w_);
    const float scale_y = static_cast<float>(VIEWER_H) / static_cast<float>(img_h_);
    const float scale   = std::min(scale_x, scale_y);

    const int fit_w = static_cast<int>(static_cast<float>(img_w_) * scale);
    const int fit_h = static_cast<int>(static_cast<float>(img_h_) * scale);

    const int fit_x = (VIEWER_W - fit_w) / 2;
    const int fit_y = (VIEWER_H - fit_h) / 2;

    return SDL_Rect { fit_x, fit_y, fit_w, fit_h };
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdImageViewer::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                              const s32 origin_x, const s32 origin_y)
{
    if (!open_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    // ── 1. Full black background (letterbox) ──────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xFFu);
    SDL_Rect bg { origin_x, origin_y, VIEWER_W, VIEWER_H };
    SDL_RenderFillRect(r, &bg);

    // ── 2. Image: compute zoomed + panned dst rect ────────────────────────────
    if (img_tex_ != nullptr) {
        const SDL_Rect base = ComputeBaseFitRect();

        // Apply zoom around the screen centre.
        const float zoomed_w = static_cast<float>(base.w) * zoom_;
        const float zoomed_h = static_cast<float>(base.h) * zoom_;

        // Centre of base fit rect in screen space.
        const float base_cx = static_cast<float>(base.x) + static_cast<float>(base.w) * 0.5f;
        const float base_cy = static_cast<float>(base.y) + static_cast<float>(base.h) * 0.5f;

        s32 dst_x = static_cast<s32>(base_cx - zoomed_w * 0.5f) + pan_x_ + origin_x;
        s32 dst_y = static_cast<s32>(base_cy - zoomed_h * 0.5f) + pan_y_ + origin_y;
        s32 dst_w = static_cast<s32>(zoomed_w);
        s32 dst_h = static_cast<s32>(zoomed_h);

        if (dst_w > 0 && dst_h > 0) {
            SDL_Rect dst { dst_x, dst_y, dst_w, dst_h };
            SDL_RenderCopy(r, img_tex_, nullptr, &dst);
        }
    }

    // ── 3. Info overlay (top-left) ────────────────────────────────────────────
    EnsureInfoTex();
    if (info_tex_ != nullptr) {
        // Semi-transparent background strip for readability.
        int iw = 0, ih = 0;
        SDL_QueryTexture(info_tex_, nullptr, nullptr, &iw, &ih);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xA0u);
        SDL_Rect info_bg { origin_x, origin_y, iw + 16, ih + 8 };
        SDL_RenderFillRect(r, &info_bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        SDL_Rect info_dst { origin_x + 8, origin_y + 4, iw, ih };
        SDL_RenderCopy(r, info_tex_, nullptr, &info_dst);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdImageViewer::OnInput(const u64 keys_down,
                             const u64 /*keys_up*/,
                             const u64 /*keys_held*/,
                             const pu::ui::TouchPoint /*touch_pos*/)
{
    if (!open_) {
        return;
    }

    // B → close.
    if (keys_down & HidNpadButton_B) {
        Close();
        return;
    }

    // ZR or Plus → zoom in.
    if ((keys_down & HidNpadButton_ZR) || (keys_down & HidNpadButton_Plus)) {
        zoom_ = std::min(ZOOM_MAX, zoom_ * ZOOM_IN_FACTOR);
        // Invalidate info tex so zoom level changes don't misalign it
        // (it doesn't show zoom; just re-anchor pan to avoid drift).
        // No change needed to info_tex_ — it doesn't include zoom value.
        return;
    }

    // ZL or Minus → zoom out; reset pan when back at base zoom.
    if ((keys_down & HidNpadButton_ZL) || (keys_down & HidNpadButton_Minus)) {
        zoom_ = std::max(ZOOM_MIN, zoom_ * ZOOM_OUT_FACTOR);
        if (zoom_ <= 1.0f + 1e-3f) {
            // Snapped back to fit; reset pan.
            zoom_  = 1.0f;
            pan_x_ = 0;
            pan_y_ = 0;
        }
        return;
    }

    // D-pad pan — only meaningful when zoomed in.
    if (zoom_ > 1.0f + 1e-3f) {
        const s32 step = static_cast<s32>(static_cast<float>(PAN_STEP) * zoom_);
        if (keys_down & HidNpadButton_Up)    { pan_y_ -= step; }
        if (keys_down & HidNpadButton_Down)  { pan_y_ += step; }
        if (keys_down & HidNpadButton_Left)  { pan_x_ -= step; }
        if (keys_down & HidNpadButton_Right) { pan_x_ += step; }
    }
}

} // namespace ul::menu::qdesktop
