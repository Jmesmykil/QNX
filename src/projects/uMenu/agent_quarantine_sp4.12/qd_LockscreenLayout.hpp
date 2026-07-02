// qd_LockscreenLayout.hpp — Q OS Lockscreen overlay for uMenu C++ (v1.0.0).
// Full-screen 1920×1080 lock screen drawn on top of the Cold Plasma Cascade
// wallpaper.  Rendered as a pu::ui::elm::Element (QdLockscreenElement) so it
// composes with the rest of the qdesktop layer stack.  The owning Layout
// (QdLockscreenLayout) forwards OnInput so any face-button press unlocks.
//
// Visual layout (1920×1080):
//   y=200  — %H:%M time,        font Large, text_primary, h-centered
//   y=310  — %A, %d %B date,    font Medium, text_secondary, h-centered
//   y=600  — user card panel    320×120, surface_glass fill + focus_ring border
//              inside: name (Medium, text_primary) + uid hex (Small, text_secondary)
//   y=930  — "Press A or any button to unlock"  font Small, text_secondary
//   y=960  — battery%·network   font Small, text_secondary, h-centered
//
// Refresh cadence:
//   Every frame  — time string (strftime from localtime)
//   Every 30 fr  — battery (psmGetBatteryChargePercentage) + network
//                  (nifmGetInternetConnectionStatus)
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/menu/qdesktop/qd_Cursor.hpp>
#include <SDL2/SDL.h>
#include <ctime>
#include <cstring>
#include <string>

namespace ul::menu::qdesktop {

// ── QdLockscreenElement ────────────────────────────────────────────────────────

/// Element layer: draws the lock screen chrome (clock, user card, hints,
/// status bar) every frame on top of the wallpaper.
class QdLockscreenElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdLockscreenElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdLockscreenElement>(theme);
    }

    explicit QdLockscreenElement(const QdTheme &theme);
    ~QdLockscreenElement();

    // ── Element interface ──────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    // Lock screen element owns no interactive input — handled by the Layout.
    void OnInput(const u64, const u64, const u64,
                 const pu::ui::TouchPoint) override {}

    /// Re-query battery + network; called by layout every 30 frames.
    void RefreshStatusLine();

private:
    QdTheme theme_;

    // ── Time display ───────────────────────────────────────────────────────
    char time_str_[8];   ///< "%H:%M\0"  (6 chars + NUL)
    char date_str_[40];  ///< "%A, %d %B\0"

    // ── User card ─────────────────────────────────────────────────────────
    char user_name_[64];    ///< AccountProfile nickname or "Guest"
    char user_uid_hex_[40]; ///< "UID: %016llx%016llx\0"

    // ── Status line ───────────────────────────────────────────────────────
    char status_str_[64]; ///< "Battery: xx%  ·  Connected" etc.

    // ── Cached SDL_Texture* (rebuilt when strings change) ─────────────────
    // Textures are rebuilt lazily: nullptr means "needs rebuild".
    SDL_Texture *time_tex_;
    SDL_Texture *date_tex_;
    SDL_Texture *name_tex_;
    SDL_Texture *uid_tex_;
    SDL_Texture *hint_tex_;
    SDL_Texture *status_tex_;

    // Previous strings — used to detect changes so textures aren't rebuilt
    // every frame.
    char prev_time_str_[8];
    char prev_date_str_[40];
    char prev_status_str_[64];

    // ── Helpers ────────────────────────────────────────────────────────────

    /// Rebuild all string caches from time(NULL) + localtime.
    void UpdateTimeStrings();

    /// Blit a cached SDL_Texture* centred at the given y, destroying and
    /// rebuilding it if `rebuild_tex` is true.
    /// After the call tex_ptr may be non-null even if it was null before.
    /// `font_path` is the path string returned by pu::ui::GetDefaultFont().
    static void BlitCentred(SDL_Renderer *r, SDL_Texture *&tex_ptr,
                            const std::string &font_path,
                            const char *text,
                            const pu::ui::Color &clr,
                            bool rebuild_tex,
                            s32 centre_y);

    /// Draw a filled rounded-looking rectangle (SDL has no rounded rect;
    /// we approximate with a filled rect + a 1-px border in a lighter colour).
    static void DrawPanel(SDL_Renderer *r,
                          s32 x, s32 y, s32 w, s32 h,
                          const pu::ui::Color &fill,
                          const pu::ui::Color &border);
};

// ── QdLockscreenLayout ─────────────────────────────────────────────────────────

/// Full-screen lock screen layout.
/// Usage:
///   auto lyt = QdLockscreenLayout::New(theme);
///   g_MenuApplication->LoadLayout(lyt);   // or via LoadMenu(MenuType::Lockscreen)
class QdLockscreenLayout : public pu::ui::Layout {
public:
    using Ref = std::shared_ptr<QdLockscreenLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdLockscreenLayout>(theme);
    }

    explicit QdLockscreenLayout(const QdTheme &theme);
    ~QdLockscreenLayout();

    /// Called each frame by Plutonium's input dispatcher.
    /// Any of A/B/X/Y/Plus/ZR → unlock (LoadMenu Main).
    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos);

    /// Force a battery+network re-query on the overlay element.
    /// Can be called from outside to invalidate the status line.
    void Refresh();

private:
    QdTheme theme_;

    QdWallpaperElement::Ref wallpaper_;
    QdLockscreenElement::Ref overlay_;
    QdCursorElement::Ref    cursor_;

    u32 frame_counter_; ///< Counts frames to gate status refresh at /30.
};

} // namespace ul::menu::qdesktop
