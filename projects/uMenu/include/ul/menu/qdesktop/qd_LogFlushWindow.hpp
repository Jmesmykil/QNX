// qd_LogFlushWindow.hpp — Telemetry flush panel for Q OS qdesktop.
// 480×180 translucent panel: title, description, flush button + last-flush time.
// Inherits pu::ui::elm::Element (same pattern as QdPowerButtonElement).
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ctime>

namespace ul::menu::qdesktop {

class QdLogFlushWindow : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdLogFlushWindow>;

    static Ref New(const QdTheme &theme);

    explicit QdLogFlushWindow(const QdTheme &theme);
    ~QdLogFlushWindow();

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
    static constexpr s32 PANEL_H = 180;

private:
    // Row layout offsets from panel top-left.
    static constexpr s32 ROW_TITLE_Y   = 16;
    static constexpr s32 ROW_DESC_Y    = 60;
    static constexpr s32 ROW_BTN_Y     = 116;
    static constexpr s32 ROW_TEXT_X    = 20;
    // Flush button geometry.
    static constexpr s32 BTN_W         = 160;
    static constexpr s32 BTN_H         = 44;
    static constexpr s32 BTN_X_OFF     = 20;

    // Lazy-build each text texture.
    void EnsureTitleTexture();
    void EnsureDescTexture();
    void EnsureBtnTexture();
    // Rebuild the "Last flush: HH:MM:SS" texture from last_flush_time_.
    // Called after every successful flush and on first render when a cached
    // time exists.  Frees the old tex_last_flush_ before rebuilding.
    void RebuildLastFlushTexture();
    void FreeTextures();

    // Perform the flush and update last_flush_time_ + tex_last_flush_.
    void DoFlush();

    QdTheme     theme_;
    s32         x_;
    s32         y_;

    // Cached text textures.
    SDL_Texture *tex_title_;      // "Flush Telemetry"
    SDL_Texture *tex_desc_;       // "Sync all log channels + fdatasync SD ring"
    SDL_Texture *tex_btn_;        // "[Flush Now]"
    SDL_Texture *tex_last_flush_; // "Last flush: HH:MM:SS" or "No flush yet"

    // Monotonic wall-clock of the last flush.  0 = never flushed this session.
    time_t      last_flush_time_;

    // Touch click-vs-drag state.
    bool        btn_press_inside_;
    s32         btn_down_x_;
    s32         btn_down_y_;
};

} // namespace ul::menu::qdesktop
