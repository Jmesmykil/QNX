// qd_Cursor.cpp — Mouse/touch cursor sprite element for uMenu C++ (v1.0.0).
//
// Texture-load path (confirmed from sibling files):
//   pu::ui::render::LoadImageFromFile("romfs:/ui/Main/OverIcon/Cursor.png")
//   — same pattern as ui_Common.cpp:123 (LoadImageFromFile("romfs:/Logo.png"))
//     and TryFindLoadImage() in ui_Common.cpp:111.
//
// Size-query path (confirmed from render_SDL2.hpp):
//   pu::ui::render::GetTextureWidth(tex) / GetTextureHeight(tex)
//
// Blit pattern (confirmed from qd_Wallpaper.cpp:368-376):
//   SDL_Renderer *r = pu::ui::render::GetMainRenderer();
//   SDL_Rect dst = { x, y, w, h };
//   SDL_RenderCopy(r, tex, nullptr, &dst);
//
// Fallback (texture load failure):
//   SDL_SetRenderDrawColor + SDL_RenderFillRect draws a visible
//   filled square so the cursor is never silently absent.

#include <ul/menu/qdesktop/qd_Cursor.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ─────────────────────────────────────────────────

QdCursorElement::QdCursorElement(const QdTheme &theme)
    : theme_(theme),
      cursor_tex_(nullptr),
      current_x_(960),   // screen centre X (1920 / 2)
      current_y_(540),   // screen centre Y (1080 / 2)
      visible_(true),
      sprite_w_(CURSOR_SPRITE_DEFAULT),
      sprite_h_(CURSOR_SPRITE_DEFAULT)
{
}

QdCursorElement::~QdCursorElement() {
    if (cursor_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(cursor_tex_);
        cursor_tex_ = nullptr;
    }
}

// ── SetCursorPos ─────────────────────────────────────────────────────────────

void QdCursorElement::SetCursorPos(s32 x, s32 y) {
    // Clamp to valid 1920×1080 layout space; ignore out-of-bounds silently.
    if (x < 0 || x >= CURSOR_SCREEN_W) {
        return;
    }
    if (y < 0 || y >= CURSOR_SCREEN_H) {
        return;
    }
    current_x_ = x;
    current_y_ = y;
}

// ── OnInput ──────────────────────────────────────────────────────────────────

void QdCursorElement::OnInput(const u64 keys_down, const u64 keys_up,
                               const u64 keys_held,
                               const pu::ui::TouchPoint touch_pos) {
    // Suppress -Wunused-parameter without naming the params we don't use.
    (void)keys_down;
    (void)keys_up;
    (void)keys_held;

    // TouchPoint::IsEmpty() returns true when x<0 && y<0 (Plutonium convention
    // for "no finger on screen" — see pu/ui/ui_Types.hpp:140-142).
    // Plutonium delivers coords already in 1920×1080 layout space; no manual
    // scale factor is needed here.  The raw HID remap in qd_Input.cpp is a
    // separate code path and does not feed through this Element.
    if (!touch_pos.IsEmpty()) {
        SetCursorPos(touch_pos.x, touch_pos.y);
    }
}

// ── OnRender ─────────────────────────────────────────────────────────────────

void QdCursorElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                const s32 /*x*/, const s32 /*y*/) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        UL_LOG_WARN("qdesktop: Cursor OnRender got NULL main renderer");
        return;
    }

    // ── Lazy texture load (runs exactly once on the first successful renderer
    //    call, then never again for the lifetime of this element) ────────────
    if (cursor_tex_ == nullptr) {
        // LoadImageFromFile is the authoritative romfs-loading path used by
        // Plutonium; see ui_Common.cpp:123 and TryFindLoadImage (same file).
        // The romfs:/ prefix is required — the VFS is mounted by main.cpp:317
        // (romfsMountFromFsdev(ul::MenuRomfsFile, 0, "romfs")).
        cursor_tex_ = pu::ui::render::LoadImageFromFile(
            "romfs:/ui/Main/OverIcon/Cursor.png");

        if (cursor_tex_ != nullptr) {
            // Query intrinsic sprite dimensions so any replacement asset
            // renders at its own natural size.
            const s32 qw = pu::ui::render::GetTextureWidth(cursor_tex_);
            const s32 qh = pu::ui::render::GetTextureHeight(cursor_tex_);
            if (qw > 0 && qh > 0) {
                sprite_w_ = qw;
                sprite_h_ = qh;
            }
            // If GetTextureWidth/Height return 0 we keep CURSOR_SPRITE_DEFAULT.
            UL_LOG_INFO("qdesktop: Cursor texture loaded %dx%d",
                        sprite_w_, sprite_h_);
        } else {
            // Cursor.png failed to load (missing romfs entry or corrupt file).
            // Log and continue — the fallback filled rectangle below will draw
            // a visible indicator so the cursor is never silently absent.
            UL_LOG_WARN("qdesktop: Cursor texture load failed for "
                        "romfs:/ui/Main/OverIcon/Cursor.png — "
                        "rendering fallback rectangle");
        }
    }

    // Do not draw when hidden, regardless of whether the texture loaded.
    if (!visible_) {
        return;
    }

    if (cursor_tex_ != nullptr) {
        // ── Happy path: blit the PNG sprite ─────────────────────────────────
        // Matches the SDL_RenderCopy pattern from qd_Wallpaper.cpp:371-376.
        SDL_Rect dst;
        dst.x = current_x_;
        dst.y = current_y_;
        dst.w = sprite_w_;
        dst.h = sprite_h_;
        SDL_RenderCopy(r, cursor_tex_, nullptr, &dst);
    } else {
        // ── Fallback path: draw a filled square in theme cursor_fill color ──
        // Ensures the cursor is always visible even if Cursor.png is absent.
        // Uses theme_.cursor_fill (0xF5,0xF5,0xFF) with a dark outline ring
        // via theme_.cursor_outline (0x05,0x05,0x10) for contrast on any bg.
        const s32 fw = sprite_w_;
        const s32 fh = sprite_h_;

        // Outer outline rectangle.
        SDL_Rect outline_rect;
        outline_rect.x = current_x_;
        outline_rect.y = current_y_;
        outline_rect.w = fw;
        outline_rect.h = fh;
        SDL_SetRenderDrawColor(r,
                               theme_.cursor_outline.r,
                               theme_.cursor_outline.g,
                               theme_.cursor_outline.b,
                               0xFF);
        SDL_RenderFillRect(r, &outline_rect);

        // Inner fill rectangle (2 px inset on each side).
        SDL_Rect fill_rect;
        fill_rect.x = current_x_ + 2;
        fill_rect.y = current_y_ + 2;
        fill_rect.w = fw - 4;
        fill_rect.h = fh - 4;
        SDL_SetRenderDrawColor(r,
                               theme_.cursor_fill.r,
                               theme_.cursor_fill.g,
                               theme_.cursor_fill.b,
                               0xFF);
        SDL_RenderFillRect(r, &fill_rect);
    }
}

} // namespace ul::menu::qdesktop
