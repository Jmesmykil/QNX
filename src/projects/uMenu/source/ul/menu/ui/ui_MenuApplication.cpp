#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/audio/audio_SystemVolume.hpp>

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        std::queue<smi::MenuMessageContext> g_PendingInitialMessageQueue;

        // Per-menu BGM volume policy as a percentage of the Switch system master
        // volume.  The physical volume buttons change the OS master; GetSystemVolume()
        // reads it back and scales these percentages so the buttons always take effect.
        //
        //   Startup  → 75 % (login sound — creator: "I love it, LOCK it in!")
        //   Main     → 15 % (barely-there ambient)
        //   Themes   → 25 % (preview while browsing themes)
        //   Settings → 25 % (preview while in settings)
        //   Lockscr  → 15 % (subtle)
        constexpr s32 BgmPctForMenu(const MenuType menu) {
            switch(menu) {
                case MenuType::Startup:    return 75;
                case MenuType::Themes:     return 25;
                case MenuType::Settings:   return 25;
                case MenuType::Lockscreen: return 15;
                case MenuType::Main:       return 15;
            }
            return 15;
        }

        // Compute SDL_mixer volume (0–128) = system_volume × per-menu-pct × (128/100).
        s32 ComputeVolume(const MenuType menu) {
            const float sys = ul::audio::GetSystemVolume();           // [0.0, 1.0]
            const float raw = sys * static_cast<float>(BgmPctForMenu(menu)) * 1.28f;
            const s32   vol = static_cast<s32>(raw);
            if(vol < 0)   { return 0;   }
            if(vol > 128) { return 128; }
            return vol;
        }

    }

    std::string GetLanguageString(const std::string &name) {
        return cfg::GetLanguageString(g_GlobalSettings.main_lang, g_GlobalSettings.default_lang, name);
    }

    void OnMessage(const smi::MenuMessageContext &msg_ctx) {
        auto ptr = g_MenuApplication->GetLayout<IMenuLayout>();
        if(ptr == nullptr) {
            UL_LOG_WARN("[MenuApplication] Layout not ready, queuing message of type %d for later processing...", (u32)msg_ctx.msg);
            g_PendingInitialMessageQueue.push(msg_ctx);
        }
        else {
            UL_LOG_INFO("[MenuApplication] Forwarding message of type %d to layout...", (u32)msg_ctx.msg);
            ptr->NotifyMessageContext(msg_ctx);    
        }        
    }

    MenuBgmEntry &MenuApplication::GetCurrentMenuBgm() {
        switch(this->loaded_menu) {
            case MenuType::Main:
                return g_GlobalSettings.main_menu_bgm;
            case MenuType::Startup:
                return g_GlobalSettings.startup_menu_bgm;
            case MenuType::Themes:
                return g_GlobalSettings.themes_menu_bgm;
            case MenuType::Settings:
                return g_GlobalSettings.settings_menu_bgm;
            case MenuType::Lockscreen:
                return g_GlobalSettings.lockscreen_menu_bgm;
        }

        UL_ASSERT_FAIL("Invalid current menu?");
    }

    void MenuApplication::EnsureLayoutCreated(const MenuType type) {
        switch(type) {
            case MenuType::Startup:
                if(this->startup_menu_lyt == nullptr) {
                    this->startup_menu_lyt = StartupMenuLayout::New();
                    this->startup_menu_lyt->LoadSfx();
                    TryParseBgmEntry("startup_menu", "Startup", g_GlobalSettings.startup_menu_bgm);
                }
                break;
            case MenuType::Main:
                if(this->main_menu_lyt == nullptr) {
                    this->main_menu_lyt = MainMenuLayout::New();
                    this->main_menu_lyt->LoadSfx();
                    TryParseBgmEntry("main_menu", "Main", g_GlobalSettings.main_menu_bgm);
                    this->main_menu_lyt->Initialize();
                }
                break;
            case MenuType::Themes:
                if(this->themes_menu_lyt == nullptr) {
                    this->themes_menu_lyt = ThemesMenuLayout::New();
                    this->themes_menu_lyt->LoadSfx();
                    TryParseBgmEntry("themes_menu", "Themes", g_GlobalSettings.themes_menu_bgm);
                }
                break;
            case MenuType::Settings:
                if(this->settings_menu_lyt == nullptr) {
                    this->settings_menu_lyt = SettingsMenuLayout::New();
                    this->settings_menu_lyt->LoadSfx();
                    TryParseBgmEntry("settings_menu", "Settings", g_GlobalSettings.settings_menu_bgm);
                }
                break;
            case MenuType::Lockscreen:
                if(this->lockscreen_menu_lyt == nullptr) {
                    this->lockscreen_menu_lyt = LockscreenMenuLayout::New();
                    this->lockscreen_menu_lyt->LoadSfx();
                    TryParseBgmEntry("lockscreen_menu", "Lockscreen", g_GlobalSettings.lockscreen_menu_bgm);
                }
                break;
        }
    }

    void MenuApplication::OnLoad() {
        UL_LOG_INFO("MenuApplication::OnLoad start...");
        const auto time = std::chrono::system_clock::now();

        #define _LOG_SOFAR(kind) UL_LOG_INFO("MenuApplication::OnLoad -> " kind ", so far took %lld ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - time).count());

        this->launch_failed = false;
        this->pending_gc_mount_rc = ResultSuccess;
        this->needs_app_records_reload = false;
        this->needs_app_entries_reload = false;
        memset(this->chosen_hb, 0, sizeof(this->chosen_hb));
        this->verify_finished_app_id = 0;
        this->verify_rc = ResultSuccess;
        this->verify_detail_rc = ResultSuccess;
        this->active_theme_load_rc = ResultSuccess;
        this->active_theme_outdated = false;

        this->startup_menu_lyt = nullptr;
        this->main_menu_lyt = nullptr;
        this->themes_menu_lyt = nullptr;
        this->settings_menu_lyt = nullptr;
        this->lockscreen_menu_lyt = nullptr;

        // FastFadeAlphaIncrementSteps = 12 → ~200ms at 60fps.  Verified correct.
        this->SetFadeAlphaIncrementStepCount(FastFadeAlphaIncrementSteps);

        InitializeResources();

        _LOG_SOFAR("done initializing resources");

        InitializeScreenCaptures(this->start_mode);

        _LOG_SOFAR("done screen capture buffers");

        // BGM

        bool global_bgm_loop;
        if(TryGetBgmValue("bgm_loop", global_bgm_loop)) {
            g_GlobalSettings.main_menu_bgm.bgm_loop = global_bgm_loop;
            g_GlobalSettings.startup_menu_bgm.bgm_loop = global_bgm_loop;
            g_GlobalSettings.themes_menu_bgm.bgm_loop = global_bgm_loop;
            g_GlobalSettings.settings_menu_bgm.bgm_loop = global_bgm_loop;
            g_GlobalSettings.lockscreen_menu_bgm.bgm_loop = global_bgm_loop;
        }

        u32 global_bgm_fade_in_ms;
        if(TryGetBgmValue("bgm_fade_in_ms", global_bgm_fade_in_ms)) {
            g_GlobalSettings.main_menu_bgm.bgm_fade_in_ms = global_bgm_fade_in_ms;
            g_GlobalSettings.startup_menu_bgm.bgm_fade_in_ms = global_bgm_fade_in_ms;
            g_GlobalSettings.themes_menu_bgm.bgm_fade_in_ms = global_bgm_fade_in_ms;
            g_GlobalSettings.settings_menu_bgm.bgm_fade_in_ms = global_bgm_fade_in_ms;
            g_GlobalSettings.lockscreen_menu_bgm.bgm_fade_in_ms = global_bgm_fade_in_ms;
        }

        u32 global_bgm_fade_out_ms;
        if(TryGetBgmValue("bgm_fade_out_ms", global_bgm_fade_out_ms)) {
            g_GlobalSettings.main_menu_bgm.bgm_fade_out_ms = global_bgm_fade_out_ms;
            g_GlobalSettings.startup_menu_bgm.bgm_fade_out_ms = global_bgm_fade_out_ms;
            g_GlobalSettings.themes_menu_bgm.bgm_fade_out_ms = global_bgm_fade_out_ms;
            g_GlobalSettings.settings_menu_bgm.bgm_fade_out_ms = global_bgm_fade_out_ms;
            g_GlobalSettings.lockscreen_menu_bgm.bgm_fade_out_ms = global_bgm_fade_out_ms;
        }

        _LOG_SOFAR("done loading bgm");

        // UI

        const auto toast_text_clr = GetRequiredUiValue<pu::ui::Color>("toast_text_color");
        const auto toast_base_clr = GetRequiredUiValue<pu::ui::Color>("toast_base_color");

        auto notif_toast_text = pu::ui::elm::TextBlock::New(0, 0, "...");
        notif_toast_text->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium));
        notif_toast_text->SetColor(toast_text_clr);
        this->notif_toast = pu::ui::extras::Toast::New(notif_toast_text, toast_base_clr);

        this->text_clr = GetRequiredUiValue<pu::ui::Color>("text_color");

        this->menu_focus_clr = GetRequiredUiValue<pu::ui::Color>("menu_focus_color");
        this->menu_bg_clr = GetRequiredUiValue<pu::ui::Color>("menu_bg_color");

        this->dialog_title_clr = GetRequiredUiValue<pu::ui::Color>("dialog_title_color");
        this->dialog_cnt_clr = GetRequiredUiValue<pu::ui::Color>("dialog_cnt_color");
        this->dialog_opt_clr = GetRequiredUiValue<pu::ui::Color>("dialog_opt_color");
        this->dialog_clr = GetRequiredUiValue<pu::ui::Color>("dialog_color");
        this->dialog_over_clr = GetRequiredUiValue<pu::ui::Color>("dialog_over_color");

        switch(this->start_mode) {
            case smi::MenuStartMode::StartupMenu:
            case smi::MenuStartMode::StartupMenuPostBoot: {
                break;
            }
            default: {
                LoadSelectedUserIconTexture();
                break;
            }
        }

        _LOG_SOFAR("done loading ui");

        this->loaded_menu = MenuType::Main;
        switch(this->start_mode) {
            case smi::MenuStartMode::StartupMenu:
            case smi::MenuStartMode::StartupMenuPostBoot: {
                this->LoadMenu(MenuType::Startup, false);
                // Pre-warm MainMenuLayout during the boot fade so the first
                // navigation to Main has zero first-frame construction lag.
                this->EnsureLayoutCreated(MenuType::Main);
                break;
            }
            case smi::MenuStartMode::SettingsMenu: {
                this->LoadMenu(MenuType::Settings, false);
                break;
            }
            default: {
                this->LoadMenu(MenuType::Main, false);
                // Pre-warm StartupMenuLayout during boot so returning to the
                // startup/lockscreen path is instant.
                this->EnsureLayoutCreated(MenuType::Startup);
                break;
            }
        }
        this->StartPlayBgm();

        // Periodic system-volume re-apply: re-reads audctl every ~30 frames
        // (~500 ms at 60 fps) and updates SDL_mixer levels so the physical
        // volume buttons take effect without restarting BGM or SFX.
        //
        // SP4.1 safety: guard against invocation during applet teardown.
        // Plutonium fires render callbacks until the very end of its Finalize
        // path.  If this lambda runs after the Home button has triggered
        // TerminateMenu() (via Finalize), g_MenuApplication / loaded_menu are
        // being destroyed — dereferencing them crashes the renderer cleanup.
        // The g_shutdown flag set in tel::Shutdown() is not available here, so
        // we use g_MenuApplication itself as the null sentinel: once Finalize
        // begins it will nullptr the ref before SDL cleanup runs, and any
        // subsequent render-callback fire sees the guard and returns cleanly.
        this->AddRenderCallback([this]() {
            if(g_MenuApplication == nullptr) { return; }

            static u32 s_vol_frame = 0;
            if(++s_vol_frame < 30) { return; }
            s_vol_frame = 0;

            // Snapshot loaded_menu once under the guard so we use a consistent
            // value even if a concurrent state transition is in progress.
            const MenuType cur_menu = this->loaded_menu;

            // Re-apply BGM volume only when music is actually playing.
            if(Mix_PlayingMusic()) {
                pu::audio::SetMusicVolume(ComputeVolume(cur_menu));
            }

            // Re-apply SFX volume: Mix_Volume(-1, v) sets all channels at once.
            // Use the current menu percentage as the global SFX reference level.
            {
                const float sys = ul::audio::GetSystemVolume();
                const float raw = sys * static_cast<float>(BgmPctForMenu(cur_menu)) * 1.28f;
                s32 sfx_vol = static_cast<s32>(raw);
                if(sfx_vol < 0)   { sfx_vol = 0;   }
                if(sfx_vol > 128) { sfx_vol = 128; }
                Mix_Volume(-1, sfx_vol);
            }
        });

        for(; !g_PendingInitialMessageQueue.empty(); ) {
            const auto pending_msg_ctx = g_PendingInitialMessageQueue.front();
            UL_LOG_INFO("[MenuApplication] Forwarding initial queued message of type %d to layout...", (u32)pending_msg_ctx.msg);
            this->GetLayout<IMenuLayout>()->NotifyMessageContext(pending_msg_ctx);
            g_PendingInitialMessageQueue.pop();
        }

        _LOG_SOFAR("done everything");
    }

    void MenuApplication::Finalize() {
        UL_LOG_INFO("Finalizing...");

        /*
        this->ResetFade();
        this->FadeOut();

        this->StopPlayBgm();

        this->DisposeAllSfx();
        ul::menu::ui::DisposeAllBgm();
        pu::audio::Finalize();

        this->Close(true);
        */

        // This might look very ugly, but it is a simple and quick way to exit fast: let uSystem terminate us directly (the OS itself deals with the cleanup)
        // Most importantly, this allows us to exit without cleaning the screen to black when exiting SDL2 stuff (as regular homebrew apps do) so we can do cool transitions with applets/games
        ul::menu::smi::TerminateMenu();
    }

    void MenuApplication::SetBackgroundFade() {
        this->SetFadeAlphaIncrementStepCount(FastFadeAlphaIncrementSteps);
        if(!this->HasFadeBackgroundImage()) {
            this->SetFadeBackgroundImage(GetBackgroundTexture());
        }
    }

    void MenuApplication::LoadMenu(const MenuType type, const bool fade, MenuFadeCallback fade_cb) {
        this->StopPlayBgm();

        if(fade) {
            this->SetBackgroundFade();
            this->FadeOut();

            if(fade_cb) {
                fade_cb();
            }
        }

        this->EnsureLayoutCreated(type);
        
        switch(type) {
            case MenuType::Startup: {
                this->startup_menu_lyt->ReloadMenu();
                this->LoadLayout(this->startup_menu_lyt);
                break;
            }
            case MenuType::Main: {
                this->main_menu_lyt->Reload();
                this->LoadLayout(this->main_menu_lyt);
                break;
            }
            case MenuType::Settings: {
                this->settings_menu_lyt->Rewind();
                this->settings_menu_lyt->Reload(false);
                this->LoadLayout(this->settings_menu_lyt);
                break;
            }
            case MenuType::Themes: {
                this->themes_menu_lyt->Reload();
                this->LoadLayout(this->themes_menu_lyt);
                break;
            }
            case MenuType::Lockscreen: {
                this->LoadLayout(this->lockscreen_menu_lyt);
                break;
            }
        }

        this->loaded_menu = type;

        this->StartPlayBgm();

        if(fade) {
            this->FadeIn();
        }
    }

    void MenuApplication::ShowNotification(const std::string &text, const u64 timeout) {
        this->EndOverlay();
        this->notif_toast->SetText(text);
        this->StartOverlayWithTimeout(this->notif_toast, timeout);
    }

    void MenuApplication::StartPlayBgm() {
        const auto &bgm = this->GetCurrentMenuBgm();
        if(bgm.bgm != nullptr) {
            pu::audio::SetMusicVolume(ComputeVolume(this->loaded_menu));

            const int loops = bgm.bgm_loop ? -1 : 1;
            if(bgm.bgm_fade_in_ms > 0) {
                pu::audio::PlayMusicWithFadeIn(bgm.bgm, loops, bgm.bgm_fade_in_ms);
            }
            else {
                pu::audio::PlayMusic(bgm.bgm, loops);
            }
        }
    }

    void MenuApplication::StopPlayBgm() {
        const auto &bgm = this->GetCurrentMenuBgm();
        if(bgm.bgm_fade_out_ms > 0) {
            pu::audio::FadeOutMusic(bgm.bgm_fade_out_ms);
        }
        else {
            pu::audio::StopMusic();
        }
    }

    void MenuApplication::LoadBgmSfxForCreatedMenus() {
        if(this->main_menu_lyt != nullptr) {
            this->main_menu_lyt->LoadSfx();
            TryParseBgmEntry("main_menu", "Main", g_GlobalSettings.main_menu_bgm);
        }
        if(this->startup_menu_lyt != nullptr) {
            this->startup_menu_lyt->LoadSfx();
            TryParseBgmEntry("startup_menu", "Startup", g_GlobalSettings.startup_menu_bgm);
        }
        if(this->themes_menu_lyt != nullptr) {
            this->themes_menu_lyt->LoadSfx();
            TryParseBgmEntry("themes_menu", "Themes", g_GlobalSettings.themes_menu_bgm);
        }
        if(this->settings_menu_lyt != nullptr) {
            this->settings_menu_lyt->LoadSfx();
            TryParseBgmEntry("settings_menu", "Settings", g_GlobalSettings.settings_menu_bgm);
        }
        if(this->lockscreen_menu_lyt != nullptr) {
            this->lockscreen_menu_lyt->LoadSfx();
            TryParseBgmEntry("lockscreen_menu", "Lockscreen", g_GlobalSettings.lockscreen_menu_bgm);
        }
    }

    void MenuApplication::DisposeAllSfx() {
        if(this->main_menu_lyt != nullptr) {
            this->main_menu_lyt->DisposeSfx();
        }
        if(this->startup_menu_lyt != nullptr) {
            this->startup_menu_lyt->DisposeSfx();
        }
        if(this->themes_menu_lyt != nullptr) {
            this->themes_menu_lyt->DisposeSfx();
        }
        if(this->settings_menu_lyt != nullptr) {
            this->settings_menu_lyt->DisposeSfx();
        }
        if(this->lockscreen_menu_lyt != nullptr) {
            this->lockscreen_menu_lyt->DisposeSfx();
        }
    }

    int MenuApplication::DisplayDialog(const std::string &title, const std::string &content, const std::vector<std::string> &opts, const bool use_last_opt_as_cancel, pu::sdl2::TextureHandle::Ref icon) {
        return this->CreateShowDialog(title, content, opts, use_last_opt_as_cancel, icon, [&](pu::ui::Dialog::Ref &dialog) {
            dialog->SetTitleColor(this->dialog_title_clr);
            dialog->SetContentColor(this->dialog_title_clr);
            dialog->SetOptionColor(this->dialog_opt_clr);
            dialog->SetDialogColor(this->dialog_clr);
            dialog->SetOverColor(this->dialog_over_clr);
        });
    }

}
