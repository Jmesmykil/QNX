// qd_UserCard.hpp — Q OS login-screen user profile card element.
// One card per Switch account; laid out by qdesktop login / startup layout.
// Card size is fixed at 240×320 px.  Avatar is circular, 160×160 px, top-centred.
// Below avatar: account name (white, large).  Below name: tap hint (small, dim).
// Panel background: translucent dark with theme-tinted border.
// Hover/focus: brightens border + 108% scale-up via wider dst rect.
// Click (touch click-vs-drag) or A-button fires the OnSelectFn callback.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <functional>
#include <string>

namespace ul::menu::qdesktop {

class QdUserCardElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdUserCardElement>;
    using OnSelectFn = std::function<void(AccountUid /*uid*/)>;

    // Card dimensions — public so the layout can position multiple cards.
    static constexpr s32 CARD_W = 240;
    static constexpr s32 CARD_H = 320;

    // Avatar circle radius (pixel radius of the circular clip mask).
    static constexpr s32 AVATAR_RADIUS = 80;   // 160×160 diameter
    // Vertical offset from card top to avatar centre.
    static constexpr s32 AVATAR_CENTER_Y = 100;
    // Vertical position of the name label top edge below avatar.
    static constexpr s32 NAME_Y_BELOW_AVATAR = AVATAR_CENTER_Y + AVATAR_RADIUS + 12;
    // Tap-hint bottom padding (px from card bottom).
    static constexpr s32 HINT_BOTTOM_PAD = 18;

    // Factory helper (matches the pattern used by QdDesktopIconsElement::New).
    static Ref New(const QdTheme &theme,
                   const AccountUid uid,
                   const std::string &name,
                   const u8 *icon_jpeg, size_t icon_jpeg_len);

    QdUserCardElement(const QdTheme &theme,
                      const AccountUid uid,
                      const std::string &name,
                      const u8 *icon_jpeg, size_t icon_jpeg_len);
    ~QdUserCardElement();

    // ── Element position/size ──────────────────────────────────────────────
    s32 GetX() override { return x_; }
    s32 GetY() override { return y_; }
    s32 GetWidth()  override { return CARD_W; }
    s32 GetHeight() override { return CARD_H; }

    void SetPos(s32 x, s32 y);

    // ── Focus (D-pad) ──────────────────────────────────────────────────────
    void SetFocused(bool focused);

    // ── Callback wiring ───────────────────────────────────────────────────
    void SetOnSelect(OnSelectFn cb);

    // ── Plutonium element overrides ───────────────────────────────────────
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 origin_x, const s32 origin_y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

private:
    QdTheme    theme_;
    AccountUid uid_;
    std::string name_;

    s32 x_ = 0;
    s32 y_ = 0;

    // Avatar decoded to STATIC SDL_Texture (160×160, ABGR8888).
    // nullptr when decode failed — falls back to initial-letter glyph.
    SDL_Texture *avatar_tex_ = nullptr;

    // First-letter fallback background colour (hash-derived from uid).
    u8 fallback_r_ = 0x4A;
    u8 fallback_g_ = 0x9E;
    u8 fallback_b_ = 0xDE;

    // State flags.
    bool focused_  = false;
    bool hovered_  = false;

    // Touch click-vs-drag state machine.
    bool press_inside_              = false;
    bool was_touch_active_          = false;
    s32  down_x_                    = 0;
    s32  down_y_                    = 0;
    s32  last_touch_x_              = 0;
    s32  last_touch_y_              = 0;
    // v1.8.23: timestamp of TouchDown, in raw 19.2 MHz armGetSystemTick ticks.
    // Used to gate TouchUp on a "tap" duration window (≤ 250 ms).
    u64  down_tick_                 = 0;

    OnSelectFn on_select_;

    // ── Private helpers ────────────────────────────────────────────────────

    // Decode icon_jpeg bytes into avatar_tex_.  Returns true on success.
    // Sets avatar_tex_ = nullptr and initialises fallback colour on failure.
    bool DecodeAvatar(const u8 *jpeg, size_t jpeg_len);

    // Render the circular avatar clipped into (cx-r, cy-r, 2r, 2r).
    // Clips the avatar texture to a circle via row-by-row SDL_RenderCopy strips.
    void RenderCircularAvatar(SDL_Renderer *r, s32 cx, s32 cy, s32 radius) const;

    // Render the fallback filled circle + initial-letter glyph.
    void RenderFallbackAvatar(SDL_Renderer *r, s32 cx, s32 cy, s32 radius);

};

} // namespace ul::menu::qdesktop
