// qd_Cursor.hpp — Q OS desktop cursor element for uMenu C++ (v3.0.0).
//
// Design: "Liquid Glass Bubble v3" (Option C refined, Q OS brand palette).
//   • 18 px outer radius on a 44×44 ABGR8888 texture (pre-built once).
//   • Body fill: cyan #00E5FF at alpha=110 (~43% opaque — glass, but visible).
//   • Outline: fully opaque cyan #00E5FF at alpha=255 (crisp 1 px boundary).
//   • Anti-alias halo: radius+1 px at alpha=80 (soft outer bleed).
//   • Centre crosshair: 5 px black filled disc + 2 px white inner dot on top.
//     Result: 2 px white tip ringed by 3 px of black — readable on any bg.
//   • No upper-left highlight (removed — canvas too small at 18 px radius).
//   • Hot-spot at texture centre (22, 22): blit at (cx - 22, cy - 22).
//
// Brand colours (Q OS, hardcoded — no matching token in QdTheme):
//   Cyan    #00E5FF   (r=0x00, g=0xE5, b=0xFF)
//   Magenta #D946EF   (r=0xD9, g=0x46, b=0xEF)  — unused in Option C
//   Lavender #A78BFA  (r=0xA7, g=0x8B, b=0xFA)  — unused in Option C
//
// API surface is identical to v1.0.0 (same public method signatures) so
// qd_Input.cpp and ui_MainMenuLayout.cpp require zero changes.
//
// romfs:/ui/Main/OverIcon/Cursor.png is no longer loaded; do not delete that
// file (it is out of scope for this element).
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <SDL2/SDL.h>
#include <memory>

namespace ul::menu::qdesktop {

// ── Screen layout constants ──────────────────────────────────────────────────
// Full 1920×1080 layout space that all qdesktop elements share.
static constexpr s32 CURSOR_SCREEN_W = 1920;
static constexpr s32 CURSOR_SCREEN_H = 1080;

// ── Programmatic cursor geometry ─────────────────────────────────────────────
// Texture is 44×44 (18 px radius + 4 px margin on each side for AA bleed).
// Texture centre = (22, 22); hot-spot blit offset = (cx - 22, cy - 22).
static constexpr s32 CURSOR_TEX_SIZE        = 44;   // pixel dimensions of the SDL texture
static constexpr s32 CURSOR_TEX_CENTRE      = 22;   // texture hot-spot coord (TEX_SIZE / 2)
static constexpr s32 CURSOR_RADIUS          = 18;   // outer radius of the bubble in pixels
static constexpr s32 CURSOR_DOT_OUTER_RADIUS = 5;   // black outer disc of centre crosshair
static constexpr s32 CURSOR_DOT_INNER_RADIUS = 2;   // white inner dot of centre crosshair

// ── QdCursorElement ──────────────────────────────────────────────────────────

/// Plutonium Element that renders the "Liquid Glass Bubble" programmatic cursor.
/// The element covers the full 1920×1080 screen so Plutonium routes all
/// OnInput calls to it, but it only draws in the tiny cursor region each frame.
class QdCursorElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdCursorElement>;

    /// Factory — preferred construction path.
    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdCursorElement>(theme);
    }

    /// Constructs the element, positions the cursor at screen centre (960, 540),
    /// and immediately pre-builds the cursor SDL_Texture via SDL pixel compositing.
    explicit QdCursorElement(const QdTheme &theme);

    /// Destroys the pre-built SDL_Texture.
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

    /// Blits the pre-built cursor texture at
    /// (current_x_ - CURSOR_TEX_CENTRE, current_y_ - CURSOR_TEX_CENTRE)
    /// so the centre dot lands on the logical cursor position.
    /// Immediately returns when visible_ is false.
    ///
    /// @param drawer  Plutonium renderer reference — unused; we call
    ///                pu::ui::render::GetMainRenderer() directly (same pattern
    ///                as qd_Wallpaper::OnRender).
    /// @param x       Layout-injected origin X — intentionally ignored.
    /// @param y       Layout-injected origin Y — intentionally ignored.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    /// Updates cursor position from a Plutonium TouchPoint.
    /// touch_pos.IsEmpty() returns true (x<0 && y<0) when no finger is down;
    /// the cursor stays at its last position in that case.
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public setters / getters ─────────────────────────────────────────────

    /// Explicitly moves the cursor to (x, y) in 1920×1080 layout space.
    /// Out-of-bounds values are ignored.
    void SetCursorPos(s32 x, s32 y);

    /// Returns the current cursor X in 1920×1080 layout space.
    s32 GetCursorX() const { return current_x_; }

    /// Returns the current cursor Y in 1920×1080 layout space.
    s32 GetCursorY() const { return current_y_; }

    /// Shows or hides the cursor sprite.
    void SetVisible(bool v) { visible_ = v; }

private:
    /// Builds the 44×44 ABGR8888 cursor texture into cursor_tex_.
    /// Called once from the constructor.  Writes every pixel directly via
    /// SDL_LockTexture / SDL_UnlockTexture.
    void BuildCursorTexture(SDL_Renderer *r);

    QdTheme           theme_;
    /// Pre-built SDL_Texture (44×44 ABGR8888) built at construction time.
    SDL_Texture      *cursor_tex_;
    /// Current cursor position in 1920×1080 layout space.
    s32               current_x_;
    s32               current_y_;
    /// Whether the cursor should be drawn each frame.
    bool              visible_;
};

} // namespace ul::menu::qdesktop
