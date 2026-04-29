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
    logo_tex_      = nullptr;
    footer_tex_    = nullptr;
    hint_bar_tex_  = nullptr;
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
            pu::ui::render::DeleteTexture(rows_[i].label_tex);
        }
        if (rows_[i].value_tex) {
            pu::ui::render::DeleteTexture(rows_[i].value_tex);
        }
    }
}

void QdAboutElement::FreeStaticTextures() {
    for (size_t i = 0; i < SECTION_COUNT; ++i) {
        if (section_tex_[i]) {
            pu::ui::render::DeleteTexture(section_tex_[i]);
        }
    }
    if (logo_tex_) {
        pu::ui::render::DeleteTexture(logo_tex_);
    }
    if (footer_tex_) {
        pu::ui::render::DeleteTexture(footer_tex_);
    }
    if (hint_bar_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_tex_);
        hint_bar_tex_ = nullptr;
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
    {
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
        // SetSysDeviceNickName.nickname is a 0x80-byte (64 u16) UTF-16LE buffer.
        // Decode to UTF-8.  The output buffer (rows_[8].value = char[128]) is
        // large enough: 64 UTF-16 code units produce at most 192 UTF-8 bytes in
        // the worst case, but the nickname field is documented as max 32
        // characters (i.e. ≤ 32 code units before the null), so worst-case
        // UTF-8 output is 32 × 4 = 128 bytes including the null terminator.
        // Use a local 128-byte buffer and write at most 127 bytes + '\0'.
        char nick[128] = {};
        const u16 *src = reinterpret_cast<const u16 *>(
            g_GlobalSettings.nickname.nickname);
        size_t out = 0;
        // Iterate over at most 32 code units (the documented field width).
        for (size_t i = 0; i < 32; ++i) {
            const u16 w0 = src[i];
            if (w0 == 0) break;

            u32 cp;
            if (w0 >= 0xD800u && w0 <= 0xDBFFu) {
                // High surrogate — consume the paired low surrogate.
                const u16 w1 = src[i + 1];
                if (w1 >= 0xDC00u && w1 <= 0xDFFFu) {
                    cp = 0x10000u
                         + (static_cast<u32>(w0 - 0xD800u) << 10)
                         + static_cast<u32>(w1 - 0xDC00u);
                    ++i; // skip the low surrogate on the next iteration
                } else {
                    // Unpaired high surrogate — emit U+FFFD replacement.
                    cp = 0xFFFDu;
                }
            } else if (w0 >= 0xDC00u && w0 <= 0xDFFFu) {
                // Lone low surrogate — emit U+FFFD replacement.
                cp = 0xFFFDu;
            } else {
                cp = static_cast<u32>(w0);
            }

            // Encode cp as UTF-8; stop if the remaining buffer is too small.
            if (cp < 0x80u) {
                if (out + 1 > 127) break;
                nick[out++] = static_cast<char>(cp);
            } else if (cp < 0x800u) {
                if (out + 2 > 127) break;
                nick[out++] = static_cast<char>(0xC0u | (cp >> 6));
                nick[out++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            } else if (cp < 0x10000u) {
                if (out + 3 > 127) break;
                nick[out++] = static_cast<char>(0xE0u | (cp >> 12));
                nick[out++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                nick[out++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            } else {
                if (out + 4 > 127) break;
                nick[out++] = static_cast<char>(0xF0u | (cp >> 18));
                nick[out++] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
                nick[out++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                nick[out++] = static_cast<char>(0x80u | (cp & 0x3Fu));
            }
        }
        // nick[] is zero-initialised; out bytes written, nick[out] == '\0'.
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

    // ── Rasterise bottom hint bar ──────────────────────────────────────────
    {
        const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
        hint_bar_tex_ = pu::ui::render::RenderText(
            small_font,
            std::string("B / + Close"),
            hint_col);
    }

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

// ── Runtime geometry ──────────────────────────────────────────────────────────

QdAboutElement::AboutGeo QdAboutElement::ComputeGeo() const {
    AboutGeo g;
    // Card is at most 1400×800 but scales down to fit within content area
    // with a 48-px margin on each side.
    const s32 max_card_w = 1400;
    const s32 max_card_h = 800;
    const s32 avail_w = content_w_ - 96;   // 48 px margin each side
    const s32 avail_h = content_h_ - 96;
    g.card_w = (avail_w < max_card_w) ? avail_w : max_card_w;
    g.card_h = (avail_h < max_card_h) ? avail_h : max_card_h;
    g.card_x = (content_w_ - g.card_w) / 2;
    g.card_y = static_cast<s32>(TOPBAR_H) + (content_h_ - static_cast<s32>(TOPBAR_H) - static_cast<s32>(DOCK_H) - g.card_h) / 2;
    // Logo panel: at most 200×200, capped at 1/3 of card width.
    const s32 max_logo = 200;
    const s32 logo_cap = g.card_w / 3;
    g.logo_size = (logo_cap < max_logo) ? logo_cap : max_logo;
    g.logo_x = g.card_x + 48;
    g.logo_y = g.card_y + 48;
    // Info column starts to the right of the logo panel.
    g.info_x = g.logo_x + g.logo_size + 48;
    g.info_y = g.card_y + 48;
    g.row_h  = 38;
    g.rule_right = g.card_x + g.card_w - 32;
    return g;
}

// ── Card + logo rendering ─────────────────────────────────────────────────────

void QdAboutElement::RenderCard(SDL_Renderer *r, const AboutGeo &geo) const {
    // Frosted-glass card background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r,
        theme_.surface_glass.g,
        theme_.surface_glass.b,
        210);
    SDL_Rect card = { geo.card_x, geo.card_y, geo.card_w, geo.card_h };
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
    SDL_Rect logo_bg = { geo.logo_x, geo.logo_y, geo.logo_size, geo.logo_size };
    SDL_RenderFillRect(r, &logo_bg);

    // Accent rounded-look: draw four lines one pixel inside the logo rect.
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 220);
    SDL_RenderDrawRect(r, &logo_bg);
    // Inner glow band (2 px inset).
    SDL_Rect logo_inner = {
        geo.logo_x + 3, geo.logo_y + 3,
        geo.logo_size - 6, geo.logo_size - 6
    };
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 80);
    SDL_RenderDrawRect(r, &logo_inner);

    // Blit "Q OS" text centred in the logo panel.
    if (logo_tex_) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(logo_tex_, nullptr, nullptr, &lw, &lh);
        const s32 lx = geo.logo_x + (geo.logo_size - lw) / 2;
        const s32 ly = geo.logo_y + (geo.logo_size - lh) / 2;
        SDL_Rect ldst = { lx, ly, lw, lh };
        SDL_RenderCopy(r, logo_tex_, nullptr, &ldst);
    }
}

// ── Section divider rendering ─────────────────────────────────────────────────

void QdAboutElement::RenderSection(SDL_Renderer *r,
                                   const size_t sec_idx,
                                   const s32 y,
                                   const AboutGeo &geo) const {
    if (sec_idx >= SECTION_COUNT) return;

    // Horizontal rule.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.accent.r, theme_.accent.g, theme_.accent.b, 120);
    SDL_RenderDrawLine(r,
        geo.info_x, y + 14,
        geo.rule_right, y + 14);

    // Section label.
    BlitTex(r, section_tex_[sec_idx], geo.info_x, y);
}

// ── Row rendering ─────────────────────────────────────────────────────────────

void QdAboutElement::RenderRows(SDL_Renderer *r, const AboutGeo &geo) {
    // Layout:
    //   Rows  0-1   → section 0 "Q OS Build"
    //   Rows  2-5   → section 1 "Hardware"
    //   Rows  6-11  → section 2 "System"
    //   Rows 12-13  → section 3 "Power"
    //
    // Section headings each take row_h + 8 px gap before their first row.
    // Value column starts 300 px to the right of the label column.

    // In windowed mode, scale the value-column offset proportionally so it
    // stays within the info column width (card_w - logo - 3×48 margin).
    const s32 info_avail_w = geo.card_x + geo.card_w - geo.info_x - 32;
    const s32 value_col = (info_avail_w < 300) ? (info_avail_w / 2) : 300;
    const s32 section_extra_h = geo.row_h + 8;

    // Section-start row indices.
    static constexpr size_t SEC_ROW[SECTION_COUNT] = { 0, 2, 6, 12 };

    s32 cur_y = geo.info_y;
    size_t next_sec = 0; // index into SEC_ROW / section_tex_

    for (size_t i = 0; i < ABOUT_ROW_COUNT; ++i) {
        // Emit section heading before the row it starts on.
        if (next_sec < SECTION_COUNT && i == SEC_ROW[next_sec]) {
            RenderSection(r, next_sec, cur_y, geo);
            cur_y += section_extra_h;
            ++next_sec;
        }

        BlitTex(r, rows_[i].label_tex, geo.info_x, cur_y);
        BlitTex(r, rows_[i].value_tex, geo.info_x + value_col, cur_y);
        cur_y += geo.row_h;
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdAboutElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                              const s32 x, const s32 y) {
    if (!refreshed_) {
        Refresh();
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) return;

    // Compute runtime geometry from current content dimensions.
    const AboutGeo geo = ComputeGeo();

    // Full-content dark scrim (uses content_w_/content_h_ not SCREEN constants).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.desktop_bg.r,
        theme_.desktop_bg.g,
        theme_.desktop_bg.b,
        240);
    SDL_Rect screen = { x, y, content_w_, content_h_ };
    SDL_RenderFillRect(r, &screen);

    // Card + logo.
    RenderCard(r, geo);

    // Info rows.
    RenderRows(r, geo);

    // Footer hint at bottom of card.
    if (footer_tex_) {
        int fw = 0, fh = 0;
        SDL_QueryTexture(footer_tex_, nullptr, nullptr, &fw, &fh);
        const s32 fx = geo.card_x + (geo.card_w - fw) / 2;
        const s32 fy = geo.card_y + geo.card_h - fh - 18;
        BlitTex(r, footer_tex_, fx, fy);
    }

    // Bottom hint bar (content-relative, below the card).
    if (hint_bar_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_bar_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = x + (content_w_ - hw) / 2;
        const s32 hy = y + content_h_ - 8 - hh;
        BlitTex(r, hint_bar_tex_, hx, hy);
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
    // v1.9.7: hot-corner overlay painted above the about panel.
    overlay_ = QdHotCornerOverlay::New();
    this->Add(overlay_);
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
