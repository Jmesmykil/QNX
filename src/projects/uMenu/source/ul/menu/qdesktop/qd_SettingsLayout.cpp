// qd_SettingsLayout.cpp — Q OS-native Settings layout implementation.
// Six tabs with real libnx data: System, Network, Audio, Display, Account, About.
// Replaces upstream ui_SettingsMenuLayout for the qdesktop surface.
// All libnx service calls are scoped open/close inside Refresh() — no persistent handles.

#include <ul/menu/qdesktop/qd_SettingsLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>  // g_MenuApplication, MenuType, ShowSettingsMenu
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <switch/services/psm.h>
#include <switch/services/nifm.h>
#include <switch/services/audctl.h>
#include <switch/services/ts.h>
#include <switch/services/lbl.h>
#include <switch/services/applet.h>
#include <switch/services/acc.h>
#include <switch/applets/psel.h>
#include <switch/arm/counter.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

// ── Extern globals (same pattern as qd_DesktopIcons.cpp) ─────────────────────
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
// g_GlobalSettings is defined at global scope in main.cpp / ui_MenuApplication.cpp.
// Declared here (file scope, NOT inside the namespace below) so the linker resolves
// it correctly.  In-namespace extern declarations would create a distinct symbol
// ul::menu::qdesktop::g_GlobalSettings which does not exist.
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── Static label arrays ───────────────────────────────────────────────────────

const char *const QdSettingsElement::SIDEBAR_LABELS[SIDEBAR_ITEM_COUNT] = {
    "System",
    "Network",
    "Audio",
    "Display",
    "Account",
    "About",
    "System Settings \xE2\x86\x92",   // UTF-8 right arrow (→)
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static SDL_Texture *MakeText(const char *str, const pu::ui::Color &clr) {
    return pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string(str),
        clr);
}

static SDL_Texture *MakeTextMedium(const char *str, const pu::ui::Color &clr) {
    return pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        std::string(str),
        clr);
}

static void DrawFilledRect(SDL_Renderer *r, s32 x, s32 y, s32 w, s32 h,
                           u8 rr, u8 gg, u8 bb, u8 aa) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
    const SDL_Rect rect{ x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

[[maybe_unused]] static void DrawOutlineRect(SDL_Renderer *r, s32 x, s32 y, s32 w, s32 h,
                                             u8 rr, u8 gg, u8 bb, u8 aa) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
    const SDL_Rect rect{ x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

static void BlitTexture(SDL_Renderer *r, SDL_Texture *tex, s32 x, s32 y) {
    if (tex == nullptr) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    const SDL_Rect dst{ x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── QdSettingsElement ctor / dtor ─────────────────────────────────────────────

QdSettingsElement::QdSettingsElement(const QdTheme &theme)
    : account_uid_{},
      acc_has_user_(false),
      title_tex_(nullptr),
      theme_(theme),
      focus_area_(FocusArea::Sidebar),
      active_tab_(SettingsTab::System),
      sidebar_focus_row_(0),
      detail_row_(0)
{
    UL_LOG_INFO("settings: QdSettingsElement ctor");

    // Zero all char buffers.
    memset(sys_fw_,         0, sizeof(sys_fw_));
    memset(sys_serial_,     0, sizeof(sys_serial_));
    memset(sys_uptime_,     0, sizeof(sys_uptime_));
    memset(sys_ams_,        0, sizeof(sys_ams_));
    memset(sys_temp_pcb_,   0, sizeof(sys_temp_pcb_));
    memset(sys_temp_soc_,   0, sizeof(sys_temp_soc_));
    memset(sys_mode_,       0, sizeof(sys_mode_));
    memset(sys_boot_count_, 0, sizeof(sys_boot_count_));

    memset(net_status_,     0, sizeof(net_status_));
    memset(net_ip_,         0, sizeof(net_ip_));
    memset(net_strength_,   0, sizeof(net_strength_));
    memset(net_wifi_,       0, sizeof(net_wifi_));
    memset(net_ethernet_,   0, sizeof(net_ethernet_));

    memset(aud_volume_,     0, sizeof(aud_volume_));
    memset(aud_bt_,         0, sizeof(aud_bt_));
    memset(aud_nfc_,        0, sizeof(aud_nfc_));

    memset(disp_brightness_,0, sizeof(disp_brightness_));
    memset(disp_mode_,      0, sizeof(disp_mode_));
    memset(disp_ambient_,   0, sizeof(disp_ambient_));
    memset(disp_usb30_,     0, sizeof(disp_usb30_));

    memset(acc_nickname_,   0, sizeof(acc_nickname_));
    memset(acc_language_,   0, sizeof(acc_language_));

    memset(abt_fw_,         0, sizeof(abt_fw_));
    memset(abt_serial_,     0, sizeof(abt_serial_));
    memset(abt_ams_ver_,    0, sizeof(abt_ams_ver_));
    memset(abt_ams_emummc_, 0, sizeof(abt_ams_emummc_));
    memset(abt_region_,     0, sizeof(abt_region_));
    memset(abt_nickname_,   0, sizeof(abt_nickname_));
    memset(abt_battery_lot_,0, sizeof(abt_battery_lot_));

    // Null-initialize texture arrays.
    for (auto &t : sidebar_tex_) t = nullptr;
    for (auto &t : detail_tex_)  t = nullptr;

    // Null-initialize row arrays.
    for (auto &row : system_rows_)  { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : network_rows_) { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : audio_rows_)   { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : display_rows_) { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : account_rows_) { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : about_rows_)   { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
}

QdSettingsElement::~QdSettingsElement() {
    UL_LOG_INFO("settings: QdSettingsElement dtor");
    FreeAllTextures();
}

void QdSettingsElement::FreeAllTextures() {
    for (auto &t : sidebar_tex_) {
        if (t) { pu::ui::render::DeleteTexture(t); }
    }
    for (auto &t : detail_tex_) {
        if (t) { pu::ui::render::DeleteTexture(t); }
    }
    if (title_tex_) { pu::ui::render::DeleteTexture(title_tex_); }
}

// ── Refresh: poll all live system data ────────────────────────────────────────

void QdSettingsElement::Refresh() {
    UL_LOG_INFO("settings: Refresh()");

    // ── Uptime ────────────────────────────────────────────────────────────
    {
        const u64 tick  = armGetSystemTick();
        const u64 secs  = tick / 19200000ULL;
        const u64 hours = secs / 3600;
        const u64 mins  = (secs % 3600) / 60;
        snprintf(sys_uptime_, sizeof(sys_uptime_), "%lluh %02llum",
                 (unsigned long long)hours, (unsigned long long)mins);
    }

    // ── Temperature (ts) ─────────────────────────────────────────────────
    {
        if (R_SUCCEEDED(tsInitialize())) {
            s32 pcb = 0, soc = 0;
            if (R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &pcb))) {
                snprintf(sys_temp_pcb_, sizeof(sys_temp_pcb_), "%d\xC2\xB0""C", pcb);
            } else {
                strncpy(sys_temp_pcb_, "n/a", sizeof(sys_temp_pcb_));
            }
            if (R_SUCCEEDED(tsGetTemperature(TsLocation_External, &soc))) {
                snprintf(sys_temp_soc_, sizeof(sys_temp_soc_), "%d\xC2\xB0""C", soc);
            } else {
                strncpy(sys_temp_soc_, "n/a", sizeof(sys_temp_soc_));
            }
            tsExit();
        } else {
            strncpy(sys_temp_pcb_, "n/a", sizeof(sys_temp_pcb_));
            strncpy(sys_temp_soc_, "n/a", sizeof(sys_temp_soc_));
        }
    }

    // ── Operation mode ────────────────────────────────────────────────────
    {
        const AppletOperationMode mode = appletGetOperationMode();
        strncpy(sys_mode_,
                mode == AppletOperationMode_Console ? "Docked" : "Handheld",
                sizeof(sys_mode_));
        strncpy(disp_mode_,
                mode == AppletOperationMode_Console ? "Docked" : "Handheld",
                sizeof(disp_mode_));
    }

    // ── Firmware version & serial (from g_GlobalSettings set at boot) ────
    // GlobalSettings is populated by main.cpp before any layout is shown.
    // Access it via the file-scope extern declared above.
    {
        // Firmware string: major.minor.micro
        snprintf(sys_fw_, sizeof(sys_fw_), "%d.%d.%d",
                 g_GlobalSettings.fw_version.major,
                 g_GlobalSettings.fw_version.minor,
                 g_GlobalSettings.fw_version.micro);
        strncpy(abt_fw_, sys_fw_, sizeof(abt_fw_));

        // Serial: mask all but last 4 chars.
        const char *raw_serial = g_GlobalSettings.serial_no.number;
        const size_t slen = strnlen(raw_serial, sizeof(g_GlobalSettings.serial_no.number));
        if (slen <= 4) {
            strncpy(sys_serial_, raw_serial, sizeof(sys_serial_));
        } else {
            const size_t mask_len = slen - 4;
            size_t out = 0;
            for (size_t i = 0; i < mask_len && out < sizeof(sys_serial_) - 1; ++i) {
                sys_serial_[out++] = '*';
            }
            for (size_t i = mask_len; i < slen && out < sizeof(sys_serial_) - 1; ++i) {
                sys_serial_[out++] = raw_serial[i];
            }
            sys_serial_[out] = '\0';
        }
        strncpy(abt_serial_, sys_serial_, sizeof(abt_serial_));

        // Atmosphère version.
        snprintf(sys_ams_, sizeof(sys_ams_), "%u.%u.%u%s",
                 g_GlobalSettings.ams_version.major,
                 g_GlobalSettings.ams_version.minor,
                 g_GlobalSettings.ams_version.micro,
                 g_GlobalSettings.ams_is_emummc ? " / EmuNAND" : "");
        strncpy(abt_ams_ver_,    sys_ams_, sizeof(abt_ams_ver_));
        strncpy(abt_ams_emummc_, g_GlobalSettings.ams_is_emummc ? "Yes" : "No",
                sizeof(abt_ams_emummc_));

        // Device nickname — truncate to 35 chars to fit acc_nickname_[36].
        snprintf(acc_nickname_, sizeof(acc_nickname_), "%.35s",
                 g_GlobalSettings.nickname.nickname);
        strncpy(abt_nickname_, acc_nickname_, sizeof(abt_nickname_));

        // Language code: packed u64 treated as 8-byte NUL-terminated string.
        // SetLanguage is an enum; use setGetLanguage to get the code string.
        // g_GlobalSettings.language is a SetLanguage enum.  Map common values.
        // The language code string is obtained via setMakeLanguage from the code.
        // However since g_GlobalSettings only stores the enum we compose via table.
        static const char *const lang_names[] = {
            "ja",    // SetLanguage_JA
            "en-US", // SetLanguage_ENUS
            "fr",    // SetLanguage_FR
            "de",    // SetLanguage_DE
            "it",    // SetLanguage_IT
            "es",    // SetLanguage_ES
            "zh-CN", // SetLanguage_ZHCN
            "ko",    // SetLanguage_KO
            "nl",    // SetLanguage_NL
            "pt",    // SetLanguage_PT
            "ru",    // SetLanguage_RU
            "zh-TW", // SetLanguage_ZHTW
            "en-GB", // SetLanguage_ENGB
            "fr-CA", // SetLanguage_FRCA
            "es-419",// SetLanguage_ES419
            "zh-Hans",//SetLanguage_ZHHANS
            "zh-Hant",//SetLanguage_ZHHANT
            "pt-BR", // SetLanguage_PTBR
        };
        const int lang_idx = static_cast<int>(g_GlobalSettings.language);
        if (lang_idx >= 0 &&
            lang_idx < static_cast<int>(sizeof(lang_names)/sizeof(lang_names[0]))) {
            snprintf(acc_language_, sizeof(acc_language_), "%s", lang_names[lang_idx]);
        } else {
            snprintf(acc_language_, sizeof(acc_language_), "(%d)", lang_idx);
        }

        // Battery lot.
        snprintf(abt_battery_lot_, sizeof(abt_battery_lot_), "%s",
                 g_GlobalSettings.battery_lot.lot);
        if (abt_battery_lot_[0] == '\0') {
            strncpy(abt_battery_lot_, "n/a", sizeof(abt_battery_lot_));
        }

        // Region code.
        static const char *const region_names[] = {
            "JPN", "USA", "EUR", "AUS", "HTK", "CHN"
        };
        const int reg = static_cast<int>(g_GlobalSettings.region);
        if (reg >= 0 && reg < 6) {
            snprintf(abt_region_, sizeof(abt_region_), "%s", region_names[reg]);
        } else {
            snprintf(abt_region_, sizeof(abt_region_), "(%d)", reg);
        }

        // Wi-Fi enabled flag.
        strncpy(net_wifi_,
                g_GlobalSettings.wireless_lan_enabled ? "Enabled" : "Disabled",
                sizeof(net_wifi_));

        // Bluetooth.
        strncpy(aud_bt_,
                g_GlobalSettings.bluetooth_enabled ? "Enabled" : "Disabled",
                sizeof(aud_bt_));

        // NFC.
        strncpy(aud_nfc_,
                g_GlobalSettings.nfc_enabled ? "Enabled" : "Disabled",
                sizeof(aud_nfc_));

        // USB 3.0.
        strncpy(disp_usb30_,
                g_GlobalSettings.usb30_enabled ? "Enabled" : "Disabled",
                sizeof(disp_usb30_));
    }

    // ── Boot count ───────────────────────────────────────────────────────
    {
        FILE *f = fopen("/qos-shell/logs/uMenu.bootseq", "r");
        if (f) {
            int count = 0;
            if (fscanf(f, "%d", &count) == 1) {
                snprintf(sys_boot_count_, sizeof(sys_boot_count_), "%d", count);
            } else {
                strncpy(sys_boot_count_, "n/a", sizeof(sys_boot_count_));
            }
            fclose(f);
        } else {
            strncpy(sys_boot_count_, "n/a", sizeof(sys_boot_count_));
        }
    }

    // ── Network (nifm) ────────────────────────────────────────────────────
    {
        if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
            NifmInternetConnectionType conn_type = NifmInternetConnectionType_WiFi;
            u32 wifi_strength = 0;
            NifmInternetConnectionStatus conn_status = NifmInternetConnectionStatus_ConnectingUnknown1;

            const Result rc_status = nifmGetInternetConnectionStatus(
                &conn_type, &wifi_strength, &conn_status);

            if (R_SUCCEEDED(rc_status) &&
                conn_status == NifmInternetConnectionStatus_Connected) {
                const char *type_str =
                    (conn_type == NifmInternetConnectionType_Ethernet) ? "Ethernet" : "Wi-Fi";
                snprintf(net_status_, sizeof(net_status_), "Connected (%s)", type_str);
                if (conn_type == NifmInternetConnectionType_WiFi) {
                    snprintf(net_strength_, sizeof(net_strength_), "%u", wifi_strength);
                } else {
                    strncpy(net_strength_, "—", sizeof(net_strength_));
                }
                strncpy(net_ethernet_,
                        (conn_type == NifmInternetConnectionType_Ethernet) ?
                            "Active" : "Not primary",
                        sizeof(net_ethernet_));

                u32 ip = 0;
                if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip))) {
                    snprintf(net_ip_, sizeof(net_ip_), "%u.%u.%u.%u",
                             (ip >>  0) & 0xFF,
                             (ip >>  8) & 0xFF,
                             (ip >> 16) & 0xFF,
                             (ip >> 24) & 0xFF);
                } else {
                    strncpy(net_ip_, "n/a", sizeof(net_ip_));
                }
            } else {
                strncpy(net_status_,   "No connection", sizeof(net_status_));
                strncpy(net_ip_,       "—",             sizeof(net_ip_));
                strncpy(net_strength_, "—",             sizeof(net_strength_));
                strncpy(net_ethernet_, "Not connected", sizeof(net_ethernet_));
            }
            nifmExit();
        } else {
            strncpy(net_status_,   "Service unavailable", sizeof(net_status_));
            strncpy(net_ip_,       "—",                  sizeof(net_ip_));
            strncpy(net_strength_, "—",                  sizeof(net_strength_));
            strncpy(net_ethernet_, "—",                  sizeof(net_ethernet_));
        }
    }

    // ── Audio volume (audctl) ─────────────────────────────────────────────
    {
        if (R_SUCCEEDED(audctlInitialize())) {
            float vol = 0.0f;
            if (R_SUCCEEDED(audctlGetSystemOutputMasterVolume(&vol))) {
                snprintf(aud_volume_, sizeof(aud_volume_), "%.0f%%", vol * 100.0f);
            } else {
                strncpy(aud_volume_, "n/a", sizeof(aud_volume_));
            }
            audctlExit();
        } else {
            strncpy(aud_volume_, "n/a", sizeof(aud_volume_));
        }
    }

    // ── Display brightness (lbl) ──────────────────────────────────────────
    {
        const AppletOperationMode mode = appletGetOperationMode();
        if (mode == AppletOperationMode_Console) {
            strncpy(disp_brightness_, "n/a (TV mode)", sizeof(disp_brightness_));
            strncpy(disp_ambient_,    "n/a (TV mode)", sizeof(disp_ambient_));
        } else {
            if (R_SUCCEEDED(lblInitialize())) {
                float brightness = 0.0f;
                if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&brightness))) {
                    snprintf(disp_brightness_, sizeof(disp_brightness_),
                             "%.0f%%", brightness * 100.0f);
                } else {
                    strncpy(disp_brightness_, "n/a", sizeof(disp_brightness_));
                }
                bool over_limit = false;
                float lux = 0.0f;
                if (R_SUCCEEDED(lblGetAmbientLightSensorValue(&over_limit, &lux))) {
                    snprintf(disp_ambient_, sizeof(disp_ambient_),
                             "%.0f lux%s", lux, over_limit ? " (!)" : "");
                } else {
                    strncpy(disp_ambient_, "n/a", sizeof(disp_ambient_));
                }
                lblExit();
            } else {
                strncpy(disp_brightness_, "n/a", sizeof(disp_brightness_));
                strncpy(disp_ambient_,    "n/a", sizeof(disp_ambient_));
            }
        }
    }

    // ── Account / selected user ───────────────────────────────────────────
    {
        account_uid_ = g_GlobalSettings.system_status.selected_user;
        acc_has_user_ = (account_uid_.uid[0] != 0 || account_uid_.uid[1] != 0);

        if (acc_has_user_) {
            AccountProfile profile;
            if (R_SUCCEEDED(accountGetProfile(&profile, account_uid_))) {
                AccountProfileBase base;
                memset(&base, 0, sizeof(base));
                if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                    strncpy(acc_nickname_, base.nickname, sizeof(acc_nickname_) - 1);
                    acc_nickname_[sizeof(acc_nickname_) - 1] = '\0';
                    strncpy(abt_nickname_, acc_nickname_, sizeof(abt_nickname_));
                }
                accountProfileClose(&profile);
            }
        } else {
            strncpy(acc_nickname_, "(no user)", sizeof(acc_nickname_));
            strncpy(abt_nickname_, "(no user)", sizeof(abt_nickname_));
        }
    }

    // ── Build all Row arrays from cached data ─────────────────────────────
    BuildRows();

    // ── Rebuild all textures ───────────────────────────────────────────────
    FreeAllTextures();

    title_tex_ = MakeTextMedium("Settings", theme_.text_primary);

    for (size_t i = 0; i < SIDEBAR_ITEM_COUNT; ++i) {
        sidebar_tex_[i] = MakeText(SIDEBAR_LABELS[i], theme_.text_primary);
    }

    // Detail textures: [tab * DETAIL_TEX_STRIDE + row * 2 + 0] = label
    //                  [tab * DETAIL_TEX_STRIDE + row * 2 + 1] = value
    auto build_tab_textures = [&](SettingsTab tab,
                                  const Row *rows, size_t n_rows) {
        const size_t base = static_cast<size_t>(tab) * DETAIL_TEX_STRIDE;
        for (size_t i = 0; i < n_rows; ++i) {
            if (rows[i].label) {
                detail_tex_[base + i * 2 + 0] =
                    MakeText(rows[i].label, theme_.text_secondary);
            }
            detail_tex_[base + i * 2 + 1] =
                MakeText(rows[i].value,
                         rows[i].is_button ? theme_.accent : theme_.text_primary);
        }
    };

    build_tab_textures(SettingsTab::System,  system_rows_,  SYSTEM_ROW_COUNT);
    build_tab_textures(SettingsTab::Network, network_rows_, NETWORK_ROW_COUNT);
    build_tab_textures(SettingsTab::Audio,   audio_rows_,   AUDIO_ROW_COUNT);
    build_tab_textures(SettingsTab::Display, display_rows_, DISPLAY_ROW_COUNT);
    build_tab_textures(SettingsTab::Account, account_rows_, ACCOUNT_ROW_COUNT);
    build_tab_textures(SettingsTab::About,   about_rows_,   ABOUT_ROW_COUNT);

    UL_LOG_INFO("settings: Refresh() complete");
}

// ── BuildRows: populate Row arrays from cached fields ─────────────────────────

void QdSettingsElement::BuildRows() {
    // System
    system_rows_[0] = { "Firmware",    {}, false };
    system_rows_[1] = { "Serial",      {}, false };
    system_rows_[2] = { "Uptime",      {}, false };
    system_rows_[3] = { "AMS / CFW",   {}, false };
    system_rows_[4] = { "Temp (PCB)",  {}, false };
    system_rows_[5] = { "Temp (SoC)",  {}, false };
    system_rows_[6] = { "Mode",        {}, false };
    system_rows_[7] = { "Boot count",  {}, false };
    strncpy(system_rows_[0].value, sys_fw_,         sizeof(system_rows_[0].value) - 1);
    strncpy(system_rows_[1].value, sys_serial_,     sizeof(system_rows_[1].value) - 1);
    strncpy(system_rows_[2].value, sys_uptime_,     sizeof(system_rows_[2].value) - 1);
    strncpy(system_rows_[3].value, sys_ams_,        sizeof(system_rows_[3].value) - 1);
    strncpy(system_rows_[4].value, sys_temp_pcb_,   sizeof(system_rows_[4].value) - 1);
    strncpy(system_rows_[5].value, sys_temp_soc_,   sizeof(system_rows_[5].value) - 1);
    strncpy(system_rows_[6].value, sys_mode_,       sizeof(system_rows_[6].value) - 1);
    strncpy(system_rows_[7].value, sys_boot_count_, sizeof(system_rows_[7].value) - 1);

    // Network
    network_rows_[0] = { "Status",    {}, false };
    network_rows_[1] = { "IP address",{}, false };
    network_rows_[2] = { "Wi-Fi",     {}, false };
    network_rows_[3] = { "Signal",    {}, false };
    network_rows_[4] = { "Ethernet",  {}, false };
    strncpy(network_rows_[0].value, net_status_,   sizeof(network_rows_[0].value) - 1);
    strncpy(network_rows_[1].value, net_ip_,       sizeof(network_rows_[1].value) - 1);
    strncpy(network_rows_[2].value, net_wifi_,     sizeof(network_rows_[2].value) - 1);
    strncpy(network_rows_[3].value, net_strength_, sizeof(network_rows_[3].value) - 1);
    strncpy(network_rows_[4].value, net_ethernet_, sizeof(network_rows_[4].value) - 1);

    // Audio
    audio_rows_[0] = { "Volume",    {}, false };
    audio_rows_[1] = { "Bluetooth", {}, false };
    audio_rows_[2] = { "NFC",       {}, false };
    strncpy(audio_rows_[0].value, aud_volume_, sizeof(audio_rows_[0].value) - 1);
    strncpy(audio_rows_[1].value, aud_bt_,     sizeof(audio_rows_[1].value) - 1);
    strncpy(audio_rows_[2].value, aud_nfc_,    sizeof(audio_rows_[2].value) - 1);

    // Display
    display_rows_[0] = { "Brightness", {}, false };
    display_rows_[1] = { "Mode",       {}, false };
    display_rows_[2] = { "Ambient",    {}, false };
    display_rows_[3] = { "USB 3.0",    {}, false };
    strncpy(display_rows_[0].value, disp_brightness_, sizeof(display_rows_[0].value) - 1);
    strncpy(display_rows_[1].value, disp_mode_,       sizeof(display_rows_[1].value) - 1);
    strncpy(display_rows_[2].value, disp_ambient_,    sizeof(display_rows_[2].value) - 1);
    strncpy(display_rows_[3].value, disp_usb30_,      sizeof(display_rows_[3].value) - 1);

    // Account (last row is the "Switch User" button)
    account_rows_[0] = { "Nickname",    {}, false };
    account_rows_[1] = { "Language",    {}, false };
    account_rows_[2] = { "Device name", {}, false };
    account_rows_[3] = { nullptr,       {}, true  }; // activatable button
    strncpy(account_rows_[0].value, acc_nickname_, sizeof(account_rows_[0].value) - 1);
    strncpy(account_rows_[1].value, acc_language_, sizeof(account_rows_[1].value) - 1);
    {
        // Truncate to 47 chars to fit value[48].
        snprintf(account_rows_[2].value, sizeof(account_rows_[2].value), "%.47s",
                 g_GlobalSettings.nickname.nickname);
    }
    strncpy(account_rows_[3].value, "Switch User", sizeof(account_rows_[3].value) - 1);

    // About
    about_rows_[0] = { "Firmware",   {}, false };
    about_rows_[1] = { "Serial",     {}, false };
    about_rows_[2] = { "AMS",        {}, false };
    about_rows_[3] = { "EmuNAND",    {}, false };
    about_rows_[4] = { "Region",     {}, false };
    about_rows_[5] = { "Username",   {}, false };
    about_rows_[6] = { "Battery lot",{}, false };
    strncpy(about_rows_[0].value, abt_fw_,         sizeof(about_rows_[0].value) - 1);
    strncpy(about_rows_[1].value, abt_serial_,     sizeof(about_rows_[1].value) - 1);
    strncpy(about_rows_[2].value, abt_ams_ver_,    sizeof(about_rows_[2].value) - 1);
    strncpy(about_rows_[3].value, abt_ams_emummc_, sizeof(about_rows_[3].value) - 1);
    strncpy(about_rows_[4].value, abt_region_,     sizeof(about_rows_[4].value) - 1);
    strncpy(about_rows_[5].value, abt_nickname_,   sizeof(about_rows_[5].value) - 1);
    strncpy(about_rows_[6].value, abt_battery_lot_,sizeof(about_rows_[6].value) - 1);
}

// ── ActiveTab helpers ─────────────────────────────────────────────────────────

size_t QdSettingsElement::ActiveTabRowCount() const {
    switch (active_tab_) {
        case SettingsTab::System:  return SYSTEM_ROW_COUNT;
        case SettingsTab::Network: return NETWORK_ROW_COUNT;
        case SettingsTab::Audio:   return AUDIO_ROW_COUNT;
        case SettingsTab::Display: return DISPLAY_ROW_COUNT;
        case SettingsTab::Account: return ACCOUNT_ROW_COUNT;
        case SettingsTab::About:   return ABOUT_ROW_COUNT;
        default:                   return 0;
    }
}

const QdSettingsElement::Row *QdSettingsElement::ActiveTabRows() const {
    switch (active_tab_) {
        case SettingsTab::System:  return system_rows_;
        case SettingsTab::Network: return network_rows_;
        case SettingsTab::Audio:   return audio_rows_;
        case SettingsTab::Display: return display_rows_;
        case SettingsTab::Account: return account_rows_;
        case SettingsTab::About:   return about_rows_;
        default:                   return nullptr;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdSettingsElement::OnRender(pu::ui::render::Renderer::Ref &drawer,
                                  const s32 x, const s32 y) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();

    // ── Background fill (entire element) ──────────────────────────────────
    {
        const auto &c = theme_.desktop_bg;
        DrawFilledRect(r, x, y, 1920, 1080, c.r, c.g, c.b, 0xFF);
    }

    // ── Title strip (topbar bottom .. topbar+title_h) ─────────────────────
    {
        const auto &c = theme_.topbar_bg;
        DrawFilledRect(r,
                       x, y + static_cast<s32>(TOPBAR_H),
                       1920, SETTINGS_TITLE_H,
                       c.r, c.g, c.b, 0xF0);
        if (title_tex_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_tex_, nullptr, nullptr, &tw, &th);
            BlitTexture(r, title_tex_,
                        x + 24,
                        y + static_cast<s32>(TOPBAR_H) + (SETTINGS_TITLE_H - th) / 2);
        }
        // Bottom border of title strip.
        const auto &ac = theme_.grid_line;
        DrawFilledRect(r,
                       x, y + static_cast<s32>(TOPBAR_H) + SETTINGS_TITLE_H - 1,
                       1920, 1,
                       ac.r, ac.g, ac.b, 0x80);
    }

    // ── Sidebar ────────────────────────────────────────────────────────────
    RenderSidebar(r, x, y);

    // ── Sidebar / detail pane vertical divider ────────────────────────────
    {
        const auto &c = theme_.grid_line;
        DrawFilledRect(r,
                       x + SETTINGS_SIDEBAR_W,
                       y + SETTINGS_BODY_TOP,
                       1, SETTINGS_BODY_H,
                       c.r, c.g, c.b, 0x60);
    }

    // ── Detail pane ────────────────────────────────────────────────────────
    RenderDetailPane(r, x, y);
}

void QdSettingsElement::RenderSidebar(SDL_Renderer *r, s32 ox, s32 oy) const {
    const auto &bg = theme_.surface_glass;
    DrawFilledRect(r,
                   ox, oy + SETTINGS_BODY_TOP,
                   SETTINGS_SIDEBAR_W, SETTINGS_BODY_H,
                   bg.r, bg.g, bg.b, 0xD0);

    for (size_t i = 0; i < SIDEBAR_ITEM_COUNT; ++i) {
        const s32 row_y = oy + SETTINGS_BODY_TOP
                        + static_cast<s32>(i) * SETTINGS_SIDEBAR_ROW_H;

        const bool is_active_tab =
            (i < static_cast<size_t>(SettingsTab::Count)) &&
            (static_cast<SettingsTab>(i) == active_tab_);

        const bool is_focused_sidebar =
            (focus_area_ == FocusArea::Sidebar) &&
            (i == sidebar_focus_row_);

        // Highlight background for active tab.
        if (is_active_tab) {
            const auto &ac = theme_.accent;
            DrawFilledRect(r,
                           ox, row_y,
                           SETTINGS_SIDEBAR_W, SETTINGS_SIDEBAR_ROW_H,
                           ac.r, ac.g, ac.b, 0x30);
        }

        // Focus ring around the row.
        if (is_focused_sidebar) {
            const auto &fr = theme_.focus_ring;
            DrawOutlineRect(r,
                            ox + 2, row_y + 2,
                            SETTINGS_SIDEBAR_W - 4, SETTINGS_SIDEBAR_ROW_H - 4,
                            fr.r, fr.g, fr.b, 0xFF);
        }

        // Label texture.
        if (sidebar_tex_[i]) {
            int tw = 0, th = 0;
            SDL_QueryTexture(sidebar_tex_[i], nullptr, nullptr, &tw, &th);
            BlitTexture(r, sidebar_tex_[i],
                        ox + 18,
                        row_y + (SETTINGS_SIDEBAR_ROW_H - th) / 2);
        }
    }
}

void QdSettingsElement::RenderDetailPane(SDL_Renderer *r, s32 ox, s32 oy) {
    const size_t n_rows = ActiveTabRowCount();
    const Row   *rows   = ActiveTabRows();
    if (!rows) return;

    const s32 detail_x = ox + SETTINGS_DETAIL_X;
    const size_t tab_idx = static_cast<size_t>(active_tab_);

    for (size_t i = 0; i < n_rows; ++i) {
        const s32 row_y = oy + SETTINGS_BODY_TOP
                        + static_cast<s32>(i) * SETTINGS_ROW_H;

        const bool focused = (focus_area_ == FocusArea::Detail) &&
                             (i == detail_row_);

        RenderDetailRow(r, rows[i], detail_x, row_y, SETTINGS_DETAIL_W,
                        focused, rows[i].is_button, tab_idx, i);
    }
}

void QdSettingsElement::RenderDetailRow(SDL_Renderer *r, const Row &row,
                                         s32 x, s32 y, s32 w,
                                         bool focused, bool is_button,
                                         size_t tab_idx, size_t row_idx) {
    // Row background on alternate rows for readability.
    if (row_idx % 2 == 0) {
        DrawFilledRect(r, x, y, w, SETTINGS_ROW_H,
                       0x10, 0x10, 0x28, 0x60);
    }

    // Focus highlight.
    if (focused) {
        const auto &fr = theme_.focus_ring;
        DrawFilledRect(r, x, y, w, SETTINGS_ROW_H,
                       fr.r, fr.g, fr.b, 0x28);
        DrawOutlineRect(r, x + 1, y + 1, w - 2, SETTINGS_ROW_H - 2,
                        fr.r, fr.g, fr.b, 0xCC);
    }

    if (is_button) {
        // Render as a centred button.
        const size_t tex_idx = tab_idx * DETAIL_TEX_STRIDE + row_idx * 2 + 1;
        if (detail_tex_[tex_idx]) {
            int tw = 0, th = 0;
            SDL_QueryTexture(detail_tex_[tex_idx], nullptr, nullptr, &tw, &th);
            const s32 btn_x = x + (w - tw) / 2;
            const s32 btn_y = y + (SETTINGS_ROW_H - th) / 2;
            // Button pill background.
            const auto &ac = theme_.accent;
            DrawFilledRect(r,
                           btn_x - 16, btn_y - 6,
                           tw + 32, th + 12,
                           ac.r, ac.g, ac.b, 0x50);
            DrawOutlineRect(r,
                            btn_x - 16, btn_y - 6,
                            tw + 32, th + 12,
                            ac.r, ac.g, ac.b, 0xCC);
            BlitTexture(r, detail_tex_[tex_idx], btn_x, btn_y);
        }
        return;
    }

    // Label (left-aligned).
    const size_t label_idx = tab_idx * DETAIL_TEX_STRIDE + row_idx * 2 + 0;
    if (detail_tex_[label_idx]) {
        int tw = 0, th = 0;
        SDL_QueryTexture(detail_tex_[label_idx], nullptr, nullptr, &tw, &th);
        BlitTexture(r, detail_tex_[label_idx],
                    x + 20,
                    y + (SETTINGS_ROW_H - th) / 2);
    }

    // Value (right-aligned).
    const size_t value_idx = tab_idx * DETAIL_TEX_STRIDE + row_idx * 2 + 1;
    if (detail_tex_[value_idx]) {
        int tw = 0, th = 0;
        SDL_QueryTexture(detail_tex_[value_idx], nullptr, nullptr, &tw, &th);
        BlitTexture(r, detail_tex_[value_idx],
                    x + w - tw - 24,
                    y + (SETTINGS_ROW_H - th) / 2);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdSettingsElement::OnInput(const u64 keys_down,
                                 const u64 keys_up,
                                 const u64 keys_held,
                                 const pu::ui::TouchPoint touch_pos) {
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;

    if (!keys_down) return;

    if (focus_area_ == FocusArea::Sidebar) {
        if (keys_down & HidNpadButton_Up) {
            if (sidebar_focus_row_ > 0) {
                --sidebar_focus_row_;
                // Sync active_tab_ to the new row (rows 0–5 map directly to tabs).
                if (sidebar_focus_row_ < static_cast<size_t>(SettingsTab::Count)) {
                    active_tab_ = static_cast<SettingsTab>(sidebar_focus_row_);
                }
                detail_row_ = 0;
            }
        } else if (keys_down & HidNpadButton_Down) {
            if (sidebar_focus_row_ < SIDEBAR_ITEM_COUNT - 1) {
                ++sidebar_focus_row_;
                // Keep active_tab_ on the last real tab when the cursor is on
                // row 6 ("System Settings →"), which is not a real tab.
                if (sidebar_focus_row_ < static_cast<size_t>(SettingsTab::Count)) {
                    active_tab_ = static_cast<SettingsTab>(sidebar_focus_row_);
                }
                // else sidebar_focus_row_ == 6 but active_tab_ stays About.
                detail_row_ = 0;
            }
        } else if (keys_down & HidNpadButton_Right) {
            // Right from the "System Settings →" row enters the detail pane of
            // the About tab (the last real tab).  D-pad Right from any other row
            // enters the detail pane for that row's tab as normal.
            focus_area_ = FocusArea::Detail;
            detail_row_ = 0;
        } else if (keys_down & HidNpadButton_A) {
            if (sidebar_focus_row_ == SIDEBAR_ITEM_COUNT - 1) {
                // Row 6 = "System Settings →": launch the upstream settings menu.
                ::ul::menu::ui::ShowSettingsMenu();
            } else {
                // Rows 0–5: pressing A from the sidebar simply enters the detail
                // pane for the currently active tab (same as pressing Right).
                focus_area_ = FocusArea::Detail;
                detail_row_ = 0;
            }
        } else if (keys_down & HidNpadButton_B) {
            if (g_MenuApplication) {
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            }
        }
    } else {
        // Detail pane focus.
        const size_t n_rows = ActiveTabRowCount();

        if (keys_down & HidNpadButton_Up) {
            if (detail_row_ > 0) {
                --detail_row_;
            }
        } else if (keys_down & HidNpadButton_Down) {
            if (detail_row_ + 1 < n_rows) {
                ++detail_row_;
            }
        } else if (keys_down & HidNpadButton_Left) {
            focus_area_ = FocusArea::Sidebar;
        } else if (keys_down & HidNpadButton_B) {
            focus_area_ = FocusArea::Sidebar;
        } else if (keys_down & HidNpadButton_A) {
            const Row *rows = ActiveTabRows();
            if (rows && detail_row_ < n_rows && rows[detail_row_].is_button) {
                // Currently only the Account tab has a button row (row 3 = Switch User).
                if (active_tab_ == SettingsTab::Account) {
                    DoUserSwitch();
                }
            }
        }
    }
}

// ── DoUserSwitch ─────────────────────────────────────────────────────────────

void QdSettingsElement::DoUserSwitch() {
    UL_LOG_INFO("settings: DoUserSwitch()");

    PselUserSelectionSettings sel;
    memset(&sel, 0, sizeof(sel));
    sel.is_skip_enabled         = 0;
    sel.is_network_service_account_required = 0;

    AccountUid new_uid{};
    const Result rc = pselShowUserSelector(&new_uid, &sel);
    if (R_SUCCEEDED(rc)) {
        account_uid_  = new_uid;
        acc_has_user_ = (new_uid.uid[0] != 0 || new_uid.uid[1] != 0);

        if (acc_has_user_) {
            AccountProfile profile;
            if (R_SUCCEEDED(accountGetProfile(&profile, new_uid))) {
                AccountProfileBase base;
                memset(&base, 0, sizeof(base));
                if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                    strncpy(acc_nickname_, base.nickname, sizeof(acc_nickname_) - 1);
                    acc_nickname_[sizeof(acc_nickname_) - 1] = '\0';
                    strncpy(abt_nickname_, acc_nickname_, sizeof(abt_nickname_));
                }
                accountProfileClose(&profile);
            }
        } else {
            strncpy(acc_nickname_, "(no user)", sizeof(acc_nickname_));
            strncpy(abt_nickname_, "(no user)", sizeof(abt_nickname_));
        }

        // Rebuild the Account and About row arrays and retexture.
        BuildRows();

        // Free and rebuild only the Account and About detail textures.
        auto retex_tab = [&](SettingsTab tab, const Row *rows, size_t n) {
            const size_t base = static_cast<size_t>(tab) * DETAIL_TEX_STRIDE;
            for (size_t i = 0; i < n; ++i) {
                SDL_Texture *&lt = detail_tex_[base + i * 2 + 0];
                SDL_Texture *&vt = detail_tex_[base + i * 2 + 1];
                if (lt) { pu::ui::render::DeleteTexture(lt); }
                if (vt) { pu::ui::render::DeleteTexture(vt); }
                if (rows[i].label) {
                    lt = MakeText(rows[i].label, theme_.text_secondary);
                }
                vt = MakeText(rows[i].value,
                              rows[i].is_button ? theme_.accent : theme_.text_primary);
            }
        };
        retex_tab(SettingsTab::Account, account_rows_, ACCOUNT_ROW_COUNT);
        retex_tab(SettingsTab::About,   about_rows_,   ABOUT_ROW_COUNT);

        UL_LOG_INFO("settings: user switched successfully");
    } else {
        UL_LOG_WARN("settings: pselShowUserSelector failed rc=0x%08x", rc);
    }
}

// ── QdSettingsLayout ──────────────────────────────────────────────────────────

QdSettingsLayout::QdSettingsLayout(const QdTheme &theme) {
    UL_LOG_INFO("settings: QdSettingsLayout ctor");
    this->SetBackgroundColor({ 0, 0, 0, 255 });
    settings_elm_ = QdSettingsElement::New(theme);
    this->Add(settings_elm_);
}

// ── IMenuLayout obligations ───────────────────────────────────────────────────

void QdSettingsLayout::OnMenuInput(const u64 keys_down,
                                   const u64 keys_up,
                                   const u64 keys_held,
                                   const pu::ui::TouchPoint touch_pos) {
    // QdSettingsElement handles its own input via its OnInput override.  The
    // base pu::ui::Layout::OnInput dispatch already routes here through the
    // child element list, so OnMenuInput stays empty for the host.
    (void)keys_down;
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;
}

bool QdSettingsLayout::OnHomeButtonPress() {
    UL_LOG_INFO("settings: OnHomeButtonPress -> returning to MainMenu");
    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
    return true;
}

void QdSettingsLayout::LoadSfx() {
    // Settings panel has no per-layout sfx today.  When sfx are added, load them here.
}

void QdSettingsLayout::DisposeSfx() {
    // Mirrors LoadSfx; kept symmetric for future sfx work.
}

} // namespace ul::menu::qdesktop
