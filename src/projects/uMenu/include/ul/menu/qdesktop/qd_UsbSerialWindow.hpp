// qd_UsbSerialWindow.hpp — USB Serial (CDC-ACM) panel for Q OS qdesktop.
// 480×260 translucent panel: title, active/inactive/blocked state, toggle button.
// Inherits pu::ui::elm::Element (same pattern as QdPowerButtonElement).
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>

namespace ul::menu::qdesktop {

class QdUsbSerialWindow : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdUsbSerialWindow>;

    static Ref New(const QdTheme &theme);

    explicit QdUsbSerialWindow(const QdTheme &theme);
    ~QdUsbSerialWindow();

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
    static constexpr s32 ROW_TITLE_Y   = 16;
    static constexpr s32 ROW_STATE_Y   = 68;
    static constexpr s32 ROW_BTN_Y     = 186;
    static constexpr s32 ROW_TEXT_X    = 20;
    // Toggle button geometry inside the panel.
    static constexpr s32 BTN_W         = 160;
    static constexpr s32 BTN_H         = 44;
    static constexpr s32 BTN_X_OFF     = 20;  // from panel left

    // State codes used by this window.
    // Active   — usbCommsInitialize succeeded, snapshot streaming active.
    // Inactive — CDC not initialised.
    // Blocked  — last TryEnableUsbSerial() returned false (UMS busy).
    enum class UsbState : u8 { Inactive, Active, Blocked };

    // Lazy-build each text texture.
    void EnsureTitleTexture();
    void EnsureStateTextures();
    void EnsureBtnTextures();
    void FreeTextures();

    // Render the action button.
    void RenderButton(SDL_Renderer *r, s32 abs_x, s32 abs_y, UsbState st);

    // Execute the toggle.
    void DoToggle();

    QdTheme     theme_;
    s32         x_;
    s32         y_;

    // Cached text textures.
    SDL_Texture *tex_title_;
    SDL_Texture *tex_state_active_;    // "Active"
    SDL_Texture *tex_state_inactive_;  // "Inactive"
    SDL_Texture *tex_state_blocked_;   // "Blocked (UMS busy)"
    SDL_Texture *tex_btn_enable_;      // "[Enable]"
    SDL_Texture *tex_btn_disable_;     // "[Disable]"

    // Last known state so we only rebuild textures when it actually changes.
    UsbState    last_state_;

    // Whether the previous TryEnableUsbSerial() call returned false.
    // Remembered across frames so we can display Blocked state.
    bool        last_enable_failed_;

    // Touch click-vs-drag state.
    bool        btn_press_inside_;
    s32         btn_down_x_;
    s32         btn_down_y_;
};

} // namespace ul::menu::qdesktop
