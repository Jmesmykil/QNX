// qd_HotCornerRightDropdown.cpp — Top-right hot-corner dropdown (uMenu v1.10.3.11).
//
// System Status rows (read-only, disabled):
//   0  Battery   — psmGetBatteryChargePercentage + psmGetChargerType
//   1  Time/Date — timeGetCurrentTime(TimeType_UserSystemClock)
//   2  Network   — nifmGetInternetConnectionStatus
//   3  Volume    — audctlGetSystemOutputMasterVolume
//
// Quick Action rows (enabled):
//   4  Sleep        — appletStartSleepSequence(true)
//   5  Restart      — ul::menu::qdesktop::power::Reboot()
//   6  Reboot Hekate— ul::menu::qdesktop::power::RebootToHekate()
//   7  Lock Screen  — appletStartLockScreen()
//
// Panel geometry (v2.1.0):
//   panel_x_=1600 (panel RIGHT edge at screen edge x=1920) — directly under the
//   OS status icons (BT@1704, CONN@1752, BATT@1800, BATT_TEXT@1848).  Earlier
//   versions placed the panel at x=1280..1600, which was ~200 px to the LEFT of
//   the icons the dropdown represents — tapping an icon popped the dropdown
//   visibly far away from the user's tap.  Aligning panel right edge to screen
//   right anchors it directly under the icon row.
//   panel_y_=40 — flush with the bottom of the vertically-centred 32-px icons
//   (icon_y=8, icon_bottom=40).  The dropdown's top cyan border lies at the
//   icon-bottom line.
//   panel_w_=320, panel_h_=400 (8×48 + 2×8).
//
// Textures pre-rendered in Open(); blitted cheaply in Render(); freed in Close().
// No per-frame IPC, no per-frame RenderText.  B41/B42-safe (DeleteTexture only).

#include <ul/menu/qdesktop/qd_HotCornerRightDropdown.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // DROPDOWN_RIGHT_*, DROPDOWN_ROW_H etc (D10-D12 fix)
#include <ul/menu/qdesktop/qd_Launchpad.hpp>     // LP_HOTCORNER_H (kept for backward compat)
#include <ul/menu/qdesktop/qd_Theme.hpp>          // v2.6.0 — panel/hover/border read g_QdTheme
#include <ul/menu/qdesktop/qd_WmConstants.hpp>   // TOPBAR_H
#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/menu/qdesktop/qd_Power.hpp>          // Reboot, RebootToHekate, Sleep
#include <ul/menu/qdesktop/qd_DevTools.hpp>          // dev::TryEnable* / Disable* / IsActive*
#include <ul/menu/qdesktop/qd_NxlinkServer.hpp>      // QdNxlinkServer::State + g_NxlinkServer
#include <ul/menu/qdesktop/qd_DebugServer.hpp>       // QdDebugServer + g_DebugServer (row 10)
#include <ul/menu/qdesktop/qd_RemoteShellServer.hpp> // QdRemoteShellServer::State + g_RemoteShellServer
#include <ul/menu/smi/smi_Commands.hpp>           // smi::LaunchHomebrewLibraryApplet (action 6 NRO delegate)
#include <ul/menu/ui/ui_MenuApplication.hpp>      // g_MenuApplication declaration
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif

// Pulled from qd_DesktopIcons.cpp translation unit.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Palette ───────────────────────────────────────────────────────────────────
static constexpr pu::ui::Color kColorEnabled  { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
static constexpr pu::ui::Color kColorDisabled { 0x60u, 0x60u, 0x60u, 0xFFu };

// v2.6.0 — same theme-aware macros as the left dropdown.
#define kPanelBg  SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.surface_glass.r,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.g,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.b,     0xEAu }
#define kHoverBg  SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.r, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.g, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.b, 0xFFu }
#define kBorderFg SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.accent.r,            ::ul::menu::qdesktop::g_QdTheme.accent.g,            ::ul::menu::qdesktop::g_QdTheme.accent.b,            0xFFu }

// ── Layout constants ──────────────────────────────────────────────────────────
// D11/D12 fix: local kRowH, kPadV, kPadH, kPanelW, kRadius, kScreenW, kPanelX
// are now aliases for qd_LayoutConstants.hpp tokens — single SSOT, no divergence.
static constexpr int kRowH   = DROPDOWN_ROW_H;    // 48 px per row
static constexpr int kPadV   = DROPDOWN_PAD_V;    // 8  top/bottom padding inside panel
static constexpr int kPadH   = DROPDOWN_PAD_H;    // 16 left text indent inside panel
static constexpr int kPanelW = DROPDOWN_RIGHT_W;  // 320 (wider than left dropdown's 280)
static constexpr int kRadius = DROPDOWN_RADIUS;   // 8 = kPadV

// v2.1.0: panel_x_ aligned to screen right edge so the dropdown lives directly
// under the OS status icon zone (icons live at x=1704..1848).  Earlier versions
// kept panel_x_=1280 to "not overlap" the icons, but that placed the dropdown
// ~200 px to the LEFT of the icons it surfaces info for — visually disconnected
// from the user's tap.  When the dropdown is OPEN, its content takes precedence
// over the hot-zone marker overlay; when CLOSED, the marker is visible again.
// panel_h_ = kRightDropdownItems * kRowH + 2*kPadV (computed at Open() time).
// D12 fix: kScreenW=1920 removed; DROPDOWN_RIGHT_X = SCR_W - DROPDOWN_RIGHT_W = 1600.
static constexpr int kPanelX  = DROPDOWN_RIGHT_X;   // 1600 — flush with screen right

// ── Item labels (indexed 0-7) ─────────────────────────────────────────────────
// Rows 0-3 are status rows (label built dynamically in Open()).
// Rows 4-7 are action rows with fixed labels.
static const char * const kActionLabels[4] = {
    "Sleep",
    "Restart",
    "Reboot to Hekate",
    "Lock Screen",
};

// ── FillRoundedRect ───────────────────────────────────────────────────────────
// Verbatim from qd_HotCornerDropdown.cpp:119-143.  Caller sets draw colour +
// blend mode; this helper does not modify them.  No textures allocated —
// B41/B42-safe.
static void FillRoundedRect(SDL_Renderer *r, SDL_Rect rect, int radius) {
    if (r == nullptr || radius <= 0 ||
        rect.w <= 2 * radius || rect.h <= 2 * radius) {
        SDL_RenderFillRect(r, &rect);
        return;
    }
    // Top + bottom curved-corner scanlines.
    for (int dy = 0; dy < radius; ++dy) {
        const double yc   = static_cast<double>(radius - 1 - dy);
        const double dx_d = (static_cast<double>(radius) * static_cast<double>(radius))
                          - (yc * yc);
        const int dx = (dx_d <= 0.0) ? 0 : static_cast<int>(__builtin_sqrt(dx_d));
        const int xx = rect.x + radius - dx;
        const int ww = (rect.w - 2 * radius) + 2 * dx;
        SDL_Rect top_line { xx, rect.y + dy,                ww, 1 };
        SDL_Rect bot_line { xx, rect.y + rect.h - 1 - dy,   ww, 1 };
        SDL_RenderFillRect(r, &top_line);
        SDL_RenderFillRect(r, &bot_line);
    }
    // Body — full width between curved cap regions.
    SDL_Rect body { rect.x, rect.y + radius, rect.w, rect.h - 2 * radius };
    SDL_RenderFillRect(r, &body);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void QdHotCornerRightDropdown::MakeText(SDL_Renderer *r,
                                         pu::ui::DefaultFontSize font_size,
                                         const char *text,
                                         pu::ui::Color color,
                                         SDL_Texture **out_tex,
                                         int *out_w, int *out_h) {
    (void)r;
    *out_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(font_size), std::string(text), color);
    if (*out_tex != nullptr) {
        SDL_QueryTexture(*out_tex, nullptr, nullptr, out_w, out_h);
    } else {
        *out_w = 0;
        *out_h = 0;
    }
}

void QdHotCornerRightDropdown::FreeTexture(SDL_Texture **tex) {
    if (*tex != nullptr) {
        pu::ui::render::DeleteTexture(*tex);
        *tex = nullptr;
    }
}

void QdHotCornerRightDropdown::Blit(SDL_Renderer *r, SDL_Texture *tex,
                                     int x, int y, int w, int h) {
    if (tex == nullptr || r == nullptr) return;
    const SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

QdHotCornerRightDropdown::QdHotCornerRightDropdown() = default;

QdHotCornerRightDropdown::~QdHotCornerRightDropdown() {
    Close();
}

void QdHotCornerRightDropdown::Open(SDL_Renderer *r) {
    Close();  // free any previous textures

    // ── Panel geometry ────────────────────────────────────────────────────────
    // D10 fix: panel_y_ is now DROPDOWN_RIGHT_Y from qd_LayoutConstants.hpp.
    //   = TOPBAR_H_S - HC_ICON_TOP_INSET = 48 - 8 = 40.
    //   HC_ICON_TOP_INSET=8 names the intent: status icons start 8 px below the
    //   topbar top, so icon_bottom = TOPBAR_H_S - HC_ICON_TOP_INSET = 40.
    //   Was: `static_cast<int>(TOPBAR_H) - 8` — bare `8` was magic.
    // This mirrors the visual relationship the LEFT dropdown has with its Q-glyph
    // widget (LEFT panel_y_=HC_VISUAL_H=72 = bottom of 96×72 widget).  The
    // dropdown's top cyan border overlays the bottom 8 px of the topbar strip
    // (translucent black + 1-px hairline at y=47), no information loss.
    panel_x_ = kPanelX;                        // DROPDOWN_RIGHT_X = 1600
    panel_y_ = DROPDOWN_RIGHT_Y;               // 40 = TOPBAR_H_S - HC_ICON_TOP_INSET
    panel_w_ = kPanelW;                        // DROPDOWN_RIGHT_W = 320
    panel_h_ = kRightDropdownItems * kRowH + 2 * kPadV;

    // ── Row geometry ──────────────────────────────────────────────────────────
    for (int i = 0; i < kRightDropdownItems; ++i) {
        row_y_[i] = panel_y_ + kPadV + i * kRowH;
        row_h_[i] = kRowH;
        disabled_[i] = (i < 4);  // rows 0-3 are status (disabled); 4-9 are actions
    }

    // ── Status row 0: Battery ─────────────────────────────────────────────────
    // v1.10.3.10.5 main-thread fix: libnx exposes PsmChargerType_*
    // (psm.h), not ChargerType_*.  Map the four canonical values used by the
    // Power tile (Unconnected / EnoughPower / LowPower / NotSupported).
    {
        u32 pct = 0;
        PsmChargerType charger = PsmChargerType_Unconnected;
        psmGetBatteryChargePercentage(&pct);
        psmGetChargerType(&charger);

        const char *charger_str = "";
        switch (charger) {
            case PsmChargerType_EnoughPower:  charger_str = " (AC)";       break;
            case PsmChargerType_LowPower:     charger_str = " (AC low)";   break;
            case PsmChargerType_NotSupported: charger_str = " (AC ?)";     break;
            case PsmChargerType_Unconnected:
            default:                          charger_str = "";            break;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Battery: %u%%%s",
                      static_cast<unsigned>(pct), charger_str);
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorDisabled, &tex_item_[0], &item_w_[0], &item_h_[0]);
    }

    // ── Status row 1: Time/Date ───────────────────────────────────────────────
    {
        u64 posix_time = 0;
        timeGetCurrentTime(TimeType_UserSystemClock, &posix_time);

        // Convert POSIX timestamp to local time fields via libc gmtime.
        // Switch libnx provides time() via newlib; gmtime is available.
        const time_t t = static_cast<time_t>(posix_time);
        struct tm *tm_info = gmtime(&t);
        char buf[64];
        if (tm_info != nullptr) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d",
                          tm_info->tm_year + 1900,
                          tm_info->tm_mon  + 1,
                          tm_info->tm_mday,
                          tm_info->tm_hour,
                          tm_info->tm_min);
        } else {
            std::snprintf(buf, sizeof(buf), "Time unavailable");
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorDisabled, &tex_item_[1], &item_w_[1], &item_h_[1]);
    }

    // ── Status row 2: Network ─────────────────────────────────────────────────
    {
        NifmInternetConnectionStatus conn_status;
        NifmInternetConnectionType   conn_type;
        u32 signal_strength = 0;
        const Result rc = nifmGetInternetConnectionStatus(
            &conn_type, &signal_strength, &conn_status);

        char buf[64];
        if (R_FAILED(rc)) {
            std::snprintf(buf, sizeof(buf), "Network: unavailable");
        } else if (conn_status == NifmInternetConnectionStatus_Connected) {
            const char *type_str =
                (conn_type == NifmInternetConnectionType_Ethernet) ? "Wired" : "Wi-Fi";
            std::snprintf(buf, sizeof(buf), "Network: %s (%u%%)", type_str,
                          static_cast<unsigned>(signal_strength));
        } else {
            std::snprintf(buf, sizeof(buf), "Network: disconnected");
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorDisabled, &tex_item_[2], &item_w_[2], &item_h_[2]);
    }

    // ── Status row 3: Volume ──────────────────────────────────────────────────
    // v1.10.3.10.5 main-thread fix: audctlGetSystemOutputMasterVolume returns
    // a float (per audctl.h:69), normalized 0.0..1.0 — not a u32 0..15.
    {
        float vol = 0.0f;
        const Result rc = audctlGetSystemOutputMasterVolume(&vol);
        char buf[64];
        if (R_FAILED(rc)) {
            std::snprintf(buf, sizeof(buf), "Volume: unavailable");
        } else {
            const unsigned pct =
                static_cast<unsigned>(vol * 100.0f + 0.5f);
            std::snprintf(buf, sizeof(buf), "Volume: %u%%",
                          pct > 100u ? 100u : pct);
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorDisabled, &tex_item_[3], &item_w_[3], &item_h_[3]);
    }

    // ── Action rows 4-7: fixed labels ────────────────────────────────────────
    for (int i = 4; i < 8; ++i) {
        MakeText(r, pu::ui::DefaultFontSize::Medium, kActionLabels[i - 4],
                 kColorEnabled, &tex_item_[i], &item_w_[i], &item_h_[i]);
    }

    // ── Dev row 8: nxlink server toggle (v2.2.0) ────────────────────────────
    // Label dynamically reflects current server state at Open() time. After
    // user toggles via FireAction(8), the user must close+reopen the dropdown
    // to see the updated label (Open()-time snapshot model).
    {
        char buf[96];
        const QdNxlinkServer::State srv_state = g_NxlinkServer.GetState();
        const bool srv_running = g_NxlinkServer.IsRunning();
        if (srv_running && srv_state == QdNxlinkServer::State::Listening) {
            // Try to surface the IP so users have everything they need at a glance.
            u32 ip = 0;
            const Result ip_rc = nifmGetCurrentIpAddress(&ip);
            if (R_SUCCEEDED(ip_rc) && ip != 0) {
                std::snprintf(buf, sizeof(buf),
                              "nxlink: ON @%u.%u.%u.%u",
                              static_cast<unsigned>(ip & 0xFF),
                              static_cast<unsigned>((ip >> 8) & 0xFF),
                              static_cast<unsigned>((ip >> 16) & 0xFF),
                              static_cast<unsigned>((ip >> 24) & 0xFF));
            } else {
                std::snprintf(buf, sizeof(buf), "nxlink: ON");
            }
        } else if (srv_running && srv_state == QdNxlinkServer::State::Receiving) {
            std::snprintf(buf, sizeof(buf), "nxlink: receiving...");
        } else if (srv_state == QdNxlinkServer::State::SocketInitFailed) {
            std::snprintf(buf, sizeof(buf), "nxlink: init failed");
        } else {
            std::snprintf(buf, sizeof(buf), "nxlink: OFF");
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorEnabled, &tex_item_[8], &item_w_[8], &item_h_[8]);

        // v2.2.1: snapshot the state used to render tex_item_[8] so that
        // RefreshDevRowLabel()'s first call is a cheap no-op (state unchanged).
        last_dev_row_state_   = srv_state;
        last_dev_row_running_ = srv_running;
    }

    // ── Dev row 9: remote shell toggle (v2.3.0 / v3.0.x PIN display) ───────────
    // v3.0.x: when the server is running, surface the auth PIN so the user
    // can read it off the screen before handing the remote end to a client.
    // Format: "shell: <ip>:9999 PIN:123456"
    {
        char buf[128];
        const QdRemoteShellServer::State sh_state   = g_RemoteShellServer.GetState();
        const bool                       sh_running  = g_RemoteShellServer.IsRunning();
        if (sh_running && sh_state == QdRemoteShellServer::State::Listening) {
            u32 ip = 0;
            const Result ip_rc = nifmGetCurrentIpAddress(&ip);
            const int pin = g_RemoteShellServer.GetAuthPin();
            if (R_SUCCEEDED(ip_rc) && ip != 0) {
                std::snprintf(buf, sizeof(buf),
                              "shell: %u.%u.%u.%u:9999 PIN:%06d",
                              static_cast<unsigned>(ip & 0xFF),
                              static_cast<unsigned>((ip >> 8) & 0xFF),
                              static_cast<unsigned>((ip >> 16) & 0xFF),
                              static_cast<unsigned>((ip >> 24) & 0xFF),
                              pin);
            } else {
                std::snprintf(buf, sizeof(buf), "shell: :9999 PIN:%06d", pin);
            }
        } else if (sh_running && sh_state == QdRemoteShellServer::State::Connected) {
            std::snprintf(buf, sizeof(buf), "shell: client connected");
        } else if (sh_state == QdRemoteShellServer::State::SocketInitFailed) {
            std::snprintf(buf, sizeof(buf), "shell: init failed");
        } else {
            std::snprintf(buf, sizeof(buf), "shell: OFF");
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorEnabled, &tex_item_[9], &item_w_[9], &item_h_[9]);
        // Snapshot state for RefreshDevRowLabel no-op fast path.
        last_shell_row_state_   = sh_state;
        last_shell_row_running_ = sh_running;
    }

    // ── Dev row 10: remote-test debug server toggle (HTTP :6010) ───────────────
    // Off by default; manual toggle (creator preference — not always-on).  Label
    // is the Open()-time snapshot; close+reopen to refresh after toggling.
    {
        char buf[96];
        if (g_DebugServer.IsRunning()) {
            u32 ip = 0;
            const Result ip_rc = nifmGetCurrentIpAddress(&ip);
            if (R_SUCCEEDED(ip_rc) && ip != 0) {
                std::snprintf(buf, sizeof(buf), "debug: ON @%u.%u.%u.%u:6010",
                              static_cast<unsigned>(ip & 0xFF),
                              static_cast<unsigned>((ip >> 8) & 0xFF),
                              static_cast<unsigned>((ip >> 16) & 0xFF),
                              static_cast<unsigned>((ip >> 24) & 0xFF));
            } else {
                std::snprintf(buf, sizeof(buf), "debug: ON :6010");
            }
        } else {
            std::snprintf(buf, sizeof(buf), "debug: OFF");
        }
        MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
                 kColorEnabled, &tex_item_[10], &item_w_[10], &item_h_[10]);
    }

    hovered_ = -1;
    open_ = true;
    armed_for_outside_close_ = false;
    was_touch_active_internal_ = false;
    skip_first_lift_ = false;   // BUG-7 — fresh slate each Open
    prev_cursor_x_ = -1;
    prev_cursor_y_ = -1;
    QdAudio::Play(DesktopSfxEvent::PowerMenuOpen);
    UL_LOG_INFO("qdesktop: right hot-corner dropdown opened");
}

// BUG-7 — see qd_HotCornerRightDropdown.hpp doc on SetSkipFirstLift.
void QdHotCornerRightDropdown::SetSkipFirstLift() {
    skip_first_lift_ = true;
    // Mark the touch as having been active so HandleInput's just_released
    // branch is reached on the lift frame (where the skip is consumed).
    was_touch_active_internal_ = true;
}

void QdHotCornerRightDropdown::Close() {
    const bool was_open = open_;
    for (int i = 0; i < kRightDropdownItems; ++i) {
        FreeTexture(&tex_item_[i]);
        item_w_[i] = 0;
        item_h_[i] = 0;
    }
    hovered_ = -1;
    open_ = false;
    if (was_open) {
        QdAudio::Play(DesktopSfxEvent::PowerMenuClose);
    }
    // v2.2.1/v2.3.0: reset change-detection state so the next Open() starts
    // clean and RefreshDevRowLabel's first post-Open() call correctly compares
    // against the state used to build tex_item_[8] and tex_item_[9] in Open().
    last_dev_row_state_      = QdNxlinkServer::State::Stopped;
    last_dev_row_running_    = false;
    last_shell_row_state_    = QdRemoteShellServer::State::Stopped;
    last_shell_row_running_  = false;
}

// ── RefreshDevRowLabel (v2.2.1) ───────────────────────────────────────────────
// Reads current server state; if it differs from the snapshot stored at the last
// call, frees tex_item_[8] via pu::ui::render::DeleteTexture (P-B safe) and
// re-renders the label with the new string.  No-op when state is unchanged.
// Called at the top of Render() when open_ is true.

void QdHotCornerRightDropdown::RefreshDevRowLabel(SDL_Renderer *r) {
    const QdNxlinkServer::State cur_state   = g_NxlinkServer.GetState();
    const bool                  cur_running = g_NxlinkServer.IsRunning();

    // Fast path: nothing has changed since the last call (or since Open()).
    if (cur_state == last_dev_row_state_ && cur_running == last_dev_row_running_) {
        return;
    }

    // State changed — rebuild the label string.
    char buf[96];
    if (cur_running && cur_state == QdNxlinkServer::State::Listening) {
        u32 ip = 0;
        const Result ip_rc = nifmGetCurrentIpAddress(&ip);
        if (R_SUCCEEDED(ip_rc) && ip != 0) {
            std::snprintf(buf, sizeof(buf),
                          "nxlink: ON @%u.%u.%u.%u",
                          static_cast<unsigned>(ip & 0xFF),
                          static_cast<unsigned>((ip >> 8) & 0xFF),
                          static_cast<unsigned>((ip >> 16) & 0xFF),
                          static_cast<unsigned>((ip >> 24) & 0xFF));
        } else {
            std::snprintf(buf, sizeof(buf), "nxlink: ON");
        }
    } else if (cur_running && cur_state == QdNxlinkServer::State::Receiving) {
        std::snprintf(buf, sizeof(buf), "nxlink: receiving...");
    } else if (cur_state == QdNxlinkServer::State::SocketInitFailed) {
        std::snprintf(buf, sizeof(buf), "nxlink: init failed");
    } else {
        std::snprintf(buf, sizeof(buf), "nxlink: OFF");
    }

    // Free old texture (P-B safe: FreeTexture uses pu::ui::render::DeleteTexture).
    FreeTexture(&tex_item_[8]);
    item_w_[8] = 0;
    item_h_[8] = 0;

    // Render new label.
    MakeText(r, pu::ui::DefaultFontSize::Medium, buf,
             kColorEnabled, &tex_item_[8], &item_w_[8], &item_h_[8]);

    // Update change-detection snapshot.
    last_dev_row_state_   = cur_state;
    last_dev_row_running_ = cur_running;

    UL_LOG_INFO("qdesktop: right dropdown row 8 refreshed: \"%s\"", buf);

    // ── Row 9: remote shell (v2.3.0) ─────────────────────────────────────────
    const QdRemoteShellServer::State cur_sh_state   = g_RemoteShellServer.GetState();
    const bool                       cur_sh_running  = g_RemoteShellServer.IsRunning();

    if (cur_sh_state != last_shell_row_state_ || cur_sh_running != last_shell_row_running_) {
        // v3.0.x: include PIN in the listening label (same format as Open()).
        char sh_buf[128];
        if (cur_sh_running && cur_sh_state == QdRemoteShellServer::State::Listening) {
            u32 ip = 0;
            const Result ip_rc = nifmGetCurrentIpAddress(&ip);
            const int pin = g_RemoteShellServer.GetAuthPin();
            if (R_SUCCEEDED(ip_rc) && ip != 0) {
                std::snprintf(sh_buf, sizeof(sh_buf),
                              "shell: %u.%u.%u.%u:9999 PIN:%06d",
                              static_cast<unsigned>(ip & 0xFF),
                              static_cast<unsigned>((ip >> 8) & 0xFF),
                              static_cast<unsigned>((ip >> 16) & 0xFF),
                              static_cast<unsigned>((ip >> 24) & 0xFF),
                              pin);
            } else {
                std::snprintf(sh_buf, sizeof(sh_buf), "shell: :9999 PIN:%06d", pin);
            }
        } else if (cur_sh_running && cur_sh_state == QdRemoteShellServer::State::Connected) {
            std::snprintf(sh_buf, sizeof(sh_buf), "shell: client connected");
        } else if (cur_sh_state == QdRemoteShellServer::State::SocketInitFailed) {
            std::snprintf(sh_buf, sizeof(sh_buf), "shell: init failed");
        } else {
            std::snprintf(sh_buf, sizeof(sh_buf), "shell: OFF");
        }

        FreeTexture(&tex_item_[9]);
        item_w_[9] = 0;
        item_h_[9] = 0;
        MakeText(r, pu::ui::DefaultFontSize::Medium, sh_buf,
                 kColorEnabled, &tex_item_[9], &item_w_[9], &item_h_[9]);

        last_shell_row_state_   = cur_sh_state;
        last_shell_row_running_ = cur_sh_running;
        UL_LOG_INFO("qdesktop: right dropdown row 9 refreshed: \"%s\"", sh_buf);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void QdHotCornerRightDropdown::Render(SDL_Renderer *r) {
    if (!open_ || r == nullptr) return;

    // v2.2.1: refresh the nxlink row label if server state has changed since
    // Open() or the last Render() call.  No-op when state is unchanged.
    RefreshDevRowLabel(r);

    const SDL_Rect outer { panel_x_,     panel_y_,     panel_w_,     panel_h_     };
    const SDL_Rect inner { panel_x_ + 1, panel_y_ + 1, panel_w_ - 2, panel_h_ - 2 };

    // Two-pass paint: identical to qd_HotCornerDropdown.cpp:217-225.
    // Pass 1 — cyan outer rounded rect (becomes the 1px border ring).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, kBorderFg.r, kBorderFg.g, kBorderFg.b, kBorderFg.a);
    FillRoundedRect(r, outer, kRadius);

    // Pass 2 — navy bg rounded rect (inset 1px so cyan ring remains visible).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, kPanelBg.r, kPanelBg.g, kPanelBg.b, kPanelBg.a);
    FillRoundedRect(r, inner, kRadius - 1);

    // Hover highlight — rectangular, inside the rounded zone.
    // kPadV == kRadius == 8 so the first row starts exactly at the end of the
    // top curve, keeping highlights fully inside the rounded border.
    if (hovered_ >= 0 && hovered_ < kRightDropdownItems && !disabled_[hovered_]) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kHoverBg.r, kHoverBg.g, kHoverBg.b, kHoverBg.a);
        const SDL_Rect hr { panel_x_ + 1, row_y_[hovered_],
                            panel_w_ - 2, row_h_[hovered_] };
        SDL_RenderFillRect(r, &hr);
    }

    // Item labels — vertically centred in their row.
    for (int i = 0; i < kRightDropdownItems; ++i) {
        if (tex_item_[i] == nullptr) continue;
        const int text_y = row_y_[i] + (kRowH - item_h_[i]) / 2;
        Blit(r, tex_item_[i],
             panel_x_ + kPadH, text_y,
             item_w_[i], item_h_[i]);
    }
}

// ── UpdateHover ───────────────────────────────────────────────────────────────
// Short-circuit when cursor has not moved (protects D-pad-set hovered_).

void QdHotCornerRightDropdown::UpdateHover(s32 x, s32 y) {
    if (!open_) return;
    if (x == prev_cursor_x_ && y == prev_cursor_y_) return;
    prev_cursor_x_ = x;
    prev_cursor_y_ = y;

    hovered_ = -1;
    if (x >= panel_x_ && x < panel_x_ + panel_w_) {
        for (int i = 0; i < kRightDropdownItems; ++i) {
            if (y >= row_y_[i] && y < row_y_[i] + row_h_[i]) {
                hovered_ = i;
                break;
            }
        }
    }
}

// ── TryClickAt ────────────────────────────────────────────────────────────────
// ZR-driven row activation.  Returns true if (x, y) hit any row.

bool QdHotCornerRightDropdown::TryClickAt(s32 x, s32 y) {
    if (!open_) return false;
    if (x < panel_x_ || x >= panel_x_ + panel_w_) return false;
    for (int i = 0; i < kRightDropdownItems; ++i) {
        if (y >= row_y_[i] && y < row_y_[i] + row_h_[i]) {
            if (!disabled_[i]) {
                FireAction(i);
            } else {
                Close();
            }
            return true;
        }
    }
    return false;
}

// ── FireAction ────────────────────────────────────────────────────────────────
// Rows 0-3 are status (disabled) — should never reach here but guard anyway.
// Rows 4-7 are actions; Close() is called before the action so the panel
// is gone before any modal / system transition begins.

void QdHotCornerRightDropdown::FireAction(int i) {
    UL_LOG_INFO("qdesktop: right hot-corner dropdown action %d fired", i);
    Close();
    switch (i) {
        case 4:  // Sleep
            appletStartSleepSequence(true);
            break;
        case 5:  // Restart
            // HW-reported bug (2026-05-19): the previous implementation called
            // ul::menu::qdesktop::power::Reboot() which uses bpcRebootSystem()
            // directly.  On HOS's own power menu, that IPC chains through
            // hekate via the established CFW boot chain; from a uMenu
            // SystemApplet context it instead rebooted to OFW (stock
            // firmware) — bypassing Q OS entirely.  Root cause is
            // SystemApplet permission / NPDM mismatch with the hekate
            // chain expectations.
            //
            // Fix: route Restart through the SAME path as "Reboot to Hekate"
            // (case 6 below) — launch tomvita's reboot_to_hekate.nro which
            // stages reboot_payload.bin via bpcamsSetRebootPayload first.
            // Hekate then chains back into Atmosphère + uMenu via the user's
            // auto-boot config (the same way hold-power → Restart does).
            //
            // Restart and "Reboot to Hekate" are now functionally identical
            // (both land in Atmosphère via the same chain).  The two menu
            // items stay for now — Restart is the user-friendly primary,
            // "Reboot to Hekate" is the explicit "advanced" label.  We can
            // collapse them later if the explicit label adds no value.
            UL_LOG_INFO("qdesktop: action 5 (Restart) — launching reboot_to_hekate.nro "
                        "(HW-fix: bpcRebootSystem alone goes to OFW from SystemApplet)");
            smi::LaunchHomebrewLibraryApplet(
                std::string("sdmc:/switch/reboot_to_hekate.nro"), std::string(""));
            if (g_MenuApplication) {
                g_MenuApplication->FadeOutToNonLibraryApplet();
                g_MenuApplication->Finalize();
            }
            break;
        case 6:  // Reboot to Hekate — explicit advanced label, same path as 5
            // Q OS design principle (2026-05-17): compose existing overlapping
            // projects, don't reinvent. Tomvita's reboot_to_hekate.nro is the
            // canonical CFW-community implementation of the bpc:ams reboot
            // chain. We launch it via the same SMI path the Launchpad uses
            // for every other homebrew NRO (qd_DesktopIcons.cpp:4867-4879).
            //
            // Earlier in-uMenu reimplementations of RebootToHekate (qd_Power.cpp)
            // failed silently on this Switch's specific environment for reasons
            // we couldn't pin down — but the NRO works reliably from the
            // Launchpad, so delegating composes the working solution into our UI.
            UL_LOG_INFO("qdesktop: action 6 — launching sdmc:/switch/reboot_to_hekate.nro");
            smi::LaunchHomebrewLibraryApplet(
                std::string("sdmc:/switch/reboot_to_hekate.nro"), std::string(""));
            // CRITICAL (cycle C1 pattern from qd_DesktopIcons.cpp:4869-4879):
            // mirror the upstream MainMenuLayout::HandleHomebrewLaunch path.
            // Without FadeOutToNonLibraryApplet + Finalize, uMenu re-asserts
            // foreground after the SMI returns and silently kills the launched
            // NRO before it can execute.
            if (g_MenuApplication) {
                g_MenuApplication->FadeOutToNonLibraryApplet();
                g_MenuApplication->Finalize();
            }
            break;
        case 7:  // Lock Screen
            // v1.10.3.10.5 main-thread fix: libnx has no appletStartLockScreen
            // primitive — the lock screen is launched as a system applet.
            // Closest libnx-supported substitute is appletStartSleepSequence
            // which wakes to the lock screen on retail.  When a public lock-
            // screen-applet API lands in libnx, swap this in.
            appletStartSleepSequence(true);
            break;
        case 8:  // Dev: nxlink server toggle (v2.2.0)
            // Toggle the inbound nxlink server on/off.  Idempotent — Start()
            // returns true if already running.  After toggling, the user must
            // re-open the dropdown to see the updated label (Open()-time
            // snapshot model).  This is the primary user-facing entry to
            // Phase 1 of the HBmenu absorption work — auto-start was rolled
            // back to here so socketInitializeDefault() can never block the
            // boot-time audio callback.
            if (ul::menu::qdesktop::dev::IsNxlinkServerActive()) {
                UL_LOG_INFO("qdesktop: dev row tapped — disabling nxlink server");
                ul::menu::qdesktop::dev::DisableNxlinkServer();
            } else {
                UL_LOG_INFO("qdesktop: dev row tapped — enabling nxlink server");
                ul::menu::qdesktop::dev::TryEnableNxlinkServer();
            }
            break;
        case 9:  // Dev: remote shell toggle (v2.3.0)
            // Toggle the TCP remote shell on port 9999.  Developers connect via
            // `telnet <ip> 9999` or `nc <ip> 9999`.  Same idiom as row 8:
            // label auto-refreshes in the next Render() call after toggle.
            if (ul::menu::qdesktop::dev::IsRemoteShellActive()) {
                UL_LOG_INFO("qdesktop: dev row tapped — disabling remote shell");
                ul::menu::qdesktop::dev::DisableRemoteShell();
            } else {
                UL_LOG_INFO("qdesktop: dev row tapped — enabling remote shell");
                ul::menu::qdesktop::dev::TryEnableRemoteShell();
            }
            break;
        case 10:  // Dev: remote-test debug server toggle (HTTP :6010)
            // The in-OS HTTP debug server (/ping /state /screenshot).  Off by
            // default — manual toggle, like rows 8/9.  Re-open to refresh label.
            if (g_DebugServer.IsRunning()) {
                UL_LOG_INFO("qdesktop: dev row tapped — disabling debug server");
                g_DebugServer.Stop();
            } else {
                UL_LOG_INFO("qdesktop: dev row tapped — enabling debug server");
                g_DebugServer.Start();
            }
            break;
        default:
            // Rows 0-3 (disabled status rows) — no action.
            break;
    }
}

// ── HandleInput ───────────────────────────────────────────────────────────────
// Mirrors qd_HotCornerDropdown::HandleInput exactly.

bool QdHotCornerRightDropdown::HandleInput(u64 keys_down, u64 keys_held,
                                            s32 touch_x, s32 touch_y) {
    (void)keys_held;
    if (!open_) return false;

    // B or Plus dismisses without firing an action.
    if ((keys_down & HidNpadButton_B)    != 0u ||
        (keys_down & HidNpadButton_Plus) != 0u) {
        UL_LOG_INFO("qdesktop: right hot-corner dropdown dismissed via button");
        Close();
        return true;
    }

    // D-pad navigation: Up/Down move focus, A fires.
    if ((keys_down & (HidNpadButton_Up | HidNpadButton_Down)) != 0u) {
        const int dir = (keys_down & HidNpadButton_Up) ? -1 : +1;
        int n = (hovered_ < 0)
              ? (dir > 0 ? -1 : kRightDropdownItems)
              : hovered_;
        for (int tries = 0; tries < kRightDropdownItems; ++tries) {
            n += dir;
            if (n < 0) n = kRightDropdownItems - 1;
            if (n >= kRightDropdownItems) n = 0;
            if (!disabled_[n]) {
                hovered_ = n;
                UL_LOG_INFO("qdesktop: right hot-corner dropdown D-pad -> %d", n);
                break;
            }
        }
        return true;
    }
    if ((keys_down & HidNpadButton_A) != 0u) {
        if (hovered_ >= 0 && hovered_ < kRightDropdownItems && !disabled_[hovered_]) {
            const int fired = hovered_;
            hovered_ = -1;
            FireAction(fired);
        }
        return true;
    }

    // Touch: track hover while finger is down; fire on lift.
    if (touch_x >= 0 && touch_y >= 0) {
        was_touch_active_internal_ = true;

        // v2.2.2: hot-corner zone (y < LP_HOTCORNER_H = 72) is ALWAYS a toggle
        // surface, never a row-tap surface.  v2.1.0's panel_y_=40 reposition
        // creates a 32-px overlap between the hot zone and the panel's first
        // row (Battery, disabled) — without this guard, a tap at y∈[40,72]
        // would hit row 0 and trigger the disabled-row-close path on release,
        // making the same tap that OPENED the dropdown immediately close it.
        // Force hovered_=-1 in the overlap so release does not fire any row.
        const bool in_hot_corner_zone = (touch_y < static_cast<s32>(LP_HOTCORNER_H));

        hovered_ = -1;
        if (!in_hot_corner_zone &&
            touch_x >= panel_x_ && touch_x < panel_x_ + panel_w_) {
            for (int i = 0; i < kRightDropdownItems; ++i) {
                if (touch_y >= row_y_[i] && touch_y < row_y_[i] + row_h_[i]) {
                    hovered_ = i;
                    break;
                }
            }
        }

        // Outside-tap close is armed only after the first no-touch frame.
        // v2.2.2: hot-corner-zone tap (in_hot_corner_zone) is treated as
        // outside-tap regardless of x — that's the toggle-close behavior the
        // user expects ("tap hot corner toggles dropdown").  Without this, a
        // tap at e.g. (1850, 30) is x-inside but y-outside the panel and
        // would already fire the close, BUT a tap at (1850, 50) is in the
        // overlap zone (x-inside AND y-inside the panel) and would NOT close
        // — breaking toggle expectation.  Force-close on any hot-corner-zone
        // tap once armed.
        if (armed_for_outside_close_) {
            const bool outside_panel =
                (touch_x < panel_x_ || touch_x >= panel_x_ + panel_w_ ||
                 touch_y < panel_y_ || touch_y >= panel_y_ + panel_h_);
            if (outside_panel || in_hot_corner_zone) {
                UL_LOG_INFO("qdesktop: right hot-corner dropdown dismissed via %s tap",
                            in_hot_corner_zone ? "hot-corner toggle" : "outside");
                Close();
            }
        }
        return true;
    }

    // No touch this frame.
    armed_for_outside_close_ = true;  // finger has lifted at least once

    const bool just_released = was_touch_active_internal_;
    was_touch_active_internal_ = false;

    // BUG-7 — consume the first lift after a touch-open (hot-corner tap).
    // Without this, natural finger drift during the lift sometimes set
    // hovered_ to a non-negative row index and the lift fired that action
    // immediately, defeating the open intent ("you have to keep your
    // finger pressed to navigate" complaint).  After consumption, the
    // dropdown stays open and a subsequent tap fires rows normally.
    if (just_released && skip_first_lift_) {
        skip_first_lift_ = false;
        hovered_ = -1;
        UL_LOG_INFO("qdesktop: right hot-corner dropdown first-lift skipped");
        return true;
    }

    if (just_released && hovered_ >= 0 && hovered_ < kRightDropdownItems) {
        const int fired = hovered_;
        hovered_ = -1;
        if (!disabled_[fired]) {
            FireAction(fired);
        } else {
            Close();
        }
        return true;
    }

    return true;  // dropdown is open — consume all input
}

} // namespace ul::menu::qdesktop
