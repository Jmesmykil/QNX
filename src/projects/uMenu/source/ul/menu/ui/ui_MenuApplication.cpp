#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/audio/audio_SystemVolume.hpp>
#include <ul/menu/qdesktop/qd_Transition.hpp>
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>
#include <atomic>

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

// Cycle D1: terminate sentinel.
// Set true at the top of Finalize(); checked by render callbacks to
// short-circuit any work that would dereference soon-to-be-destroyed state.
//
// We MUST NOT null g_MenuApplication directly in Finalize() — that drops the
// shared_ptr refcount 1→0, destroys MenuApplication mid-frame while Show() is
// iterating lyt->GetRenderCallbacks(), and produces a dangling std::function
// whose _M_invoker points to freed memory. The next render iteration calls
// PC=0 → instruction abort. C3 (SP4.11) introduced this regression; D1 reverts
// it by using an atomic bool sentinel that costs nothing.
//
// Visible to other TUs as a free-standing weak symbol via the extern below.
std::atomic<bool> g_uMenuTerminating { false };

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
                case MenuType::Startup:     return 75;
                case MenuType::Themes:      return 25;
                case MenuType::Settings:    return 25;
                case MenuType::Lockscreen:  return 15;
                case MenuType::Main:        return 15;
                case MenuType::Vault:       return 15;
                case MenuType::Monitor:     return 15;
                case MenuType::About:       return 15;
                case MenuType::QSettings:   return 25;
                case MenuType::QLockscreen: return 15;
                case MenuType::Launchpad:   return 15;
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
        // ⚠ ARCHITECTURAL CONTRACT — every `pu::ui::Layout::Ref` that
        // MenuApplication can have ACTIVE at the moment a message arrives MUST
        // be an `IMenuLayout` subclass.  Plutonium's GetLayout<L>() is an
        // UNCHECKED `std::static_pointer_cast<L>(this->lyt)` and the build
        // disables RTTI (`-fno-rtti`), so we cannot dynamic-cast as a runtime
        // safety net.  If you add a new menu surface (Vault, Folders, About,
        // etc.) and wrap it in a bare `pu::ui::Layout::New()`, ANY incoming
        // smi::MenuMessage (e.g. HOME) reinterprets the layout's vtable and
        // Data-Aborts at 0x0.  Crashed exactly this way in
        // atmosphere/crash_reports/01777163509_010000000000100d.log (Vault →
        // HOME).  See qd_VaultHostLayout.hpp for the canonical wrapper pattern.
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
            case MenuType::Vault:
            case MenuType::Monitor:
            case MenuType::About:
            case MenuType::Launchpad:
                return g_GlobalSettings.main_menu_bgm;
            case MenuType::QSettings:
                return g_GlobalSettings.settings_menu_bgm;
            case MenuType::QLockscreen:
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
            case MenuType::Vault:
                if(this->vault_lyt == nullptr) {
                    const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();
                    this->vault_lyt = qdesktop::QdVaultLayout::New(qdt);
                    // Cycle K-vaultfix: host MUST be a real IMenuLayout to satisfy
                    // OnMessage()'s static_pointer_cast<IMenuLayout> contract.  See
                    // qd_VaultHostLayout.hpp for the crash chain this fixes.
                    this->vault_host_lyt_ = qdesktop::QdVaultHostLayout::New(this->vault_lyt);
                }
                this->vault_lyt->Navigate("sdmc:/switch/");
                break;
            // K-cycle promoted qdesktop surfaces — all hosts MUST be IMenuLayout
            // subclasses for the same static_pointer_cast<IMenuLayout> reason as
            // Vault above (see qd_VaultHostLayout.hpp crash chain description).
            //
            // Cycle K-noterminal: MenuType::Terminal case removed — feature
            // dropped per creator decision; QdTerminalLayout +
            // QdTerminalHostLayout source files deleted.
            case MenuType::Monitor:
                if(this->monitor_lyt_ == nullptr) {
                    const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();
                    this->monitor_lyt_ = qdesktop::QdMonitorLayout::New(qdt);
                    this->monitor_host_lyt_ = qdesktop::QdMonitorHostLayout::New(this->monitor_lyt_);
                    this->monitor_host_lyt_->LoadSfx();
                }
                break;
            case MenuType::About:
                if(this->about_lyt_ == nullptr) {
                    const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();
                    this->about_lyt_ = qdesktop::QdAboutLayout::New(qdt);
                    this->about_lyt_->LoadSfx();
                }
                break;
            case MenuType::QSettings:
                if(this->qsettings_lyt_ == nullptr) {
                    const qdesktop::QdTheme qdt_s = qdesktop::QdTheme::DarkLiquidGlass();
                    this->qsettings_lyt_ = qdesktop::QdSettingsLayout::New(qdt_s);
                    this->qsettings_lyt_->LoadSfx();
                    TryParseBgmEntry("settings_menu", "QSettings", g_GlobalSettings.settings_menu_bgm);
                }
                break;
            case MenuType::QLockscreen:
                if(this->qlockscreen_lyt_ == nullptr) {
                    const qdesktop::QdTheme qdt_l = qdesktop::QdTheme::DarkLiquidGlass();
                    this->qlockscreen_lyt_ = qdesktop::QdLockscreenLayout::New(qdt_l);
                    this->qlockscreen_lyt_->LoadSfx();
                    TryParseBgmEntry("lockscreen_menu", "QLockscreen", g_GlobalSettings.lockscreen_menu_bgm);
                }
                break;
            // K-cycle Track D: Launchpad — full-screen app-grid (Element + IMenuLayout host).
            // Same pattern as Monitor: host MUST be IMenuLayout for the
            // static_pointer_cast<IMenuLayout> contract in OnMessage().
            case MenuType::Launchpad:
                if(this->launchpad_lyt_ == nullptr) {
                    const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();
                    this->launchpad_lyt_ = qdesktop::QdLaunchpadElement::New(qdt);
                    this->launchpad_host_lyt_ = qdesktop::QdLaunchpadHostLayout::New(this->launchpad_lyt_);
                    this->launchpad_host_lyt_->LoadSfx();
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
        this->vault_lyt = nullptr;
        this->vault_host_lyt_ = nullptr;
        // Cycle K-noterminal: terminal_lyt_/terminal_host_lyt_ removed.
        this->monitor_lyt_ = nullptr;
        this->monitor_host_lyt_ = nullptr;
        this->about_lyt_ = nullptr;
        this->qsettings_lyt_ = nullptr;
        this->qlockscreen_lyt_ = nullptr;
        // K-cycle Track D: Launchpad element + host wrapper.
        this->launchpad_lyt_ = nullptr;
        this->launchpad_host_lyt_ = nullptr;

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
            // Cycle D1: check the terminate sentinel instead of g_MenuApplication
            // (the latter is no longer nulled in Finalize — see D1 rationale).
            if(::g_uMenuTerminating.load(std::memory_order_acquire)) { return; }
            if(g_MenuApplication == nullptr) { return; }

            // Cycle D5 dev toggle: when disabled, the volume re-apply loop
            // is skipped — useful when diagnosing audio glitches that might
            // be caused by repeated SetMusicVolume / Mix_Volume calls.
            if(!::ul::menu::qdesktop::g_dev_volume_policy_enabled.load(std::memory_order_relaxed)) { return; }

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

        // Cycle D1 (revert C3): set the global terminate sentinel BEFORE
        // smi::TerminateMenu(). DO NOT null g_MenuApplication here — that
        // dropped the only strong shared_ptr ref to MenuApplication, ran the
        // destructor mid-frame, freed Layout::render_cbs while Show()'s
        // for-loop was iterating it, and produced a dangling std::function
        // whose operator() jumped to PC=0. That regression caused every
        // crash the user reported in SP4.11 (Home press, game launch,
        // 'self-crash on emuMMC boot' — all the same bug firing on
        // different frames).
        //
        // The render callbacks check ::g_uMenuTerminating and early-return
        // when set, which gives us the same kill-after-Finalize semantics
        // without touching the shared_ptr. Show()'s while-loop will exit
        // cleanly when smi::TerminateMenu()'s process kill arrives, the
        // stack unwinds, and main()'s scope releases g_MenuApplication
        // naturally — no mid-frame destruction.
        ::g_uMenuTerminating.store(true, std::memory_order_release);

        // This might look very ugly, but it is a simple and quick way to exit fast: let uSystem terminate us directly (the OS itself deals with the cleanup)
        // Most importantly, this allows us to exit without cleaning the screen to black when exiting SDL2 stuff (as regular homebrew apps do) so we can do cool transitions with applets/games
        ul::menu::smi::TerminateMenu();
    }

    void MenuApplication::SetBackgroundFade() {
        this->SetFadeAlphaIncrementStepCount(FastFadeAlphaIncrementSteps);

#ifdef QDESKTOP_MODE
        // Cycle D4 (SP4.12): branded fade texture = vertical gradient bg +
        // centered Q glyph in cyan→lavender. Replaces C5's solid-colour fade
        // with something that actually reads as a Q OS-identity transition
        // instead of "screen dimmed to dark blue". The texture is a
        // procedurally generated 1280×720 ABGR8888 surface produced by
        // qdesktop::GetBrandFadeTexture(); first call generates and uploads,
        // every later fade reuses the cached SDL_Texture.
        //
        // Falls back to the C5 solid-colour path if SDL_CreateTexture or
        // SDL_LockTexture fails — that way a transient GPU pool exhaustion
        // never costs the user the entire fade transition.
        //
        // (The C5 rationale still applies for *why* we don't use the upstream
        // ui/Background.png: it reads as a "uLaunch style" flash and breaks
        // the Q OS visual identity. D4 just gives us a branded texture to
        // hand the fade compositor instead of a flat colour.)
        //
        // Cycle D5 dev toggle: when g_dev_brand_fade_enabled is false the
        // user has explicitly chosen the C5 solid-colour fade — skip the
        // texture path entirely (faster, useful for diagnosing whether the
        // fade itself is causing a visual artifact vs. the brand texture).
        const bool brand_fade_on = ::ul::menu::qdesktop::g_dev_brand_fade_enabled.load(
            std::memory_order_relaxed);
        ::pu::sdl2::TextureHandle::Ref brand_fade_tex;
        if (brand_fade_on) {
            brand_fade_tex = ::ul::menu::qdesktop::GetBrandFadeTexture();
        }
        if (brand_fade_tex != nullptr) {
            this->SetFadeBackgroundImage(brand_fade_tex);
        }
        else {
            this->ResetFadeBackgroundImage();
            this->SetFadeBackgroundColor({ 0x0Cu, 0x0Cu, 0x20u, 0xFFu });
        }
#else
        if(!this->HasFadeBackgroundImage()) {
            this->SetFadeBackgroundImage(GetBackgroundTexture());
        }
#endif
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
            case MenuType::Vault: {
                this->vault_lyt->Navigate("sdmc:/switch/");
                this->LoadLayout(this->vault_host_lyt_);
                break;
            }
            // K-cycle promoted qdesktop surfaces. Monitor is an Element+Host
            // pair; About/QSettings/QLockscreen are direct IMenuLayout
            // subclasses that act as their own host.
            // Cycle K-noterminal: MenuType::Terminal LoadLayout case removed.
            case MenuType::Monitor: {
                this->LoadLayout(this->monitor_host_lyt_);
                break;
            }
            case MenuType::About: {
                this->LoadLayout(this->about_lyt_);
                break;
            }
            case MenuType::QSettings: {
                this->LoadLayout(this->qsettings_lyt_);
                break;
            }
            case MenuType::QLockscreen: {
                this->LoadLayout(this->qlockscreen_lyt_);
                break;
            }
            // K-cycle Track D: Launchpad — Element+Host pair, same as Monitor.
            // The Element holds is_open_ = false until Open() is called with a
            // QdDesktopIconsElement to snapshot.  Without that call OnRender
            // early-returns (line 326 in qd_Launchpad.cpp) and the surface
            // shows nothing — the "question mark just shows a black screen"
            // bug from the K-iconsfit cycle.  Snapshot the live MainMenu
            // desktop_icons (creating MainMenu first if it somehow wasn't
            // built — defensive; in practice MainMenu is the boot menu).
            case MenuType::Launchpad: {
#ifdef QDESKTOP_MODE
                this->EnsureLayoutCreated(MenuType::Main);
                if(this->main_menu_lyt && this->launchpad_lyt_) {
                    auto icons = this->main_menu_lyt->GetQdesktopIcons();
                    this->launchpad_lyt_->Open(icons.get());
                }
#endif
                this->LoadLayout(this->launchpad_host_lyt_);
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
        // Fix A (v1.6.12): The Startup BGM is the login chime (75% volume).
        // LoadMenu() calls StartPlayBgm() on every navigation, so without a
        // guard the chime replays whenever the Startup screen is revisited
        // (lockscreen cycle, Settings return, etc.).  Play exactly once per
        // process lifetime.
        static bool g_login_chime_played = false;
        if(this->loaded_menu == MenuType::Startup && g_login_chime_played) {
            return;
        }

        // v1.7.0-stabilize-2 REC-01: arm the gate BEFORE the bgm-null check.
        // Previously the gate was only armed inside `if (bgm.bgm != nullptr)`,
        // which meant a failed audio load on first Startup entry left the gate
        // un-armed; subsequent LoadMenu(Startup) calls would re-trigger the
        // chime path and replay the audio when it finally loaded. Arming the
        // gate first guarantees "exactly once per process lifetime" regardless
        // of whether the audio resource was available at first attempt.
        if(this->loaded_menu == MenuType::Startup) {
            g_login_chime_played = true;
        }

        const auto &bgm = this->GetCurrentMenuBgm();
        if(bgm.bgm != nullptr) {
            pu::audio::SetMusicVolume(ComputeVolume(this->loaded_menu));

            const bool force_one_shot = (this->loaded_menu == MenuType::Startup);
            const int loops = force_one_shot ? 1 : (bgm.bgm_loop ? -1 : 1);
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
            dialog->SetContentColor(this->dialog_cnt_clr);
            dialog->SetOptionColor(this->dialog_opt_clr);
            dialog->SetDialogColor(this->dialog_clr);
            dialog->SetOverColor(this->dialog_over_clr);
        });
    }

}
