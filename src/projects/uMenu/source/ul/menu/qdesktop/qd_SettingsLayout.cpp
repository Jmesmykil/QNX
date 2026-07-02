// qd_SettingsLayout.cpp — Q OS-native Settings layout implementation.
// Seven tabs with real libnx data: System, Network, Audio, Display, Account, About, Folders.
// Replaces upstream ui_SettingsMenuLayout for the qdesktop surface.
// All libnx service calls are scoped open/close inside Refresh() — no persistent handles.

#include <ul/menu/qdesktop/qd_SettingsLayout.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>  // g_MenuApplication, MenuType, ShowSettingsMenu
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/menu/smi/smi_Commands.hpp>       // v2.3.6: smi::RebootToStockQlaunch
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>
#include <ul/menu/qdesktop/qd_FolderTheme.hpp>
#include <switch/services/psm.h>
#include <switch/services/nifm.h>
#include <switch/services/audctl.h>
#include <switch/services/ts.h>
#include <switch/services/lbl.h>
#include <switch/services/applet.h>
#include <switch/services/acc.h>
#include <switch/services/time.h>           // timeInitialize / timeSetCurrentTime
#include <switch/applets/psel.h>
#include <switch/arm/counter.h>
#include <switch/runtime/devices/socket.h>  // socketInitializeDefault
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>                         // v3.6 overlay manager (Overlays tab)
#include <cerrno>                           // v3.6 overlay manager
#include <cstring>                          // v3.6 overlay manager
#include <cstdio>                           // v3.6 overlay manager
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <cerrno>

// NTP epoch offset: seconds from 1900-01-01 to 1970-01-01.
static constexpr u64 NTP_EPOCH_DELTA = 2208988800ULL;

// uMenu launches with __nx_time_service_type = TimeServiceType_Menu (see
// projects/uMenu/source/main.cpp).  time:a (Menu) can read every clock but
// CANNOT write the NetworkSystemClock — timeSetCurrentTime with
// TimeType_NetworkSystemClock on time:a returns 0x275 (module=117, desc=1 →
// the time service rejects the privileged write).
//
// Fix: temporarily swap to time:s before SetCurrentTime, then restore
// time:a.  Pattern mirrors libstratosphere's ams::time::InitializeForSystem
// (libs/Atmosphere-libs/libstratosphere/source/time/time_api.cpp).
// uMenu has service_access:"*" in its .npdm so smGetService("time:s") works.
extern "C" {
    extern TimeServiceType __nx_time_service_type;
}

// ── Extern globals (same pattern as qd_DesktopIcons.cpp) ─────────────────────
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
// g_GlobalSettings is defined at global scope in main.cpp / ui_MenuApplication.cpp.
// Declared here (file scope, NOT inside the namespace below) so the linker resolves
// it correctly.  In-namespace extern declarations would create a distinct symbol
// ul::menu::qdesktop::g_GlobalSettings which does not exist.
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── Legacy SetContentSize / SetOwnerWindow stripped (v1.10.3.10) ─────────────
// QdSettingsElement now inherits QdContentElement; QdWindow reads natural size
// via GetNaturalW()/GetNaturalH() and owns ALL scale state.  These two methods
// + the GetNaturalW() / GetNaturalH() / owner_window_ fields no longer exist.
// Stale impls deleted in v1.10.3.10.5 main-thread integration sweep.

// ── Static label arrays ───────────────────────────────────────────────────────

const char *const QdSettingsElement::SIDEBAR_LABELS[SIDEBAR_ITEM_COUNT] = {
    "System",
    "Network",
    "Audio",
    "Display",
    "Account",
    "About",
    "Folders",
    "Overlays",   // v3.6: Tesla overlay enable/disable manager
    // v2.3.5: dropped "System Settings →" sentinel (crashed via
    // ShowSettingsMenu → LoadMenu(MenuType::Settings) → fsdev unwind).
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
      hint_bar_tex_(nullptr),
      theme_(theme),
      focus_area_(FocusArea::Sidebar),
      active_tab_(SettingsTab::System),
      sidebar_focus_row_(0),
      detail_row_(0)
{
    UL_LOG_INFO("settings: QdSettingsElement ctor");

    // v3.6: load persisted display calibration preset (idx 0..2).
    // File is sdmc:/ulaunch/qos-display-cal.toml; absent = idx 0 (Default).
    {
        FILE *f = std::fopen("sdmc:/ulaunch/qos-display-cal.toml", "r");
        if (f) {
            int parsed = 0;
            if (std::fscanf(f, "preset = %d", &parsed) == 1) {
                if (parsed >= 0 && parsed < 3) {
                    disp_cal_idx_ = parsed;
                }
            }
            std::fclose(f);
        }
    }

    // Zero all char buffers.
    memset(sys_fw_,         0, sizeof(sys_fw_));
    memset(sys_serial_,     0, sizeof(sys_serial_));
    memset(sys_uptime_,     0, sizeof(sys_uptime_));
    memset(sys_ams_,        0, sizeof(sys_ams_));
    memset(sys_temp_pcb_,   0, sizeof(sys_temp_pcb_));
    memset(sys_temp_soc_,   0, sizeof(sys_temp_soc_));
    memset(sys_mode_,       0, sizeof(sys_mode_));
    memset(sys_boot_count_, 0, sizeof(sys_boot_count_));
    strncpy(sys_ntp_status_, "\xe2\x80\x94", sizeof(sys_ntp_status_) - 1); // U+2014 em-dash = "not synced yet"

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

    // Build the bottom hint bar once; freed in FreeAllTextures / dtor.
    const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
    hint_bar_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("B / + Close   \xe2\x80\xa2   Up/Down Navigate   \xe2\x80\xa2   A Toggle / Open"),
        hint_col);

    // Null-initialize row arrays.
    for (auto &row : system_rows_)  { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : network_rows_) { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : audio_rows_)   { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : display_rows_)  { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : account_rows_)  { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : about_rows_)    { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }
    for (auto &row : folders_rows_)  { row.label = nullptr; row.value[0] = '\0'; row.is_button = false; }

    // ── ts service: long-lived init (W3-FOLLOWUP, mirrors qd_MonitorLayout) ──
    // Legacy tsGetTemperature[MilliC] wrappers were removed in HorizonOS
    // 14.0.0+; on modern FW they silently return non-success and temp rows
    // render "n/a". TsSession API works across the FW boundary.
    {
        const Result rc_init = tsInitialize();
        if (R_SUCCEEDED(rc_init)) {
            ts_inited_ = true;
            // W6-LEDGER: track ts service handle.
            svc_lh_ts_ = UL_LEDGER_TRACK(QdResKind::Service, "settings:ts", 0);
            if (hosversionAtLeast(10, 0, 0)) {
                const Result rc_soc = tsOpenSession(&ts_sess_soc_, TsDeviceCode_LocationExternal);
                if (R_SUCCEEDED(rc_soc)) {
                    ts_sess_soc_open_ = true;
                } else {
                    UL_LOG_WARN("settings: tsOpenSession(SoC/External) rc=0x%08X", rc_soc);
                }
                const Result rc_pcb = tsOpenSession(&ts_sess_pcb_, TsDeviceCode_LocationInternal);
                if (R_SUCCEEDED(rc_pcb)) {
                    ts_sess_pcb_open_ = true;
                } else {
                    UL_LOG_WARN("settings: tsOpenSession(PCB/Internal) rc=0x%08X", rc_pcb);
                }
                ts_use_session_ = (ts_sess_soc_open_ || ts_sess_pcb_open_);
            }
        } else {
            UL_LOG_WARN("settings: tsInitialize rc=0x%08X — temp rows will be n/a", rc_init);
        }
    }

    // W5-PERF-HOTSPOTS #9 (P0-C): hoist service sessions — open once, query many times.
    // nifm: used by the Network tab for connection status + IP.
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        nifm_inited_ = true;
        // W6-LEDGER: track nifm service handle.
        svc_lh_nifm_ = UL_LEDGER_TRACK(QdResKind::Service, "settings:nifm", 0);
    } else {
        UL_LOG_WARN("settings: nifmInitialize failed — network rows will be unavailable");
    }

    // audctl: used by the Audio tab for system volume.
    if (R_SUCCEEDED(audctlInitialize())) {
        audctl_inited_ = true;
        // W6-LEDGER: track audctl service handle.
        svc_lh_audctl_ = UL_LEDGER_TRACK(QdResKind::Service, "settings:audctl", 0);
    } else {
        UL_LOG_WARN("settings: audctlInitialize failed — volume row will be n/a");
    }

    // lbl: used by the Display tab for brightness + ambient sensor.
    // Only valid in handheld mode; TV mode rows show "n/a (TV mode)" anyway.
    if (R_SUCCEEDED(lblInitialize())) {
        lbl_inited_ = true;
        // W6-LEDGER: track lbl service handle.
        svc_lh_lbl_ = UL_LEDGER_TRACK(QdResKind::Service, "settings:lbl", 0);
    } else {
        UL_LOG_WARN("settings: lblInitialize failed — brightness row will be n/a");
    }

    // W5-PERF-HOTSPOTS #10 (P0-C): read boot count once; FIRMWARE counter is
    // immutable during a single uMenu session (it increments only on reboot).
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
        boot_count_loaded_ = true;
    }
}

QdSettingsElement::~QdSettingsElement() {
    UL_LOG_INFO("settings: QdSettingsElement dtor");
    if (ts_sess_soc_open_) { tsSessionClose(&ts_sess_soc_); ts_sess_soc_open_ = false; }
    if (ts_sess_pcb_open_) { tsSessionClose(&ts_sess_pcb_); ts_sess_pcb_open_ = false; }
    if (ts_inited_)        {
        // W6-LEDGER: untrack ts service.
        UL_LEDGER_UNTRACK(svc_lh_ts_); svc_lh_ts_ = 0;
        tsExit();                       ts_inited_        = false;
    }
    // W5-PERF-HOTSPOTS #9 (P0-C): close hoisted service sessions.
    if (acc_profile_open_)  { accountProfileClose(&acc_profile_); acc_profile_open_ = false; }
    if (lbl_inited_)        {
        UL_LEDGER_UNTRACK(svc_lh_lbl_);    svc_lh_lbl_    = 0;
        lblExit();       lbl_inited_    = false;
    }
    if (audctl_inited_)     {
        UL_LEDGER_UNTRACK(svc_lh_audctl_); svc_lh_audctl_ = 0;
        audctlExit();    audctl_inited_ = false;
    }
    if (nifm_inited_)       {
        UL_LEDGER_UNTRACK(svc_lh_nifm_);   svc_lh_nifm_   = 0;
        nifmExit();      nifm_inited_   = false;
    }
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
    if (hint_bar_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_tex_);
        hint_bar_tex_ = nullptr;
    }
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
    // Mirrors qd_MonitorLayout's W3-MONTEMP probe pattern:
    //   1. Session API (preferred FW 10.0.0+) → float °C direct.
    //   2. tsGetTemperatureMilliC (FW 1.0.0-13.2.1) → s32 millicelsius.
    //   3. tsGetTemperature (legacy, all FW per header) → s32 celsius.
    // sys_temp_pcb_ ← Internal, sys_temp_soc_ ← External per libnx ts.h
    // (Internal=PCB, External=SoC).
    {
        auto probe_temp = [&](TsSession *sess, bool sess_open,
                              TsLocation legacy_loc,
                              char (&out_buf)[16], u32 &out_rc,
                              const char *tag) {
            out_rc = 0;
            if (!ts_inited_) {
                strncpy(out_buf, "n/a", sizeof(out_buf) - 1);
                out_buf[sizeof(out_buf) - 1] = '\0';
                return;
            }
            // 1. Session API.
            if (ts_use_session_ && sess_open && sess != nullptr) {
                float f = 0.0f;
                const Result rc = tsSessionGetTemperature(sess, &f);
                if (R_SUCCEEDED(rc)) {
                    snprintf(out_buf, sizeof(out_buf), "%d\xC2\xB0""C", static_cast<int>(f));
                    return;
                }
                out_rc = rc;
                UL_LOG_WARN("settings: tsSessionGetTemperature(%s) rc=0x%08X", tag, rc);
            }
            // 2. Legacy millicelsius wrapper.
            {
                s32 milli = 0;
                const Result rc_mc = tsGetTemperatureMilliC(legacy_loc, &milli);
                if (R_SUCCEEDED(rc_mc)) {
                    snprintf(out_buf, sizeof(out_buf), "%d\xC2\xB0""C", milli / 1000);
                    return;
                }
                out_rc = rc_mc;
            }
            // 3. Legacy celsius wrapper.
            {
                s32 deg = 0;
                const Result rc = tsGetTemperature(legacy_loc, &deg);
                if (R_SUCCEEDED(rc)) {
                    snprintf(out_buf, sizeof(out_buf), "%d\xC2\xB0""C", deg);
                    return;
                }
                out_rc = rc;
                UL_LOG_WARN("settings: ts %s legacy probes failed (last rc=0x%08X)", tag, rc);
            }
            // All paths failed.
            snprintf(out_buf, sizeof(out_buf), "n/a");
        };
        probe_temp(&ts_sess_pcb_, ts_sess_pcb_open_, TsLocation_Internal,
                   sys_temp_pcb_, ts_pcb_last_rc_, "PCB");
        probe_temp(&ts_sess_soc_, ts_sess_soc_open_, TsLocation_External,
                   sys_temp_soc_, ts_soc_last_rc_, "SoC");
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

    // ── Boot count (W5-PERF-HOTSPOTS #3: read once in ctor, cached) ──────
    (void)boot_count_loaded_; // sys_boot_count_ populated in ctor

    // ── Network (nifm) (W5-PERF-HOTSPOTS #3: session opened in ctor) ─────
    {
        if (nifm_inited_) {
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
                    strncpy(net_strength_, "\xe2\x80\x94", sizeof(net_strength_));
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
                strncpy(net_status_,   "No connection",    sizeof(net_status_));
                strncpy(net_ip_,       "\xe2\x80\x94",     sizeof(net_ip_));
                strncpy(net_strength_, "\xe2\x80\x94",     sizeof(net_strength_));
                strncpy(net_ethernet_, "Not connected",    sizeof(net_ethernet_));
            }
        } else {
            strncpy(net_status_,   "Service unavailable", sizeof(net_status_));
            strncpy(net_ip_,       "\xe2\x80\x94",        sizeof(net_ip_));
            strncpy(net_strength_, "\xe2\x80\x94",        sizeof(net_strength_));
            strncpy(net_ethernet_, "\xe2\x80\x94",        sizeof(net_ethernet_));
        }
    }

    // ── Audio volume (audctl) (W5-PERF-HOTSPOTS #3: session opened in ctor)
    {
        if (audctl_inited_) {
            float vol = 0.0f;
            if (R_SUCCEEDED(audctlGetSystemOutputMasterVolume(&vol))) {
                snprintf(aud_volume_, sizeof(aud_volume_), "%.0f%%", vol * 100.0f);
            } else {
                strncpy(aud_volume_, "n/a", sizeof(aud_volume_));
            }
        } else {
            strncpy(aud_volume_, "n/a", sizeof(aud_volume_));
        }
    }

    // ── Display brightness (lbl) (W5-PERF-HOTSPOTS #3: session in ctor) ──
    {
        const AppletOperationMode mode = appletGetOperationMode();
        if (mode == AppletOperationMode_Console) {
            strncpy(disp_brightness_, "n/a (TV mode)", sizeof(disp_brightness_));
            strncpy(disp_ambient_,    "n/a (TV mode)", sizeof(disp_ambient_));
        } else {
            if (lbl_inited_) {
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
            } else {
                strncpy(disp_brightness_, "n/a", sizeof(disp_brightness_));
                strncpy(disp_ambient_,    "n/a", sizeof(disp_ambient_));
            }
        }
    }

    // ── Account / selected user (W5-PERF-HOTSPOTS #3: profile cached) ────
    {
        const AccountUid new_uid = g_GlobalSettings.system_status.selected_user;
        const bool uid_changed = (new_uid.uid[0] != account_uid_.uid[0] ||
                                  new_uid.uid[1] != account_uid_.uid[1]);

        if (uid_changed && acc_profile_open_) {
            accountProfileClose(&acc_profile_);
            acc_profile_open_ = false;
        }

        account_uid_ = new_uid;
        acc_has_user_ = (account_uid_.uid[0] != 0 || account_uid_.uid[1] != 0);

        if (acc_has_user_) {
            if (!acc_profile_open_) {
                acc_profile_open_ = R_SUCCEEDED(
                    accountGetProfile(&acc_profile_, account_uid_));
            }
            if (acc_profile_open_) {
                AccountProfileBase base;
                memset(&base, 0, sizeof(base));
                if (R_SUCCEEDED(accountProfileGet(&acc_profile_, nullptr, &base))) {
                    strncpy(acc_nickname_, base.nickname, sizeof(acc_nickname_) - 1);
                    acc_nickname_[sizeof(acc_nickname_) - 1] = '\0';
                    strncpy(abt_nickname_, acc_nickname_, sizeof(abt_nickname_));
                }
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

    // v3.2.1 (W4-LEAKS P0 #2): FreeAllTextures destroys hint_bar_tex_, but the
    // ctor was the only previous rebuild site, so after the first Refresh()
    // the hint bar went permanently blank (and the orphan slot stayed null
    // forever — visual regression, not a byte leak).  Rebuild here so the
    // hint bar survives every Refresh() cycle.
    {
        const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
        hint_bar_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string("B / + Close   \xe2\x80\xa2   Up/Down Navigate   \xe2\x80\xa2   A Toggle / Open"),
            hint_col);
    }

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
    build_tab_textures(SettingsTab::Folders, folders_rows_, FOLDERS_ROW_COUNT);

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
    // v2.3.6: row-8 button — toggles the qlaunch override and reboots so
    // the user can reach Nintendo's built-in System Settings UI (which is
    // baked into qlaunch and therefore unreachable while uSystem replaces it).
    system_rows_[8] = { "Boot to Nintendo Home Menu", {}, true };
    // Sphaira-absorb sprint 1: row-9 button — one-shot NTP sync.
    system_rows_[9] = { "Sync clock with internet",   {}, true };
    strncpy(system_rows_[0].value, sys_fw_,         sizeof(system_rows_[0].value) - 1);
    strncpy(system_rows_[1].value, sys_serial_,     sizeof(system_rows_[1].value) - 1);
    strncpy(system_rows_[2].value, sys_uptime_,     sizeof(system_rows_[2].value) - 1);
    strncpy(system_rows_[3].value, sys_ams_,        sizeof(system_rows_[3].value) - 1);
    strncpy(system_rows_[4].value, sys_temp_pcb_,   sizeof(system_rows_[4].value) - 1);
    strncpy(system_rows_[5].value, sys_temp_soc_,   sizeof(system_rows_[5].value) - 1);
    strncpy(system_rows_[6].value, sys_mode_,       sizeof(system_rows_[6].value) - 1);
    strncpy(system_rows_[7].value, sys_boot_count_, sizeof(system_rows_[7].value) - 1);
    strncpy(system_rows_[8].value, "Reboot \xE2\x86\x92", sizeof(system_rows_[8].value) - 1); // → arrow
    // memcpy + explicit null-terminator — bypasses GCC's increasingly strict
    // -Werror=stringop-truncation which fires on strncpy even when src and
    // dst are the same width.
    {
        const size_t cap = sizeof(system_rows_[9].value);
        size_t n = 0;
        while (n < cap - 1 && sys_ntp_status_[n] != '\0') ++n;
        memcpy(system_rows_[9].value, sys_ntp_status_, n);
        system_rows_[9].value[n] = '\0';
    }

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
    display_rows_[0] = { "Brightness",  {}, false };
    display_rows_[1] = { "Mode",        {}, false };
    display_rows_[2] = { "Ambient",     {}, false };
    display_rows_[3] = { "USB 3.0",     {}, false };
    display_rows_[4] = { "Cal Profile", {}, true  };  // v3.6: cycle button
    strncpy(display_rows_[0].value, disp_brightness_, sizeof(display_rows_[0].value) - 1);
    strncpy(display_rows_[1].value, disp_mode_,       sizeof(display_rows_[1].value) - 1);
    strncpy(display_rows_[2].value, disp_ambient_,    sizeof(display_rows_[2].value) - 1);
    strncpy(display_rows_[3].value, disp_usb30_,      sizeof(display_rows_[3].value) - 1);
    {
        const char *kCalNames[3] = { "Default", "TV (Limited)", "Bright" };
        const int  idx           = (disp_cal_idx_ >= 0 && disp_cal_idx_ < 3)
                                       ? disp_cal_idx_ : 0;
        strncpy(display_rows_[4].value, kCalNames[idx],
                sizeof(display_rows_[4].value) - 1);
    }

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

    // Folders (5 auto-folder enable toggles + 1 theme cycle button)
    folders_rows_[0] = { "NX Games",  {}, false };
    folders_rows_[1] = { "Homebrew",  {}, false };
    folders_rows_[2] = { "System",    {}, false };
    folders_rows_[3] = { "Payloads",  {}, false };
    folders_rows_[4] = { "Builtin",   {}, false };
    folders_rows_[5] = { nullptr,     {}, true  };  // theme cycle button (no label)
    strncpy(folders_rows_[0].value, IsAutoFolderEnabled(AutoFolderIdx::NxGames)  ? "On" : "Off", sizeof(folders_rows_[0].value) - 1);
    strncpy(folders_rows_[1].value, IsAutoFolderEnabled(AutoFolderIdx::Homebrew) ? "On" : "Off", sizeof(folders_rows_[1].value) - 1);
    strncpy(folders_rows_[2].value, IsAutoFolderEnabled(AutoFolderIdx::System)   ? "On" : "Off", sizeof(folders_rows_[2].value) - 1);
    strncpy(folders_rows_[3].value, IsAutoFolderEnabled(AutoFolderIdx::Payloads) ? "On" : "Off", sizeof(folders_rows_[3].value) - 1);
    strncpy(folders_rows_[4].value, IsAutoFolderEnabled(AutoFolderIdx::Builtin)  ? "On" : "Off", sizeof(folders_rows_[4].value) - 1);
    {
        const size_t pack = LoadFolderThemePack();
        snprintf(folders_rows_[5].value, sizeof(folders_rows_[5].value), "Theme: %s", FolderThemePackName(pack));
    }

    // v3.6 Overlays — populate from previously-scanned overlay_paths_.
    // ScanOverlays() is called lazily on first sidebar focus of the tab so
    // BuildRows during ctor doesn't trigger SD I/O.
    for (size_t i = 0; i < OVERLAYS_ROW_COUNT; ++i) {
        overlays_rows_[i] = { nullptr, {}, false };
    }
    for (size_t i = 0; i < overlays_count_; ++i) {
        // Find the filename portion (after the last '/').
        const char *p = overlay_paths_[i];
        const char *last_slash = nullptr;
        for (const char *c = p; *c; ++c) {
            if (*c == '/') last_slash = c;
        }
        const char *filename = last_slash ? (last_slash + 1) : p;
        // Strip the trailing ".disabled" so the label is the canonical
        // overlay filename regardless of current enable state.
        char display[64];
        snprintf(display, sizeof(display), "%.*s",
                 static_cast<int>(sizeof(display) - 1), filename);
        // Truncate ".disabled" suffix in the label if present.
        const size_t dlen = std::strlen(display);
        const char  *suf  = ".disabled";
        const size_t slen = std::strlen(suf);
        if (dlen > slen && std::strcmp(display + dlen - slen, suf) == 0) {
            display[dlen - slen] = '\0';
        }
        // Row label points to a stable static literal if we can't store it;
        // we instead pack the label into a per-row static slot.  Easier:
        // store the label string in the value buffer of an adjacent field?
        // Simplest path here: stash the label in a static-per-row buffer.
        static char overlay_label_storage[OVERLAYS_ROW_COUNT][64];
        snprintf(overlay_label_storage[i], sizeof(overlay_label_storage[i]),
                 "%s", display);
        overlays_rows_[i].label     = overlay_label_storage[i];
        overlays_rows_[i].is_button = true;
        strncpy(overlays_rows_[i].value,
                overlay_enabled_[i] ? "Enabled" : "Disabled",
                sizeof(overlays_rows_[i].value) - 1);
    }
}

// ── v3.6 ScanOverlays / ToggleOverlay ────────────────────────────────────────

void QdSettingsElement::ScanOverlays() {
    overlays_count_ = 0;
    for (size_t i = 0; i < OVERLAYS_ROW_COUNT; ++i) {
        overlay_paths_[i][0] = '\0';
        overlay_enabled_[i]  = false;
    }
    const char *dir_path = "sdmc:/switch/.overlays";
    DIR *d = ::opendir(dir_path);
    if (!d) {
        // Directory absent — nothing to manage.  Leave overlays_count_ = 0
        // so the tab renders empty.
        overlays_scanned_ = true;
        return;
    }
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        const size_t nlen = std::strlen(de->d_name);
        bool is_enabled  = false;
        bool is_overlay  = false;
        const char *suf_on  = ".ovl";
        const char *suf_off = ".ovl.disabled";
        const size_t lon  = std::strlen(suf_on);
        const size_t loff = std::strlen(suf_off);
        if (nlen > lon &&
            std::strcmp(de->d_name + nlen - lon, suf_on) == 0) {
            is_overlay = true;
            is_enabled = true;
        } else if (nlen > loff &&
                   std::strcmp(de->d_name + nlen - loff, suf_off) == 0) {
            is_overlay = true;
            is_enabled = false;
        }
        if (!is_overlay) continue;
        if (overlays_count_ >= OVERLAYS_ROW_COUNT) break;
        std::snprintf(overlay_paths_[overlays_count_],
                       sizeof(overlay_paths_[overlays_count_]),
                       "%s/%s", dir_path, de->d_name);
        overlay_enabled_[overlays_count_] = is_enabled;
        ++overlays_count_;
    }
    ::closedir(d);
    overlays_scanned_ = true;
    UL_LOG_INFO("settings: ScanOverlays found %zu overlay(s)",
                overlays_count_);
}

bool QdSettingsElement::ToggleOverlay(size_t idx) {
    if (idx >= overlays_count_) return false;
    const char *suf_off = ".ovl.disabled";
    const char *suf_on  = ".ovl";
    char new_path[256];
    const char *cur = overlay_paths_[idx];
    if (overlay_enabled_[idx]) {
        // Currently enabled — rename to .ovl.disabled.
        std::snprintf(new_path, sizeof(new_path), "%s.disabled", cur);
    } else {
        // Currently disabled — strip ".disabled" suffix.
        const size_t cur_len = std::strlen(cur);
        const size_t suf_len = std::strlen(".disabled");
        if (cur_len <= suf_len ||
            std::strcmp(cur + cur_len - suf_len, ".disabled") != 0) {
            UL_LOG_WARN("settings: ToggleOverlay: unexpected path %s",
                        cur);
            return false;
        }
        std::snprintf(new_path, sizeof(new_path), "%.*s",
                      static_cast<int>(cur_len - suf_len), cur);
    }
    if (std::rename(cur, new_path) != 0) {
        UL_LOG_WARN("settings: ToggleOverlay rename(%s,%s) errno=%d",
                    cur, new_path, errno);
        return false;
    }
    (void)suf_off;
    (void)suf_on;
    UL_LOG_INFO("settings: ToggleOverlay %s -> %s", cur, new_path);
    // Commit Horizon's write-back cache so the rename survives a reboot.
    fsdevCommitDevice("sdmc");
    // Update in-memory state without rescanning.
    overlay_enabled_[idx] = !overlay_enabled_[idx];
    std::snprintf(overlay_paths_[idx], sizeof(overlay_paths_[idx]),
                   "%s", new_path);
    return true;
}

// ── v3.6 CycleDisplayCalibration ─────────────────────────────────────────────
//
// Cycles disp_cal_idx_ through 0 → 1 → 2 → 0 and applies the matching
// SetSysTvSettings preset.  Persists the chosen idx to
// sdmc:/ulaunch/qos-display-cal.toml so it survives reboot.  Read at ctor
// from the same file (see ctor).
//
// Presets (gamma / contrast / cmu_mode / rgb_range):
//   0 = "Default"      — gamma 1.0, contrast 1.0, CmuMode::None, Full range
//   1 = "TV (Limited)" — gamma 1.0, contrast 1.0, CmuMode::None, Limited range
//   2 = "Bright"       — gamma 0.9, contrast 1.1, CmuMode::Standard, Full
//
// Toast shows the new preset name.  On HW the Switch repaints colour
// tables immediately.

void QdSettingsElement::CycleDisplayCalibration() {
    SetSysTvSettings tv = {};
    Result rc = setsysGetTvSettings(&tv);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("settings: setsysGetTvSettings rc=0x%08X — calibration "
                    "no-op", rc);
        if (g_MenuApplication) {
            g_MenuApplication->ShowNotification(
                "Display: failed to read TV settings (handheld?)", 4000);
        }
        return;
    }
    disp_cal_idx_ = (disp_cal_idx_ + 1) % 3;
    switch (disp_cal_idx_) {
        case 0:  // Default
            tv.gamma            = 1.0f;
            tv.contrast         = 1.0f;
            tv.cmu_mode         = 0;  // SetSysCmuMode_None
            tv.rgb_range        = 1;  // SetSysRgbRange_Full
            tv.hdmi_content_type = 0;
            break;
        case 1:  // TV Limited
            tv.gamma            = 1.0f;
            tv.contrast         = 1.0f;
            tv.cmu_mode         = 0;
            tv.rgb_range        = 0;  // SetSysRgbRange_Limited
            tv.hdmi_content_type = 0;
            break;
        case 2:  // Bright
            tv.gamma            = 0.9f;
            tv.contrast         = 1.1f;
            tv.cmu_mode         = 1;  // SetSysCmuMode_Standard
            tv.rgb_range        = 1;
            tv.hdmi_content_type = 0;
            break;
    }
    rc = setsysSetTvSettings(&tv);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("settings: setsysSetTvSettings rc=0x%08X", rc);
        if (g_MenuApplication) {
            g_MenuApplication->ShowNotification(
                "Display: TV settings write failed", 4000);
        }
        return;
    }
    // Persist the chosen index.
    FILE *f = std::fopen("sdmc:/ulaunch/qos-display-cal.toml", "w");
    if (f) {
        std::fprintf(f, "preset = %d\n", disp_cal_idx_);
        std::fflush(f);
        std::fclose(f);
        fsdevCommitDevice("sdmc");
    }
    const char *kCalNames[3] = { "Default", "TV (Limited)", "Bright" };
    if (g_MenuApplication) {
        g_MenuApplication->ShowNotification(
            std::string("Calibration: ") + kCalNames[disp_cal_idx_],
            3000);
    }
    UL_LOG_INFO("settings: CycleDisplayCalibration -> idx=%d (%s)",
                disp_cal_idx_, kCalNames[disp_cal_idx_]);
}

// ── ActiveTab helpers ─────────────────────────────────────────────────────────

size_t QdSettingsElement::ActiveTabRowCount() const {
    switch (active_tab_) {
        case SettingsTab::System:   return SYSTEM_ROW_COUNT;
        case SettingsTab::Network:  return NETWORK_ROW_COUNT;
        case SettingsTab::Audio:    return AUDIO_ROW_COUNT;
        case SettingsTab::Display:  return DISPLAY_ROW_COUNT;
        case SettingsTab::Account:  return ACCOUNT_ROW_COUNT;
        case SettingsTab::About:    return ABOUT_ROW_COUNT;
        case SettingsTab::Folders:  return FOLDERS_ROW_COUNT;
        case SettingsTab::Overlays: return overlays_count_;   // v3.6: dynamic
        default:                    return 0;
    }
}

const QdSettingsElement::Row *QdSettingsElement::ActiveTabRows() const {
    switch (active_tab_) {
        case SettingsTab::System:   return system_rows_;
        case SettingsTab::Network:  return network_rows_;
        case SettingsTab::Audio:    return audio_rows_;
        case SettingsTab::Display:  return display_rows_;
        case SettingsTab::Account:  return account_rows_;
        case SettingsTab::About:    return about_rows_;
        case SettingsTab::Folders:  return folders_rows_;
        case SettingsTab::Overlays: return overlays_rows_;   // v3.6
        default:                    return nullptr;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdSettingsElement::OnRender(pu::ui::render::Renderer::Ref &drawer,
                                  const s32 x, const s32 y) {
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();

    // ── Background fill (entire element) ──────────────────────────────────
    {
        const auto &c = theme_.desktop_bg;
        DrawFilledRect(r, x, y, GetNaturalW(), GetNaturalH(), c.r, c.g, c.b, 0xFF);
    }

    // ── Title strip (topbar bottom .. topbar+title_h) ─────────────────────
    {
        const auto &c = theme_.topbar_bg;
        DrawFilledRect(r,
                       x, y + static_cast<s32>(TOPBAR_H),
                       GetNaturalW(), SETTINGS_TITLE_H,
                       c.r, c.g, c.b, 0xF0);
        if (title_tex_) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_tex_, nullptr, nullptr, &tw, &th);
            BlitTexture(r, title_tex_,
                        x + SETTINGS_TEXT_LEFT_PAD,   // D14 fix: was 24, now 20
                        y + static_cast<s32>(TOPBAR_H) + (SETTINGS_TITLE_H - th) / 2);
        }
        // Bottom border of title strip.
        const auto &ac = theme_.grid_line;
        DrawFilledRect(r,
                       x, y + static_cast<s32>(TOPBAR_H) + SETTINGS_TITLE_H - 1,
                       GetNaturalW(), 1,
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

    // ── Bottom hint bar ────────────────────────────────────────────────────
    if (hint_bar_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_bar_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = x + (GetNaturalW() - hw) / 2;
        const s32 hy = y + GetNaturalH() - 8 - hh;
        SDL_Rect hdst { hx, hy, hw, hh };
        SDL_RenderCopy(r, hint_bar_tex_, nullptr, &hdst);
    }

    // v2.5.0 — theme picker overlay removed. Folder Theme row in the
    // Folders tab now opens the full-screen ui::ThemesMenuLayout instead
    // (via ShowThemesMenu()) — no longer renders inside the Settings window.
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
                        ox + SETTINGS_TEXT_LEFT_PAD,   // D14 fix: was 18, now 20
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
                    x + SETTINGS_TEXT_LEFT_PAD,   // D14: was 20 (now named constant)
                    y + (SETTINGS_ROW_H - th) / 2);
    }

    // Value (right-aligned).
    const size_t value_idx = tab_idx * DETAIL_TEX_STRIDE + row_idx * 2 + 1;
    if (detail_tex_[value_idx]) {
        int tw = 0, th = 0;
        SDL_QueryTexture(detail_tex_[value_idx], nullptr, nullptr, &tw, &th);
        BlitTexture(r, detail_tex_[value_idx],
                    x + w - tw - SETTINGS_TEXT_RIGHT_PAD,  // D14: was 24 (now named constant)
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

    // v3.6 absorb wave 1: lazy-scan the Overlays tab on first focus.
    // Triggered the first time active_tab_ == Overlays in a session so the
    // expensive SD-card directory walk doesn't run on every Settings open
    // (only when the user actually wants to manage overlays).  Rebuilds
    // BOTH the row data AND the per-tab detail textures so the very next
    // OnRender pass shows the populated list.
    if (active_tab_ == SettingsTab::Overlays && !overlays_scanned_) {
        ScanOverlays();
        BuildRows();
        const size_t base = static_cast<size_t>(SettingsTab::Overlays) * DETAIL_TEX_STRIDE;
        for (size_t i = 0; i < OVERLAYS_ROW_COUNT; ++i) {
            SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
            SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
            if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
            if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
            if (overlays_rows_[i].label) {
                lt = MakeText(overlays_rows_[i].label, theme_.text_secondary);
            }
            if (overlays_rows_[i].value[0]) {
                vt = MakeText(overlays_rows_[i].value, theme_.accent);
            }
        }
    }

    // v1.8.29 Slice 2: touch hit-test for sidebar and detail pane rows.
    // The element renders at (0, 0) as a full-screen element; constants are
    // in screen coordinates.  A touch in the sidebar selects that row (same
    // as D-pad Up/Down + A); a touch in the detail pane selects that row and
    // activates it (same as D-pad Up/Down + A); ZR activates the focused row.
    if (!touch_pos.IsEmpty()) {
        const s32 tx = touch_pos.x;
        const s32 ty = touch_pos.y;
        const s32 body_top = SETTINGS_BODY_TOP;  // 104

        if (tx < SETTINGS_SIDEBAR_W) {
            // Touch in the sidebar column.
            if (ty >= body_top) {
                const s32 rel = ty - body_top;
                const size_t row = static_cast<size_t>(rel / SETTINGS_SIDEBAR_ROW_H);
                if (row < SIDEBAR_ITEM_COUNT) {
                    sidebar_focus_row_ = row;
                    if (sidebar_focus_row_ < static_cast<size_t>(SettingsTab::Count)) {
                        active_tab_ = static_cast<SettingsTab>(sidebar_focus_row_);
                    }
                    detail_row_ = 0;
                    // v2.3.5: every sidebar row is a real tab now; touch
                    // immediately enters its detail pane (no SettingsMenu
                    // bridge — that path crashed in fsdev unwind).
                    focus_area_ = FocusArea::Detail;
                }
            }
        } else {
            // Touch in the detail pane.
            if (ty >= body_top) {
                const s32 rel = ty - body_top;
                const size_t row = static_cast<size_t>(rel / SETTINGS_ROW_H);
                const size_t n_rows = ActiveTabRowCount();
                if (row < n_rows) {
                    detail_row_  = row;
                    focus_area_  = FocusArea::Detail;
                    // Activate the row immediately (same as pressing A).
                    const Row *rows = ActiveTabRows();
                    if (rows) {
                        QdAudio::Play(DesktopSfxEvent::SettingsItemChange);
                        if (active_tab_ == SettingsTab::System && rows[row].is_button) {
                            if (row == 9u) {
                                // Sphaira-absorb sprint 1: NTP sync button.
                                DoNtpSync();
                            } else {
                                // v2.3.6: row 8 of System tab is the
                                // "Boot to Nintendo Home Menu" button.
                                DoBootToStockQlaunch();
                            }
                        } else if (active_tab_ == SettingsTab::Account && rows[row].is_button) {
                            DoUserSwitch();
                        } else if (active_tab_ == SettingsTab::Display && rows[row].is_button && row == 4u) {
                            // v3.6: Cal Profile cycle (Display row 4).
                            CycleDisplayCalibration();
                            BuildRows();
                            const size_t base = static_cast<size_t>(SettingsTab::Display) * DETAIL_TEX_STRIDE;
                            for (size_t i = 0; i < DISPLAY_ROW_COUNT; ++i) {
                                SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                                SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                                if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                                if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                                if (display_rows_[i].label) {
                                    lt = MakeText(display_rows_[i].label, theme_.text_secondary);
                                }
                                vt = MakeText(display_rows_[i].value,
                                              display_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                            }
                        } else if (active_tab_ == SettingsTab::Folders) {
                            static const AutoFolderIdx kFolderMapT[5] = {
                                AutoFolderIdx::NxGames,
                                AutoFolderIdx::Homebrew,
                                AutoFolderIdx::System,
                                AutoFolderIdx::Payloads,
                                AutoFolderIdx::Builtin,
                            };
                            if (row < 5u) {
                                const AutoFolderIdx fid = kFolderMapT[row];
                                SetAutoFolderEnabled(fid, !IsAutoFolderEnabled(fid));
                            } else {
                                // v2.5.0: Folder Theme row activated → open
                                // the full-screen Themes menu (ui_ThemesMenuLayout)
                                // instead of the in-window popup. The Themes
                                // menu shows the 10 in-binary themes as cards
                                // with palette-swatch icons, plus any installed
                                // .ultheme files below them.
                                ul::menu::ui::ShowThemesMenu();
                                return;
                            }
                            BuildRows();
                            const size_t base = static_cast<size_t>(SettingsTab::Folders) * DETAIL_TEX_STRIDE;
                            for (size_t i = 0; i < FOLDERS_ROW_COUNT; ++i) {
                                SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                                SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                                if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                                if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                                if (folders_rows_[i].label) {
                                    lt = MakeText(folders_rows_[i].label, theme_.text_secondary);
                                }
                                vt = MakeText(folders_rows_[i].value,
                                              folders_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                            }
                        } else if (active_tab_ == SettingsTab::Overlays && rows[row].is_button) {
                            // v3.6 absorb wave 1: touch-activate overlay row → toggle .ovl ↔ .ovl.disabled.
                            if (row < overlays_count_) {
                                if (ToggleOverlay(row)) {
                                    BuildRows();
                                    const size_t base = static_cast<size_t>(SettingsTab::Overlays) * DETAIL_TEX_STRIDE;
                                    for (size_t i = 0; i < OVERLAYS_ROW_COUNT; ++i) {
                                        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                                        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                                        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                                        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                                        if (overlays_rows_[i].label) {
                                            lt = MakeText(overlays_rows_[i].label, theme_.text_secondary);
                                        }
                                        if (overlays_rows_[i].value[0]) {
                                            vt = MakeText(overlays_rows_[i].value, theme_.accent);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    if (!keys_down) return;

    // v1.8.29 Slice 2: ZR activates the focused detail-pane row (same as A
    // in detail mode).  If focus is in the sidebar, ZR enters the detail pane.
    if (keys_down & HidNpadButton_ZR) {
        if (focus_area_ == FocusArea::Sidebar) {
            // v2.3.5: ZR from sidebar always enters detail (the legacy
            // ShowSettingsMenu bridge was removed because it crashed).
            focus_area_ = FocusArea::Detail;
            detail_row_ = 0;
            return;
        } else {
            const size_t n_rows = ActiveTabRowCount();
            const Row *rows     = ActiveTabRows();
            if (rows && detail_row_ < n_rows) {
                QdAudio::Play(DesktopSfxEvent::SettingsItemChange);
                if (active_tab_ == SettingsTab::System && rows[detail_row_].is_button) {
                    if (detail_row_ == 9u) {
                        // Sphaira-absorb sprint 1: NTP sync button (ZR path).
                        DoNtpSync();
                    } else {
                        // v2.3.6: System row 8 = Boot to Nintendo Home Menu.
                        DoBootToStockQlaunch();
                    }
                } else if (active_tab_ == SettingsTab::Account && rows[detail_row_].is_button) {
                    DoUserSwitch();
                } else if (active_tab_ == SettingsTab::Display && rows[detail_row_].is_button && detail_row_ == 4u) {
                    // v3.6: Cal Profile cycle (D-pad A / ZR).
                    CycleDisplayCalibration();
                    BuildRows();
                    const size_t base = static_cast<size_t>(SettingsTab::Display) * DETAIL_TEX_STRIDE;
                    for (size_t i = 0; i < DISPLAY_ROW_COUNT; ++i) {
                        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                        if (display_rows_[i].label) {
                            lt = MakeText(display_rows_[i].label, theme_.text_secondary);
                        }
                        vt = MakeText(display_rows_[i].value,
                                      display_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                    }
                } else if (active_tab_ == SettingsTab::Folders) {
                    static const AutoFolderIdx kFolderMap[5] = {
                        AutoFolderIdx::NxGames,
                        AutoFolderIdx::Homebrew,
                        AutoFolderIdx::System,
                        AutoFolderIdx::Payloads,
                        AutoFolderIdx::Builtin,
                    };
                    if (detail_row_ < 5u) {
                        const AutoFolderIdx fid = kFolderMap[detail_row_];
                        SetAutoFolderEnabled(fid, !IsAutoFolderEnabled(fid));
                    } else {
                        // v2.5.0: Folder Theme row → full-screen Themes menu.
                        ul::menu::ui::ShowThemesMenu();
                        return;
                    }
                    BuildRows();
                    const size_t base = static_cast<size_t>(SettingsTab::Folders) * DETAIL_TEX_STRIDE;
                    for (size_t i = 0; i < FOLDERS_ROW_COUNT; ++i) {
                        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                        if (folders_rows_[i].label) {
                            lt = MakeText(folders_rows_[i].label, theme_.text_secondary);
                        }
                        vt = MakeText(folders_rows_[i].value,
                                      folders_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                    }
                } else if (active_tab_ == SettingsTab::Overlays && rows[detail_row_].is_button) {
                    // v3.6 absorb wave 1: ZR-activate overlay row → toggle.
                    if (detail_row_ < overlays_count_) {
                        if (ToggleOverlay(detail_row_)) {
                            BuildRows();
                            const size_t base = static_cast<size_t>(SettingsTab::Overlays) * DETAIL_TEX_STRIDE;
                            for (size_t i = 0; i < OVERLAYS_ROW_COUNT; ++i) {
                                SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                                SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                                if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                                if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                                if (overlays_rows_[i].label) {
                                    lt = MakeText(overlays_rows_[i].label, theme_.text_secondary);
                                }
                                if (overlays_rows_[i].value[0]) {
                                    vt = MakeText(overlays_rows_[i].value, theme_.accent);
                                }
                            }
                        }
                    }
                }
            }
            return;
        }
    }

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
                // v2.3.5: every sidebar row is now a real tab (legacy "System
                // Settings →" sentinel removed); always sync active_tab_.
                active_tab_ = static_cast<SettingsTab>(sidebar_focus_row_);
                detail_row_ = 0;
            }
        } else if (keys_down & HidNpadButton_Right) {
            // D-pad Right enters the detail pane for the focused tab.
            focus_area_ = FocusArea::Detail;
            detail_row_ = 0;
        } else if (keys_down & HidNpadButton_A) {
            // v2.3.5: A from any sidebar row enters its detail pane.  The old
            // row-7 "System Settings →" sentinel was removed because it
            // bridged into the legacy SettingsMenuLayout, whose first paint
            // crashed inside fsdev (mesosphere reported "svc 0x6f").
            focus_area_ = FocusArea::Detail;
            detail_row_ = 0;
        } else if (keys_down & HidNpadButton_B) {
            QdAudio::Play(DesktopSfxEvent::SettingsClose);
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
            if (rows && detail_row_ < n_rows) {
                QdAudio::Play(DesktopSfxEvent::SettingsItemChange);
                if (active_tab_ == SettingsTab::System && rows[detail_row_].is_button) {
                    if (detail_row_ == 9u) {
                        // Sphaira-absorb sprint 1: NTP sync button (A path).
                        DoNtpSync();
                    } else {
                        // v2.3.6: System row 8 = Boot to Nintendo Home Menu.
                        DoBootToStockQlaunch();
                    }
                } else if (active_tab_ == SettingsTab::Account && rows[detail_row_].is_button) {
                    DoUserSwitch();
                } else if (active_tab_ == SettingsTab::Display && rows[detail_row_].is_button && detail_row_ == 4u) {
                    // v3.6: Cal Profile cycle (D-pad A / ZR).
                    CycleDisplayCalibration();
                    BuildRows();
                    const size_t base = static_cast<size_t>(SettingsTab::Display) * DETAIL_TEX_STRIDE;
                    for (size_t i = 0; i < DISPLAY_ROW_COUNT; ++i) {
                        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                        if (display_rows_[i].label) {
                            lt = MakeText(display_rows_[i].label, theme_.text_secondary);
                        }
                        vt = MakeText(display_rows_[i].value,
                                      display_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                    }
                } else if (active_tab_ == SettingsTab::Folders) {
                    // Rows 0–4: toggle auto-folder enable/disable.
                    // Row 5: cycle the folder theme pack.
                    static const AutoFolderIdx kFolderMap[5] = {
                        AutoFolderIdx::NxGames,
                        AutoFolderIdx::Homebrew,
                        AutoFolderIdx::System,
                        AutoFolderIdx::Payloads,
                        AutoFolderIdx::Builtin,
                    };
                    if (detail_row_ < 5u) {
                        const AutoFolderIdx fid = kFolderMap[detail_row_];
                        SetAutoFolderEnabled(fid, !IsAutoFolderEnabled(fid));
                    } else {
                        // v2.5.0: Folder Theme row → full-screen Themes menu.
                        ul::menu::ui::ShowThemesMenu();
                        return;
                    }
                    // Rebuild row values and retexture only the Folders detail pane.
                    BuildRows();
                    const size_t base = static_cast<size_t>(SettingsTab::Folders) * DETAIL_TEX_STRIDE;
                    for (size_t i = 0; i < FOLDERS_ROW_COUNT; ++i) {
                        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
                        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
                        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
                        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
                        if (folders_rows_[i].label) {
                            lt = MakeText(folders_rows_[i].label, theme_.text_secondary);
                        }
                        vt = MakeText(folders_rows_[i].value,
                                      folders_rows_[i].is_button ? theme_.accent : theme_.text_primary);
                    }
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

        // v3.6 absorb wave 1 (#4): propagate the new uid into
        // g_GlobalSettings so the rest of the OS sees the user switch.
        // Prior versions ONLY updated the Settings-tab local state — the
        // dock icons, Games folder, suspended-app routing, save autoscan,
        // and cheats UI all kept using the OLD user.  This call:
        //   1. sets g_GlobalSettings.system_status.selected_user
        //   2. runs InitializeEntries() so g_ApplicationRecords is
        //      reloaded for the new user (~1.5s NS IPC)
        //   3. tells uSystem via SMI so suspended-app routing matches
        //   4. updates the active menu path
        // After that we re-apply the MainMenuLayout's icon set so the
        // user sees their new game list as soon as they close Settings.
        if (acc_has_user_) {
            g_GlobalSettings.SetSelectedUser(new_uid);
            if (g_MenuApplication) {
                auto &mmlt = g_MenuApplication->GetMainMenuLayout();
                if (mmlt) {
                    mmlt->NotifyNextReloadUserChanged();
#ifdef QDESKTOP_MODE
                    mmlt->ApplyAppAndSpecialEntries();
#endif
                }
                g_MenuApplication->ShowNotification(
                    std::string("Switched to ") + std::string(acc_nickname_),
                    4000);
            }
        }

        UL_LOG_INFO("settings: user switched successfully (uid propagated)");
    } else {
        UL_LOG_WARN("settings: pselShowUserSelector failed rc=0x%08x", rc);
    }
}

// ── DoBootToStockQlaunch ──────────────────────────────────────────────────────
// v2.3.6: confirm + dispatch the boot-to-stock-qlaunch toggle.
//
// What this does:
//   1. Asks the user via DisplayDialog.  This is irreversible-from-uMenu
//      because once the rename happens we reboot, and the user's only path
//      back is to manually toggle again from the same menu (which will
//      appear in stock qlaunch only as raw OFW Settings — they re-enter
//      uMenu by either un-disabling on the SD or invoking the same row
//      after coming back).
//   2. Calls smi::RebootToStockQlaunch().  uSystem renames
//      atmosphere/contents/0100000000001000/exefs.nsp <-> .nsp.disabled
//      and triggers appletRequestToReboot().  Process does not return.
//
// Why this exists:
//   AppletId_LibraryAppletSet is documented in libnx as "currently not
//   present on retail devices" — there is no library applet to launch
//   Nintendo's System Settings UI from a custom qlaunch replacement.  The
//   Settings UI is built INTO qlaunch and is replaced wholesale by uSystem.
//   To reach it, the user has to boot stock qlaunch.  This row is the
//   one-tap path.
void QdSettingsElement::DoBootToStockQlaunch() {
    UL_LOG_INFO("settings: DoBootToStockQlaunch() — requesting confirm");

    if (g_MenuApplication == nullptr) {
        UL_LOG_WARN("settings: g_MenuApplication is null; cannot show dialog");
        return;
    }

    const auto choice = g_MenuApplication->DisplayDialog(
        "Boot to Nintendo Home Menu",
        "This will rename the qlaunch override on the SD and reboot.  Stock "
        "Nintendo home menu (with full System Settings) will load on next boot.\n\n"
        "Press the Settings row again from stock qlaunch to come back to Q OS.",
        { "Reboot now", "Cancel" }, true);

    if (choice != 0) {
        UL_LOG_INFO("settings: DoBootToStockQlaunch() cancelled (choice=%d)", (int)choice);
        return;
    }

    UL_LOG_INFO("settings: DoBootToStockQlaunch() confirmed; sending SMI");
    const Result rc = ul::menu::smi::RebootToStockQlaunch();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("settings: RebootToStockQlaunch SMI rc=0x%08x", rc);
        g_MenuApplication->DisplayDialog(
            "Reboot failed",
            "Could not request reboot from uSystem.  Try again, or reboot manually.",
            { "OK" }, true);
    }
    // On success uSystem reboots; this thread will not return.
}

// ── DoNtpSync ─────────────────────────────────────────────────────────────────
// Sphaira-absorb sprint 1: one-shot NTP sync to pool.ntp.org.
//
// Protocol summary (RFC 4330 — SNTPv4):
//   - Send a 48-byte request packet with LI=0, VN=4, Mode=3 (client), all else 0.
//   - Receive a 48-byte response.
//   - Transmit Timestamp (bytes 40-47) holds seconds since 1900-01-01 in the
//     upper 32 bits; convert to Unix epoch by subtracting NTP_EPOCH_DELTA.
//   - Set the Nintendo time:s NetworkSystemClock via timeSetCurrentTime().
//
// Failure modes — ALL graceful (clock never changed on error):
//   1. socketInitializeDefault() fails  → "NTP sync failed: no network"
//   2. getaddrinfo() / connect() fails  → "NTP sync failed: DNS / connect"
//   3. send/recv returns error          → "NTP sync failed: socket I/O"
//   4. Response validation fails        → "NTP sync failed: bad response"
//   5. timeInitialize() / SetCurrentTime fails → "NTP sync failed: time service"
void QdSettingsElement::DoNtpSync() {
    UL_LOG_INFO("settings: DoNtpSync() — starting NTP sync to pool.ntp.org");

    if (g_MenuApplication == nullptr) {
        UL_LOG_WARN("settings: DoNtpSync() — g_MenuApplication is null");
        return;
    }

    // ── Step 1: Ensure socket subsystem is up ──────────────────────────────
    // uMenu does NOT auto-initialize the BSD socket layer at __appInit (it
    // does not call socketInitializeDefault), so this is the first and only
    // call.  Track success so we can call socketExit() at the end (avoid
    // leaking the bsd:u session across NTP attempts).
    bool socket_owned_here = false;
    const Result sock_rc = socketInitializeDefault();
    if (R_SUCCEEDED(sock_rc)) {
        socket_owned_here = true;
    } else {
        // EALREADY (already initialised by another subsystem in some other
        // build configuration) is benign — keep going.  Any other failure
        // means the BSD socket stack is unavailable and we cannot proceed.
        // Real failure codes have the BSD service module; surface them.
        UL_LOG_WARN("settings: DoNtpSync() socketInitializeDefault rc=0x%08X",
                    static_cast<unsigned>(sock_rc));
        // Heuristic: a result of 0 (success) is already handled.  Any
        // non-zero non-EALREADY result is fatal for NTP.  EALREADY isn't a
        // libnx-published constant; tolerate it by trying getaddrinfo
        // anyway — if the stack truly isn't up, getaddrinfo will fail and
        // we'll exit cleanly via the DNS-failed branch below.
    }

    // ── Step 2: Resolve pool.ntp.org ──────────────────────────────────────
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *res = nullptr;
    const int gai_rc = getaddrinfo("pool.ntp.org", "123", &hints, &res);
    if (gai_rc != 0 || res == nullptr) {
        UL_LOG_WARN("settings: DoNtpSync() getaddrinfo gai=%d errno=%d", gai_rc, errno);
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: DNS (gai=%d)", gai_rc);
        if (g_MenuApplication) {
            g_MenuApplication->DisplayDialog(
                "NTP sync failed",
                "Could not resolve pool.ntp.org.\nCheck Wi-Fi connection.",
                { "OK" }, true);
        }
        if (socket_owned_here) socketExit();
        return;
    }

    // ── Step 3: Create UDP socket and set a 3-second receive timeout ──────
    // ::socket — qualify against rc::socket (a Result-namespace in
    // ul_Results.gen.hpp) which would otherwise shadow this BSD POSIX call.
    const int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        UL_LOG_WARN("settings: DoNtpSync() socket() failed errno=%d", errno);
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: socket (errno=%d)", errno);
        g_MenuApplication->DisplayDialog(
            "NTP sync failed", "Could not create UDP socket.", { "OK" }, true);
        if (socket_owned_here) socketExit();
        return;
    }

    // 3-second timeout so we don't block the UI thread indefinitely.
    struct timeval tv;
    tv.tv_sec  = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // ── Step 4: Build and send NTP request (RFC 4330 §5) ──────────────────
    // 48-byte packet:
    //   Byte 0:  LI=0 (no warning), VN=4, Mode=3 (client) → 0b00_100_011 = 0x23
    //   Bytes 1-47: all zero.
    u8 ntp_pkt[48] = {};
    ntp_pkt[0] = 0x23u;  // LI=0, VN=4, Mode=3

    const ssize_t sent = sendto(sock, ntp_pkt, sizeof(ntp_pkt), 0,
                                res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
    freeaddrinfo(res);
    res = nullptr;

    if (sent != static_cast<ssize_t>(sizeof(ntp_pkt))) {
        close(sock);
        UL_LOG_WARN("settings: DoNtpSync() sendto failed sent=%zd errno=%d",
                    static_cast<long>(sent), errno);
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: send (errno=%d)", errno);
        g_MenuApplication->DisplayDialog(
            "NTP sync failed", "Could not send NTP request.\nCheck network connection.", { "OK" }, true);
        if (socket_owned_here) socketExit();
        return;
    }

    // ── Step 5: Receive NTP response ──────────────────────────────────────
    u8 resp[48] = {};
    const ssize_t rcvd = recv(sock, resp, sizeof(resp), 0);
    close(sock);

    if (rcvd < static_cast<ssize_t>(sizeof(resp))) {
        UL_LOG_WARN("settings: DoNtpSync() recv failed rcvd=%zd errno=%d",
                    static_cast<long>(rcvd), errno);
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: recv (errno=%d)", errno);
        g_MenuApplication->DisplayDialog(
            "NTP sync failed", "No response from pool.ntp.org.\nCheck network connection.", { "OK" }, true);
        if (socket_owned_here) socketExit();
        return;
    }

    // ── Step 6: Parse Transmit Timestamp (bytes 40–43 = seconds, big-endian) ──
    // RFC 4330 §5: Transmit Timestamp starts at byte offset 40.
    // Upper 32 bits = seconds since 1900-01-01.
    const u32 ntp_secs_be =
        (static_cast<u32>(resp[40]) << 24) |
        (static_cast<u32>(resp[41]) << 16) |
        (static_cast<u32>(resp[42]) <<  8) |
        (static_cast<u32>(resp[43])      );

    if (ntp_secs_be == 0u) {
        UL_LOG_WARN("settings: DoNtpSync() server returned zero timestamp");
        strncpy(sys_ntp_status_, "Sync failed: bad response", sizeof(sys_ntp_status_) - 1);
        g_MenuApplication->DisplayDialog(
            "NTP sync failed", "Server returned an invalid timestamp.", { "OK" }, true);
        if (socket_owned_here) socketExit();
        return;
    }

    // Convert NTP epoch (1900) to Unix epoch (1970).
    const u64 unix_ts = static_cast<u64>(ntp_secs_be) - NTP_EPOCH_DELTA;

    // Basic sanity: year must be ≥ 2024 (unix_ts ≥ 1704067200).
    if (unix_ts < 1704067200ULL) {
        UL_LOG_WARN("settings: DoNtpSync() implausible timestamp %llu", (unsigned long long)unix_ts);
        strncpy(sys_ntp_status_, "Sync failed: bad timestamp", sizeof(sys_ntp_status_) - 1);
        g_MenuApplication->DisplayDialog(
            "NTP sync failed", "Server returned an implausible time.\nNot applying.", { "OK" }, true);
        if (socket_owned_here) socketExit();
        return;
    }

    // Socket subsystem is no longer needed past this point.  Releasing it
    // before the time-service swap keeps the failure-mode story simple
    // (each block has at most one cleanup).
    if (socket_owned_here) { socketExit(); socket_owned_here = false; }

    // ── Step 7: Set NetworkSystemClock via time:s ──────────────────────────
    // uMenu's __appInit opened time:a (TimeServiceType_Menu).  time:a CAN
    // read every clock but CANNOT write the NetworkSystemClock — the system
    // rejects the call with result 0x275 (module=117 desc=1 → permission /
    // privileged-clock).  Temporarily swap the libnx default to time:s for
    // this single write, then restore time:a so the rest of uMenu (which
    // expects time:a) is unaffected.
    //
    // Sequence: timeExit() drops the refcount on the open time:a session
    // (libnx serviceClose closes when refcount hits zero), set the global,
    // timeInitialize() opens time:s with the new global, set, timeExit() to
    // close time:s, restore the global, timeInitialize() to re-open time:a.
    const TimeServiceType saved_service_type = __nx_time_service_type;

    timeExit();
    __nx_time_service_type = TimeServiceType_System;
    const Result init_rc = timeInitialize();
    if (R_FAILED(init_rc)) {
        UL_LOG_WARN("settings: DoNtpSync() timeInitialize(time:s) failed rc=0x%08X",
                    static_cast<unsigned>(init_rc));
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: time:s init 0x%08X", static_cast<unsigned>(init_rc));
        // Best-effort restore so subsequent uMenu time queries keep working.
        __nx_time_service_type = saved_service_type;
        timeInitialize();
        g_MenuApplication->DisplayDialog(
            "NTP sync failed",
            "Could not open the system time service (time:s).\nClock unchanged.",
            { "OK" }, true);
        return;
    }

    const Result time_rc = timeSetCurrentTime(TimeType_NetworkSystemClock, unix_ts);

    // Close time:s and restore time:a regardless of the SetCurrentTime result.
    timeExit();
    __nx_time_service_type = saved_service_type;
    const Result restore_rc = timeInitialize();
    if (R_FAILED(restore_rc)) {
        UL_LOG_WARN("settings: DoNtpSync() restore timeInitialize(time:a) failed rc=0x%08X",
                    static_cast<unsigned>(restore_rc));
        // Not fatal for the user, but log it — every other uMenu time query
        // will now silently fall back to its own retry path.
    }

    if (R_FAILED(time_rc)) {
        UL_LOG_WARN("settings: DoNtpSync() timeSetCurrentTime rc=0x%08X",
                    static_cast<unsigned>(time_rc));
        snprintf(sys_ntp_status_, sizeof(sys_ntp_status_),
                 "Sync failed: set 0x%08X", static_cast<unsigned>(time_rc));
        g_MenuApplication->DisplayDialog(
            "NTP sync failed",
            "Could not set the system clock.\n(rc shown in row below)",
            { "OK" }, true);
        return;
    }

    // ── Step 8: Update status display and show success notice ─────────────
    UL_LOG_INFO("settings: DoNtpSync() success — unix_ts=%llu", (unsigned long long)unix_ts);
    snprintf(sys_ntp_status_, sizeof(sys_ntp_status_), "Synced (NTP ok)");

    // Rebuild the System tab rows in-place to show the updated status string.
    BuildRows();
    const size_t base = static_cast<size_t>(SettingsTab::System) * DETAIL_TEX_STRIDE;
    for (size_t i = 0; i < SYSTEM_ROW_COUNT; ++i) {
        SDL_Texture *&lt = detail_tex_[base + i * 2u + 0u];
        SDL_Texture *&vt = detail_tex_[base + i * 2u + 1u];
        if (lt) { pu::ui::render::DeleteTexture(lt); lt = nullptr; }
        if (vt) { pu::ui::render::DeleteTexture(vt); vt = nullptr; }
        if (system_rows_[i].label) {
            lt = MakeText(system_rows_[i].label, theme_.text_secondary);
        }
        vt = MakeText(system_rows_[i].value,
                      system_rows_[i].is_button ? theme_.accent : theme_.text_primary);
    }

    g_MenuApplication->DisplayDialog(
        "NTP sync complete", "Clock updated successfully from pool.ntp.org.", { "OK" }, true);
}

// ── QdSettingsLayout ──────────────────────────────────────────────────────────

QdSettingsLayout::QdSettingsLayout(const QdTheme &theme) {
    UL_LOG_INFO("settings: QdSettingsLayout ctor");
    this->SetBackgroundColor({ 0, 0, 0, 255 });
    settings_elm_ = QdSettingsElement::New(theme);
    this->Add(settings_elm_);
    // v1.9.7: hot-corner overlay painted above the settings panel.
    overlay_ = QdHotCornerOverlay::New();
    this->Add(overlay_);
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
