#include <ul/fs/fs_Stdio.hpp>
#include <ul/cfg/cfg_Config.hpp>
#include <ul/util/util_Json.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/util/util_Size.hpp>
#include <ul/net/net_Service.hpp>
#include <ul/menu/smi/sf/sf_PrivateService.hpp>
#include <ul/menu/am/am_LibraryAppletUtils.hpp>
#include <ul/menu/am/am_LibnxLibappletWrap.hpp>
#include <cstdarg>

using namespace ul::util::size;

ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace {

    NsApplicationControlData g_TemporaryControlData;

    bool MenuControlEntryLoadFunction(const u64 app_id, std::string &out_name, std::string &out_author, std::string &out_version) {
        if(R_FAILED(nsextGetApplicationControlData(NsApplicationControlSource_Storage, app_id, &g_TemporaryControlData, sizeof(NsApplicationControlData), nullptr))) {
            UL_LOG_WARN("Failed to get application control data for application %016lX", app_id);
            return false;
        }

        NacpLanguageEntry *lang_entry = nullptr;
        if(R_FAILED(nacpGetLanguageEntry(&g_TemporaryControlData.nacp, &lang_entry))) {
            UL_LOG_WARN("Failed to get NACP language entry for application %016lX", app_id);
            return false;
        }

        out_name = lang_entry->name;
        out_author = lang_entry->author;
        out_version = g_TemporaryControlData.nacp.display_version;
        return true;
    }

}

extern "C" {

    AppletType __nx_applet_type = AppletType_LibraryApplet; // Explicitly declare we're a library applet (need to do so for non-hbloader homebrew)
    TimeServiceType __nx_time_service_type = TimeServiceType_Menu;
    u32 __nx_fs_num_sessions = 1;
    // Q OS v0.6.5: SW renderer keeps all pixel data in applet heap — 128MB is ample.
    // Mesa/nouveau GPU buffer allocation path entirely bypassed with SDL_RENDERER_SOFTWARE.
    size_t __nx_heap_size = 128_MB;

    SetSysFirmwareVersion g_FirmwareVersion;

    void __libnx_init_time();

    void __nx_win_init();
    void __nx_win_exit();

    // ---------------------------------------------------------------------------
    // Fine-grained init diagnostic log (v0.6.3)
    //
    // g_InitLog is a static in-memory ring that records every init call result
    // before sdmc is mounted. After fsdevMountSdmc succeeds it is flushed to
    // sdmc:/switch/qos-menu-init.log (append mode). The buffer survives even if
    // a later mandatory init asserts — the flush happens immediately after mount
    // so the log is on-disk before appletInitialize / hidInitialize run.
    // ---------------------------------------------------------------------------
    static char g_InitLog[4096];
    static u32  g_InitLogOffset = 0;

    static void InitLogAppend(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    static void InitLogAppend(const char *fmt, ...) {
        if(g_InitLogOffset >= sizeof(g_InitLog) - 1) {
            return;
        }
        va_list args;
        va_start(args, fmt);
        const int written = vsnprintf(
            g_InitLog + g_InitLogOffset,
            sizeof(g_InitLog) - g_InitLogOffset,
            fmt, args);
        va_end(args);
        if(written > 0) {
            g_InitLogOffset += static_cast<u32>(written);
            if(g_InitLogOffset > sizeof(g_InitLog) - 1) {
                g_InitLogOffset = sizeof(g_InitLog) - 1;
            }
        }
    }

    // Log a mandatory-service result (fatal if non-zero).
    // Appends to g_InitLog BEFORE calling UL_RC_LOG_ASSERT so the entry is
    // always written even when the assert fires and we never return.
    #define INIT_LOG_ASSERT(label, expr) \
        do { \
            const auto _rc_ila = ::ul::res::TransformIntoResult(expr); \
            InitLogAppend("[INIT] " label ": 0x%08X (%04d-%04d)\n", \
                _rc_ila, R_MODULE(_rc_ila) + 2000, R_DESCRIPTION(_rc_ila)); \
            if(R_FAILED(_rc_ila)) { \
                ::ul::OnAssertionFailed(_rc_ila, \
                    label " asserted %04d-%04d/0x%X (FATAL __appInit)", \
                    R_MODULE(_rc_ila) + 2000, R_DESCRIPTION(_rc_ila), \
                    R_VALUE(_rc_ila)); \
            } \
        } while(0)

    // Log a non-critical service result (warn + continue on failure).
    #define INIT_LOG_OPTIONAL(label, expr) \
        do { \
            const auto _rc_ilo = ::ul::res::TransformIntoResult(expr); \
            InitLogAppend("[INIT] " label ": 0x%08X (%04d-%04d)%s\n", \
                _rc_ilo, R_MODULE(_rc_ilo) + 2000, R_DESCRIPTION(_rc_ilo), \
                R_FAILED(_rc_ilo) ? " [SKIPPED]" : ""); \
            if(R_FAILED(_rc_ilo)) { \
                UL_LOG_WARN(label " failed: 0x%X (%04d-%04d) — continuing without it", \
                    _rc_ilo, R_MODULE(_rc_ilo) + 2000, R_DESCRIPTION(_rc_ilo)); \
            } \
        } while(0)

    static void FlushInitLog() {
        FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if(f != nullptr) {
            fwrite(g_InitLog, 1, g_InitLogOffset, f);
            fclose(f);
        }
    }

    void __appInit() {
        InitLogAppend("[INIT] uMenu v0.6.12-debug (SDL render driver enumeration + HINT_RENDER_DRIVER=software) starting\n");
        // --- Mandatory: sm, fs, sdmc ---
        INIT_LOG_ASSERT("smInitialize",     smInitialize());
        INIT_LOG_ASSERT("fsInitialize",     fsInitialize());
        INIT_LOG_ASSERT("fsdevMountSdmc",   fsdevMountSdmc());

        // sdmc is now mounted — flush what we have immediately so the log
        // survives any subsequent fatal assertion.
        FlushInitLog();

        // --- Non-critical: time (degrade gracefully if fw 20 rejects) ---
        INIT_LOG_OPTIONAL("timeInitialize", timeInitialize());
        __libnx_init_time();

        ul::InitializeLogging("qos-menu");
        UL_LOG_INFO("[INIT] uMenu v0.6.12-debug (SDL render driver enumeration + HINT_RENDER_DRIVER=software) starting");
        UL_LOG_INFO("Alive! (uMenu v0.6.12-debug SDL render driver enumeration + HINT_RENDER_DRIVER=software)");

        // --- Non-critical: settings services ---
        INIT_LOG_OPTIONAL("setsysInitialize",           setsysInitialize());
        INIT_LOG_OPTIONAL("setInitialize",              setInitialize());
        INIT_LOG_OPTIONAL("setsysGetFirmwareVersion",   setsysGetFirmwareVersion(&g_FirmwareVersion));
        hosversionSet(MAKEHOSVERSION(g_FirmwareVersion.major, g_FirmwareVersion.minor, g_FirmwareVersion.micro) | BIT(31));

        // --- Mandatory: applet stack, HID, accounts ---
        INIT_LOG_ASSERT("appletInitialize",              appletInitialize());
        INIT_LOG_ASSERT("hidInitialize",                 hidInitialize());
        INIT_LOG_ASSERT("accountInitialize",             accountInitialize(AccountServiceType_System));

        // --- Non-critical: ns, nssu, avm, net, psm ---
        INIT_LOG_OPTIONAL("nsInitialize",           nsInitialize());
        INIT_LOG_OPTIONAL("nssuInitialize",         nssuInitialize());
        INIT_LOG_OPTIONAL("avmInitialize",          avmInitialize());
        INIT_LOG_OPTIONAL("ul::net::Initialize",    ul::net::Initialize());
        INIT_LOG_OPTIONAL("psmInitialize",          psmInitialize());

        // Flush final state so the complete log is on disk before returning.
        FlushInitLog();

        ul::menu::SetControlEntryLoadFunction(MenuControlEntryLoadFunction);
        ul::menu::bt::InitializeBluetoothManager();

        __nx_win_init();
    }

    void __appExit() {
        ul::menu::smi::sf::FinalizePrivateService();

        // Exit RomFs manually, since we also initialized it manually
        romfsExit();

        UL_LOG_INFO("Goodbye!");

        __nx_win_exit();

        ul::menu::bt::FinalizeBluetoothManager();
        setExit();
        setsysExit();
        psmExit();
        ul::net::Finalize();
        avmExit();
        nssuExit();
        nsExit();
        accountExit();

        timeExit();

        hidExit();

        appletExit();

        fsdevUnmountAll();
        fsExit();

        smExit();
    }

}

ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace {

    ul::smi::MenuStartMode g_StartMode;

    void InitializeSettings() {
        UL_LOG_INFO("Initializing settings...");
        g_GlobalSettings = {};

        UL_RC_ASSERT(timeGetDeviceLocationName(&g_GlobalSettings.timezone));

        ul::os::GetAmsConfig(g_GlobalSettings.ams_version, g_GlobalSettings.ams_is_emummc);
        g_GlobalSettings.fw_version = g_FirmwareVersion;

        UL_RC_ASSERT(setsysGetSerialNumber(&g_GlobalSettings.serial_no));
        UL_RC_ASSERT(setsysGetSleepSettings(&g_GlobalSettings.sleep_settings));
        UL_RC_ASSERT(setGetRegionCode(&g_GlobalSettings.region));
        UL_RC_ASSERT(setsysGetPrimaryAlbumStorage(&g_GlobalSettings.album_storage));
        UL_RC_ASSERT(setsysGetNfcEnableFlag(&g_GlobalSettings.nfc_enabled));
        UL_RC_ASSERT(setsysGetUsb30EnableFlag(&g_GlobalSettings.usb30_enabled));
        UL_RC_ASSERT(setsysGetBluetoothEnableFlag(&g_GlobalSettings.bluetooth_enabled));
        UL_RC_ASSERT(setsysGetWirelessLanEnableFlag(&g_GlobalSettings.wireless_lan_enabled));
        UL_RC_ASSERT(setsysGetAutoUpdateEnableFlag(&g_GlobalSettings.auto_update_enabled));
        UL_RC_ASSERT(setsysGetAutomaticApplicationDownloadFlag(&g_GlobalSettings.auto_app_download_enabled));
        UL_RC_ASSERT(setsysGetConsoleInformationUploadFlag(&g_GlobalSettings.console_info_upload_enabled));

        u64 lang_code;
        UL_RC_ASSERT(setGetSystemLanguage(&lang_code));
        UL_RC_ASSERT(setMakeLanguage(lang_code, &g_GlobalSettings.language));

        s32 tmp;
        UL_RC_ASSERT(setGetAvailableLanguageCodes(&tmp, g_GlobalSettings.available_language_codes, ul::os::LanguageNameCount));

        UL_RC_ASSERT(setsysGetDeviceNickname(&g_GlobalSettings.nickname));
        UL_RC_ASSERT(setsysGetBatteryLot(&g_GlobalSettings.battery_lot));
    }

    // v0.6.8-debug: write a single checkpoint line to init log with flush.
    // Macro keeps call sites single-line and grep-friendly.
    #define CHECKPOINT(msg) \
        do { \
            FILE *_cpf = fopen("sdmc:/switch/qos-menu-init.log", "a"); \
            if(_cpf) { fputs("[CHECKPOINT] " msg "\n", _cpf); fflush(_cpf); fclose(_cpf); } \
        } while(0)

    void MainLoop() {
        // Load menu config
        g_GlobalSettings.config = ul::cfg::LoadConfig();

        // Cache active theme, if needed
        if(g_GlobalSettings.system_status.reload_theme_cache) {
            ul::cfg::CacheActiveTheme(g_GlobalSettings.config);
        }

        UL_ASSERT_TRUE(g_GlobalSettings.config.GetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, g_GlobalSettings.cache_hb_takeover_app_id));

        // Load active theme, if set
        std::string active_theme_name;
        Result active_theme_load_rc = ul::ResultSuccess;
        UL_ASSERT_TRUE(g_GlobalSettings.config.GetEntry(ul::cfg::ConfigEntryId::ActiveThemeName, active_theme_name));
        if(!active_theme_name.empty()) {
            const auto rc = ul::cfg::TryLoadTheme(active_theme_name, g_GlobalSettings.active_theme);
            if(R_SUCCEEDED(rc)) {
                ul::cfg::EnsureCacheActiveTheme(g_GlobalSettings.config);
            }
            else {
                g_GlobalSettings.active_theme = {};
                UL_LOG_WARN("Unable to load active theme '%s': %s, resetting to default theme...", active_theme_name.c_str(), ul::util::FormatResultDisplay(rc).c_str());
                UL_ASSERT_TRUE(g_GlobalSettings.config.SetEntry(ul::cfg::ConfigEntryId::ActiveThemeName, g_GlobalSettings.active_theme.name));
                g_GlobalSettings.SaveConfig();
                ul::cfg::RemoveActiveThemeCache();
                active_theme_load_rc = rc;
            }
        }
        else {
            UL_LOG_INFO("No active theme set...");
        }

        // List added/removed/in verify applications
    
        UL_LOG_INFO("Added app count: %d", g_GlobalSettings.system_status.last_added_app_count);
        if(g_GlobalSettings.system_status.last_added_app_count > 0) {
            auto app_buf = new u64[g_GlobalSettings.system_status.last_added_app_count]();
            UL_RC_ASSERT(ul::menu::smi::ListAddedApplications(g_GlobalSettings.system_status.last_added_app_count, app_buf));
            for(u32 i = 0; i < g_GlobalSettings.system_status.last_added_app_count; i++) {
                UL_LOG_INFO("> Added app: 0x%016lX", app_buf[i]);
                g_GlobalSettings.added_app_ids.push_back(app_buf[i]);
            }
            delete[] app_buf;
        }

        UL_LOG_INFO("Deleted app count: %d", g_GlobalSettings.system_status.last_deleted_app_count);
        if(g_GlobalSettings.system_status.last_deleted_app_count > 0) {
            auto app_buf = new u64[g_GlobalSettings.system_status.last_deleted_app_count]();
            UL_RC_ASSERT(ul::menu::smi::ListDeletedApplications(g_GlobalSettings.system_status.last_deleted_app_count, app_buf));
            for(u32 i = 0; i < g_GlobalSettings.system_status.last_deleted_app_count; i++) {
                UL_LOG_INFO("> Deleted app: 0x%016lX", app_buf[i]);

                if(g_GlobalSettings.cache_hb_takeover_app_id == app_buf[i]) {
                    g_GlobalSettings.ResetHomebrewTakeoverApplicationId();
                }

                g_GlobalSettings.deleted_app_ids.push_back(app_buf[i]);
            }
            delete[] app_buf;
        }

        UL_LOG_INFO("In verify app count: %d", g_GlobalSettings.system_status.in_verify_app_count);
        if(g_GlobalSettings.system_status.in_verify_app_count > 0) {
            auto app_buf = new u64[g_GlobalSettings.system_status.in_verify_app_count]();
            UL_RC_ASSERT(ul::menu::smi::ListInVerifyApplications(g_GlobalSettings.system_status.in_verify_app_count, app_buf));
            for(u32 i = 0; i < g_GlobalSettings.system_status.in_verify_app_count; i++) {
                UL_LOG_INFO("> App being verified: 0x%016lX", app_buf[i]);
                g_GlobalSettings.in_verify_app_ids.push_back(app_buf[i]);
            }
            delete[] app_buf;
        }

        // Get system language and load translations (default one if not present)
        ul::cfg::LoadLanguageJsons(ul::MenuLanguagesPath, g_GlobalSettings.main_lang, g_GlobalSettings.default_lang);

        // Q OS v0.6.7: SW renderer — Mesa/nouveau link-time deps (-lEGL -lGLESv2 -lglapi
        // -ldrm_nouveau) removed from Makefile entirely. On fw 20.0.0 the 14MB applet pool
        // cannot sustain nouveau_bo_new for GPU render targets, and those libs were being
        // loaded into the process at NRO load time regardless of this runtime flag.
        // SDL_RENDERER_SOFTWARE keeps all composition in applet heap; no GL/Mesa involvement.
        auto renderer_opts = pu::ui::render::RendererInitOptions(SDL_INIT_EVERYTHING, pu::ui::render::RendererSoftwareFlags);
        renderer_opts.SetPlServiceType(PlServiceType_User);
        renderer_opts.SetInputPlayerCount(8);
        renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No2);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No3);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No4);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No5);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No6);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No7);
        renderer_opts.AddInputNpadIdType(HidNpadIdType_No8);

        const auto default_font_path = ul::menu::ui::TryGetActiveThemeResource("ui/Font.ttf");
        if(!default_font_path.empty()) {
            renderer_opts.AddDefaultFontPath(default_font_path);
        }
        else {
            renderer_opts.AddDefaultSharedFont(PlSharedFontType_Standard);
            renderer_opts.AddDefaultSharedFont(PlSharedFontType_ChineseSimplified);
            renderer_opts.AddDefaultSharedFont(PlSharedFontType_ExtChineseSimplified);
            renderer_opts.AddDefaultSharedFont(PlSharedFontType_ChineseTraditional);
            renderer_opts.AddDefaultSharedFont(PlSharedFontType_KO);
        }
        renderer_opts.AddDefaultSharedFont(PlSharedFontType_NintendoExt);
        // Q OS v0.6.5: UseImage(ImgAllFlags) removed — PNG/JPEG/WEBP decoder init triggers
        // additional texture-related allocations. SW renderer loads images as surfaces
        // via SDL_image without touching Mesa pools. Basic PNG support is still available.
        renderer_opts.UseImage(IMG_INIT_PNG);

        CHECKPOINT("post-__appInit, entering Plutonium::Initialize");
        auto renderer = pu::ui::render::Renderer::New(renderer_opts);
        if(renderer == nullptr) {
            const char *sdl_err = SDL_GetError();
            FILE *_ef = fopen("sdmc:/switch/qos-menu-init.log", "a");
            if(_ef) { fprintf(_ef, "[CHECKPOINT] SDL_CreateRenderer FAILED: %s\n", sdl_err ? sdl_err : "(null)"); fclose(_ef); }
            UL_LOG_WARN("SDL renderer creation failed: %s", sdl_err ? sdl_err : "(null)");
        }
        CHECKPOINT("post-Plutonium::Initialize, creating Application");
        // v0.6.12-debug: log renderer backend info immediately after renderer creation,
        // before MenuApplication::New, to confirm SDL renderer state entering the crash window.
        // Plutonium Renderer::Initialize checkpoints are written to qos-menu-init.log by PU_INIT_LOG.
        {
            SDL_Renderer *_sdl_r = pu::ui::render::GetMainRenderer();
            if(_sdl_r != nullptr) {
                SDL_RendererInfo _rinfo;
                if(SDL_GetRendererInfo(_sdl_r, &_rinfo) == 0) {
                    FILE *_rf = fopen("sdmc:/switch/qos-menu-init.log", "a");
                    if(_rf) {
                        fprintf(_rf, "[RENDERER] name=%s flags=0x%08X num_texture_formats=%u\n",
                                _rinfo.name ? _rinfo.name : "(null)",
                                (unsigned)_rinfo.flags,
                                (unsigned)_rinfo.num_texture_formats);
                        fclose(_rf);
                    }
                }
            } else {
                FILE *_rf = fopen("sdmc:/switch/qos-menu-init.log", "a");
                if(_rf) { fputs("[RENDERER] GetMainRenderer returned NULL before MenuApplication::New\n", _rf); fclose(_rf); }
            }
        }
        CHECKPOINT("main: before MenuApplication::New");
        g_MenuApplication = ul::menu::ui::MenuApplication::New(renderer);
        CHECKPOINT("main: after MenuApplication::New, before audio::Initialize");

        UL_ASSERT_TRUE(pu::audio::Initialize(MIX_INIT_MP3));
        CHECKPOINT("main: after audio::Initialize, before MenuApp::Initialize");

        g_MenuApplication->Initialize(g_StartMode);
        CHECKPOINT("main: after MenuApp::Initialize, before SMI register");

        ul::menu::ui::RegisterMenuOnMessageDetect();
        CHECKPOINT("main: after SMI register, before InitializePrivateService");

        UL_RC_ASSERT(ul::menu::smi::sf::InitializePrivateService());
        CHECKPOINT("main: after InitializePrivateService, before MenuApp::Load");

        UL_RC_ASSERT(g_MenuApplication->Load());
        CHECKPOINT("post-Application::Load, entering Show/render loop");

        // Initialize uSystem message handling (need to do it after Load so that the message handlers are registered)
        // UL_RC_ASSERT(ul::menu::smi::sf::InitializePrivateService()); 

        if(R_FAILED(active_theme_load_rc)) {
            g_MenuApplication->NotifyActiveThemeLoadFailure(active_theme_load_rc);
        }
        if(g_GlobalSettings.active_theme.IsValid() && g_GlobalSettings.active_theme.IsOutdated()) {
            g_MenuApplication->NotifyActiveThemeOutdated();
        }

        if(g_StartMode == ul::smi::MenuStartMode::StartupMenuPostBoot) {
            g_MenuApplication->ShowWithFadeIn();
        }
        else {
            g_MenuApplication->Show();
        }
        CHECKPOINT("post-first-render");
    }

}

// uMenu procedure: read sent storages, initialize RomFs (externally), load config and other stuff, finally create the renderer and start the UI

int main() {
    // v0.6.8-debug: 2-second quiesce delay after __appInit.
    // Gives Horizon time to fully settle the applet pool before any Mesa/SDL allocation,
    // and lets the user see the last init log entry on a crash.
    svcSleepThread(2'000'000'000);

    InitializeSettings();

    UL_RC_ASSERT(ul::menu::am::ReadStartMode(g_StartMode));
    UL_ASSERT_TRUE(g_StartMode != ul::smi::MenuStartMode::Invalid);

    // Information sent as an extra storage to uMenu
    UL_RC_ASSERT(ul::menu::am::ReadFromInputStorage(&g_GlobalSettings.system_status, sizeof(g_GlobalSettings.system_status)));
    g_GlobalSettings.initial_last_menu_index = g_GlobalSettings.system_status.last_menu_index;
    g_GlobalSettings.initial_last_menu_fs_path = g_GlobalSettings.system_status.last_menu_fs_path;
    UL_LOG_INFO("Start mode: %d, last path: '%s', last fs path: '%s', last menu index: %d", (u32)g_StartMode, g_GlobalSettings.system_status.last_menu_path, g_GlobalSettings.system_status.last_menu_fs_path, g_GlobalSettings.system_status.last_menu_index);

    // Check if our RomFs data exists...
    if(!ul::fs::ExistsFile(ul::MenuRomfsFile)) {
        UL_RC_ASSERT(ul::ResultRomfsNotFound);
    }

    // Try to mount it
    UL_RC_ASSERT(romfsMountFromFsdev(ul::MenuRomfsFile, 0, "romfs"));

    // Register handlers for HOME button press detection
    ul::menu::am::RegisterLibnxLibappletHomeButtonDetection();
    ul::menu::ui::QuickMenu::RegisterHomeButtonDetection();

    MainLoop();

    return 0;
}
