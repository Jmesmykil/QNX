// qd_PowerButton.hpp -- Single power-action button for the Q OS login screen.
// Renders a pill-shaped 180x64 element: PNG glyph icon + label on a translucent
// panel, focus ring when selected, 50% alpha when disabled.
// Glyph icons load from ui/Main/PowerIcon/<Restart|Shutdown|Sleep|Hekate>.png.
// All rendering via SDL2 primitives + Plutonium TextureHandle.
// Owned file: do not edit from other agents.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <functional>
#include <string>

namespace ul::menu::qdesktop {

class QdPowerButtonElement : public pu::ui::elm::Element {
public:
    using Ref      = std::shared_ptr<QdPowerButtonElement>;
    using OnClickFn = std::function<void()>;

    enum class Kind : u8 { Restart, Shutdown, Sleep, Hekate, Custom };

    static Ref New(const QdTheme &theme, Kind kind, const std::string &label);

    QdPowerButtonElement(const QdTheme &theme, Kind kind, const std::string &label);
    ~QdPowerButtonElement();

    // ── pu::ui::elm::Element interface ────────────────────────────────────────
    s32 GetX() override { return x_; }
    s32 GetY() override { return y_; }
    s32 GetWidth()  override { return BTN_W; }
    s32 GetHeight() override { return BTN_H; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 origin_x, const s32 origin_y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Mutators ───────────────────────────────────────────────────────────────
    void SetPos(s32 x, s32 y);
    void SetOnClick(OnClickFn cb);
    void SetEnabled(bool e);
    void SetFocused(bool f);

    // ── Button dimensions (constants) ─────────────────────────────────────────
    static constexpr s32 BTN_W = 180;
    static constexpr s32 BTN_H = 64;

private:
    // Returns the PNG asset basename for each Kind (e.g. "Restart", "Hekate").
    // Custom returns "Custom" but ships no default asset; OnRender skips draw
    // when the lazy-loaded handle is null, so the panel still renders.
    static const char *GlyphAssetName(Kind k);

    // Lazy-load the glyph texture from ui/Main/PowerIcon/<name>.png via
    // TryFindLoadImageHandle. Sets glyph_tex_handle_ to a wrapping
    // TextureHandle::Ref; the inner SDL_Texture* may still be nullptr if the
    // asset is missing (graceful degrade).
    void EnsureGlyphTexture();

    // ── Lazy-load the label texture (Medium font, theme.text_primary) ─────────
    void EnsureLabelTexture();

    // ── Free both cached textures ─────────────────────────────────────────────
    void FreeTextures();

    // ── Fields ────────────────────────────────────────────────────────────────
    QdTheme       theme_;
    Kind          kind_;
    std::string   label_;

    s32           x_;
    s32           y_;
    bool          enabled_;
    bool          focused_;

    OnClickFn     on_click_;

    // Cached glyph texture handle (managed; nullptr inner = asset missing).
    pu::sdl2::TextureHandle::Ref glyph_tex_handle_;
    // Cached label text texture (nullptr = not yet rendered, raw owned by us).
    SDL_Texture  *label_tex_;   // label string, theme.text_primary, Medium

    // Touch click-vs-drag state — same pattern as QdDesktopIconsElement.
    bool          press_inside_;   // set on TouchDown inside rect, cleared on drag out
    s32           down_x_;
    s32           down_y_;
};

} // namespace ul::menu::qdesktop
