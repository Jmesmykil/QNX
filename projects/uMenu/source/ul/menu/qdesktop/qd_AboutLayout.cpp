// qd_AboutLayout.cpp — Q OS-native About panel implementation (v1.0.0).
// Slot-4 dock-tile destination; replaces ShowAboutDialog.
// Design principles:
//   • Procedural "Q OS" logo — no romfs PNG dependency.
//   • Every libnx call R_FAILED-checked; failure path stores "n/a" or "?".
//   • UL_LOG_INFO telemetry at Refresh() entry and exit.
//   • SDL textures lazily built in Refresh(), destroyed in dtor and re-Refresh.
//   • Reading battery/charger/op-mode live; firmware/serial from g_GlobalSettings
//     (already queried at startup, no redundant service open/close).

#include <ul/menu/qdesktop/qd_AboutLayout.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <ul/util/util_Telemetry.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// g_MenuApplication and g_GlobalSettings are defined in main.cpp.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── ProductModel string table ─────────────────────────────────────────────────

const char *QdAboutElement::ProductModelString(const s32 model) {
    // libnx SetSysProductModel enum values (as of 18.x SDK).
    switch (model) {
        case SetSysProductModel_Nx:       return "Switch (HAC-001)";
        case SetSysProductModel_Copper:   return "Switch (HAC-001B)";
        case SetSysProductModel_Iowa:     return "Switch Lite (HDH-001)";
        case SetSysProductModel_Hoag:     return "Switch v2 (HAC-001) OLED?";
        case SetSysProductModel_Calcio:   return "Switch OLED (HEG-001)";
        case SetSysProductModel_Aula:     return "Switch OLED (HEG-001) v2";
        default:                          return "Switch (unknown model)";
    }
}

// ── QdAboutElement ctor / dtor ────────────────────────────────────────────────

QdAboutElement::QdAboutElement(const QdTheme &theme)
    : theme_(theme), refreshed_(false)
{
    UL_LOG_INFO("about: QdAboutElement ctor");
    for (size_t i = 0; i < ABOUT_ROW_COUNT; ++i) {
        rows_[i].label[0]     = '\0';
        rows_[i].value[0]     = '\0';
        rows_[i].label_tex    = nullptr;
        rows_[i].value_tex    = nullptr;
    }
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        section_tex_[i] = nullptr;
    }
    logo_tex_   = nullptr;
    footer_tex_ = nullptr;
}

QdAboutElement::~QdAboutElement() {
    UL_LOG_INFO("about: QdAboutElement dtor");
    FreeRowTextures();
    FreeStaticTextures();
}

// ── Texture lifecycle ─────────────────────────────────────────────────────────

void QdAboutElement::FreeRowTextures() {
    for (size_t i = 0; i < ABOUT_ROW_COUNT; ++i) {
        if (rows_[i].label_tex) {
            SDL_DestroyTexture(rows_[i].label_tex);
            rows_[i].label_tex = nullptr;
        }
        if (rows_[i].value_tex) {
            SDL_DestroyTexture(rows_[i].value_tex);
            rows_[i].value_tex = nullptr;
        }
    }
}

void QdAboutElement::FreeStaticTextures() {
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        if (section_tex_[i]) {
            SDL_DestroyTexture(section_tex_[i]);
            section_tex_[i] = nullptr;
        }
    }
    if (logo_tex_) {
        SDL_DestroyTexture(logo_tex_);
        logo_tex_ = nullptr;
    }
    if (footer_tex_) {
        SDL_DestroyTexture(footer_tex_);
        footer_tex_ = nullptr;
    }
}

// ── Refresh — populate row data + rasterise textures ─────────────────────────

void QdAboutElement::Refresh() {
    UL_LOG_INFO("about: Refresh() begin");

    // Destroy stale textures before rebuilding.
    FreeRowTextures();
    FreeStaticTextures();

    // ── Row 0: uLaunch / Q OS shell version ───────────────────────────────
    snprintf(rows_[0].label, sizeof(rows_[0].label), "Q OS Shell");
    snprintf(rows_[0].value, sizeof(rows_[0].value), "v" UL_VERSION "  (built " __DATE__ " " __TIME__ ")");

    // ── Row 1: Atmosphère version ──────────────────────────────────────────
    snprintf(rows_[1].label, sizeof(rows_[1].label), "Atmosphère");
    if (g_GlobalSettings.ams_version.major == 0 &&
        g_GlobalSettings.ams_version.minor == 0 &&
        g_GlobalSettings.ams_version.micro == 0) {
        snprintf(rows_[1].value, sizeof(rows_[1].value), "n/a");
    } else {
        snprintf(rows_[1].value, sizeof(rows_[1].value), "%u.%u.%u%s",
            static_cast<u32>(g_GlobalSettings.ams_version.major),
            static_cast<u32>(g_GlobalSettings.ams_version.minor),
            static_cast<u32>(g_GlobalSettings.ams_version.micro),
            g_GlobalSettings.ams_is_emummc ? "  (emuMMC)" : "");
    }

    // ── Row 2: Firmware version ────────────────────────────────────────────
    snprintf(rows_[2].label, sizeof(rows_[2].label), "Firmware");
    if (rows_[2].value[0] == '\0' || true) {
        // fw_version.display_version is a null-terminated char[0x10] string.
        const char *fwstr = g_GlobalSettings.fw_version.display_version;
        if (fwstr[0] != '\0') {
            snprintf(rows_[2].value, sizeof(rows_[2].value), "%s", fwstr);
        } else {
            snprintf(rows_[2].value, sizeof(rows_[2].value), "n/a");
        }
    }

    // ── Row 3: Console serial (last 4 chars visible, rest masked) ─────────
    snprintf(rows_[3].label, sizeof(rows_[3].label), "Serial");
    {
        // SetSysSerialNumber.number is a 0x19-byte null-terminated string.
        const char *serial = g_GlobalSettings.serial_no.number;
        const size_t slen  = strnlen(serial, sizeof(g_GlobalSettings.serial_no.number));
        if (slen == 0) {
            snprintf(rows_[3].value, sizeof(rows_[3].value), "n/a");
        } else {
            // Keep last 4 chars; mask everything before them with '*'.
            char masked[sizeof(g_GlobalSettings.serial_no.number) + 1] = {};
            const size_t keep_start = (slen > 4) ? (slen - 4) : 0;
            for (size_t i = 0; i < keep_start; ++i) {
                masked[i] = '*';
            }
            for (size_t i = keep_start; i < slen; ++i) {
                masked[i] = serial[i];
            }
            snprintf(rows_[3].value, sizeof(rows_[3].value), "%s", masked);
        }
    }

    // ── Row 4: Product model ───────────────────────────────────────────────
    snprintf(rows_[4].label, sizeof(rows_[4].label), "Model");
    {
        SetSysProductModel model = SetSysProductModel_Invalid;
        const Result rc = setsysGetProductModel(&model);
        if (R_FAILED(rc)) {
            snprintf(rows_[4].value, sizeof(rows_[4].value), "n/a");
        } else {
            snprintf(rows_[4].value, sizeof(rows_[4].value), "%s",
                ProductModelString(static_cast<s32>(model)));
        }
    }

    // ── Row 5: Operation mode (handheld / docked) ──────────────────────────
    snprintf(rows_[5].label, sizeof(rows_[5].label), "Operation mode");
    {
        const AppletOperationMode op_mode = appletGetOperationMode();
        snprintf(rows_[5].value, sizeof(rows_[5].value), "%s",
            (op_mode == AppletOperationMode_Console) ? "Docked" : "Handheld");
    }

    // ── Row 6: Battery level ───────────────────────────────────────────────
    snprintf(rows_[6].label, sizeof(rows_[6].label), "Battery");
    {
        u32 pct = 0;
        const Result rc = psmGetBatteryChargePercentage(&pct);
        if (R_FAILED(rc)) {
            snprintf(rows_[6].value, sizeof(rows_[6].value), "n/a");
        } else {
            snprintf(rows_[6].value, sizeof(rows_[6].value), "%u%%", pct);
        }
    }

    // ── Row 7: Charger type ────────────────────────────────────────────────
    snprintf(rows_[7].label, sizeof(rows_[7].label), "Charger");
    {
        PsmChargerType charger = PsmChargerType_Unconnected;
        const Result rc = psmGetChargerType(&charger);
        if (R_FAILED(rc)) {
            snprintf(rows_[7].value, sizeof(rows_[7].value), "n/a");
        } else {
            switch (charger) {
                case PsmChargerType_Unconnected:
                    snprintf(rows_[7].value, sizeof(rows_[7].value), "Not connected");
                    break;
                case PsmChargerType_EnoughPower:
                    snprintf(rows_[7].value, sizeof(rows_[7].value), "Regular charger");
                    break;
                case PsmChargerType_LowPower:
                    snprintf(rows_[7].value, sizeof(rows_[7].value), "Low-power charger");
                    break;
                default:
                    snprintf(rows_[7].value, sizeof(rows_[7].value), "Charger (type %d)",
                        static_cast<int>(charger));
                    break;
            }
        }
    }

    // ── Row 8: Console nickname ────────────────────────────────────────────
    snprintf(rows_[8].label, sizeof(rows_[8].label), "Console name");
    {
        // SetSysDeviceNickName.nickname is a 0x80-byte UTF-16LE buffer.
        // Convert naively: copy ASCII-range bytes, skip high byte of each u16.
        char nick[64] = {};
        const u16 *u16src = reinterpret_cast<const u16 *>(
            g_GlobalSettings.nickname.nickname);
        size_t out = 0;
        for (size_t i = 0; i < 32 && out < 63; ++i) {
            const u16 ch = u16src[i];
            if (ch == 0) break;
            nick[out++] = (ch < 128) ? static_cast<char>(ch) : '?';
        }
        if (out == 0) {
            snprintf(rows_[8].value, sizeof(rows_[8].value), "n/a");
        } else {
            snprintf(rows_[8].value, sizeof(rows_[8].value), "%s", nick);
        }
    }

    // ── Row 9: Active user account name ───────────────────────────────────
    snprintf(rows_[9].label, sizeof(rows_[9].label), "Active user");
    {
        AccountUid uid = g_GlobalSettings.system_status.selected_user;
        // accountUidIsValid returns false if uid is all-zero.
        if (!accountUidIsValid(&uid)) {
            snprintf(rows_[9].value, sizeof(rows_[9].value), "No user selected");
        } else {
            std::string name_str;
            if (R_FAILED(acc::GetAccountName(uid, name_str))) {
                snprintf(rows_[9].value, sizeof(rows_[9].value), "?");
            } else {
                snprintf(rows_[9].value, sizeof(rows_[9].value), "%s",
                         name_str.c_str());
            }
        }
    }

    // ── Row 10: Region code ────────────────────────────────────────────────
    snprintf(rows_[10].label, sizeof(rows_[10].label), "Region");
    {
        // SetRegion is an enum: JPN=0, USA=1, EUR=2, AUS=3, HTK=4, CHN=5.
        const char *region_names[] = { "Japan", "Americas", "Europe",
                                        "Australia", "Korea/HK/Taiwan", "China" };
        const int reg = static_cast<int>(g_GlobalSettings.region);
        if (reg >= 0 && reg < 6) {
            snprintf(rows_[10].value, sizeof(rows_[10].value), "%s", region_names[reg]);
        } else {
            snprintf(rows_[10].value, sizeof(rows_[10].value), "Region %d", reg);
        }
    }

    // ── Row 11: emuMMC status ──────────────────────────────────────────────
    snprintf(rows_[11].label, sizeof(rows_[11].label), "emuMMC");
    snprintf(rows_[11].value, sizeof(rows_[11].value), "%s",
        g_GlobalSettings.ams_is_emummc ? "Active" : "Not active (sysMMC)");

    // ── Row 12: NFC enabled ────────────────────────────────────────────────
    snprintf(rows_[12].label, sizeof(rows_[12].label), "NFC");
    snprintf(rows_[12].value, sizeof(rows_[12].value), "%s",
        g_GlobalSettings.nfc_enabled ? "Enabled" : "Disabled");

    // ── Row 13: USB 3.0 ───────────────────────────────────────────────────
    snprintf(rows_[13].label, sizeof(rows_[13].label), "USB 3.0");
    snprintf(rows_[13].value, sizeof(rows_[13].value), "%s",
        g_GlobalSettings.usb30_enabled ? "Enabled" : "Disabled");

    // ── Rasterise row textures ─────────────────────────────────────────────
    const pu::ui::Color label_clr = theme_.text_secondary;
    const pu::ui::Color value_clr = theme_.text_primary;
    const auto small_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

    for (size_t i = 0; i < ABOUT_ROW_COUNT; ++i) {
        if (rows_[i].label[0] != '\0') {
            rows_[i].label_tex = pu::ui::render::RenderText(
                small_font, std::string(rows_[i].label), label_clr);
        }
        if (rows_[i].value[0] != '\0') {
            rows_[i].value_tex = pu::ui::render::RenderText(
                small_font, std::string(rows_[i].value), value_clr);
        }
    }

    // ── Rasterise section headings ─────────────────────────────────────────
    const pu::ui::Color accent_clr  = theme_.accent;
    const auto medium_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

    const char *section_labels[SECTION_COUNT] = {
        "Q OS Build", "Hardware", "System", "Power"
    };
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        section_tex_[i] = pu::ui::render::RenderText(
            medium_font, section_labels[i], accent_clr);
    }

    // ── Rasterise logo label ───────────────────────────────────────────────
    const auto large_font = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
    logo_tex_ = pu::ui::render::RenderText(
        large_font, std::string("Q OS"), theme_.text_primary);

    // ── Rasterise footer hint ──────────────────────────────────────────────
    footer_tex_ = pu::ui::render::RenderText(
        small_font,
        std::string("\xe2\x8a\x95 Back     \xe2\x96\xb3 Refresh"),
        theme_.text_secondary);

    refreshed_ = true;
    UL_LOG_INFO("about: Refresh() done — %zu rows populated", ABOUT_ROW_COUNT);
}

// ── Static blit helper ────────────────────────────────────────────────────────

void QdAboutElement::BlitTex(SDL_Renderer *r, SDL_Texture *tex,
                              const s32 x, const s32 y) {
    if (tex == nullptr) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── Card + logo rendering ─────────────────────────────────────────────────────

void QdAboutElement::RenderCard(SDL_Renderer *r) const {
    // Frosted-glass card background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r,
        theme_.surface_glass.g,
        theme_.surface_glass.b,
        210);
    SDL_Rect card = { ABOUT_CARD_X, ABOUT_CARD_Y, ABOUT_CARD_W, ABOUT_CARD_H };
    SDL_RenderFillRect(r, &card);

    // Thin accent border around the card.
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 180);
    SDL_RenderDrawRect(r, &card);

    // Procedural logo panel: solid surface_glass + border.
    SDL_SetRenderDrawColor(r,
        theme_.desktop_bg.r,
        theme_.desktop_bg.g,
        theme_.desktop_bg.b,
        240);
    SDL_Rect logo_bg = { ABOUT_LOGO_X, ABOUT_LOGO_Y, ABOUT_LOGO_SIZE, ABOUT_LOGO_SIZE };
    SDL_RenderFillRect(r, &logo_bg);

    // Accent rounded-look: draw four lines one pixel inside the logo rect.
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 220);
    SDL_RenderDrawRect(r, &logo_bg);
    // Inner glow band (2 px inset).
    SDL_Rect logo_inner = {
        ABOUT_LOGO_X + 3, ABOUT_LOGO_Y + 3,
        ABOUT_LOGO_SIZE - 6, ABOUT_LOGO_SIZE - 6
    };
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 80);
    SDL_RenderDrawRect(r, &logo_inner);

    // Blit "Q OS" text centred in the logo panel.
    if (logo_tex_) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(logo_tex_, nullptr, nullptr, &lw, &lh);
        const s32 lx = ABOUT_LOGO_X + (ABOUT_LOGO_SIZE - lw) / 2;
        const s32 ly = ABOUT_LOGO_Y + (ABOUT_LOGO_SIZE - lh) / 2;
        SDL_Rect ldst = { lx, ly, lw, lh };
        SDL_RenderCopy(r, logo_tex_, nullptr, &ldst);
    }
}

// ── Section divider rendering ─────────────────────────────────────────────────

void QdAboutElement::RenderSection(SDL_Renderer *r,
                                   const size_t sec_idx,
                                   const s32 y) const {
    if (sec_idx >= SECTION_COUNT) return;

    // Horizontal rule.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 120);
    SDL_RenderDrawLine(r,
        ABOUT_INFO_X, y + 14,
        ABOUT_CARD_X + ABOUT_CARD_W - 32, y + 14);

    // Section label.
    BlitTex(r, section_tex_[sec_idx], ABOUT_INFO_X, y);
}

// ── Row rendering ─────────────────────────────────────────────────────────────

void QdAboutElement::RenderRows(SDL_Renderer *r) {
    // Layout:
    //   Rows  0-1   → section 0 "Q OS Build"
    //   Rows  2-5   → section 1 "Hardware"
    //   Rows  6-11  → section 2 "System"
    //   Rows 12-13  → section 3 "Power"
    //
    // Section headings each take ABOUT_ROW_H + 8 px gap before their first row.
    // Value column starts 300 px to the right of the label column.

    static constexpr s32 VALUE_COL_OFFSET = 300;
    static constexpr s32 SECTION_EXTRA_H  = ABOUT_ROW_H + 8;

    // Section-start row indices.
    static constexpr size_t SEC_ROW[SECTION_COUNT] = { 0, 2, 6, 12 };

    s32 cur_y = ABOUT_INFO_Y;
    size_t next_sec = 0; // index into SEC_ROW / section_tex_

    for (size_t i = 0; i < ABOUT_ROW_COUNT; ++i) {
        // Emit section heading before the row it starts on.
        if (next_sec < SECTION_COUNT && i == SEC_ROW[next_sec]) {
            RenderSection(r, next_sec, cur_y);
            cur_y += SECTION_EXTRA_H;
            ++next_sec;
        }

        BlitTex(r, rows_[i].label_tex, ABOUT_INFO_X, cur_y);
        BlitTex(r, rows_[i].value_tex, ABOUT_INFO_X + VALUE_COL_OFFSET, cur_y);
        cur_y += ABOUT_ROW_H;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdAboutElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                              const s32 /*x*/, const s32 /*y*/) {
    if (!refreshed_) {
        Refresh();
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) return;

    // Full-screen dark scrim.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.desktop_bg.r,
        theme_.desktop_bg.g,
        theme_.desktop_bg.b,
        240);
    SDL_Rect screen = { 0, 0, SCREEN_W, SCREEN_H };
    SDL_RenderFillRect(r, &screen);

    // Card + logo.
    RenderCard(r);

    // Info rows.
    RenderRows(r);

    // Footer hint at bottom of card.
    if (footer_tex_) {
        int fw = 0, fh = 0;
        SDL_QueryTexture(footer_tex_, nullptr, nullptr, &fw, &fh);
        const s32 fx = ABOUT_CARD_X + (ABOUT_CARD_W - fw) / 2;
        const s32 fy = ABOUT_CARD_Y + ABOUT_CARD_H - fh - 18;
        BlitTex(r, footer_tex_, fx, fy);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdAboutElement::OnInput(const u64 keys_down, const u64 /*keys_up*/,
                             const u64 /*keys_held*/,
                             const pu::ui::TouchPoint /*touch_pos*/) {
    if (keys_down & HidNpadButton_B) {
        UL_LOG_INFO("about: B pressed — returning to main menu");
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
        return;
    }

    if (keys_down & HidNpadButton_Y) {
        UL_LOG_INFO("about: Y pressed — refreshing system info");
        Refresh();
    }
}

// ── QdAboutLayout ctor / dtor ─────────────────────────────────────────────────

QdAboutLayout::QdAboutLayout(const QdTheme &theme) {
    UL_LOG_INFO("about: QdAboutLayout ctor");
    this->SetBackgroundColor({ 0, 0, 0, 255 });
    about_element_ = QdAboutElement::New(theme);
    this->Add(about_element_);
}

QdAboutLayout::~QdAboutLayout() {
    UL_LOG_INFO("about: QdAboutLayout dtor");
    // about_element_ shared_ptr is released here; its dtor destroys textures.
}

// ── IMenuLayout obligations ───────────────────────────────────────────────────

void QdAboutLayout::OnMenuInput(const u64 keys_down,
                                const u64 keys_up,
                                const u64 keys_held,
                                const pu::ui::TouchPoint touch_pos) {
    // QdAboutElement handles its own input via its OnInput override.  The base
    // pu::ui::Layout::OnInput dispatch already routes here through the child
    // element list, so OnMenuInput stays empty for the host.
    (void)keys_down;
    (void)keys_up;
    (void)keys_held;
    (void)touch_pos;
}

bool QdAboutLayout::OnHomeButtonPress() {
    UL_LOG_INFO("about: OnHomeButtonPress -> returning to MainMenu");
    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
    return true;
}

void QdAboutLayout::LoadSfx() {
    // About panel has no per-layout sfx today.  When sfx are added, load them here.
}

void QdAboutLayout::DisposeSfx() {
    // Mirrors LoadSfx; kept symmetric for future sfx work.
}

// ── QdAboutLayout::Refresh ────────────────────────────────────────────────────

void QdAboutLayout::Refresh() {
    if (about_element_) {
        about_element_->Refresh();
    }
}

} // namespace ul::menu::qdesktop
