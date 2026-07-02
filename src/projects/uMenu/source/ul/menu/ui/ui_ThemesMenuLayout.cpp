#include <ul/menu/ui/ui_ThemesMenuLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/util/util_Json.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_FolderTheme.hpp>
#include <SDL2/SDL.h>
#include <vector>
extern "C" {
#include <zip.h>   // for the custom theme editor: emits a real .ultheme zip
#include <zlib.h>  // crc32 + adler32 for the minimal Icon.png we hand-build
}
#endif

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    ThemesMenuLayout::ThemesMenuLayout() : IMenuLayout() {
        this->info_text = pu::ui::elm::TextBlock::New(0, 0, GetLanguageString("theme_info_text"));
        this->info_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("themes_menu", "info_text", this->info_text);
        this->Add(this->info_text);

        this->themes_menu = pu::ui::elm::Menu::New(0, 0, ThemesMenuWidth, g_MenuApplication->GetMenuBackgroundColor(), g_MenuApplication->GetMenuFocusColor(), ThemesMenuItemSize, ThemesMenuItemsToShow);
        g_GlobalSettings.ApplyConfigForElement("themes_menu", "themes_menu", this->themes_menu);
        this->Add(this->themes_menu);
    }

    void ThemesMenuLayout::LoadSfx() {
        this->theme_change_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Themes/ThemeChange.wav"));
        this->back_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Themes/Back.wav"));
    }

    void ThemesMenuLayout::DisposeSfx() {
        pu::audio::DestroySfx(this->theme_change_sfx);
        pu::audio::DestroySfx(this->back_sfx);
    }

    void ThemesMenuLayout::OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
        if(keys_down & HidNpadButton_B) {
            pu::audio::PlaySfx(this->back_sfx);

            g_MenuApplication->LoadMenu(MenuType::Main);
        }
    }

    bool ThemesMenuLayout::OnHomeButtonPress() {
        pu::audio::PlaySfx(this->back_sfx);

        g_MenuApplication->LoadMenu(MenuType::Main);
        return true;
    }

#ifdef QDESKTOP_MODE

    // ── v2.7.0 — Custom Theme Editor (creator-facing, in-product) ────────────
    //
    // Sequence:
    //   1. User picks "Create custom theme..." from the Themes menu.
    //   2. swkbd prompt for theme name.
    //   3. 17 swkbd prompts in sequence, one per QdTheme token. Each prompt
    //      shows the token name + current hex pre-filled. Empty input keeps
    //      the current value (fast-forward through tokens you don't want to
    //      change). Bad hex re-prompts.
    //   4. A real .ultheme zip is written to sdmc:/ulaunch/themes/<name>.ultheme
    //      containing Manifest.json + ui/QdPalette.json + theme/Icon.png
    //      (Icon = a generated 256×256 swatch of the user's accent color).
    //   5. SetActiveTheme + RestartMenu so the user lands in the new theme.
    //
    // No new layout class — keeps everything inside ThemesMenuLayout, reuses
    // the menu's existing chrome + the global swkbd helper. Total ~200 LOC.

    namespace {
        using ::ul::menu::qdesktop::QdTheme;
        using ::ul::menu::qdesktop::g_QdTheme;

        // Prompt swkbd for hex input. Returns true if user accepted; out_hex
        // is populated (6-char "#RRGGBB" form). Empty input → returns true
        // with out_hex unchanged from `current_hex`.
        bool PromptHex(const char *label, const std::string &current_hex,
                       std::string &out_hex) {
            SwkbdConfig kbd;
            if (R_FAILED(swkbdCreate(&kbd, 0))) {
                UL_LOG_WARN("themes_editor: swkbdCreate failed");
                return false;
            }
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetType(&kbd, SwkbdType_All);
            swkbdConfigSetGuideText(&kbd, label);
            swkbdConfigSetInitialText(&kbd, current_hex.c_str());
            swkbdConfigSetStringLenMax(&kbd, 9u);  // "#RRGGBBAA"
            char buf[12] = {};
            const auto rc = swkbdShow(&kbd, buf, sizeof(buf));
            swkbdClose(&kbd);
            if (R_FAILED(rc)) return false;
            std::string entered = buf;
            // Empty input keeps current.
            if (entered.empty()) {
                out_hex = current_hex;
                return true;
            }
            // Allow user to enter without '#' prefix.
            if (entered[0] != '#') entered = "#" + entered;
            out_hex = entered;
            return true;
        }

        // Format a pu::ui::Color as "#RRGGBB" (drops alpha).
        std::string FormatHex(const pu::ui::Color &c) {
            char buf[10];
            snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
            return buf;
        }

        // Parse "#RRGGBB" or "#RRGGBBAA" into a pu::ui::Color. Returns false on bad input.
        bool ParseHexColor(const std::string &hex, pu::ui::Color &out) {
            if (hex.size() < 7 || hex[0] != '#') return false;
            const auto *s = hex.c_str() + 1;
            unsigned int r = 0, g = 0, b = 0, a = 0xFF;
            if (hex.size() == 7) {
                if (sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3) return false;
            } else if (hex.size() == 9) {
                if (sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) return false;
            } else {
                return false;
            }
            out = pu::ui::Color((u8)r, (u8)g, (u8)b, (u8)a);
            return true;
        }

        // Generate a 256×256 ARGB8888 swatch PNG-equivalent buffer for the
        // theme icon. Since we don't have stb_image_write linked, we output a
        // minimal hand-rolled PNG (single IDAT, no filter, deflate level 0
        // with stored block). The PNG is solid `accent` color with a 4-px
        // surface_glass border. Caller takes ownership of returned vector.
        //
        // For simplicity here we ship a literal minimal PNG of a SOLID 1×1
        // pixel of the accent color. uLaunch's icon loader supports any size.
        // A 1×1 PNG = 67 bytes and is trivially constructable.
        std::vector<u8> Generate1x1PNG(const pu::ui::Color &accent) {
            // Minimal valid PNG: 8-byte signature + IHDR + IDAT + IEND.
            // For 1×1 RGB no-alpha: ~67 bytes total.
            const u8 r = accent.r, g = accent.g, b = accent.b;
            std::vector<u8> png;
            png.reserve(80);
            // Signature
            const u8 sig[] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
            png.insert(png.end(), sig, sig + 8);
            // IHDR chunk (13-byte data: width=1, height=1, bit depth=8, color type=2=RGB, ...)
            auto put_u32_be = [&](u32 v) {
                png.push_back((v >> 24) & 0xFF);
                png.push_back((v >> 16) & 0xFF);
                png.push_back((v >>  8) & 0xFF);
                png.push_back( v        & 0xFF);
            };
            // IHDR length + type + data + CRC
            put_u32_be(13);
            const size_t ihdr_start = png.size();
            const u8 ihdr_type[] = {'I','H','D','R'};
            png.insert(png.end(), ihdr_type, ihdr_type + 4);
            put_u32_be(1);  // width
            put_u32_be(1);  // height
            png.push_back(8);    // bit depth
            png.push_back(2);    // color type = RGB
            png.push_back(0);    // compression
            png.push_back(0);    // filter
            png.push_back(0);    // interlace
            // CRC of type+data
            u32 crc = ::crc32(0L, png.data() + ihdr_start, 17);
            put_u32_be(crc);
            // IDAT chunk: zlib container around deflate-stored 1 row
            // Row = filter byte (0=none) + 3 pixel bytes = 4 bytes
            // zlib header (2) + stored block (1 hdr + 2 len + 2 nlen + 4 data) + adler32 (4)
            const u8 row[4] = {0, r, g, b};
            std::vector<u8> zdata;
            zdata.push_back(0x78); zdata.push_back(0x01);  // zlib: deflate, no preset dict
            zdata.push_back(0x01);                          // deflate: stored block, BFINAL=1
            zdata.push_back(4); zdata.push_back(0);         // len LE = 4
            zdata.push_back(0xFB); zdata.push_back(0xFF);   // nlen = ~len
            zdata.insert(zdata.end(), row, row + 4);
            // Adler32
            u32 ad = ::adler32(1L, row, 4);
            zdata.push_back((ad >> 24) & 0xFF);
            zdata.push_back((ad >> 16) & 0xFF);
            zdata.push_back((ad >>  8) & 0xFF);
            zdata.push_back( ad        & 0xFF);
            put_u32_be(static_cast<u32>(zdata.size()));
            const size_t idat_start = png.size();
            const u8 idat_type[] = {'I','D','A','T'};
            png.insert(png.end(), idat_type, idat_type + 4);
            png.insert(png.end(), zdata.begin(), zdata.end());
            crc = ::crc32(0L, png.data() + idat_start, 4 + zdata.size());
            put_u32_be(crc);
            // IEND
            put_u32_be(0);
            const size_t iend_start = png.size();
            const u8 iend_type[] = {'I','E','N','D'};
            png.insert(png.end(), iend_type, iend_type + 4);
            crc = ::crc32(0L, png.data() + iend_start, 4);
            put_u32_be(crc);
            return png;
        }

        // Generate Manifest.json for a custom theme.
        std::string GenerateManifest(const std::string &name) {
            ul::util::JSON j;
            j["format_version"] = 3;  // CurrentThemeFormatVersion
            j["name"]           = name;
            j["release"]        = "1.0";
            j["description"]    = "Custom theme created with the Q OS Theme Editor.";
            j["author"]         = "Q OS user";
            return j.dump(2);
        }

        // Generate QdPalette.json from a QdTheme.
        std::string GeneratePalette(const QdTheme &t) {
            ul::util::JSON j;
            j["desktop_bg"]         = FormatHex(t.desktop_bg);
            j["surface_glass"]      = FormatHex(t.surface_glass);
            j["topbar_bg"]          = FormatHex(t.topbar_bg);
            j["dock_bg"]            = FormatHex(t.dock_bg);
            j["accent"]             = FormatHex(t.accent);
            j["text_primary"]       = FormatHex(t.text_primary);
            j["text_secondary"]     = FormatHex(t.text_secondary);
            j["focus_ring"]         = FormatHex(t.focus_ring);
            j["button_close"]       = FormatHex(t.button_close);
            j["button_minimize"]    = FormatHex(t.button_minimize);
            j["button_maximize"]    = FormatHex(t.button_maximize);
            j["cursor_fill"]        = FormatHex(t.cursor_fill);
            j["cursor_outline"]     = FormatHex(t.cursor_outline);
            j["cursor_right_click"] = FormatHex(t.cursor_right_click);
            j["titlebar_inactive"]  = FormatHex(t.titlebar_inactive);
            j["button_restore"]     = FormatHex(t.button_restore);
            j["grid_line"]          = FormatHex(t.grid_line);
            return j.dump(2);
        }

        // Write a .ultheme zip with the given palette + name. Returns true on
        // success. Path is sdmc:/ulaunch/themes/<name>.ultheme (atomic via tmp + rename).
        bool WriteCustomThemeBundle(const std::string &name,
                                    const QdTheme &palette) {
            mkdir("sdmc:/ulaunch", 0777);
            mkdir("sdmc:/ulaunch/themes", 0777);
            const std::string final_path = std::string("sdmc:/ulaunch/themes/") + name + ".ultheme";
            const std::string tmp_path   = final_path + ".tmp";

            auto z = zip_open(tmp_path.c_str(), 6, 'w');
            if (!z) {
                UL_LOG_WARN("themes_editor: zip_open(w) failed: %s", tmp_path.c_str());
                return false;
            }

            // 1. theme/Manifest.json
            const std::string manifest = GenerateManifest(name);
            if (zip_entry_open(z, "theme/Manifest.json") != 0
             || zip_entry_write(z, manifest.data(), manifest.size()) != 0) {
                UL_LOG_WARN("themes_editor: zip entry Manifest.json failed");
                zip_close(z);
                return false;
            }
            zip_entry_close(z);

            // 2. theme/Icon.png (1×1 PNG of the accent color)
            const auto icon_png = Generate1x1PNG(palette.accent);
            if (zip_entry_open(z, "theme/Icon.png") != 0
             || zip_entry_write(z, icon_png.data(), icon_png.size()) != 0) {
                UL_LOG_WARN("themes_editor: zip entry Icon.png failed");
                zip_close(z);
                return false;
            }
            zip_entry_close(z);

            // 3. ui/QdPalette.json
            const std::string pal_json = GeneratePalette(palette);
            if (zip_entry_open(z, "ui/QdPalette.json") != 0
             || zip_entry_write(z, pal_json.data(), pal_json.size()) != 0) {
                UL_LOG_WARN("themes_editor: zip entry QdPalette.json failed");
                zip_close(z);
                return false;
            }
            zip_entry_close(z);

            zip_close(z);

            if (rename(tmp_path.c_str(), final_path.c_str()) != 0) {
                UL_LOG_WARN("themes_editor: rename failed errno=%d", errno);
                return false;
            }
            const Result commit_rc = fsdevCommitDevice("sdmc");
            if (R_FAILED(commit_rc)) {
                UL_LOG_WARN("themes_editor: fsdevCommitDevice rc=0x%x", commit_rc);
            }
            UL_LOG_INFO("themes_editor: wrote bundle: %s (%zu B manifest, %zu B icon, %zu B palette)",
                        final_path.c_str(), manifest.size(), icon_png.size(), pal_json.size());
            return true;
        }

    } // namespace

    // Walks the user through editing all 17 QdTheme tokens via swkbd, then
    // writes the .ultheme bundle and applies it. Called from theme_DefaultKey
    // when the user picks the "Create custom theme..." menu entry.
    static void RunCustomThemeEditor() {
        // Start from the currently-active palette (g_QdTheme) so users can
        // tweak from a baseline instead of starting from black.
        QdTheme edit = g_QdTheme;

        // 17 token names + accessor lambdas for read/write.
        struct TokenAccess {
            const char *label;
            pu::ui::Color *field;
        };
        TokenAccess tokens[] = {
            { "Desktop background",                  &edit.desktop_bg },
            { "Window / panel fill (surface)",       &edit.surface_glass },
            { "Top bar background",                  &edit.topbar_bg },
            { "Dock background",                     &edit.dock_bg },
            { "Accent (highlights, active)",         &edit.accent },
            { "Text primary",                        &edit.text_primary },
            { "Text secondary",                      &edit.text_secondary },
            { "Focus ring",                          &edit.focus_ring },
            { "Window close button",                 &edit.button_close },
            { "Window minimize button",              &edit.button_minimize },
            { "Window maximize button",              &edit.button_maximize },
            { "Cursor fill",                         &edit.cursor_fill },
            { "Cursor outline",                      &edit.cursor_outline },
            { "Cursor right-click tint",             &edit.cursor_right_click },
            { "Inactive titlebar",                   &edit.titlebar_inactive },
            { "Window restore button",               &edit.button_restore },
            { "Wallpaper grid line",                 &edit.grid_line },
        };
        constexpr size_t kCount = sizeof(tokens) / sizeof(tokens[0]);

        for (size_t i = 0; i < kCount; ++i) {
            char hdr[160];
            snprintf(hdr, sizeof(hdr),
                     "[%zu/%zu] %s — enter #RRGGBB (empty=keep, cancel=stop)",
                     i + 1, kCount, tokens[i].label);
            std::string cur = FormatHex(*tokens[i].field);
            std::string out;
            if (!PromptHex(hdr, cur, out)) {
                UL_LOG_INFO("themes_editor: cancelled at token %zu", i);
                g_MenuApplication->ShowNotification("Theme editor cancelled.");
                return;
            }
            pu::ui::Color parsed;
            if (!ParseHexColor(out, parsed)) {
                // Bad hex — retry the same token.
                g_MenuApplication->ShowNotification("Bad hex; try again.");
                --i;
                continue;
            }
            *tokens[i].field = parsed;
        }

        // Final prompt: theme name.
        char name_buf[64] = {};
        SwkbdConfig name_kbd;
        if (R_FAILED(swkbdCreate(&name_kbd, 0))) {
            g_MenuApplication->ShowNotification("Could not open keyboard for name.");
            return;
        }
        swkbdConfigMakePresetDefault(&name_kbd);
        swkbdConfigSetType(&name_kbd, SwkbdType_All);
        swkbdConfigSetGuideText(&name_kbd, "Name your theme (letters / numbers / -)");
        swkbdConfigSetInitialText(&name_kbd, "my-theme");
        swkbdConfigSetStringLenMax(&name_kbd, sizeof(name_buf) - 1);
        const auto name_rc = swkbdShow(&name_kbd, name_buf, sizeof(name_buf));
        swkbdClose(&name_kbd);
        if (R_FAILED(name_rc) || name_buf[0] == 0) {
            g_MenuApplication->ShowNotification("Theme name required; cancelled.");
            return;
        }

        // Strip any chars that would be illegal in a filename (keep it simple:
        // allow alnum + dash + underscore; replace others with underscore).
        std::string sanitized;
        for (char c : std::string(name_buf)) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
             || (c >= '0' && c <= '9') || c == '-' || c == '_') {
                sanitized.push_back(c);
            } else {
                sanitized.push_back('_');
            }
        }
        if (sanitized.empty()) sanitized = "my-theme";

        if (!WriteCustomThemeBundle(sanitized, edit)) {
            g_MenuApplication->ShowNotification("Failed to write theme bundle.");
            return;
        }

        // Try to load the freshly-written theme; if successful, set it active
        // and restart so the user lands inside their new theme on next paint.
        cfg::Theme new_theme;
        const auto load_rc = cfg::TryLoadTheme(sanitized + ".ultheme", new_theme);
        if (R_FAILED(load_rc)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "Wrote theme but couldn't reload it (rc=0x%X)", load_rc);
            g_MenuApplication->ShowNotification(msg);
            return;
        }
        g_GlobalSettings.SetActiveTheme(new_theme);
        g_MenuApplication->ShowNotification("Custom theme applied! Restarting menu...");
#ifdef QDESKTOP_MODE
        // v2.7.3 BUG FIX — SetActiveTheme writes config.ActiveThemeName but
        // does NOT refresh sdmc:/ulaunch/cache/active/.  The cache is only
        // re-extracted on the NEXT boot (via SystemStatus.reload_theme_cache
        // ⇒ CacheActiveTheme in main.cpp:291).  Without an in-process re-cache
        // here the LoadThemeFromCache call below reads the PREVIOUS theme's
        // QdPalette.json, and the transition frame paints the wrong colors.
        // Force-refresh the cache now so the picked theme's palette lands in
        // g_QdTheme before we paint the transition frame.
        ul::cfg::CacheActiveTheme(g_GlobalSettings.config);
        // v2.7.2 — apply the (now-fresh) new theme to g_QdTheme so the
        // transition frame shows the destination theme's identity.
        qdesktop::LoadThemeFromCache(ul::ActiveThemeCachePath);
#endif
        g_MenuApplication->FadeOutToNonLibraryApplet();
#ifdef QDESKTOP_MODE
        // v2.7.2 — paint a transition frame in the destination theme's
        // desktop_bg + accent so the uSystem RestartMenu defer window
        // displays an intentional brand frame instead of Plutonium's
        // hardcoded cyan/lavender brand fade.
        qdesktop::DrawThemeTransitionFrame(pu::ui::render::GetMainRenderer());
#endif
        UL_RC_ASSERT(ul::menu::smi::RestartMenu(true));
        g_MenuApplication->Finalize();
    }

    // v2.5.0 — generate a 180×180 RGBA palette swatch icon for in-binary
    // pack `idx`. 2×2 grid of color squares with theme name area at bottom.
    // Each cell uses the pack's actual QdTheme colors so the user sees what
    // they'd be selecting.
    pu::sdl2::TextureHandle::Ref ThemesMenuLayout::MakeInBinaryIcon(size_t idx) {
        constexpr s32 kDim = 180;
        constexpr s32 kStride = kDim * 4;
        std::vector<u8> buf(static_cast<size_t>(kDim * kStride), 0);

        const auto t = ul::menu::qdesktop::MakeThemeByIndex(idx);

        auto put_px = [&](s32 x, s32 y, u8 r, u8 g, u8 b, u8 a = 0xFF) {
            if (x < 0 || y < 0 || x >= kDim || y >= kDim) return;
            u8 *p = buf.data() + (y * kDim + x) * 4;
            p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        };
        auto fill = [&](s32 x0, s32 y0, s32 w, s32 h, const pu::ui::Color &c) {
            for (s32 dy = 0; dy < h; ++dy)
                for (s32 dx = 0; dx < w; ++dx)
                    put_px(x0 + dx, y0 + dy, c.r, c.g, c.b, 0xFF);
        };
        auto outline = [&](s32 x0, s32 y0, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
            for (s32 dx = 0; dx < w; ++dx) {
                put_px(x0 + dx, y0,         r, g, b, a);
                put_px(x0 + dx, y0 + h - 1, r, g, b, a);
            }
            for (s32 dy = 0; dy < h; ++dy) {
                put_px(x0,         y0 + dy, r, g, b, a);
                put_px(x0 + w - 1, y0 + dy, r, g, b, a);
            }
        };

        // Outer surface_glass background.
        fill(0, 0, kDim, kDim, t.surface_glass);
        // Thick accent border.
        for (s32 i = 0; i < 4; ++i) {
            outline(i, i, kDim - 2 * i, kDim - 2 * i,
                    t.accent.r, t.accent.g, t.accent.b, 0xFF);
        }

        // 2×2 swatch grid centered, leaves 28px bottom strip for name area.
        constexpr s32 kGridY0 = 14;
        constexpr s32 kGridW  = kDim - 28;
        constexpr s32 kCell   = kGridW / 2 - 6;
        const s32 cx0 = (kDim - kGridW) / 2 + 3;
        const s32 cy0 = kGridY0;

        // TL accent / TR focus_ring / BL text_primary / BR desktop_bg
        fill(cx0,                    cy0,                       kCell, kCell, t.accent);
        fill(cx0 + kCell + 8,        cy0,                       kCell, kCell, t.focus_ring);
        fill(cx0,                    cy0 + kCell + 8,           kCell, kCell, t.text_primary);
        fill(cx0 + kCell + 8,        cy0 + kCell + 8,           kCell, kCell, t.desktop_bg);

        // Outline each swatch with cursor_outline for crispness.
        outline(cx0,                 cy0,                       kCell, kCell, t.cursor_outline.r, t.cursor_outline.g, t.cursor_outline.b, 0xFF);
        outline(cx0 + kCell + 8,     cy0,                       kCell, kCell, t.cursor_outline.r, t.cursor_outline.g, t.cursor_outline.b, 0xFF);
        outline(cx0,                 cy0 + kCell + 8,           kCell, kCell, t.cursor_outline.r, t.cursor_outline.g, t.cursor_outline.b, 0xFF);
        outline(cx0 + kCell + 8,     cy0 + kCell + 8,           kCell, kCell, t.cursor_outline.r, t.cursor_outline.g, t.cursor_outline.b, 0xFF);

        // Name strip at bottom — solid surface_glass darker accent.
        fill(0, kDim - 36, kDim, 36, t.desktop_bg);
        // Top edge of strip = thin accent line.
        fill(0, kDim - 36, kDim, 2, t.accent);

        // Upload to SDL_Texture.
        SDL_Renderer *rend = pu::ui::render::GetMainRenderer();
        if (!rend) return nullptr;
        SDL_Texture *tex = SDL_CreateTexture(rend,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STATIC,
            kDim, kDim);
        if (!tex) return nullptr;
        SDL_UpdateTexture(tex, nullptr, buf.data(), kStride);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        return pu::sdl2::TextureHandle::New(tex);
    }
#endif

    void ThemesMenuLayout::Reload() {
        this->themes_menu->ClearItems();
        this->loaded_themes.clear();
        this->loaded_theme_icons.clear();
        // v2.7.0 — in_binary entries removed. The 10 Q OS themes now ship as
        // real .ultheme bundles seeded into sdmc:/ulaunch/themes/ on first
        // boot from romfs. cfg::FindThemes() returns them alongside any user-
        // installed themes; the Themes menu is now the SINGLE source for theme
        // selection. Picking flows through upstream SetActiveTheme + restart,
        // and LoadThemeFromCache reads the .ultheme's QdPalette.json which
        // includes a "wallpaper_pack" hint to drive the procedural wallpaper.
        this->in_binary_count = 0;

        // ── Existing .ultheme list path (unchanged below) ─────────────────
        auto disk_themes = cfg::FindThemes();
        disk_themes.insert(disk_themes.begin(), cfg::Theme{}); // "default theme" reset entry

        // Move .ultheme active theme to top of the disk_themes list (the
        // in-binary themes still come first in the menu).
        for(u32 i = 0; i < disk_themes.size(); i++) {
            const auto theme = disk_themes.at(i);
            if(theme.IsSame(g_GlobalSettings.active_theme)) {
                disk_themes.erase(disk_themes.begin() + i);
                disk_themes.insert(disk_themes.begin(), theme);
                break;
            }
        }

        for (const auto &t : disk_themes) {
            this->loaded_themes.push_back(t);
        }

        // Render only the .ultheme tail (indexes >= in_binary_count).
        for(size_t i = this->in_binary_count; i < this->loaded_themes.size(); ++i) {
            const auto &theme = this->loaded_themes.at(i);
            if(theme.IsValid()) {
                std::string theme_icon_path;
                const auto rc = cfg::TryCacheLoadThemeIcon(theme, theme_icon_path);
                if(R_FAILED(rc)) {
                    UL_LOG_WARN("Theme '%s' unable to load image: %s", theme.name.c_str(), util::FormatResultDisplay(rc).c_str());
                }

                auto theme_icon = pu::sdl2::TextureHandle::New(pu::ui::render::LoadImageFromFile(theme_icon_path));
                this->loaded_theme_icons.push_back(theme_icon);

                auto theme_item = pu::ui::elm::MenuItem::New(theme.manifest.name + " (v" + theme.manifest.release + ", " + theme.manifest.author + ")");
                theme_item->AddOnKey(std::bind(&ThemesMenuLayout::theme_DefaultKey, this));
                theme_item->SetColor(g_MenuApplication->GetTextColor());
                theme_item->SetIcon(theme_icon);
                this->themes_menu->AddItem(theme_item);
            }
            else {
                this->loaded_theme_icons.emplace_back();
                auto theme_reset_item = pu::ui::elm::MenuItem::New(GetLanguageString("theme_reset"));
                theme_reset_item->AddOnKey(std::bind(&ThemesMenuLayout::theme_DefaultKey, this));
                theme_reset_item->SetColor(g_MenuApplication->GetTextColor());
                theme_reset_item->SetIcon(GetLogoTexture());
                this->themes_menu->AddItem(theme_reset_item);
            }
        }

#ifdef QDESKTOP_MODE
        // v2.7.0 — "Create custom theme..." entry always shown last.
        // Stored at the final index of loaded_themes (as a sentinel valid
        // entry); theme_DefaultKey branches on idx == loaded_themes.size()-1
        // AND name == "__editor__" to invoke the editor.
        {
            cfg::Theme editor_sentinel;
            editor_sentinel.name = "__editor__";
            editor_sentinel.manifest.name = "Create custom theme...";
            editor_sentinel.manifest.format_version = 3;  // current
            editor_sentinel.manifest.release = "editor";
            editor_sentinel.manifest.description = "Walk through 17 color tokens to author a new .ultheme bundle.";
            editor_sentinel.manifest.author = "Q OS";
            this->loaded_themes.push_back(editor_sentinel);
            this->loaded_theme_icons.emplace_back();  // no icon for editor row
            auto editor_item = pu::ui::elm::MenuItem::New("\xF0\x9F\x8E\xA8 Create custom theme...");
            editor_item->AddOnKey(std::bind(&ThemesMenuLayout::theme_DefaultKey, this));
            editor_item->SetColor(g_MenuApplication->GetTextColor());
            editor_item->SetIcon(GetLogoTexture());  // reuse Q OS logo as editor icon
            this->themes_menu->AddItem(editor_item);
        }
#endif

        this->themes_menu->SetSelectedIndex(0);
    }

    void ThemesMenuLayout::theme_DefaultKey() {
        const auto idx_signed = this->themes_menu->GetSelectedIndex();
        if (idx_signed < 0) return;
        const size_t idx = static_cast<size_t>(idx_signed);

        // v2.7.0 — in-binary branch removed. All themes (including the 10 Q OS
        // ones) are now real .ultheme files on disk. Pick goes through the
        // upstream SetActiveTheme + RestartMenu path below. The Theme Editor
        // sentinel (name == "__editor__") still exists at the end of the list.

        const auto &selected_theme = this->loaded_themes.at(idx);

#ifdef QDESKTOP_MODE
        // v2.7.0 — editor sentinel detection. The "Create custom theme..." row
        // has a placeholder cfg::Theme with name == "__editor__"; route it
        // through the in-product editor instead of the .ultheme apply flow.
        if (selected_theme.name == "__editor__") {
            RunCustomThemeEditor();
            return;
        }
#endif

        if(selected_theme.IsValid()) {
            if(selected_theme.IsSame(g_GlobalSettings.active_theme)) {
                g_MenuApplication->ShowNotification(GetLanguageString("theme_active_this"));
            }
            else {
                std::string theme_conf_msg = selected_theme.manifest.name + "\n";
                theme_conf_msg += selected_theme.manifest.description + "\n";
                theme_conf_msg += "(" + selected_theme.manifest.release + ", " + selected_theme.manifest.author + ")\n\n";
                theme_conf_msg += GetLanguageString("theme_set_conf");

                const auto option = g_MenuApplication->DisplayDialog(selected_theme.manifest.name, theme_conf_msg, { GetLanguageString("yes"), GetLanguageString("cancel") }, true, this->loaded_theme_icons.at(idx));
                if(option == 0) {
                    if(selected_theme.IsOutdated()) {
                        const auto option_2 = g_MenuApplication->DisplayDialog(selected_theme.manifest.name, GetLanguageString("theme_outdated"), { GetLanguageString("ok"), GetLanguageString("cancel") }, true);
                        if(option_2 != 0) {
                            return;
                        }
                    }

                    g_GlobalSettings.SetActiveTheme(selected_theme);
                    g_MenuApplication->ShowNotification(GetLanguageString("theme_cache"));

                    pu::audio::PlaySfx(this->theme_change_sfx);
                    ul::menu::qdesktop::QdAudio::Play(ul::menu::qdesktop::DesktopSfxEvent::ThemeChange);
                    g_MenuApplication->ShowNotification(GetLanguageString("theme_changed"));

#ifdef QDESKTOP_MODE
                    // v2.7.3 BUG FIX — SetActiveTheme above only wrote
                    // config.ActiveThemeName; the cache (sdmc:/ulaunch/cache/active/)
                    // is otherwise refreshed only on the NEXT boot via
                    // SystemStatus.reload_theme_cache.  Without forcing an
                    // in-process re-cache here, LoadThemeFromCache below reads
                    // the PREVIOUS theme's QdPalette.json — so the transition
                    // frame paints the wrong colors (verified on HW 2026-05-18:
                    // picked Glass, transition frame painted Dark from stale
                    // cache).  Force re-extract the .ultheme zip now.
                    ul::cfg::CacheActiveTheme(g_GlobalSettings.config);
                    // v2.7.0 — the .ultheme's ui/QdPalette.json now drives
                    // BOTH the palette (17 tokens) AND the procedural wallpaper
                    // via its "wallpaper_pack" field. LoadThemeFromCache reads
                    // both. No forced reset of the wallpaper pack here — the
                    // .ultheme is authoritative.
                    qdesktop::LoadThemeFromCache(ul::ActiveThemeCachePath);
#endif
                    g_MenuApplication->FadeOutToNonLibraryApplet();
#ifdef QDESKTOP_MODE
                    // v2.7.2 — paint a transition frame in the destination
                    // theme's bg + accent so the uSystem RestartMenu defer
                    // window shows the new theme's identity rather than
                    // Plutonium's hardcoded cyan/lavender brand fade.
                    qdesktop::DrawThemeTransitionFrame(pu::ui::render::GetMainRenderer());
#endif
                    UL_RC_ASSERT(ul::menu::smi::RestartMenu(true));
                    g_MenuApplication->Finalize();
                }
            }
        }
        else {
            if(g_GlobalSettings.active_theme.IsValid()) {
                const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("theme_reset"), GetLanguageString("theme_reset_conf"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true);
                if(option == 0) {
                    g_GlobalSettings.SetActiveTheme({});
                    g_MenuApplication->ShowNotification(GetLanguageString("theme_cache"));

                    pu::audio::PlaySfx(this->theme_change_sfx);
                    ul::menu::qdesktop::QdAudio::Play(ul::menu::qdesktop::DesktopSfxEvent::ThemeChange);
                    g_MenuApplication->ShowNotification(GetLanguageString("theme_changed"));

#ifdef QDESKTOP_MODE
                    // Reverting to default: the cache is now wiped (via
                    // SetActiveTheme({}) which calls RemoveActiveThemeCache).
                    // Reset g_QdTheme + wallpaper pack to Q OS Glass defaults.
                    qdesktop::SetActiveThemePack(0);
                    qdesktop::SaveActiveThemePack(0);
#endif
                    g_MenuApplication->FadeOutToNonLibraryApplet();
#ifdef QDESKTOP_MODE
                    // v2.7.2 — paint transition frame in Q OS Glass (the
                    // reset destination) so the defer window shows the
                    // canonical cyan-on-black brand frame instead of the
                    // previous theme's lingering identity.
                    qdesktop::DrawThemeTransitionFrame(pu::ui::render::GetMainRenderer());
#endif
                    UL_RC_ASSERT(ul::menu::smi::RestartMenu(true));
                    g_MenuApplication->Finalize();
                }
            }
            else {
                g_MenuApplication->ShowNotification(GetLanguageString("theme_no_active"));
            }
        }
    }

}
