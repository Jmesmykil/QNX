// qd_NxlinkWindow.hpp — NXLink session panel for Q OS qdesktop.
// 480×260 translucent panel: title, active/inactive state, host info, toggle button.
// Inherits pu::ui::elm::Element (same pattern as QdPowerButtonElement).
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <functional>

namespace ul::menu::qdesktop {

class QdNxlinkWindow : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdNxlinkWindow>;

    static Ref New(const QdTheme &theme);

    explicit QdNxlinkWindow(const QdTheme &theme);
    ~QdNxlinkWindow();

    // ── pu::ui::elm::Element interface ────────────────────────────────────────
    s32 GetX()      override { return x_; }
    s32 GetY()      override { return y_; }
    s32 GetWidth()  override { return PANEL_W; }
    s32 GetHeight() override { return PANEL_H; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 origin_x, const s32 origin_y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Mutators ───────────────────────────────────────────────────────────────
    void SetPos(s32 x, s32 y);

    // ── Panel dimensions ───────────────────────────────────────────────────────
    static constexpr s32 PANEL_W = 480;
    static constexpr s32 PANEL_H = 260;

private:
    // Row layout offsets from panel top-left (all in pixels).
    static constexpr s32 ROW_TITLE_Y    = 16;
    static constexpr s32 ROW_STATE_Y    = 68;
    static constexpr s32 ROW_HOST_Y     = 116;
    static constexpr s32 ROW_BTN_Y      = 186;
    static constexpr s32 ROW_TEXT_X     = 20;
    // Toggle button geometry inside the panel.
    static constexpr s32 BTN_W          = 160;
    static constexpr s32 BTN_H          = 44;
    static constexpr s32 BTN_X_OFF      = 20;  // from panel left

    // Lazy-build each text texture; each returns immediately if already built.
    void EnsureTitleTexture();
    void EnsureStateTexture(bool active);
    void EnsureHostTexture(bool active);
    void EnsureBtnTexture(bool active);
    void FreeTextures();

    // Render the action button at (abs_x + BTN_X_OFF, abs_y + ROW_BTN_Y).
    void RenderButton(SDL_Renderer *r, s32 abs_x, s32 abs_y, bool active);

    // Execute the toggle: enable or disable nxlink.
    void DoToggle();

    QdTheme     theme_;
    s32         x_;
    s32         y_;

    // Cached text textures (nullptr = not yet rendered or invalidated).
    SDL_Texture *tex_title_;
    SDL_Texture *tex_state_active_;   // "Active"
    SDL_Texture *tex_state_inactive_; // "Inactive"
    SDL_Texture *tex_host_;           // "stdout redirected" or empty
    SDL_Texture *tex_btn_enable_;     // "[Enable]"
    SDL_Texture *tex_btn_disable_;    // "[Disable]"

    // Cached nxlink active state at last texture build.
    // When the state changes we rebuild the relevant textures.
    bool        last_active_;

    // Touch click-vs-drag state (same pattern as QdPowerButtonElement).
    bool        btn_press_inside_;
    s32         btn_down_x_;
    s32         btn_down_y_;
};

} // namespace ul::menu::qdesktop
