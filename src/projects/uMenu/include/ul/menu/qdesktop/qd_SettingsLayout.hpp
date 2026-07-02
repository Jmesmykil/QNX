// qd_SettingsLayout.hpp — Q OS-native Settings layout for the qdesktop "C" dock tile.
// Replaces upstream ui_SettingsMenuLayout for the qdesktop surface.
// Seven tabs: System, Network, Audio, Display, Account, About, Folders (v2.0).
// Layout: sidebar (240 px) + detail pane, 1920 x 1080.
// D-pad: Up/Down changes tab (sidebar focus), Right enters detail pane,
//        Left returns to sidebar, B returns to Main menu, A activates widget.
#pragma once
#include <pu/Plutonium>
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // PANEL_TEXT_LEFT_PAD etc (D14 fix)
#include <ul/menu/qdesktop/qd_HotCornerOverlay.hpp>
#include <ul/menu/qdesktop/qd_ContentElement.hpp>
// v2.5.0 — qd_ThemePicker include removed; Folder Theme row now opens the
// full-screen ui::ThemesMenuLayout instead of an in-window popup.
#include <switch.h>
#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Settings layout pixel constants ──────────────────────────────────────────

/// Width of the left settings sidebar.
static constexpr s32 SETTINGS_SIDEBAR_W = 240;

/// Height of the title strip below the topbar.
static constexpr s32 SETTINGS_TITLE_H = 56;

/// Usable body top: topbar (48) + title strip (56).
static constexpr s32 SETTINGS_BODY_TOP = static_cast<s32>(TOPBAR_H) + SETTINGS_TITLE_H; // 104

/// Usable body height: screen - topbar - title strip - dock.
/// Note: this constant uses SCREEN_H=1080 and is larger than the natural canvas
/// height (600 px). It is used only for background fill rects which are clipped
/// by the SDL clip rect set by QdWindow. Row layout does NOT use SETTINGS_BODY_H.
static constexpr s32 SETTINGS_BODY_H = static_cast<s32>(SCREEN_H)
                                     - static_cast<s32>(TOPBAR_H)
                                     - SETTINGS_TITLE_H
                                     - static_cast<s32>(DOCK_H); // 828

/// Detail pane left edge.
static constexpr s32 SETTINGS_DETAIL_X = SETTINGS_SIDEBAR_W;

/// Detail pane width.
/// Must be derived from the natural canvas width (GetNaturalW()=800), NOT SCREEN_W.
/// QdSettingsElement is a 800×600 windowed element. Using SCREEN_W=1920 places
/// right-aligned value text at natural x~1896, which is 1096 px past the canvas
/// right edge; after scale the values land ~454 px outside the viewport clip rect
/// and are invisible. Fix: anchor to the 800 px natural canvas.
static constexpr s32 SETTINGS_NATURAL_W = 800; ///< matches QdSettingsElement::GetNaturalW()
static constexpr s32 SETTINGS_DETAIL_W = SETTINGS_NATURAL_W - SETTINGS_SIDEBAR_W; // 560

/// Row height inside the detail pane.
static constexpr s32 SETTINGS_ROW_H = 54;

/// Sidebar row height.
static constexpr s32 SETTINGS_SIDEBAR_ROW_H = 62;

/// Unified text left-margin inside all Settings panes.
/// D14 fix: three separate magic offsets existed (sidebar=18, detail=20, title=24).
/// All set to PANEL_TEXT_LEFT_PAD=20 from qd_LayoutConstants.hpp.
static constexpr s32 SETTINGS_TEXT_LEFT_PAD = PANEL_TEXT_LEFT_PAD;  // 20

/// Text right-margin in the detail pane.
static constexpr s32 SETTINGS_TEXT_RIGHT_PAD = PANEL_TEXT_RIGHT_PAD;  // 24

// ── Tab identifiers ───────────────────────────────────────────────────────────

enum class SettingsTab : u8 {
    System   = 0,
    Network  = 1,
    Audio    = 2,
    Display  = 3,
    Account  = 4,
    About    = 5,
    Folders  = 6,  ///< v2.0: per-category auto-folder toggles + theme-pack cycle
    Overlays = 7,  ///< v3.6: Tesla overlay enable/disable manager
    Count    = 8,
};

// ── QdSettingsElement ─────────────────────────────────────────────────────────

/// The full-screen settings element.  Hosted by QdSettingsLayout.
/// Active when the "C" (Control) dock tile is triggered.
class QdSettingsElement : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdSettingsElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdSettingsElement>(theme);
    }

    explicit QdSettingsElement(const QdTheme &theme);
    ~QdSettingsElement();

    // ── QdContentElement interface ─────────────────────────────────────────
    s32 GetNaturalW() const override { return 800; }
    s32 GetNaturalH() const override { return 600; }
    // W8-FIX Bug 5: use uniform-fit scale (not width-bound) so the 800×600
    // natural canvas is scaled to fit the viewport in BOTH axes.  With
    // width-bound scale the 800×600 canvas is scaled by 1.6× (1280/800)
    // making the 600-tall content render at 960 screen px → overflows the
    // 800-px-tall window and clips the hint bar at the bottom.
    // Uniform-fit picks min(1280/800, 715/600)=1.19 — content fits exactly,
    // hint bar is always visible, no row clipping.
    bool PrefersWidthBoundScale() const override { return false; }

    // ── Element interface ──────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return GetNaturalW(); }
    s32 GetHeight() override { return GetNaturalH(); }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Re-poll all live system data into cached fields.
    /// Call once when the element becomes visible.
    void Refresh();

private:
    // ── Focus model ────────────────────────────────────────────────────────

    enum class FocusArea : u8 { Sidebar, Detail };

    // ── Sidebar entries ────────────────────────────────────────────────────
    // v2.3.5: removed legacy "System Settings →" sentinel row that bridged
    // into upstream SettingsMenuLayout via ShowSettingsMenu().  That layout's
    // first paint hits a use-after-free in fsdev unwind code (PC inside
    // _fsdevUnmountDeviceStruct / fsdev_fixpath, mesosphere reports the
    // process abort as "Undefined System Call svc 0x6f").  All 7 categories
    // we surface are already real tabs — there is nothing to bridge to.
    static constexpr size_t SIDEBAR_ITEM_COUNT = 8;
    static const char *const SIDEBAR_LABELS[SIDEBAR_ITEM_COUNT];

    // ── Detail row caps per tab ────────────────────────────────────────────
    // v2.3.6: System gains a 9th row — "Boot to Nintendo Home Menu" button —
    // so the user can flip into stock qlaunch (and thus reach Nintendo's
    // built-in System Settings UI) without manually editing the SD.
    // Sphaira-absorb sprint 1: System gains a 10th row — "Sync clock with internet"
    // NTP button — one-shot UDP sync to pool.ntp.org using bsd:u + time:s.
    static constexpr size_t SYSTEM_ROW_COUNT  = 10;
    static constexpr size_t NETWORK_ROW_COUNT = 5;
    static constexpr size_t AUDIO_ROW_COUNT   = 3;
    // v3.6: bumped from 4 to 5 — adds a "Cal Profile" cycle button
    // that writes setsysSetTvSettings with 3 calibration presets
    // (Default / TV Limited Range / Bright).
    static constexpr size_t DISPLAY_ROW_COUNT = 5;
    static constexpr size_t ACCOUNT_ROW_COUNT = 4; // last row = "Switch User" button
    static constexpr size_t ABOUT_ROW_COUNT   = 7;
    // Folders tab (v2.0): 5 toggle rows (NxGames/Homebrew/System/Payloads/Builtin)
    //                   + 1 theme-pack cycle row.
    static constexpr size_t FOLDERS_ROW_COUNT = 6;
    // Overlays tab (v3.6): up to N .ovl files in sdmc:/switch/.overlays/.
    // Each row shows filename + Enabled/Disabled, on activation toggles via
    // rename (.ovl ↔ .ovl.disabled).  Cap matches MAX_DETAIL_ROWS so the
    // existing detail_tex_ buffer doesn't overflow.
    static constexpr size_t OVERLAYS_ROW_COUNT = 16;  ///< up to 16 overlays
    static constexpr size_t MAX_DETAIL_ROWS   = 16;   ///< bumped from 10 for overlays tab

    // ── Cached system data: System tab ─────────────────────────────────────
    char sys_fw_[32];          ///< e.g. "18.1.0"
    char sys_serial_[32];      ///< last 4 visible, rest masked
    char sys_uptime_[32];      ///< e.g. "2h 14m"
    char sys_ams_[32];         ///< e.g. "1.8.0 / EmuNAND"
    char sys_temp_pcb_[16];    ///< e.g. "42°C"
    char sys_temp_soc_[16];    ///< e.g. "38°C"
    char sys_mode_[24];        ///< "Handheld" or "Docked"
    char sys_boot_count_[24];  ///< e.g. "47" or "n/a"
    // Sized to match Row.value width (48) so strncpy → row doesn't trip
    // -Werror=stringop-truncation / -Wformat-truncation.
    char sys_ntp_status_[48];  ///< last NTP sync result or "—" (Sphaira-absorb sprint 1)

    // ── Cached system data: Network tab ───────────────────────────────────
    char net_status_[32];      ///< "Connected (Wi-Fi)" / "No connection"
    char net_ip_[20];          ///< "LAN-IP" or "—"
    char net_strength_[8];     ///< "3" (1–3 bars) or "—"
    char net_wifi_[12];        ///< "Enabled" or "Disabled"
    char net_ethernet_[32];    ///< "Active" / "Not connected"

    // ── Cached system data: Audio tab ─────────────────────────────────────
    char aud_volume_[16];      ///< e.g. "78%"
    char aud_bt_[16];          ///< "Enabled" or "Disabled"
    char aud_nfc_[16];         ///< "Enabled" or "Disabled" (NFC is audio-adjacent in HW)

    // ── Cached system data: Display tab ───────────────────────────────────
    char disp_brightness_[16]; ///< e.g. "62%" or "n/a (TV mode)"
    char disp_mode_[24];       ///< "Handheld" or "Docked"
    char disp_ambient_[20];    ///< ambient lux or "n/a"
    char disp_usb30_[16];      ///< "Enabled" or "Disabled"
    // v3.6: display calibration preset index 0..2 (Default/TV/Bright).
    // Persists across sessions in sdmc:/ulaunch/qos-display-cal.toml.
    int  disp_cal_idx_ = 0;

    // ── Cached system data: Account tab ───────────────────────────────────
    AccountUid account_uid_;   ///< currently selected user UID (zero if none)
    char acc_nickname_[0x20 + 4]; ///< user nickname + NUL
    char acc_language_[16];    ///< e.g. "en-US"
    bool acc_has_user_;        ///< true if a user is selected

    // ── Cached system data: About tab ─────────────────────────────────────
    char abt_fw_[32];
    char abt_serial_[32];
    char abt_ams_ver_[32];
    char abt_ams_emummc_[8];   ///< "Yes" or "No"
    char abt_region_[16];      ///< region code string
    char abt_nickname_[0x20 + 4];
    char abt_battery_lot_[24];

    // ── Detail row label/value buffers ────────────────────────────────────
    // Fixed-size label+value arrays for each tab; avoids heap per row.
    struct Row {
        const char *label;  ///< static literal
        char        value[48];
        bool        is_button; ///< true = activatable (rendered with button style)
    };

    Row system_rows_[SYSTEM_ROW_COUNT];
    Row network_rows_[NETWORK_ROW_COUNT];
    Row audio_rows_[AUDIO_ROW_COUNT];
    Row display_rows_[DISPLAY_ROW_COUNT];
    Row account_rows_[ACCOUNT_ROW_COUNT];
    Row about_rows_[ABOUT_ROW_COUNT];
    Row folders_rows_[FOLDERS_ROW_COUNT]; ///< v2.0: 5 toggles + 1 theme cycle
    Row overlays_rows_[OVERLAYS_ROW_COUNT]; ///< v3.6: Tesla overlay toggles

    // v3.6 absorb wave 1 — Tesla overlay manager state.
    //
    // overlay_paths_:    absolute paths to discovered .ovl / .ovl.disabled
    //                    files in /switch/.overlays/.  Sized at OVERLAYS_ROW_COUNT
    //                    (16) and populated by BuildOverlaysRows().
    // overlay_enabled_:  parallel to overlay_paths_; true if file ends in
    //                    ".ovl", false if ".ovl.disabled".
    // overlays_scanned_: set true after the first dir walk; ToggleOverlay
    //                    re-scans on demand after a successful rename.
    // overlays_count_:   actual number of overlays discovered (≤
    //                    OVERLAYS_ROW_COUNT).
    char  overlay_paths_[OVERLAYS_ROW_COUNT][256];
    bool  overlay_enabled_[OVERLAYS_ROW_COUNT];
    bool  overlays_scanned_ = false;
    size_t overlays_count_  = 0;

    // ── SDL text textures: sidebar ────────────────────────────────────────
    SDL_Texture *sidebar_tex_[SIDEBAR_ITEM_COUNT];

    // ── SDL text textures: detail rows (label + value, per row per tab) ───
    // Indexed [tab][row]: label_tex + value_tex pair.
    // Flattened: 7 tabs * MAX_DETAIL_ROWS * 2 = 112 textures.
    static constexpr size_t DETAIL_TEX_STRIDE = MAX_DETAIL_ROWS * 2;
    SDL_Texture *detail_tex_[static_cast<size_t>(SettingsTab::Count) * MAX_DETAIL_ROWS * 2];

    // ── SDL text texture: title ────────────────────────────────────────────
    SDL_Texture *title_tex_;

    // ── SDL text texture: bottom hint bar ─────────────────────────────────
    SDL_Texture *hint_bar_tex_;

    // ── Focus state ────────────────────────────────────────────────────────
    QdTheme     theme_;
    FocusArea   focus_area_;
    SettingsTab active_tab_;
    size_t      sidebar_focus_row_; ///< highlighted sidebar row 0–6 (rows 0–5 = tabs, row 6 = System Settings →)
    size_t      detail_row_; ///< focused detail row index (when in Detail focus)

    // ── ts service (System tab temperature rows) ──────────────────────────
    // Mirrors the W3-MONTEMP pattern in qd_MonitorLayout: legacy ts wrappers
    // (tsGetTemperature / tsGetTemperatureMilliC) were removed in HorizonOS
    // 14.0.0+. Use TsSession on FW 10.0.0+, fall back to legacy wrappers
    // otherwise. Sessions are long-lived (opened in ctor, closed in dtor) —
    // avoids per-Refresh open/close churn.
    bool      ts_inited_         = false; ///< tsInitialize() succeeded
    bool      ts_sess_soc_open_  = false; ///< TsDeviceCode_LocationExternal session open
    bool      ts_sess_pcb_open_  = false; ///< TsDeviceCode_LocationInternal session open
    bool      ts_use_session_    = false; ///< prefer session API (FW 10.0.0+)
    TsSession ts_sess_soc_       = {};    ///< SoC (External) device-code session
    TsSession ts_sess_pcb_       = {};    ///< PCB (Internal) device-code session
    u32       ts_soc_last_rc_    = 0;     ///< last SoC probe failure rc (0 = ok)
    u32       ts_pcb_last_rc_    = 0;     ///< last PCB probe failure rc (0 = ok)

    // W5-PERF-HOTSPOTS #9 (P0-C): hoisted service sessions — opened in ctor, closed in dtor.
    // Eliminates per-Refresh IPC open/close cost for nifm, audctl, lbl, and account.
    bool nifm_inited_   = false;  ///< nifmInitialize(User) succeeded
    bool audctl_inited_ = false;  ///< audctlInitialize() succeeded
    bool lbl_inited_    = false;  ///< lblInitialize() succeeded; only valid in handheld mode
    // Account profile: opened once per selected user; re-opened when account_uid_ changes.
    // Close with accountProfileClose when a new user is selected.
    bool           acc_profile_open_ = false;  ///< accountGetProfile succeeded and profile_ is open
    AccountProfile acc_profile_      = {};     ///< open AccountProfile handle

    // W5-PERF-HOTSPOTS #10 (P0-C): boot count cached at ctor; FIRMWARE counter is
    // stable for the duration of the process (it only increments on reboot).
    bool boot_count_loaded_ = false;  ///< true after first successful read

    // W6-LEDGER: per-service ledger handles (kind=Service).  0 = not tracked.
    uint64_t svc_lh_ts_      = 0;
    uint64_t svc_lh_nifm_    = 0;
    uint64_t svc_lh_audctl_  = 0;
    uint64_t svc_lh_lbl_     = 0;

    // ── Private helpers ────────────────────────────────────────────────────

    /// Destroy all SDL_Texture* resources owned by this element.
    void FreeAllTextures();

    /// Populate the Row arrays from cached data fields.
    void BuildRows();

    /// v3.6: Walk sdmc:/switch/.overlays/ and populate overlay_paths_ +
    /// overlay_enabled_ from the .ovl / .ovl.disabled files found.  Idempotent.
    void ScanOverlays();

    /// v3.6: Toggle the overlay at overlay_paths_[idx] between .ovl and
    /// .ovl.disabled via std::rename.  On success rescans + rebuilds rows.
    /// Returns true on successful toggle.
    bool ToggleOverlay(size_t idx);

    /// v3.6: Cycle the Display calibration preset index 0..2 and apply via
    /// setsysSetTvSettings.  Presets cover gamma/contrast/rgb_range/
    /// hdmi_content_type tuning.  Persists disp_cal_idx_ on disk.
    void CycleDisplayCalibration();

    /// Render the left sidebar panel.
    void RenderSidebar(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Render the right detail pane for the active tab.
    void RenderDetailPane(SDL_Renderer *r, s32 ox, s32 oy);

    /// Render a single detail row at the given y position.
    void RenderDetailRow(SDL_Renderer *r, const Row &row,
                         s32 x, s32 y, s32 w,
                         bool focused, bool is_button,
                         size_t tab_idx, size_t row_idx);

    /// Invoke pselShowUserSelector and refresh Account/About caches on success.
    void DoUserSwitch();

    /// v2.3.6: Confirm + dispatch the boot-to-stock-qlaunch toggle.
    /// Asks the user via DisplayDialog, then sends smi::RebootToStockQlaunch
    /// (uSystem renames the qlaunch override on disk and calls
    /// appletRequestToReboot()).  This function does not return on success.
    void DoBootToStockQlaunch();

    /// Sphaira-absorb sprint 1: one-shot NTP sync to pool.ntp.org.
    /// Opens bsd:u + time:s, sends a 48-byte NTP request to 123/UDP,
    /// parses the response, and calls timeSetsystemNetworkClock.
    /// On any failure the error is stored in sys_ntp_status_ and shown to
    /// the user via DisplayDialog.  Clock is NEVER changed if any step fails.
    void DoNtpSync();

    /// Return the count of detail rows for the active tab.
    size_t ActiveTabRowCount() const;

    /// Return a pointer to the first Row of the active tab.
    const Row *ActiveTabRows() const;

    /// Convenience: safe snprintf into a fixed char buffer.
    template <size_t N>
    static void SafeSnprintf(char (&buf)[N], const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, N, fmt, ap);
        va_end(ap);
        buf[N - 1] = '\0';
    }

};

// ── QdSettingsLayout ──────────────────────────────────────────────────────────

/// Plutonium Layout that hosts the wallpaper + the settings element.
/// Subclasses IMenuLayout so OnMessage()'s static_pointer_cast<IMenuLayout>
/// is type-safe — a bare pu::ui::Layout here would Data-Abort at 0x0 on any
/// smi::MenuMessage (HOME, GameCardMountFailure, SdCardEjected, etc.).
/// See qd_VaultHostLayout.hpp for the canonical crash chain description.
class QdSettingsLayout : public ul::menu::ui::IMenuLayout {
public:
    using Ref = std::shared_ptr<QdSettingsLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdSettingsLayout>(theme);
    }

    explicit QdSettingsLayout(const QdTheme &theme);
    ~QdSettingsLayout() = default;

    // ── IMenuLayout pure-virtual obligations ─────────────────────────────────

    void OnMenuInput(const u64 keys_down,
                     const u64 keys_up,
                     const u64 keys_held,
                     const pu::ui::TouchPoint touch_pos) override;

    // Return to the main desktop on HOME and consume the message.
    bool OnHomeButtonPress() override;

    // Settings has no per-layout sfx — intentional no-ops; not stubs.
    // Input and rendering go through the child QdSettingsElement directly.
    void LoadSfx() override;
    void DisposeSfx() override;

    /// Refresh live system data. Call once each time the layout becomes visible.
    void Refresh() { settings_elm_->Refresh(); }

private:
    QdSettingsElement::Ref settings_elm_;
    QdHotCornerOverlay::Ref overlay_;
};

} // namespace ul::menu::qdesktop
