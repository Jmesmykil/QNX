// qd_Cursor.hpp — Mouse/touch cursor sprite element for uMenu C++ (v1.0.0).
// Renders romfs:/ui/Main/OverIcon/Cursor.png at the current pointer position.
// Texture is loaded once on first OnRender; subsequent frames blit the cached
// SDL_Texture directly via SDL_RenderCopy.
//
// Position source priority:
//   1. OnInput: Plutonium TouchPoint (1920×1080 layout space, already scaled).
//   2. SetCursorPos: explicit setter driven by the qd_Input touch-coord remap
//      path in the parent layout (MainMenuLayout::OnMenuUpdate).
//
// The cursor renders in absolute screen space; the x/y parameters injected by
// Plutonium's layout engine are intentionally ignored in OnRender.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <memory>

namespace ul::menu::qdesktop {

// ── Screen layout constants ──────────────────────────────────────────────────
// Full 1920×1080 layout space that all qdesktop elements share.
static constexpr s32 CURSOR_SCREEN_W = 1920;
static constexpr s32 CURSOR_SCREEN_H = 1080;

// Default cursor sprite display size.
// Cursor.png was authored at 32×32 in the reference Rust desktop GUI
// (tools/mock-nro-desktop-gui/src/cursor.rs uses a 32-pixel crosshair).
// We use the texture's intrinsic size (queried via SDL_QueryTexture after load)
// and clamp to 32 if the query returns an unexpected value, so any replacement
// sprite of different dimensions will render at its own natural size.
static constexpr s32 CURSOR_SPRITE_DEFAULT = 32;

// ── QdCursorElement ──────────────────────────────────────────────────────────

/// Plutonium Element that blits the cursor sprite at the current pointer
/// position. The element covers the full 1920×1080 screen so Plutonium routes
/// all OnInput calls to it, but it only draws in the tiny cursor-sprite region.
class QdCursorElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdCursorElement>;

    /// Factory — preferred construction path.
    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdCursorElement>(theme);
    }

    /// Constructs the element and positions the cursor at screen centre
    /// (960, 540) so it is on-screen before the first input event arrives.
    explicit QdCursorElement(const QdTheme &theme);

    /// Frees the cached SDL_Texture if it was loaded.
    ~QdCursorElement();

    // ── Element interface ────────────────────────────────────────────────────

    /// Returns the cursor's current X position in 1920×1080 layout space.
    s32 GetX() override { return current_x_; }

    /// Returns the cursor's current Y position in 1920×1080 layout space.
    s32 GetY() override { return current_y_; }

    /// Reports the full screen width so Plutonium routes all input here.
    s32 GetWidth() override  { return CURSOR_SCREEN_W; }

    /// Reports the full screen height so Plutonium routes all input here.
    s32 GetHeight() override { return CURSOR_SCREEN_H; }

    /// On first call: loads Cursor.png from romfs, caches the SDL_Texture, and
    /// queries its intrinsic size into sprite_w_/sprite_h_.  On every call:
    /// blits the cached texture at (current_x_, current_y_) if visible_.
    ///
    /// @param drawer  Plutonium renderer reference — unused (we call
    ///                pu::ui::render::GetMainRenderer() directly, same pattern
    ///                as qd_Wallpaper::OnRender).
    /// @param x       Layout-injected origin X — intentionally ignored; the
    ///                cursor is in absolute screen space.
    /// @param y       Layout-injected origin Y — intentionally ignored.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    /// Updates cursor position from a Plutonium TouchPoint.
    /// touch_pos.IsEmpty() returns true (x<0 && y<0) when no finger is down;
    /// in that case the cursor stays at its last position.
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public setters ───────────────────────────────────────────────────────

    /// Explicitly moves the cursor to (x, y) in 1920×1080 layout space.
    /// Out-of-bounds values (x < 0, x >= 1920, y < 0, y >= 1080) are ignored.
    void SetCursorPos(s32 x, s32 y);

    /// Shows or hides the cursor sprite.  When hidden, OnRender returns
    /// immediately without drawing.
    void SetVisible(bool v) { visible_ = v; }

private:
    QdTheme             theme_;
    /// Cached SDL_Texture loaded from romfs on first OnRender.
    /// nullptr until the first successful load.
    pu::sdl2::Texture   cursor_tex_;
    /// Current cursor position in 1920×1080 layout space.
    s32                 current_x_;
    s32                 current_y_;
    /// Whether the cursor should be drawn each frame.
    bool                visible_;
    /// Intrinsic sprite dimensions, cached after the first successful texture
    /// load via pu::ui::render::GetTextureWidth/Height.
    s32                 sprite_w_;
    s32                 sprite_h_;
};

} // namespace ul::menu::qdesktop
