// qd_SettingsLayout.hpp — Q OS-native Settings layout for the qdesktop "C" dock tile.
// Replaces upstream ui_SettingsMenuLayout for the qdesktop surface.
// Six tabs: System, Network, Audio, Display, Account, About.
// Layout: sidebar (240 px) + detail pane, 1920 x 1080.
// D-pad: Up/Down changes tab (sidebar focus), Right enters detail pane,
//        Left returns to sidebar, B returns to Main menu, A activates widget.
#pragma once
#include <pu/Plutonium>
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <cstddef>

namespace ul::menu::qdesktop {

// ── Settings layout pixel constants ──────────────────────────────────────────

/// Width of the left settings sidebar.
static constexpr s32 SETTINGS_SIDEBAR_W = 240;

/// Height of the title strip below the topbar.
static constexpr s32 SETTINGS_TITLE_H = 56;

/// Usable body top: topbar (48) + title strip (56).
static constexpr s32 SETTINGS_BODY_TOP = static_cast<s32>(TOPBAR_H) + SETTINGS_TITLE_H; // 104

/// Usable body height: screen - topbar - title strip - dock.
static constexpr s32 SETTINGS_BODY_H = static_cast<s32>(SCREEN_H)
                                     - static_cast<s32>(TOPBAR_H)
                                     - SETTINGS_TITLE_H
                                     - static_cast<s32>(DOCK_H); // 868

/// Detail pane left edge.
static constexpr s32 SETTINGS_DETAIL_X = SETTINGS_SIDEBAR_W;

/// Detail pane width.
static constexpr s32 SETTINGS_DETAIL_W = static_cast<s32>(SCREEN_W) - SETTINGS_SIDEBAR_W;

/// Row height inside the detail pane.
static constexpr s32 SETTINGS_ROW_H = 54;

/// Sidebar row height.
static constexpr s32 SETTINGS_SIDEBAR_ROW_H = 62;

// ── Tab identifiers ───────────────────────────────────────────────────────────

enum class SettingsTab : u8 {
    System  = 0,
    Network = 1,
    Audio   = 2,
    Display = 3,
    Account = 4,
    About   = 5,
    Count   = 6,
};

// ── QdSettingsElement ─────────────────────────────────────────────────────────

/// The full-screen settings element.  Hosted by QdSettingsLayout.
/// Active when the "C" (Control) dock tile is triggered.
class QdSettingsElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdSettingsElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdSettingsElement>(theme);
    }

    explicit QdSettingsElement(const QdTheme &theme);
    ~QdSettingsElement();

    // ── Element interface ──────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

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
    // 6 tab items + 1 "System Settings →" entry at the bottom.
    static constexpr size_t SIDEBAR_ITEM_COUNT = 7;
    static const char *const SIDEBAR_LABELS[SIDEBAR_ITEM_COUNT];

    // ── Detail row caps per tab ────────────────────────────────────────────
    static constexpr size_t SYSTEM_ROW_COUNT  = 8;
    static constexpr size_t NETWORK_ROW_COUNT = 5;
    static constexpr size_t AUDIO_ROW_COUNT   = 3;
    static constexpr size_t DISPLAY_ROW_COUNT = 4;
    static constexpr size_t ACCOUNT_ROW_COUNT = 4; // last row = "Switch User" button
    static constexpr size_t ABOUT_ROW_COUNT   = 7;
    static constexpr size_t MAX_DETAIL_ROWS   = 8;

    // ── Cached system data: System tab ─────────────────────────────────────
    char sys_fw_[32];          ///< e.g. "18.1.0"
    char sys_serial_[32];      ///< last 4 visible, rest masked
    char sys_uptime_[32];      ///< e.g. "2h 14m"
    char sys_ams_[32];         ///< e.g. "1.8.0 / EmuNAND"
    char sys_temp_pcb_[16];    ///< e.g. "42°C"
    char sys_temp_soc_[16];    ///< e.g. "38°C"
    char sys_mode_[24];        ///< "Handheld" or "Docked"
    char sys_boot_count_[24];  ///< e.g. "47" or "n/a"

    // ── Cached system data: Network tab ───────────────────────────────────
    char net_status_[32];      ///< "Connected (Wi-Fi)" / "No connection"
    char net_ip_[20];          ///< "192.168.1.42" or "—"
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

    // ── SDL text textures: sidebar ────────────────────────────────────────
    SDL_Texture *sidebar_tex_[SIDEBAR_ITEM_COUNT];

    // ── SDL text textures: detail rows (label + value, per row per tab) ───
    // Indexed [tab][row]: label_tex + value_tex pair.
    // Flattened: 6 tabs * MAX_DETAIL_ROWS * 2 = 96 textures.
    static constexpr size_t DETAIL_TEX_STRIDE = MAX_DETAIL_ROWS * 2;
    SDL_Texture *detail_tex_[static_cast<size_t>(SettingsTab::Count) * MAX_DETAIL_ROWS * 2];

    // ── SDL text texture: title ────────────────────────────────────────────
    SDL_Texture *title_tex_;

    // ── Focus state ────────────────────────────────────────────────────────
    QdTheme     theme_;
    FocusArea   focus_area_;
    SettingsTab active_tab_;
    size_t      sidebar_focus_row_; ///< highlighted sidebar row 0–6 (rows 0–5 = tabs, row 6 = System Settings →)
    size_t      detail_row_; ///< focused detail row index (when in Detail focus)

    // ── Private helpers ────────────────────────────────────────────────────

    /// Destroy all SDL_Texture* resources owned by this element.
    void FreeAllTextures();

    /// Populate the Row arrays from cached data fields.
    void BuildRows();

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
};

} // namespace ul::menu::qdesktop
