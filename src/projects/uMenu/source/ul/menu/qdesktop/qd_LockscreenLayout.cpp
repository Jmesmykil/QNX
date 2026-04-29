// qd_LockscreenLayout.cpp — Q OS Lockscreen overlay for uMenu C++ (v1.0.0).
// Ported spec from docs/45_HBMenu_Replacement_Design.md §Lockscreen.
// Visual spec:
//   y=200  — %H:%M (Large, text_primary, centred)
//   y=310  — %A, %d %B (Medium, text_secondary, centred)
//   y=600  — user card (320×120 panel, surface_glass fill, focus_ring border)
//               name (Medium, text_primary) + uid hex (Small, text_secondary)
//   y=930  — "Press A or any button to unlock" (Small, text_secondary, centred)
//   y=960  — battery%·network (Small, text_secondary, centred)
// Unlock: any of A/B/X/Y/Plus/ZR → g_MenuApplication->LoadMenu(Main).
// Battery+network polled every 30 frames; time polled every frame.

#include <ul/menu/qdesktop/qd_LockscreenLayout.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <ctime>

// ── Externals ──────────────────────────────────────────────────────────────────
extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Layout pixel constants ────────────────────────────────────────────────────

static constexpr s32 LS_TIME_Y      = 200;   ///< Baseline y for time text
static constexpr s32 LS_DATE_Y      = 310;   ///< Baseline y for date text
static constexpr s32 LS_CARD_Y      = 600;   ///< Top of user card panel
static constexpr s32 LS_CARD_W      = 320;   ///< User card panel width
static constexpr s32 LS_CARD_H      = 120;   ///< User card panel height
static constexpr s32 LS_HINT_Y      = 930;   ///< "Press A…" hint y
static constexpr s32 LS_STATUS_Y    = 960;   ///< Battery/net status y
static constexpr s32 LS_CARD_X      = (1920 - LS_CARD_W) / 2;   ///< 800
static constexpr s32 LS_CARD_NAME_PAD = 14;  ///< Inner top padding for name text
static constexpr s32 LS_STATUS_REFRESH_FRAMES = 30;

// ── QdLockscreenElement — constructor / destructor ────────────────────────────

QdLockscreenElement::QdLockscreenElement(const QdTheme &theme)
    : theme_(theme),
      time_tex_(nullptr), date_tex_(nullptr),
      name_tex_(nullptr), uid_tex_(nullptr),
      hint_tex_(nullptr), status_tex_(nullptr),
      hint_bar_tex_(nullptr)
{
    UL_LOG_INFO("lockscreen: QdLockscreenElement ctor");

    // Zero all string buffers.
    time_str_[0]      = '\0';
    date_str_[0]      = '\0';
    user_name_[0]     = '\0';
    user_uid_hex_[0]  = '\0';
    status_str_[0]    = '\0';
    prev_time_str_[0] = '\0';
    prev_date_str_[0] = '\0';
    prev_status_str_[0] = '\0';

    // Populate user name + uid from the selected account.
    const AccountUid &uid = g_GlobalSettings.system_status.selected_user;
    if (accountUidIsValid(&uid)) {
        std::string name_str;
        if (R_FAILED(ul::acc::GetAccountName(uid, name_str))) {
            name_str = "User";
        }
        const size_t copy_n = name_str.size() < sizeof(user_name_) - 1
                              ? name_str.size()
                              : sizeof(user_name_) - 1;
        __builtin_memcpy(user_name_, name_str.c_str(), copy_n);
        user_name_[copy_n] = '\0';

        // Format uid as two 64-bit hex words.
        snprintf(user_uid_hex_, sizeof(user_uid_hex_),
                 "UID: %016llx%016llx",
                 (unsigned long long)uid.uid[0],
                 (unsigned long long)uid.uid[1]);
    }
    else {
        // No selected user — fall back to generic label.
        snprintf(user_name_,    sizeof(user_name_),    "Guest");
        snprintf(user_uid_hex_, sizeof(user_uid_hex_), "UID: (none)");
    }

    // Rasterise user card textures (these never change during a lock session).
    const std::string med_font   = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
    const std::string small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

    name_tex_ = pu::ui::render::RenderText(
        med_font, std::string(user_name_), theme_.text_primary);

    uid_tex_ = pu::ui::render::RenderText(
        small_font, std::string(user_uid_hex_), theme_.text_secondary);

    // Rasterise the static unlock-hint text.
    hint_tex_ = pu::ui::render::RenderText(
        small_font,
        std::string("Press \xe2\x96\xb3 or any button to unlock"),
        theme_.text_secondary);

    // Build the bottom hint bar once; freed in the destructor.
    {
        const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
        hint_bar_tex_ = pu::ui::render::RenderText(
            small_font,
            std::string("A / Touch Unlock"),
            hint_col);
    }

    // Populate time + status for the first frame.
    UpdateTimeStrings();
    RefreshStatusLine();
}

QdLockscreenElement::~QdLockscreenElement() {
    UL_LOG_INFO("lockscreen: QdLockscreenElement dtor");
    // Destroy all cached textures.
    pu::ui::render::DeleteTexture(time_tex_);
    pu::ui::render::DeleteTexture(date_tex_);
    pu::ui::render::DeleteTexture(name_tex_);
    pu::ui::render::DeleteTexture(uid_tex_);
    pu::ui::render::DeleteTexture(hint_tex_);
    pu::ui::render::DeleteTexture(status_tex_);
    pu::ui::render::DeleteTexture(hint_bar_tex_);
}

// ── UpdateTimeStrings ─────────────────────────────────────────────────────────

void QdLockscreenElement::UpdateTimeStrings() {
    const time_t now = ::time(nullptr);
    const struct tm *t = localtime(&now);
    if (t == nullptr) {
        // Should never happen but guard anyway.
        snprintf(time_str_, sizeof(time_str_), "--:--");
        snprintf(date_str_, sizeof(date_str_), "---");
        return;
    }
    strftime(time_str_, sizeof(time_str_), "%H:%M", t);
    strftime(date_str_, sizeof(date_str_), "%A, %d %B", t);
}

// ── RefreshStatusLine ─────────────────────────────────────────────────────────

void QdLockscreenElement::RefreshStatusLine() {
    // Battery %.
    u32 battery_pct = 0;
    if (R_FAILED(psmGetBatteryChargePercentage(&battery_pct))) {
        battery_pct = 0;
    }

    // Network connection status.
    NifmInternetConnectionType  conn_type   = {};
    u32                         wifi_str    = 0;
    NifmInternetConnectionStatus conn_status = NifmInternetConnectionStatus_ConnectingUnknown1;
    const bool net_ok =
        R_SUCCEEDED(nifmGetInternetConnectionStatus(&conn_type, &wifi_str, &conn_status))
        && (conn_status == NifmInternetConnectionStatus_Connected);

    const char *net_label = net_ok ? "Connected" : "Offline";

    snprintf(status_str_, sizeof(status_str_),
             "Battery: %u%%  \xc2\xb7  %s", battery_pct, net_label);
}

// ── DrawPanel helper ──────────────────────────────────────────────────────────

void QdLockscreenElement::DrawPanel(SDL_Renderer *r,
                                    s32 x, s32 y, s32 w, s32 h,
                                    const pu::ui::Color &fill,
                                    const pu::ui::Color &border)
{
    // Filled interior.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 200);
    const SDL_Rect inner = { x, y, w, h };
    SDL_RenderFillRect(r, &inner);

    // 1-px border.
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 180);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderDrawRect(r, &inner);
}

// ── BlitCentred helper ────────────────────────────────────────────────────────

void QdLockscreenElement::BlitCentred(SDL_Renderer *r,
                                      SDL_Texture *&tex_ptr,
                                      const std::string &font_path,
                                      const char *text,
                                      const pu::ui::Color &clr,
                                      bool rebuild_tex,
                                      s32 centre_y)
{
    if (rebuild_tex && tex_ptr != nullptr) {
        pu::ui::render::DeleteTexture(tex_ptr);
    }
    if (tex_ptr == nullptr && text[0] != '\0') {
        tex_ptr = pu::ui::render::RenderText(font_path, std::string(text), clr);
    }
    if (tex_ptr == nullptr) {
        return;
    }
    int tw = 0, th = 0;
    SDL_QueryTexture(tex_ptr, nullptr, nullptr, &tw, &th);
    const s32 bx = (1920 - tw) / 2;
    const SDL_Rect dst = { bx, centre_y, tw, th };
    SDL_RenderCopy(r, tex_ptr, nullptr, &dst);
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdLockscreenElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                   const s32 /*x*/, const s32 /*y*/)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    // ── Update time strings each frame ────────────────────────────────────
    UpdateTimeStrings();

    const bool time_changed   = (strncmp(time_str_, prev_time_str_,
                                         sizeof(time_str_)) != 0);
    const bool date_changed   = (strncmp(date_str_, prev_date_str_,
                                         sizeof(date_str_)) != 0);
    const bool status_changed = (strncmp(status_str_, prev_status_str_,
                                         sizeof(status_str_)) != 0);

    if (time_changed) {
        __builtin_memcpy(prev_time_str_, time_str_, sizeof(time_str_));
    }
    if (date_changed) {
        __builtin_memcpy(prev_date_str_, date_str_, sizeof(date_str_));
    }
    if (status_changed) {
        __builtin_memcpy(prev_status_str_, status_str_, sizeof(status_str_));
    }

    // ── Semi-transparent full-screen dark scrim ───────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.desktop_bg.r,
        theme_.desktop_bg.g,
        theme_.desktop_bg.b,
        192);
    const SDL_Rect full = { 0, 0, SCREEN_W, SCREEN_H };
    SDL_RenderFillRect(r, &full);

    // ── Time (Large, centred at y=LS_TIME_Y) ─────────────────────────────
    {
        const std::string large_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
        BlitCentred(r, time_tex_, large_font, time_str_,
                    theme_.text_primary, time_changed, LS_TIME_Y);
    }

    // ── Date (Medium, centred at y=LS_DATE_Y) ────────────────────────────
    {
        const std::string med_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
        BlitCentred(r, date_tex_, med_font, date_str_,
                    theme_.text_secondary, date_changed, LS_DATE_Y);
    }

    // ── User card panel ───────────────────────────────────────────────────
    DrawPanel(r,
              LS_CARD_X, LS_CARD_Y, LS_CARD_W, LS_CARD_H,
              theme_.surface_glass, theme_.focus_ring);

    // Name centred inside card horizontally, padded from top.
    if (name_tex_ != nullptr) {
        int nw = 0, nh = 0;
        SDL_QueryTexture(name_tex_, nullptr, nullptr, &nw, &nh);
        const s32 nx = LS_CARD_X + (LS_CARD_W - nw) / 2;
        const s32 ny = LS_CARD_Y + LS_CARD_NAME_PAD;
        const SDL_Rect ndst = { nx, ny, nw, nh };
        SDL_RenderCopy(r, name_tex_, nullptr, &ndst);
    }

    // UID hex below name (Small, text_secondary).
    if (uid_tex_ != nullptr) {
        int uw = 0, uh = 0;
        SDL_QueryTexture(uid_tex_, nullptr, nullptr, &uw, &uh);
        const s32 ux = LS_CARD_X + (LS_CARD_W - uw) / 2;
        // Place below name: pad 14 + name-height estimate (Medium ~28px) + 6px gap.
        // Use a fixed offset from card top so it doesn't rely on name_tex_ query.
        const s32 uy = LS_CARD_Y + LS_CARD_NAME_PAD + 34 + 6;
        const SDL_Rect udst = { ux, uy, uw, uh };
        SDL_RenderCopy(r, uid_tex_, nullptr, &udst);
    }

    // ── Unlock hint (Small, centred at y=LS_HINT_Y) ──────────────────────
    if (hint_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = (1920 - hw) / 2;
        const SDL_Rect hdst = { hx, LS_HINT_Y, hw, hh };
        SDL_RenderCopy(r, hint_tex_, nullptr, &hdst);
    }

    // ── Status line (Small, centred at y=LS_STATUS_Y) ────────────────────
    {
        const std::string small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        BlitCentred(r, status_tex_, small_font, status_str_,
                    theme_.text_secondary, status_changed, LS_STATUS_Y);
    }

    // ── Bottom hint bar ───────────────────────────────────────────────────
    if (hint_bar_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_bar_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = (1920 - hw) / 2;
        const s32 hy = 1080 - 8 - hh;
        const SDL_Rect hdst = { hx, hy, hw, hh };
        SDL_RenderCopy(r, hint_bar_tex_, nullptr, &hdst);
    }
}

// ── QdLockscreenLayout — constructor / destructor ─────────────────────────────

QdLockscreenLayout::QdLockscreenLayout(const QdTheme &theme)
    : theme_(theme), frame_counter_(0)
{
    UL_LOG_INFO("lockscreen: QdLockscreenLayout ctor");

    wallpaper_ = QdWallpaperElement::New(theme_);
    overlay_   = QdLockscreenElement::New(theme_);
    cursor_    = QdCursorElement::New(theme_);

    // Add elements in draw order: wallpaper → overlay chrome → cursor on top.
    this->Add(wallpaper_);
    this->Add(overlay_);
    this->Add(cursor_);
}

QdLockscreenLayout::~QdLockscreenLayout() {
    UL_LOG_INFO("lockscreen: QdLockscreenLayout dtor");
}

// ── Refresh ───────────────────────────────────────────────────────────────────

void QdLockscreenLayout::Refresh() {
    if (overlay_) {
        overlay_->RefreshStatusLine();
    }
}

// ── IMenuLayout obligations ───────────────────────────────────────────────────

void QdLockscreenLayout::OnMenuInput(const u64 keys_down, const u64 /*keys_up*/,
                                     const u64 /*keys_held*/,
                                     const pu::ui::TouchPoint touch_pos)
{
    // Refresh battery+network every 30 frames even with no input.
    ++frame_counter_;
    if (frame_counter_ >= LS_STATUS_REFRESH_FRAMES) {
        frame_counter_ = 0;
        Refresh();
    }

    // Any of A/B/X/Y/Plus/ZR unlocks the screen.
    static constexpr u64 UNLOCK_MASK =
        HidNpadButton_A    | HidNpadButton_B   |
        HidNpadButton_X    | HidNpadButton_Y   |
        HidNpadButton_Plus | HidNpadButton_ZR;

    bool should_unlock = (keys_down & UNLOCK_MASK) != 0;

    // v1.8.29 Slice 2: touch tap unlocks — equivalent to pressing A.
    if (!touch_pos.IsEmpty()) {
        UL_LOG_INFO("lockscreen: touch tap at (%d,%d) — unlock",
                    touch_pos.x, touch_pos.y);
        should_unlock = true;
    }

    if (should_unlock) {
        UL_LOG_INFO("lockscreen: unlock (keys_down=0x%llx touch=%s) — loading Main",
                    (unsigned long long)keys_down,
                    touch_pos.IsEmpty() ? "no" : "yes");
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
    }
}

bool QdLockscreenLayout::OnHomeButtonPress() {
    UL_LOG_INFO("lockscreen: OnHomeButtonPress -> returning to MainMenu");
    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
    return true;
}

void QdLockscreenLayout::LoadSfx() {
    // Lockscreen has no per-layout sfx today.  When sfx are added, load them here.
}

void QdLockscreenLayout::DisposeSfx() {
    // Mirrors LoadSfx; kept symmetric for future sfx work.
}

} // namespace ul::menu::qdesktop
