// qd_Cursor.hpp — Q OS desktop cursor element for uMenu C++ (v3.0.0).
//
// Design: "Liquid Glass Bubble v3" (Option C refined, Q OS brand palette).
//   • 18 px outer radius on a 44×44 ABGR8888 texture (pre-built once).
//   • Body fill: theme cursor_fill at alpha=110 (~43% opaque — glass, but visible).
//   • Outline: theme cursor_outline at alpha=255 (crisp 1 px boundary).
//   • Anti-alias halo: radius+1 px at alpha=80 (soft outer bleed).
//   • Centre crosshair: 5 px black filled disc + 2 px white inner dot on top.
//     Result: 2 px white tip ringed by 3 px of black — readable on any bg.
//   • No upper-left highlight (removed — canvas too small at 18 px radius).
//   • Hot-spot at texture centre (22, 22): blit at (cx - 22, cy - 22).
//
// Colours are THEME-DRIVEN (v3.6.x): body = g_QdTheme.cursor_fill,
// outline = g_QdTheme.cursor_outline, and a right-click variant uses
// g_QdTheme.cursor_right_click.  The textures rebuild when g_cursor_dirty is
// set (on palette/theme change) — see qd_Theme.cpp::SetActivePalettePack.
//
// API surface is identical to v1.0.0 (same public method signatures) so
// qd_Input.cpp and ui_MainMenuLayout.cpp require zero changes.
//
// Cursor is drawn procedurally via SDL_RenderFillRect; no PNG asset is loaded.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <SDL2/SDL.h>
#include <memory>
#include <atomic>

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

// Set true whenever the active palette (g_QdTheme) changes, so the cursor
// rebuilds its textures with the new theme colours.  Mirrors g_wallpaper_dirty
// (qd_Wallpaper.hpp); consumed in QdCursorElement::OnRender.
extern std::atomic<bool> g_cursor_dirty;

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
    /// Builds BOTH cursor textures from the live theme (g_QdTheme):
    /// cursor_tex_ uses cursor_fill, cursor_tex_rclick_ uses cursor_right_click;
    /// both share cursor_outline + the centre marker.  Called from the ctor and
    /// re-called in OnRender when g_cursor_dirty is consumed.
    void BuildCursorTexture(SDL_Renderer *r);

    /// Builds one 44×44 ABGR8888 cursor texture with the given body + ring
    /// colours.  The centre core (cursor_outline disc + g_QdTheme.accent dot) and
    /// the contrast-survival light edge are theme-driven per CURSOR-SPEC.  When
    /// is_right_click is true the outline is the right-click colour and a
    /// right-click badge disc is added at lower-right.  Returns nullptr on failure.
    SDL_Texture *BuildOneCursorTexture(SDL_Renderer *r,
                                       pu::ui::Color body,
                                       pu::ui::Color outline,
                                       bool is_right_click);

    QdTheme           theme_;  // captured at ctor; live colours come from g_QdTheme
    /// Pre-built normal cursor texture (44×44 ABGR8888); body = cursor_fill.
    SDL_Texture      *cursor_tex_;
    /// Pre-built right-click cursor texture; body = cursor_right_click.
    SDL_Texture      *cursor_tex_rclick_;
    /// Current cursor position in 1920×1080 layout space.
    s32               current_x_;
    s32               current_y_;
    /// Whether the cursor should be drawn each frame.
    bool              visible_;
    /// True while the secondary (right-click / ZL) button is held — selects the
    /// right-click cursor texture in OnRender.
    bool              right_click_active_;
};

} // namespace ul::menu::qdesktop
