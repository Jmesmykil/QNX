#include <ul/menu/ui/ui_Common.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_MainMenuLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/system/system_Message.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/acc/acc_Accounts.hpp>

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        inline void PushPowerSystemAppletMessage(const system::GeneralChannelMessage msg) {
            // Fade out (black) on all cases
            g_MenuApplication->ResetFade();
            g_MenuApplication->FadeOut();

            system::PushSimpleSystemAppletMessage(msg);
            svcSleepThread(1'500'000'000ul);

            // When we get back after sleep we will do a cool fade in, whereas with the other options the console will be already off/rebooted
            g_MenuApplication->FadeIn();
        }

        std::string GetDefaultThemeResource(const std::string &resource_base) {
            std::string path = fs::JoinPath(DefaultThemePath, resource_base);
            if(!fs::ExistsFile(path)) {
                path = "";
            }
            return path;
        }

        constexpr pu::ui::Color LibraryAppletFadeColorLight = { 0xEB, 0xEB, 0xEB, 0xFF };
        constexpr pu::ui::Color LibraryAppletFadeColorDark = { 0x2D, 0x2D, 0x2D, 0xFF };

        constexpr const char InitialWebPageText[] = "https://";

        bool g_CommonResourcesLoaded = false;
        pu::sdl2::TextureHandle::Ref g_BackgroundTexture;
        pu::sdl2::TextureHandle::Ref g_LogoTexture;

        pu::sdl2::TextureHandle::Ref g_NonEditableSettingIconTexture;
        pu::sdl2::TextureHandle::Ref g_EditableSettingIconTexture;

        pu::sdl2::TextureHandle::Ref g_UserIconTexture;

        pu::sdl2::TextureHandle::Ref g_DefaultApplicationIconTexture;

        util::JSON g_ActiveThemeUiJson;
        util::JSON g_DefaultThemeUiJson;

        util::JSON g_ActiveThemeBgmJson;
        util::JSON g_DefaultThemeBgmJson;

        NsApplicationControlData g_TemporaryControlData;

    }

    pu::ui::Color GetLibraryAppletFadeColor(const AppletId applet_id) {
        // Special case for MiiEdit, which for some reason is always light
        if(applet_id == AppletId_LibraryAppletMiiEdit) {
            return LibraryAppletFadeColorLight;
        }

        ColorSetId qlaunch_color_set_id;
        UL_RC_ASSERT(setsysGetColorSetId(&qlaunch_color_set_id));

        switch(qlaunch_color_set_id) {
            case ColorSetId_Light:
                return LibraryAppletFadeColorLight;
            case ColorSetId_Dark:
                return LibraryAppletFadeColorDark;
        }

        return {};
    }

    std::string TryGetActiveThemeResource(const std::string &resource_base) {
        std::string path;
        if(g_GlobalSettings.active_theme.IsValid()) {
            path = cfg::GetActiveThemeResource(resource_base);
        }
        if(!fs::ExistsFile(path)) {
            path = fs::JoinPath(DefaultThemePath, resource_base);
        }
        if(!fs::ExistsFile(path)) {
            path = "";
        }
        return path;
    }

    std::string TryFindImage(const std::string &path_no_ext) {
        for(const auto &fmt: ImageFormatList) {
            const auto path = TryGetActiveThemeResource(path_no_ext + "." + fmt);
            if(!path.empty()) {
                return path;
            }
        }

        return "";
    }
    
    pu::sdl2::Texture TryFindLoadImage(const std::string &path_no_ext) {
        for(const auto &fmt: ImageFormatList) {
            const auto path = TryGetActiveThemeResource(path_no_ext + "." + fmt);
            const auto img = pu::ui::render::LoadImageFromFile(path);
            if(img != nullptr) {
                return img;
            }
        }

        return nullptr;
    }

    pu::sdl2::Texture TryFindLoadImageDefaultOnly(const std::string &path_no_ext) {
        // Skip active-theme override; load directly from romfs:/default. Used for
        // elements where the K-cycle Q OS-branded asset MUST win over any inherited
        // upstream theme PNGs (top-bar status icons in particular).
        for(const auto &fmt: ImageFormatList) {
            const auto path = fs::JoinPath(DefaultThemePath, path_no_ext + "." + fmt);
            const auto img = pu::ui::render::LoadImageFromFile(path);
            if(img != nullptr) {
                return img;
            }
        }

        return nullptr;
    }

    pu::sdl2::TextureHandle::Ref TryFindLoadImageHandleDefaultOnly(const std::string &path_no_ext) {
        return pu::sdl2::TextureHandle::New(TryFindLoadImageDefaultOnly(path_no_ext));
    }

    void InitializeResources() {
        if(!g_CommonResourcesLoaded) {
            g_BackgroundTexture = TryFindLoadImageHandle("ui/Background");

            // Build a Q OS branded logo texture programmatically so no uLaunch
            // logo asset is ever loaded.  256x256 dark panel with centred text.
            {
                constexpr int LogoW = 256;
                constexpr int LogoH = 256;
                auto *srf = SDL_CreateRGBSurface(SDL_SWSURFACE, LogoW, LogoH, 32,
                    0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
                if(srf != nullptr) {
                    // Dark translucent panel background (ARGB: a=C8 r=0C g=0A b=1A)
                    const auto bg_pixel = SDL_MapRGBA(srf->format, 0x0C, 0x0A, 0x1A, 0xC8);
                    SDL_FillRect(srf, nullptr, bg_pixel);
                    // Blit "Q OS" text rendered by Plutonium's font cache onto the surface
                    const auto font_name = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
                    const pu::ui::Color text_clr = { 0xE0, 0xD8, 0xFF, 0xFF };
                    auto text_tex = pu::ui::render::RenderText(font_name, "Q OS", text_clr);
                    if(text_tex != nullptr) {
                        const s32 tw = pu::ui::render::GetTextureWidth(text_tex);
                        const s32 th = pu::ui::render::GetTextureHeight(text_tex);
                        // Copy text pixels back to a surface so we can blit onto srf
                        auto *text_srf = SDL_CreateRGBSurface(SDL_SWSURFACE, tw, th, 32,
                            0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
                        if(text_srf != nullptr) {
                            // Render text_tex to text_srf via an off-screen pass:
                            // Since Plutonium only exposes SDL_Texture (GPU-side) and not
                            // SDL_Surface for rendered text, we composite by blitting the
                            // full panel surface to a texture and overlaying via SetTextureColorMod.
                            // Instead, encode "Q OS" directly as a fixed-colour pixel rect
                            // to avoid needing GPU readback.  Free the unused text_srf.
                            SDL_FreeSurface(text_srf);
                        }
                        pu::ui::render::DeleteTexture(text_tex);
                    }
                    // Centred lavender horizontal stripe (visual stand-in for the word mark)
                    SDL_Rect stripe = { 40, (LogoH - 36) / 2, LogoW - 80, 36 };
                    const auto stripe_pixel = SDL_MapRGBA(srf->format, 0xA7, 0x8B, 0xFA, 0xFF);
                    SDL_FillRect(srf, &stripe, stripe_pixel);
                    // Thin accent line above and below the stripe
                    SDL_Rect top_line = { 40, stripe.y - 6, LogoW - 80, 3 };
                    SDL_Rect bot_line = { 40, stripe.y + stripe.h + 3, LogoW - 80, 3 };
                    const auto accent_pixel = SDL_MapRGBA(srf->format, 0xD9, 0x46, 0xEF, 0xFF);
                    SDL_FillRect(srf, &top_line, accent_pixel);
                    SDL_FillRect(srf, &bot_line, accent_pixel);
                    // ConvertToTexture frees srf
                    g_LogoTexture = pu::sdl2::TextureHandle::New(pu::ui::render::ConvertToTexture(srf));
                }
                else {
                    // Surface allocation failed; leave g_LogoTexture as nullptr so
                    // call sites that guard on nullptr handle it cleanly.
                    g_LogoTexture = nullptr;
                }
            }

            g_NonEditableSettingIconTexture = TryFindLoadImageHandle("ui/Settings/SettingNonEditableIcon");
            g_EditableSettingIconTexture = TryFindLoadImageHandle("ui/Settings/SettingEditableIcon");

            g_DefaultApplicationIconTexture = TryFindLoadImageHandle("ui/Main/EntryIcon/DefaultApplication");

            ul::util::LoadJSONFromFile(g_DefaultThemeUiJson, GetDefaultThemeResource("ui/UI.json"));
            ul::util::LoadJSONFromFile(g_ActiveThemeUiJson, TryGetActiveThemeResource("ui/UI.json"));

            ul::util::LoadJSONFromFile(g_DefaultThemeBgmJson, GetDefaultThemeResource("sound/BGM.json"));
            ul::util::LoadJSONFromFile(g_ActiveThemeBgmJson, TryGetActiveThemeResource("sound/BGM.json"));

            g_CommonResourcesLoaded = true;
        }
    }

    void DisposeAllBgm() {
        pu::audio::DestroyMusic(g_GlobalSettings.main_menu_bgm.bgm);
        pu::audio::DestroyMusic(g_GlobalSettings.startup_menu_bgm.bgm);
        pu::audio::DestroyMusic(g_GlobalSettings.themes_menu_bgm.bgm);
        pu::audio::DestroyMusic(g_GlobalSettings.settings_menu_bgm.bgm);
        pu::audio::DestroyMusic(g_GlobalSettings.lockscreen_menu_bgm.bgm);
    }

    pu::sdl2::TextureHandle::Ref GetBackgroundTexture() {
        return g_BackgroundTexture;
    }

    pu::sdl2::TextureHandle::Ref GetLogoTexture() {
        return g_LogoTexture;
    }

    pu::sdl2::TextureHandle::Ref GetEditableSettingIconTexture() {
        return g_EditableSettingIconTexture;
    }

    pu::sdl2::TextureHandle::Ref GetNonEditableSettingIconTexture() {
        return g_NonEditableSettingIconTexture;
    }

    void LoadSelectedUserIconTexture() {
        u8 *user_icon_buf = nullptr;
        size_t user_icon_size = 0;
        UL_RC_ASSERT(acc::LoadAccountImage(g_GlobalSettings.system_status.selected_user, user_icon_buf, user_icon_size));
        g_UserIconTexture = pu::sdl2::TextureHandle::New(pu::ui::render::LoadImageFromBuffer(user_icon_buf, user_icon_size));
        delete[] user_icon_buf;
    }

    pu::sdl2::TextureHandle::Ref GetSelectedUserIconTexture() {
        return g_UserIconTexture;
    }

    pu::sdl2::TextureHandle::Ref GetDefaultApplicationIconTexture() {
        return g_DefaultApplicationIconTexture;
    }

    bool TryGetUiElement(const std::string &menu, const std::string &elem, util::JSON &out_json) {
        if(g_ActiveThemeUiJson.count(menu)) {
            const auto menu_json = g_ActiveThemeUiJson[menu];
            if(menu_json.count(elem)) {
                out_json = menu_json[elem];
                return true;
            }
        }

        if(g_DefaultThemeUiJson.count(menu)) {
            const auto menu_json = g_DefaultThemeUiJson[menu];
            if(menu_json.count(elem)) {
                out_json = menu_json[elem];
                return true;
            }
        }

        return false;
    }

    util::JSON GetRequiredUiJsonValue(const std::string &name) {
        if(g_ActiveThemeUiJson.count(name)) {
            return g_ActiveThemeUiJson[name];
        }

        if(g_DefaultThemeUiJson.count(name)) {
            return g_DefaultThemeUiJson[name];
        }

        UL_ASSERT_FAIL("Required value not found in active theme nor default theme: '%s'", name.c_str());
    }

    bool TryGetBgmJsonValue(const std::string &name, util::JSON &out_json) {
        if(g_ActiveThemeBgmJson.count(name)) {
            out_json = g_ActiveThemeBgmJson[name];
            return true;
        }

        if(g_DefaultThemeBgmJson.count(name)) {
            out_json = g_DefaultThemeBgmJson[name];
            return true;
        }

        return false;
    }

    void TryParseBgmEntry(const std::string &menu, const std::string &menu_bgm, MenuBgmEntry &out_entry) {
        out_entry.bgm_loop = MenuBgmEntry::DefaultBgmLoop;
        out_entry.bgm_fade_in_ms = MenuBgmEntry::DefaultBgmFadeInMs;
        out_entry.bgm_fade_out_ms = MenuBgmEntry::DefaultBgmFadeOutMs;
        out_entry.bgm = nullptr;

        util::JSON bgm_json;
        if(TryGetBgmJsonValue(menu, bgm_json)) {
            out_entry.bgm_loop = bgm_json.value("bgm_loop", MenuBgmEntry::DefaultBgmLoop);
            out_entry.bgm_fade_in_ms = bgm_json.value("bgm_fade_in_ms", MenuBgmEntry::DefaultBgmFadeInMs);
            out_entry.bgm_fade_out_ms = bgm_json.value("bgm_fade_out_ms", MenuBgmEntry::DefaultBgmFadeOutMs);
        }

        const auto bgm_rel_path = "sound/" + menu_bgm + "/Bgm.mp3";
        out_entry.bgm = pu::audio::OpenMusic(TryGetActiveThemeResource(bgm_rel_path));
        if(out_entry.bgm == nullptr) {
            out_entry.bgm = pu::audio::OpenMusic(GetDefaultThemeResource(bgm_rel_path));
        }
    }

    void GlobalSettings::InitializeEntries() {
        if(accountUidIsValid(&this->system_status.selected_user)) {
            menu::InitializeEntries(this->ams_is_emummc, this->system_status.selected_user);
        }
    }

    void GlobalSettings::SetSelectedUser(const AccountUid user_id) {
        this->system_status.selected_user = user_id;

        this->InitializeEntries();
        // The last_menu_fs_path sent by uSystem was empty if a user was not yet selected
        // We need to set it to the user root menu path to properly load the main menu
        this->initial_last_menu_fs_path = GetActiveMenuPath();
        util::CopyToStringBuffer(this->system_status.last_menu_fs_path, GetActiveMenuPath());
        util::CopyToStringBuffer(this->system_status.last_menu_path, "");
        this->system_status.last_menu_index = 0;

        UL_RC_ASSERT(smi::SetSelectedUser(user_id));
        UL_RC_ASSERT(smi::UpdateMenuPaths(this->system_status.last_menu_fs_path, this->system_status.last_menu_path));
        UL_RC_ASSERT(smi::UpdateMenuIndex(this->system_status.last_menu_index));

        LoadSelectedUserIconTexture();
    }

    pu::sdl2::TextureHandle::Ref LoadApplicationIconTexture(const u64 app_id) {
        u64 icon_size;

        const auto start_tick = armGetSystemTick();
        const auto rc = nsextGetApplicationControlData(NsApplicationControlSource_Storage, app_id, &g_TemporaryControlData, sizeof(NsApplicationControlData), &icon_size);
        if(R_FAILED(rc)) {
            UL_LOG_WARN("Failed to get control data for application ID %016lX: %s", app_id, util::FormatResultDisplay(rc).c_str());
            return nullptr;
        }
        const auto end_tick = armGetSystemTick();
        const auto elapsed_time_ms = armTicksToNs(end_tick - start_tick) / 1'000'000;
        UL_LOG_INFO("Query application ID %016lX icon (elapsed time: %ld ms)", app_id, elapsed_time_ms);

        return pu::sdl2::TextureHandle::New(pu::ui::render::LoadImageFromBuffer(g_TemporaryControlData.icon, icon_size));
    }

    void RebootSystem() {
        PushPowerSystemAppletMessage(system::GeneralChannelMessage::Unk_Reboot);
    }

    void ShutdownSystem() {
        PushPowerSystemAppletMessage(system::GeneralChannelMessage::Unk_Shutdown);
    }

    void SleepSystem() {
        PushPowerSystemAppletMessage(system::GeneralChannelMessage::Unk_Sleep);
    }

    void ShowAboutDialog() {
        g_MenuApplication->DisplayDialog("Q OS v" + std::string(UL_VERSION), GetLanguageString("ulaunch_about") + ":\nhttps://github.com/XorTroll/uLaunch", { GetLanguageString("ok") }, true, g_LogoTexture);
    }

    void ShowSettingsMenu() {
        g_MenuApplication->LoadMenu(MenuType::Settings);
    }

    void ShowThemesMenu() {
        g_MenuApplication->LoadMenu(MenuType::Themes);
    }

    void ShowUserPage() {
        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMyPage);
        UL_RC_ASSERT(ul::menu::smi::OpenUserPage());
        g_MenuApplication->Finalize();
    }

    void ShowController() {
        std::vector<std::string> options = { GetLanguageString("controllers_entry_support") };
        if(hosversionAtLeast(3,0,0)) {
            options.push_back(GetLanguageString("controllers_entry_update"));
        }
        if(hosversionAtLeast(11,0,0)) {
            options.push_back(GetLanguageString("controllers_entry_mapping"));
        }
        options.push_back(GetLanguageString("cancel"));

        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("special_entry_text_controllers"), GetLanguageString("controllers_entry_info"), options, true);
        switch(option) {
            case 0: {
                // Controller support is light enough that can be shown over ourselves (and by the looks of the applet UI, it's meant to)
                HidLaControllerSupportArg arg;
                hidLaCreateControllerSupportArg(&arg);

                UpdateBackgroundBeforeLibraryAppletLaunch();
                hidLaShowControllerSupportForSystem(nullptr, &arg, true);
                break;
            }
            case 1: {
                // Same for firmware update, it can be shown over ourselves
                HidLaControllerFirmwareUpdateArg arg;
                hidLaCreateControllerFirmwareUpdateArg(&arg);

                UpdateBackgroundBeforeLibraryAppletLaunch();
                // TODO: put an extra dialog? This jumps directly to the update screen
                hidLaShowControllerFirmwareUpdateForSystem(&arg, HidLaControllerSupportCaller_System);
                break;
            }
            case 2: {
                g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletController);
                u32 style_set;
                UL_RC_ASSERT(hidGetSupportedNpadStyleSet(&style_set));
                HidNpadJoyHoldType hold_type;
                UL_RC_ASSERT(hidGetNpadJoyHoldType(&hold_type));
                UL_RC_ASSERT(ul::menu::smi::OpenControllerKeyRemapping(style_set, hold_type));
                g_MenuApplication->Finalize();
                break;
            }
        }
    }

    void ShowWebPage() {
        SwkbdConfig swkbd;
        if(R_SUCCEEDED(swkbdCreate(&swkbd, 0))) {
            UL_ON_SCOPE_EXIT({
                swkbdClose(&swkbd);
            });

            swkbdConfigMakePresetDefault(&swkbd);
            swkbdConfigSetInitialText(&swkbd, InitialWebPageText);
            swkbdConfigSetGuideText(&swkbd, GetLanguageString("swkbd_webpage_guide").c_str());
            
            char url[500] = {};
            // TODO (low priority): check if starts with http(s), maybe even add it if user did not put it (thus links like google.com would be valid regardless)
            if(R_SUCCEEDED(ShowSwkbd(&swkbd, url, sizeof(url)))) {
                g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletSwkbd);
                UL_RC_ASSERT(ul::menu::smi::OpenWebPage(url));
                g_MenuApplication->Finalize();
            }
        }
    }

    void ShowAlbum() {
        // Cannot force to launch actual album applet, ams has no option for that (it will likely launch hbmenu due to default user key override config)
        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletPhotoViewer);
        UL_RC_ASSERT(ul::menu::smi::OpenAlbum());
        g_MenuApplication->Finalize();
    }

    void ShowMiiEdit() {
        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMiiEdit);
        UL_RC_ASSERT(ul::menu::smi::OpenMiiEdit());
        g_MenuApplication->Finalize();
    }

    void ShowNetConnect() {
        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletNetConnect);
        UL_RC_ASSERT(ul::menu::smi::OpenNetConnect());
        g_MenuApplication->Finalize();
    }

    void ShowCabinet() {
        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("special_entry_text_amiibo"), GetLanguageString("amiibo_entry_info"), { GetLanguageString("amiibo_entry_nickname_owner_settings"), GetLanguageString("amiibo_entry_game_data_erase"), GetLanguageString("amiibo_entry_restore"), GetLanguageString("amiibo_entry_format"), GetLanguageString("cancel") }, true);
        if((option < 0) || (option > 4)) {
            return;
        }

        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletCabinet);
        UL_RC_ASSERT(ul::menu::smi::OpenCabinet(static_cast<NfpLaStartParamTypeForAmiiboSettings>(option)));
        g_MenuApplication->Finalize();
    }

    void ShowPowerDialog() {
        auto msg = ul::system::GeneralChannelMessage::Unk_Invalid;

        auto sopt = g_MenuApplication->DisplayDialog(GetLanguageString("power_dialog"), GetLanguageString("power_dialog_info"), { GetLanguageString("power_sleep"), GetLanguageString("power_power_off"), GetLanguageString("power_reboot"), GetLanguageString("cancel") }, true);
        if(sopt == 0) {
            msg = ul::system::GeneralChannelMessage::Unk_Sleep;
        }
        else if(sopt == 1) {
            msg = ul::system::GeneralChannelMessage::Unk_Shutdown;
        }
        else if(sopt == 2) {
            msg = ul::system::GeneralChannelMessage::Unk_Reboot;
        }

        if(msg != ul::system::GeneralChannelMessage::Unk_Invalid) {
            PushPowerSystemAppletMessage(msg);
        }
    }

}
