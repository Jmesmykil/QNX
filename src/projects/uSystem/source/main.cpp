#include <ul/system/ecs/ecs_ExternalContent.hpp>
#include <ul/system/la/la_Open.hpp>
#include <ul/system/sf/sf_IpcManager.hpp>
#include <ul/system/smi/smi_SystemProtocol.hpp>
#include <ul/system/sys/sys_SystemApplet.hpp>
#include <ul/system/system_Message.hpp>
#include <ul/system/app/app_ControlCache.hpp>
#include <ul/system/overlay/overlay_TestLayer.hpp>
#include <ul/system/overlay/overlay_Actions.hpp>
#include <ul/system/led/led_ComputeIndicator.hpp>
#include <ul/cfg/cfg_Config.hpp>
#include <ul/menu/menu_Entries.hpp>
#include <ul/menu/menu_Cache.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <ul/os/os_Applications.hpp>
#include <ul/os/os_System.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/util/util_Size.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <queue>
#include <unordered_set>

extern "C" {

    extern u32 __nx_applet_type;
    extern u32 __nx_fs_num_sessions;

    // So that libstratosphere doesn't redefine them as invalid

    void *__libnx_alloc(size_t size) {
        return malloc(size);
    }

    void *__libnx_aligned_alloc(size_t align, size_t size) {
        return aligned_alloc(align, size);
    }

    void __libnx_free(void *ptr) {
        return free(ptr);
    }

}

using namespace ul::util::size;
using namespace ul::system;

// Note: these are global since they are accessed by IPC

ul::RecursiveMutex g_MenuMessageQueueLock;
std::queue<ul::smi::MenuMessageContext> *g_MenuMessageQueue;

namespace {

    constexpr const char ChooseHomebrewCaption[] = "Choose a homebrew for uMenu";

    struct ApplicationVerifyContext {
        static constexpr size_t ThreadStackSize = 64_KB;

        u64 app_id;
        Thread thread;
        alignas(ams::os::ThreadStackAlignment) u8 thread_stack[ThreadStackSize];
        bool finished;

        ApplicationVerifyContext(const u64 app_id) : app_id(app_id), thread(), thread_stack(), finished(false) {}
    };

    enum class ActionType : u32 {
        LaunchApplication,
        LaunchHomebrewLibraryApplet,
        LaunchHomebrewApplication,
        OpenWebPage,
        OpenAlbum,
        RestartMenu,
        OpenUserPage,
        OpenMiiEdit,
        OpenAddUser,
        OpenNetConnect,
        OpenCabinet,
        TerminateMenu,
        OpenControllerKeyRemapping,
    };

    struct Action {
        ActionType type;
        union {
            struct {
                u64 app_id;
            } launch_application;
            struct {
                ul::loader::TargetInput target_input;
                bool choose_mode;
            } launch_loader;
            struct {
                u64 app_id;
                ul::loader::TargetInput app_target_input;
            } launch_homebrew_application;
            struct {
                WebCommonConfig cfg;
            } open_web_page;
            struct {
                NfpLaStartParamTypeForAmiiboSettings type;
            } open_cabinet;
            struct {
                u32 npad_style_set;
                HidNpadJoyHoldType hold_type;
            } open_controller_key_remapping;
        };
    };

    // Global state variables

    std::vector<Action> g_ActionQueue;

    AppletOperationMode g_OperationMode;
    bool g_ExpectsLoaderChooseOutput = false;
    ul::loader::TargetInput g_LastHomebrewApplicationLaunchTarget = {};
    bool g_LastLibraryAppletLaunchedNotMenu = false;
    // 2026-05-06 AMS-1.11 clean-exit fix: track which program_id has an
    // open ECS RegisterExternalContent session against AMS Loader.  Every
    // ecs::RegisterLaunchAs* opens IPC cmd 65000 and allocates one slot in
    // AMS's 6-slot ServerManager pool — the matching cmd 65001 release
    // call was missing.  After 6 launches the pool exhausted, uSystem
    // aborted at sf_hipc_server_session_manager.hpp:109 with
    // 2011-0102 ResultOutOfSessionMemory.  See
    // docs/research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md.
    //
    // 0 = no current registration.  Any non-zero value indicates that
    // ecs::UnregisterExternalContent must be called before the next
    // RegisterLaunchAs* on the same or different program_id.
    u64 g_PendingEcsProgramId = 0;
    bool g_NextMenuLaunchAtStartup = false;
    bool g_NextMenuLaunchAtSettings = false;
    bool g_MenuRestartReloadThemeCache = false;
    bool g_IsLibraryAppletActive = false;

    // v3.1 Phase 2 Task 3 Slice 4 (architectural inversion): carry-over for
    // the LaunchHomebrewWindowedLibraryApplet SMI command.  Originally Slice 2
    // had the server LAUNCH the BG-indirect applet here and push back the
    // IndirectLayerConsumerHandle.  HW test 2026-05-19 proved this hangs:
    // uSystem is in background after handoff to uMenu, and AM rejects
    // appletCreateLibraryApplet from background system applets — the IPC
    // never returned, uMenu's SMI blocked indefinitely → black screen.
    //
    // Slice 4 inverts: server-side does ONLY ecs::RegisterExternalContent
    // (the ldr:shell hijack uMenu can't do itself), then pushes back the
    // resolved AppletId so uMenu can call appletCreateLibraryAppletSelf
    // (libnx applet.h:1183, the LibraryApplet-caller variant) locally.
    // uMenu is the FOREGROUND applet so AM accepts the call.
    u32 g_PendingWindowedAppletId = 0;

    NX_INLINE bool IsMenuRunning() {
        return la::IsActive() && !g_LastLibraryAppletLaunchedNotMenu;
    }

    NX_INLINE bool WasLoaderOpenedAsApplication() {
        return g_LastHomebrewApplicationLaunchTarget.IsValid();
    }

    AccountUid g_SelectedUser = {};

    ul::RecursiveMutex g_ConfigLock;
    ul::cfg::Config g_Config;

    std::atomic_bool g_AmsIsEmuMMC = false;
    bool g_WarnedAboutOutdatedTheme = false;

    ul::smi::SystemStatus g_CurrentStatus = {};

    ul::system::app::ApplicationNacpMisc g_LaunchHomebrewApplicationNacpMisc;

    char g_CurrentMenuFsPath[FS_MAX_PATH] = {};
    char g_CurrentMenuPath[FS_MAX_PATH] = {};
    u32 g_CurrentMenuIndex = 0;

    constexpr size_t VerifyWorkBufferSize = 0x100000;
    constexpr size_t VerifyStepWaitTimeNs = 100'000;

    std::vector<ApplicationVerifyContext> *g_ApplicationVerifyContexts;

    ul::RecursiveMutex g_CurrentRecordsLock;
    std::vector<NsExtApplicationRecord> g_CurrentRecords;

    ul::RecursiveMutex g_LastDeletedApplicationsLock;
    std::vector<u64> g_LastDeletedApplications;

    ul::RecursiveMutex g_LastAddedApplicationsLock;
    std::vector<u64> g_LastAddedApplications;

    Thread g_EventManagerThread;
    constexpr size_t EventManagerThreadStackSize = 64_KB;

    // USB types and globals

    enum class UsbMode : u32 {
        Invalid,
        Rgba,
        Jpeg
    };

    struct UsbPacketHeader {
        UsbMode mode;
        union {
            struct {
            } rgba;
            struct {
                u32 size;
            } jpeg;
        };
    };
    static_assert(sizeof(UsbPacketHeader) == 0x8);

    constexpr size_t PlainRgbaScreenBufferSize = 1280 * 720 * sizeof(u32);
    constexpr size_t UsbPacketSize = sizeof(UsbPacketHeader) + PlainRgbaScreenBufferSize;

    alignas(ams::os::ThreadStackAlignment) constinit u8 g_UsbViewerReadThreadStack[16_KB];
    Thread g_UsbViewerReadThread;
    alignas(ams::os::ThreadStackAlignment) constinit u8 g_UsbViewerWriteThreadStack[16_KB];
    Thread g_UsbViewerWriteThread;
    RwLock g_UsbRwLock;
    UsbPacketHeader *g_UsbViewerBuffer = nullptr;
    u8 *g_UsbViewerBufferDataOffset = nullptr;

    // Heap definitions

    // libstratosphere heap: used for malloc/free/new/delete and everything using them
    // We specially need to take into account app icon/NACP caching (it's around 0.14MB per app) plus thread stack buffers, vectors and so on
    // SP4.15.1 hotfix: revert cycleE0's untested 20→10 MB tightening.  At 10 MB
    // the boot path crashes (black screen on hardware test 2026-04-25).  20 MB
    // matches the SP4.13 hardware-validated baseline and is what every shipped
    // build through SP4.14 used.  Re-tune only with hardware verification.
    constexpr size_t LibstratosphereHeapSize = 20_MB;
    alignas(ams::os::MemoryPageSize) constinit u8 g_LibstratosphereHeap[LibstratosphereHeapSize];

    // libnx heap: used for internal malloc_r/etc called by stdlib stuff, thus a modest size is enough
    // v3.1 Phase 2 Slice T1 v2: bumped 1_MB → 32_MB to fit the Tesla-overlay
    // framebuffer's shadow linear buffer + binder/parcel allocations.
    //
    // Math: RGBA_4444 @ 1280×720 = 1.8 MB shadow + 2×1.8 MB swap buffers =
    // ~5.4 MB total for the framebuffer.  Tesla recommends 64 MB headroom
    // for the libnx heap on overlay-style sysmodules but our overlay is
    // simpler (single color rect), so 32 MB is comfortable.
    //
    // History: 1_MB → 16_MB on first iteration didn't fix the rc=0x559
    // OOM — turned out the real bug was __nx_vi_layer_id global not being
    // set, which caused viCreateLayer to make a SECOND managed layer and
    // double parcel/binder allocations.  Fixed in overlay_TestLayer.cpp.
    constexpr size_t LibnxHeapSize = 32_MB;
    alignas(ams::os::MemoryPageSize) constinit u8 g_LibnxHeap[LibnxHeapSize];

    // BOOT-SPEED (2026-06-13): tick captured at the start of app-record init.
    // Logged at the first uMenu launch as "[BOOT-SPEED] ... -> first uMenu
    // launch: N ms" so the cache-defer win is measurable from the log without
    // needing wall-clock timestamps on every line.
    u64 g_BootInitTick = 0;

    // DEV-ONLY: set once at startup by Initialize() if sdmc:/ulaunch/debug.flag
    // exists (the same flag uMenu uses for dev mode).  When false every dev-
    // path below is compiled-in dead code with negligible overhead.
    bool g_DevMode = false;

    // ---------------------------------------------------------------------------
    // Non-blocking NRO terminate state (Fix B — HOME-over-NRO hang).
    //
    // When HandleHomeButton fires over an NRO (la::IsActive && !IsMenuRunning)
    // it can no longer call the blocking la::Terminate() because that parks the
    // MainLoop inside appletHolderJoin for up to 2 s (or forever if the NRO
    // ignores RequestExit, as sphaira does).
    //
    // Instead HandleHomeButton:
    //   1. Calls sys::SetForeground()          — reclaim foreground (Fix A, kept)
    //   2. Calls la::RequestExitNonBlocking()  — sends cmd 20, returns immediately
    //   3. Sets g_TerminatingNro = true + records a 500 ms deadline tick
    //   4. Returns immediately
    //
    // The MainLoop polls g_TerminatingNro each iteration.  When
    // la::CheckTerminated() is true (or the deadline elapses) it calls
    // la::ForceTerminateNow() (Fix C: 500 ms hard-kill), clears the flag, and
    // calls LaunchMenu(MainMenu).  Fix D: failures are logged, never asserted.
    // ---------------------------------------------------------------------------
    bool g_TerminatingNro = false;
    u64  g_TerminatingNroDeadlineTick = 0;

    // ---------------------------------------------------------------------------
    // Non-blocking APPLICATION terminate state (Fix 1 — HOME-over-APP black-screen).
    //
    // When HandleHomeButton fires over a running APPLICATION (app::IsActive &&
    // app::HasForeground) the old code left the application SUSPENDED and called
    // LaunchMenu(MenuApplicationSuspended) — uMenu mode 3 — which caused a black-
    // screen.  The fix terminates the application non-blockingly (mirroring the
    // NRO pattern above) then launches uMenu in NORMAL mode (MainMenu / mode 2).
    //
    // HandleHomeButton (app:: branch):
    //   1. Calls sys::SetForeground()             — reclaim foreground
    //   2. Calls app::RequestExitNonBlocking()    — sends RequestExit, returns immediately
    //   3. Sets g_TerminatingApp = true + records a 500 ms deadline tick
    //   4. Returns immediately
    //
    // The MainLoop polls g_TerminatingApp each iteration.  When
    // app::CheckTerminated() is true (or the deadline elapses) it calls
    // app::ForceTerminateNow(), clears the flag, and calls LaunchMenu(MainMenu).
    // Failures are logged, never asserted.
    // ---------------------------------------------------------------------------
    bool g_TerminatingApp = false;
    u64  g_TerminatingAppDeadlineTick = 0;

    // ---------------------------------------------------------------------------
    // SD trace log helper (Fix 1 — dev-only, gated on g_DevMode).
    //
    // Appends one tagged line to sdmc:/ulaunch/home_trace.log using
    // ul::fs::WriteFile with overwrite=false (append mode, "ab+" fopen flag).
    // Each line: "[HOME_TRACE tick=<armGetSystemTick>] <tag>\n"
    // Readable via FTP even when uMenu is dead.  Zero overhead when g_DevMode
    // is false (call sites are inside `if(g_DevMode)` guards).
    // ---------------------------------------------------------------------------
    constexpr const char kHomeTraceLog[] = "sdmc:/ulaunch/home_trace.log";

    inline void HomeTrace(const char *tag) {
        // Build the line in a small stack buffer — no heap allocation.
        char buf[128];
        const int len = ::snprintf(buf, sizeof(buf), "[HOME_TRACE tick=%lu] %s\n",
            static_cast<unsigned long>(armGetSystemTick()), tag);
        if(len > 0) {
            ul::fs::WriteFile(kHomeTraceLog, buf, static_cast<size_t>(len), /*overwrite=*/false);
        }
    }

    // ---------------------------------------------------------------------------
    // SELF-HEAL WATCHDOG — uSystem side (2026-06-20)
    //
    // uMenu sends SystemMessage::Heartbeat every ~30s (≈1800 frames).
    // g_LastMenuHeartbeatTick records the armGetSystemTick() at which the
    // last heartbeat arrived.  0 = no heartbeat received yet (uMenu hasn't
    // sent one; skip watchdog until first beat to avoid false-positives at boot).
    //
    // MainLoop checks: if IsMenuRunning() && g_LastMenuHeartbeatTick != 0
    // && (now - g_LastMenuHeartbeatTick) > kWatchdogStaleTicks (~30s), then
    // uMenu has been silently hung.  Force-recover (cmd_resetmenu-style) and
    // increment g_WatchdogFailCount.  After kWatchdogMaxFails consecutive
    // failures (no recovery within the next window), escalate to
    // fatalThrow(0xCAFE) → Atmosphère auto-reboot (~50s) → clean CFW boot.
    //
    // "Consecutive" means: on a successful recovery the new uMenu starts
    // sending heartbeats again; g_WatchdogFailCount resets to 0 on the next
    // beat received.  If the relaunch itself fails (uMenu stays black) and
    // we trip the watchdog again before a beat arrives, the count grows until
    // the fatal threshold.
    //
    // Timing: kWatchdogStaleTicks = 30s × 19,200,000 ticks/s = 576,000,000 ticks.
    // ---------------------------------------------------------------------------
    u64  g_LastMenuHeartbeatTick = 0;
    u32  g_WatchdogFailCount     = 0;
    constexpr u32  kWatchdogMaxFails   = 3;
    constexpr u64  kWatchdogStaleTicks = 576'000'000ul;     // 30s in 19.2 MHz ticks (30 × 19 200 000)

    // BOOT-SPEED: kept (gated off at the call site) for easy re-enable; marked
    // maybe_unused so the now-callerless "Test" telemetry doesn't fail -Werror.
    [[maybe_unused]] void DebugLogApplicationParameters() {
        ul::ScopedLock lock(g_CurrentRecordsLock);

        std::vector<u64> app_ids;
        for(auto &rec: g_CurrentRecords) {
            app_ids.push_back(rec.id);
        }

        auto app_views = new NsApplicationView[app_ids.size()];
        UL_ON_SCOPE_EXIT({ delete[] app_views; });
        UL_RC_ASSERT(nsGetApplicationView(app_views, app_ids.data(), app_ids.size()));

        for(u32 i = 0; i < app_ids.size(); i++) {
            auto &view = app_views[i];
            auto &rec = g_CurrentRecords[i];

            // Test: qlaunch checks these flags on apps
            uintptr_t a1 = (uintptr_t)std::addressof(view);
            u8 flags1[12] = {};
            flags1[0] = *(u32*)(a1 + 12) & 1;
            flags1[1] = (*(u8 *)(a1 + 12) >> 1) & 1;
            flags1[2] = (*(u8 *)(a1 + 12) >> 4) & 1;
            flags1[3] = (*(u8 *)(a1 + 12) >> 5) & 1;
            flags1[4] = (*(u8 *)(a1 + 12) >> 6) & 1;
            flags1[5] = *(u8 *)(a1 + 12) >> 7;
            flags1[6] = *(u8 *)(a1 + 13) & 1;
            flags1[7] = (*(u8 *)(a1 + 13) >> 1) & 1;
            flags1[8] = (*(u8 *)(a1 + 13) >> 2) & 1;
            flags1[9] = (*(u8 *)(a1 + 13) >> 5) & 1;
            flags1[10] = (*(u32 *)(a1 + 12) & 0x4C000) != 0;
            flags1[11] = *(u8 *)(a1 + 13) >> 7;
            u8 flags2[6] = {};
            flags2[0] = *(u8 *)(a1 + 14) >> 7;
            flags2[1] = *(u8 *)(a1 + 14) & 1;
            flags2[2] = (*(u8 *)(a1 + 14) >> 1) & 1;
            flags2[3] = ((*(u32 *)(a1 + 12) & 0x4C000) != 0) && (*(u8 *)(a1 + 36) == 5); // is waiting commit + other fn
            flags2[4] = (*(u8 *)(a1 + 14) >> 5) & 1;
            flags2[5] = (*(u8 *)(a1 + 14) >> 6) & 1;
            u8 flags3[5] = {};
            flags3[0] = (u8)(*(NsApplicationView*)a1).unk_x24;
            flags3[1] = (u8)((*(NsApplicationView*)a1).unk_x24 >> 8);
            flags3[2] = (*(NsApplicationView*)a1).unk_x26[0];
            flags3[3] = (*(NsApplicationView*)a1).unk_x45[0];
            flags3[4] = (*(NsApplicationView*)a1).unk_x44;

            std::string flagbits;
            for(u32 i = 0; i < 12; i++) {
                if(flags1[i] != 0) {
                    flagbits += "1";
                }
                else {
                    flagbits += "0";
                }
            }

            flagbits += "-";
            for(u32 i = 0; i < 6; i++) {
                if(flags2[i] != 0) {
                    flagbits += "1";
                }
                else {
                    flagbits += "0";
                }
            }

            flagbits += "-";
            for(u32 i = 0; i < 5; i++) {
                flagbits += std::to_string((int)flags3[i]);
                if(i < 4) {
                    flagbits += ":";
                }
            }

            bool is_update_requested = false;
            u32 tmp = 0;
            auto rc = nsIsApplicationUpdateRequested(rec.id, &is_update_requested, &tmp);
            if(R_FAILED(rc)) {
                flagbits += "-" + ul::util::FormatResultDisplay(rc);
            }
            else {
                flagbits += "-";
                flagbits += is_update_requested ? "1" : "0";
            }

            rc = nsCheckApplicationLaunchVersion(rec.id);
            flagbits += "-" + ul::util::FormatResultDisplay(rc);

            UL_LOG_INFO("[!Flags] %s -> %s", ul::util::FormatProgramId(rec.id).c_str(), flagbits.c_str());
        }
    }

}

namespace {

    void LoadConfig() {
        ul::ScopedLock lk(g_ConfigLock);
        g_Config = ul::cfg::LoadConfig();

        u64 menu_program_id;
        UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::MenuTakeoverProgramId, menu_program_id));
        la::SetMenuProgramId(menu_program_id);
    }

    void PushMenuMessageContext(const ul::smi::MenuMessageContext msg_ctx) {
        ul::ScopedLock lk(g_MenuMessageQueueLock);
        g_MenuMessageQueue->push(msg_ctx);
    }

    void PushSimpleMenuMessage(const ul::smi::MenuMessage msg) {
        ul::ScopedLock lk(g_MenuMessageQueueLock);
        const ul::smi::MenuMessageContext msg_ctx = {
            .msg = msg
        };
        g_MenuMessageQueue->push(msg_ctx);
    }

    void NotifyApplicationDeleted(const u64 app_id) {
        ul::ScopedLock lk(g_ConfigLock);
        ul::ScopedLock lk2(g_LastDeletedApplicationsLock);
        u64 takeover_app_id;
        if(g_Config.GetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, takeover_app_id)) {
            if(takeover_app_id == app_id) {
                g_Config.SetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, ul::os::InvalidApplicationId);
                ul::cfg::SaveConfig(g_Config);
            }
        }
        g_LastDeletedApplications.push_back(app_id);
    }

    void UpdateStatus() {
        ul::ScopedLock lock(g_LastAddedApplicationsLock);
        ul::ScopedLock lock2(g_LastDeletedApplicationsLock);
        g_CurrentStatus = {
            .selected_user = g_SelectedUser,
            .last_menu_index = g_CurrentMenuIndex,
            .warned_about_outdated_theme = g_WarnedAboutOutdatedTheme,
            .last_added_app_count = (u32)g_LastAddedApplications.size(),
            .last_deleted_app_count = (u32)g_LastDeletedApplications.size(),
            .in_verify_app_count = (u32)g_ApplicationVerifyContexts->size()
        };

        if(g_MenuRestartReloadThemeCache) {
            g_CurrentStatus.reload_theme_cache = true;
            g_MenuRestartReloadThemeCache = false;
        }

        ul::util::CopyToStringBuffer(g_CurrentStatus.last_menu_fs_path, g_CurrentMenuFsPath);
        ul::util::CopyToStringBuffer(g_CurrentStatus.last_menu_path, g_CurrentMenuPath);

        if(app::IsActive()) {
            if(WasLoaderOpenedAsApplication()) {
                // Homebrew
                g_CurrentStatus.suspended_hb_target_ipt = g_LastHomebrewApplicationLaunchTarget;
            }
            else {
                // Regular title
                g_CurrentStatus.suspended_app_id = app::GetId();
            }
        }
    }

    void HandleSleep() {
        // DEV backstop: NEVER sleep when g_DevMode is set. All three sleep
        // triggers route here — Unk_Sleep (general channel), the power button
        // (DetectShortPressingPowerButton), and idle auto-sleep
        // (AppletMessage::AutoPowerDown). Sleeping drops WiFi and strands every
        // remote-recovery path (sys-ftpd :5000, uMenu debug :6010), which forces
        // a physical RCM re-inject. appletSetAutoSleepDisabled only suppresses the
        // idle timer and is not honored across applet transitions, so this is the
        // hard guarantee: in dev the console stays awake and remotely recoverable.
        // Never ships (g_DevMode = sdmc:/ulaunch/debug.flag presence only).
        if(g_DevMode) {
            UL_LOG_INFO("[DEV] HandleSleep suppressed — staying awake so remote recovery is always possible");
            return;
        }
        appletStartSleepSequence(true);
    }

    void LocateApplicationAndSpecialEntries(const std::string &path, std::vector<ul::menu::Entry> &out_app_entries, u32 &rem_special_entry_mask) {
        auto entries = ul::menu::LoadEntries(path);
        for(const auto &entry: entries) {
            if(entry.Is<ul::menu::EntryType::Application>()) {
                out_app_entries.push_back(entry);
            }
            else if(entry.IsSpecial()) {
                // This special entry exists, remove from remaining
                rem_special_entry_mask &= ~BITL(static_cast<u32>(entry.type));
            }
            else if(entry.Is<ul::menu::EntryType::Folder>()) {
                LocateApplicationAndSpecialEntries(ul::fs::JoinPath(path, entry.folder_info.fs_name), out_app_entries, rem_special_entry_mask);
            }
        }
    }

    void CheckApplicationRecordChanges() {
        ul::ScopedLock lock(g_CurrentRecordsLock);
        ul::ScopedLock lock2(g_LastDeletedApplicationsLock);
        ul::ScopedLock lock3(g_LastAddedApplicationsLock);

        g_LastDeletedApplications.clear();
        g_LastAddedApplications.clear();

        const auto menu_path = ul::menu::MakeMenuPath(g_AmsIsEmuMMC, g_SelectedUser);

        std::vector<ul::menu::Entry> existing_app_entries;
        u32 rem_special_entry_mask =
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryMiiEdit)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryWebBrowser)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryUserPage)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntrySettings)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryThemes)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryControllers)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryAlbum)) |
            BITL(static_cast<u32>(ul::menu::EntryType::SpecialEntryAmiibo));

        LocateApplicationAndSpecialEntries(menu_path, existing_app_entries, rem_special_entry_mask);

        // Ensure all special entries exist
        #define _CHECK_HAS_SPECIAL_ENTRY(type) { \
            if((rem_special_entry_mask & BITL(static_cast<u32>(type))) != 0) { \
                ul::menu::CreateSpecialEntry(menu_path, type); \
            } \
        }

        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryMiiEdit);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryWebBrowser);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryUserPage);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntrySettings);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryThemes);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryControllers);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryAlbum);
        _CHECK_HAS_SPECIAL_ENTRY(ul::menu::EntryType::SpecialEntryAmiibo);

        // Check applications

        for(auto &app_entry: existing_app_entries) {
            if(std::find_if(g_CurrentRecords.begin(), g_CurrentRecords.end(), [app_entry](const NsExtApplicationRecord &rec) -> bool {
                return rec.id == app_entry.app_info.app_id;
            }) == g_CurrentRecords.end()) {
                UL_LOG_INFO("Deleted application 0x%016lX", app_entry.app_info.app_id);
                NotifyApplicationDeleted(app_entry.app_info.app_id);
                ul::menu::Entry rm_entry(app_entry);
                rm_entry.Remove();
            }
        }

        for(const auto &record: g_CurrentRecords) {
            if(std::find_if(existing_app_entries.begin(), existing_app_entries.end(), [record](const ul::menu::Entry &entry) -> bool {
                return entry.app_info.app_id == record.id;
            }) == existing_app_entries.end()) {
                UL_LOG_INFO("Added application 0x%016lX", record.id);
                g_LastAddedApplications.push_back(record.id);
                while(!ul::menu::CacheSingleApplication(record.id)) {
                    UL_LOG_INFO("> Failed to cache, retrying...");
                    svcSleepThread(100'000ul);
                }
                ul::menu::CacheSingleApplication(record.id);

                ul::menu::EnsureApplicationEntry(record, menu_path);
            }
        }
    }

    Result LaunchMenu(const ul::smi::MenuStartMode st_mode) {
        g_LastLibraryAppletLaunchedNotMenu = false;
        UpdateStatus();

        // AMS-1.11 clean-exit: release the previous ECS registration before
        // allocating a new ServerManager slot for this launch.  Skipped on
        // first boot (g_PendingEcsProgramId == 0).
        // (2026-06-20: tested DISABLING this unregister as a layer-2 fix — upstream uLaunch
        //  has no UnregisterExternalContent at all — but the relaunch STILL failed with the
        //  same am 2128-0035. So the unregister is NOT the cause; restored for the session-pool fix.)
        if(g_PendingEcsProgramId != 0) {
            const Result unreg_rc = ecs::UnregisterExternalContent(g_PendingEcsProgramId);
            if(R_FAILED(unreg_rc)) {
                UL_LOG_WARN("LaunchMenu: UnregisterExternalContent(0x%016lX) "
                            "rc=0x%08X (continuing — slot may leak)",
                            g_PendingEcsProgramId, unreg_rc);
            }
            g_PendingEcsProgramId = 0;
        }

        const u64 menu_pid = la::GetMenuProgramId();
        UL_LOG_INFO("Launching uMenu with start mode %d...", static_cast<u32>(st_mode));
        const Result reg_rc = ecs::RegisterLaunchAsApplet(menu_pid, static_cast<u32>(st_mode), "/ulaunch/bin/uMenu", std::addressof(g_CurrentStatus), sizeof(g_CurrentStatus));
        if(R_SUCCEEDED(reg_rc)) {
            g_PendingEcsProgramId = menu_pid;
            // BOOT-SPEED: log the app-init -> first-launch latency once, so the
            // cache-defer win is measurable straight from the log.
            static bool s_boot_latency_logged = false;
            if(!s_boot_latency_logged && g_BootInitTick != 0) {
                s_boot_latency_logged = true;
                const u64 ms = armTicksToNs(armGetSystemTick() - g_BootInitTick) / 1'000'000;
                UL_LOG_INFO("[BOOT-SPEED] uSystem app-init -> first uMenu launch: %llu ms (cache deferred to background)", static_cast<unsigned long long>(ms));
            }
            // uMenu is now launching — release the gated app-cache worker so its
            // ~2.8s NACP+icon build runs in the background, not on the boot path.
            ul::system::app::AllowCacheDrain();
        }
        return reg_rc;
    }

    void ApplicationVerifyMain(void *ctx_raw) {
        auto ctx = reinterpret_cast<ApplicationVerifyContext*>(ctx_raw);

        // Like qlaunch does, same size and align
        auto verify_buf = new (std::align_val_t(ams::os::MemoryPageSize)) u8[VerifyWorkBufferSize]();
        NsProgressAsyncResult async_rc;
        NsSystemUpdateProgress progress;
        UL_RC_ASSERT(nsRequestVerifyApplication(&async_rc, ctx->app_id, 0x7, verify_buf, VerifyWorkBufferSize));

        Result verify_rc;
        Result verify_detail_rc;
        while(true) {
            const auto rc = nsProgressAsyncResultWait(&async_rc, VerifyStepWaitTimeNs);
            if(rc == ul::svc::ResultTimedOut) {
                // Still not finished
                UL_RC_ASSERT(nsProgressAsyncResultGetProgress(&async_rc, &progress, sizeof(progress)));

                if(progress.total_size > 0) {
                    const auto progress_val = (float)progress.current_size / (float)progress.total_size;
                    UL_LOG_INFO("[Verify-0x%016lX] done: %lld, total: %lld, prog: %.2f%%", ctx->app_id, progress.current_size, progress.total_size, progress_val * 100.0f);

                    if(IsMenuRunning()) {
                        const ul::smi::MenuMessageContext menu_ctx = {
                            .msg = ul::smi::MenuMessage::ApplicationVerifyProgress,
                            .app_verify_progress = {
                                .app_id = ctx->app_id,
                                .done = (u64)progress.current_size,
                                .total = (u64)progress.total_size
                            }
                        };
                        PushMenuMessageContext(menu_ctx);
                    }
                }
                else {
                    UL_LOG_INFO("[Verify-0x%016lX] invalid progress...", ctx->app_id);
                }
            }
            else if(R_SUCCEEDED(rc)) {
                // Finished
                verify_rc = nsProgressAsyncResultGet(&async_rc);
                verify_detail_rc = nsProgressAsyncResultGetDetailResult(&async_rc);
                break;
            }
            else {
                // Unexpected
                UL_LOG_WARN("[Verify-0x%016lX] nsProgressAsyncResultWait failed unexpectedly: %s", ctx->app_id, ul::util::FormatResultDisplay(rc).c_str());

                verify_rc = rc;
                verify_detail_rc = rc;
                break;
            }
        }

        ctx->finished = true;

        nsProgressAsyncResultClose(&async_rc);
        delete[] verify_buf;

        if(R_SUCCEEDED(verify_rc) && R_SUCCEEDED(verify_detail_rc)) {
            // Note: qlaunch apparently calls this command after verification succeeds, it resets the app record/view so that it can be launchable now
            nsClearApplicationTerminateResult(ctx->app_id);
        }

        const ul::smi::MenuMessageContext menu_ctx = {
            .msg = ul::smi::MenuMessage::ApplicationVerifyResult,
            .app_verify_rc = {
                .app_id = ctx->app_id,
                .rc = verify_rc,
                .detail_rc = verify_detail_rc
            }
        };
        PushMenuMessageContext(menu_ctx);
    }

    void HandleHomeButton() {
        if(la::IsActive() && !IsMenuRunning()) {
            // A library applet (NRO) is in the foreground that is not uMenu.
            // Close it and return to uMenu.
            //
            // Fix A (kept): reclaim foreground BEFORE requesting exit.
            // Sending RequestExit from background is the HW-verified hang class —
            // the foreground applet does not process IPC while we are in background.
            //
            // Fix B: do NOT call the blocking la::Terminate().  Instead, issue a
            // non-blocking RequestExit and arm a 500 ms deadline.  The MainLoop
            // polls g_TerminatingNro and completes the sequence without blocking.
            if(g_DevMode) { HomeTrace("ENTER HandleHomeButton (NRO branch)"); }

            if(g_DevMode) { HomeTrace("BEFORE sys::SetForeground"); }
            const auto fg_rc = sys::SetForeground();
            if(g_DevMode) { HomeTrace("AFTER sys::SetForeground"); }

            if(R_FAILED(fg_rc)) {
                // Fix D: log failure but do not assert — proceed anyway; in the
                // worst case the NRO stays foreground but uMenu still relaunches.
                UL_LOG_WARN("[HandleHomeButton] sys::SetForeground failed: %s — continuing", ul::util::FormatResultDisplay(fg_rc).c_str());
                if(g_DevMode) { HomeTrace("sys::SetForeground FAILED — continuing"); }
            }

            if(g_DevMode) { HomeTrace("BEFORE RequestExitNonBlocking"); }
            la::RequestExitNonBlocking();
            if(g_DevMode) { HomeTrace("AFTER RequestExitNonBlocking — arming deadline"); }

            // Arm the 500 ms deadline (Fix C).  MainLoop will poll and call
            // ForceTerminateNow() + LaunchMenu() when it fires or the NRO exits.
            // 500 ms expressed in system-tick units: armGetSystemTick() uses the
            // 19.2 MHz crystal oscillator on the Erista, so 1 s ≈ 19 200 000 ticks.
            // kTicksPerSec / 2  =  9 600 000 ticks  ≈  500 ms.
            constexpr u64 kTicksPerSec   = 19'200'000ul;        // Erista 19.2 MHz
            constexpr u64 kDeadlineTicks = kTicksPerSec / 2;    // 500 ms in ticks

            g_TerminatingNroDeadlineTick = armGetSystemTick() + kDeadlineTicks;
            g_TerminatingNro = true;
            g_LastLibraryAppletLaunchedNotMenu = false;

            // Return immediately — LaunchMenu() will be called from MainLoop
            // once the NRO exits or the deadline elapses.
            if(g_DevMode) { HomeTrace("EXIT HandleHomeButton — g_TerminatingNro armed, returning"); }
            return;
        }
        else if(app::IsActive() && app::HasForeground()) {
            // RESTORED v3.7.0 (2026-06-21): HOME over a running game SUSPENDS it (reclaim
            // foreground) and opens uMenu once. Do NOT terminate the app + re-create uMenu —
            // that post-game double-create regressed into am 2128-0035 / the black relaunch loop.
            UL_RC_ASSERT(sys::SetForeground());

            UL_RC_ASSERT(LaunchMenu(ul::smi::MenuStartMode::MainMenu));
        }
        else if(IsMenuRunning()) {
            // Send a message to our menu to handle itself the home press
            PushSimpleMenuMessage(ul::smi::MenuMessage::HomeRequest);
        }
    }

    void HandleGeneralChannelMessage() {
        AppletStorage sams_st;
        if(R_SUCCEEDED(appletPopFromGeneralChannel(&sams_st))) {
            ul::util::OnScopeExit close_sams_st([&]() {
                appletStorageClose(&sams_st);
            });

            StorageReader sams_st_reader(sams_st);

            // Note: are we expecting a certain size? (sometimes we get 0x10, others 0x800...)
            const auto sams_st_size = sams_st_reader.GetSize();

            SystemAppletMessageHeader sams_header;
            UL_RC_ASSERT(sams_st_reader.Read(sams_header));
            UL_LOG_INFO("SystemAppletMessageHeader [size: 0x%lX] { magic: 0x%X, unk: 0x%X, msg: %d, unk_2: 0x%X }", sams_st_size, sams_header.magic, sams_header.unk, static_cast<u32>(sams_header.msg), sams_header.unk_2);
            if(sams_header.IsValid()) {
                switch(sams_header.msg) {
                    case GeneralChannelMessage::Unk_Invalid: {
                        UL_LOG_WARN("Invalid general channel message!");
                        break;
                    }
                    case GeneralChannelMessage::RequestHomeMenu: {
                        HandleHomeButton();
                        break;
                    }
                    case GeneralChannelMessage::Unk_Sleep: {
                        HandleSleep();
                        break;
                    }
                    case GeneralChannelMessage::Unk_Shutdown: {
                        // DEV backstop: a shutdown powers the console fully off, and on
                        // Erista that requires an RCM re-inject to boot again. Suppress in
                        // dev so the unit stays remotely recoverable. Never ships.
                        if(g_DevMode) {
                            UL_LOG_INFO("[DEV] Unk_Shutdown suppressed — staying powered for remote access");
                            break;
                        }
                        UL_RC_ASSERT(appletStartShutdownSequence());
                        break;
                    }
                    case GeneralChannelMessage::Unk_Reboot: {
                        UL_RC_ASSERT(appletStartRebootSequence());
                        break;
                    }
                    case GeneralChannelMessage::RequestJumpToSystemUpdate: {
                        UL_LOG_WARN("Got GeneralChannelMessage: RequestJumpToSystemUpdate");
                        break;
                    }
                    case GeneralChannelMessage::Unk_OverlayBrightValueChanged: {
                        UL_LOG_WARN("Got GeneralChannelMessage: Unk_OverlayBrightValueChanged");
                        break;
                    }
                    case GeneralChannelMessage::Unk_OverlayAutoBrightnessChanged: {
                        UL_LOG_WARN("Got GeneralChannelMessage: Unk_OverlayAutoBrightnessChanged");
                        break;
                    }
                    case GeneralChannelMessage::Unk_OverlayAirplaneModeChanged: {
                        UL_LOG_WARN("Got GeneralChannelMessage: Unk_OverlayAirplaneModeChanged");
                        break;
                    }
                    case GeneralChannelMessage::Unk_OverlayShown: {
                        UL_LOG_WARN("Got GeneralChannelMessage: Unk_OverlayShown");
                        break;
                    }
                    case GeneralChannelMessage::Unk_OverlayHidden: {
                        UL_LOG_WARN("Got GeneralChannelMessage: Unk_OverlayHidden");
                        break;
                    }
                    case GeneralChannelMessage::RequestToLaunchApplication: {
                        // TODO (low priority): enum?
                        u32 launch_app_request_sender;
                        UL_RC_ASSERT(sams_st_reader.Read(launch_app_request_sender));

                        u64 app_id;
                        UL_RC_ASSERT(sams_st_reader.Read(app_id));

                        AccountUid uid;
                        UL_RC_ASSERT(sams_st_reader.Read(uid));

                        u32 launch_params_buf_size;
                        UL_RC_ASSERT(sams_st_reader.Read(launch_params_buf_size));

                        auto launch_params_buf = new u8[launch_params_buf_size];
                        UL_RC_ASSERT(sams_st_reader.ReadBuffer(launch_params_buf, launch_params_buf_size));

                        UL_LOG_WARN("Got GeneralChannelMessage: RequestToLaunchApplication { launch_app_request_sender: %d, app_id: 0%16lX, uid: %016lX + %016lX, launch params buf size: 0x%X }", launch_app_request_sender, app_id, uid.uid[0], uid.uid[1], launch_params_buf_size);

                        delete[] launch_params_buf;
                        break;
                    }
                    case GeneralChannelMessage::RequestJumpToStory: {
                        AccountUid uid;
                        UL_RC_ASSERT(sams_st_reader.Read(uid));

                        u64 app_id;
                        UL_RC_ASSERT(sams_st_reader.Read(app_id));

                        UL_LOG_WARN("Unimplemented: RequestJumpToStory { uid: %016lX + %016lX, app_id: 0%16lX }", uid.uid[0], uid.uid[1], app_id);
                        break;
                    }
                    default:
                        // TODO (long term): try to find and implement more messages (mostly those sent by applets!)
                        UL_LOG_WARN("Unhandled general channel message!");
                        break;
                }
            }
        }
    }

    void UpdateOperationMode() {
        // Thank you so much libnx for not exposing the actual call to get the mode via IPC :P
        // We're qlaunch, not using appletMainLoop, thus we have to take care of this manually...
        u8 raw_mode = 0;
        UL_RC_ASSERT(serviceDispatchOut(appletGetServiceSession_CommonStateGetter(), 5, raw_mode));
        g_OperationMode = static_cast<AppletOperationMode>(raw_mode);
    }

    void HandleAppletMessage() {
        u32 finished_verify_count = 0;
        for(auto &ctx: *g_ApplicationVerifyContexts) {
            if(ctx.finished) {
                UL_RC_ASSERT(threadWaitForExit(&ctx.thread));
                UL_RC_ASSERT(threadClose(&ctx.thread));
                finished_verify_count++;
            }
        }

        if(finished_verify_count > 0) {
            auto new_end = std::remove_if(g_ApplicationVerifyContexts->begin(), g_ApplicationVerifyContexts->end(), [](const ApplicationVerifyContext &ctx) { return ctx.finished; });
            g_ApplicationVerifyContexts->erase(new_end, g_ApplicationVerifyContexts->end());   
        }

        u32 raw_msg = 0;
        if(R_SUCCEEDED(appletGetMessage(&raw_msg))) {
            switch(static_cast<ul::system::AppletMessage>(raw_msg)) {
                case ul::system::AppletMessage::ChangeIntoForeground: {
                    UL_LOG_INFO("Got AppletMessage: ChangeIntoForeground");
                    break;
                }
                case ul::system::AppletMessage::ChangeIntoBackground: {
                    UL_LOG_INFO("Got AppletMessage: ChangeIntoBackground");
                    break;
                }
                case ul::system::AppletMessage::ApplicationExited: {
                    UL_LOG_INFO("Got AppletMessage: ApplicationExited");
                    break;
                }
                case ul::system::AppletMessage::DetectShortPressingHomeButton: {
                    HandleHomeButton();
                    break;
                }
                case ul::system::AppletMessage::DetectLongPressingHomeButton: {
                    // Q OS cycle SP4.14: forward the OS's long-press signal
                    // to uMenu so qdesktop can open its dev mini-menu without
                    // depending on the double-press fallback.  We DO NOT call
                    // HandleHomeButton() here — that path interprets the
                    // press as "go home" and would terminate the foreground
                    // applet/app.  Long-press is a uMenu-internal gesture,
                    // valid only when uMenu itself is the active foreground.
                    if(IsMenuRunning()) {
                        UL_LOG_INFO("Got AppletMessage: DetectLongPressingHomeButton "
                                    "→ forwarding HomeLongRequest to uMenu");
                        PushSimpleMenuMessage(ul::smi::MenuMessage::HomeLongRequest);
                    }
                    else {
                        UL_LOG_INFO("Got AppletMessage: DetectLongPressingHomeButton "
                                    "while non-uMenu is foreground — ignoring");
                    }
                    break;
                }
                case ul::system::AppletMessage::DetectShortPressingPowerButton: {
                    HandleSleep();
                    break;
                }
                case ul::system::AppletMessage::FinishedSleepSequence: {
                    if(IsMenuRunning()) {
                        PushSimpleMenuMessage(ul::smi::MenuMessage::FinishedSleep);
                    }
                    break;
                }
                case ul::system::AppletMessage::AutoPowerDown: {
                    // From auto-sleep functionality
                    HandleSleep();
                    break;
                }
                case ul::system::AppletMessage::OperationModeChanged: {
                    UpdateOperationMode();
                    break;
                }
                case ul::system::AppletMessage::SdCardRemoved: {
                    if(IsMenuRunning()) {
                        PushSimpleMenuMessage(ul::smi::MenuMessage::SdCardEjected);
                    }
                    else if(g_DevMode) {
                        // DEV backstop: a shutdown forces a physical RCM re-inject on
                        // Erista.  In dev the SD card hosts sys-ftpd (:5000) which runs
                        // from RAM — it stays reachable even after the card is removed.
                        // A transient SD glitch (dirty pull, re-seat) would otherwise
                        // permanently strand the device.  Log and stay up; the operator
                        // can FTP-push a fix or drop cmd_crash to trigger auto-reboot
                        // recovery via the Atmosphère fatal handler.  Never ships.
                        UL_LOG_WARN("[DEV] SdCardRemoved while uMenu inactive — suppressing shutdown "
                                    "to preserve remote recovery (sys-ftpd runs from RAM)");
                    }
                    else {
                        // Power off, since uMenu's UI relies on the SD card, so trying to use uMenu without the SD is not possible at all without any caching...
                        // TODO (low priority): consider handling this in a better way?
                        UL_RC_ASSERT(appletStartShutdownSequence());
                    }
                    break;
                }
                default:
                    UL_LOG_WARN("Unimplemented applet message: %d", raw_msg);
                    break;
            }
        } 
    }

    void HandleMenuMessage() {
        if(IsMenuRunning()) {
            u32 app_list_count;

            // Note: ignoring result since this won't always succeed, and would error if no commands were received
            smi::ReceiveCommand(
                [&](const ul::smi::SystemMessage msg, smi::ScopedStorageReader &reader) -> Result {
                    switch(msg) {
                        case ul::smi::SystemMessage::SetSelectedUser: {
                            UL_RC_TRY(reader.Pop(g_SelectedUser));
                            CheckApplicationRecordChanges();
                            break;
                        }
                        case ul::smi::SystemMessage::LaunchApplication: {
                            u64 launch_app_id;
                            UL_RC_TRY(reader.Pop(launch_app_id));

                            if(app::IsActive()) {
                                return ul::ResultApplicationActive;
                            }
                            if(!accountUidIsValid(&g_SelectedUser)) {
                                return ul::ResultInvalidSelectedUser;
                            }

                            g_ActionQueue.push_back({
                                .type = ActionType::LaunchApplication,
                                .launch_application = {
                                    .app_id = launch_app_id
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::ResumeApplication: {
                            if(!app::IsActive()) {
                                return ul::ResultApplicationNotActive;
                            }

                            UL_RC_TRY(app::SetForeground());
                            break;
                        }
                        case ul::smi::SystemMessage::TerminateApplication: {
                            UL_RC_TRY(app::Terminate());
                            g_LastHomebrewApplicationLaunchTarget = {};
                            break;
                        }
                        case ul::smi::SystemMessage::LaunchHomebrewLibraryApplet: {
                            ul::loader::TargetInput temp_ipt;
                            UL_RC_TRY(reader.Pop(temp_ipt));

                            g_ActionQueue.push_back({
                                .type = ActionType::LaunchHomebrewLibraryApplet,
                                .launch_loader = {
                                    .target_input = temp_ipt,
                                    .choose_mode = false
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::LaunchHomebrewWindowedLibraryApplet: {
                            // v3.1 Phase 2 Task 3 Slice 4 (architectural inversion).
                            //
                            // Server-side responsibility: ONLY do the ECS
                            // hijack registration (which requires ldr:shell —
                            // uMenu's NPDM doesn't grant it).  uMenu does the
                            // actual library-applet creation locally via
                            // appletCreateLibraryAppletSelf because uMenu is
                            // the FOREGROUND applet and AM permits that path;
                            // background uSystem can't call the regular
                            // appletCreateLibraryApplet (HW-verified hang).
                            //
                            // Server pushes the resolved AppletId back so
                            // uMenu doesn't need a copy of la::GetAppletId-
                            // ForProgramId.
                            ul::loader::TargetInput temp_ipt;
                            UL_RC_TRY(reader.Pop(temp_ipt));

                            u64 hb_applet_takeover_program_id;
                            {
                                ul::ScopedLock lk(g_ConfigLock);
                                UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::HomebrewAppletTakeoverProgramId, hb_applet_takeover_program_id));
                            }

                            // AMS-1.11 clean-exit: release any previous ECS
                            // registration before allocating a new slot.
                            if(g_PendingEcsProgramId != 0) {
                                const Result unreg_rc = ecs::UnregisterExternalContent(g_PendingEcsProgramId);
                                if(R_FAILED(unreg_rc)) {
                                    UL_LOG_WARN("LaunchHomebrewWindowedLibraryApplet: "
                                                "UnregisterExternalContent(0x%016lX) "
                                                "rc=0x%08X", g_PendingEcsProgramId, unreg_rc);
                                }
                                g_PendingEcsProgramId = 0;
                            }

                            UL_LOG_INFO("LaunchHomebrewWindowedLibraryApplet: ECS-register only "
                                        "(uMenu does the launch via appletCreateLibraryAppletSelf) "
                                        "target='%s' program_id=0x%016lX",
                                        temp_ipt.nro_path, hb_applet_takeover_program_id);

                            const Result reg_rc = ecs::RegisterExternalContent(hb_applet_takeover_program_id, "/ulaunch/bin/uLoader/applet");
                            if(R_FAILED(reg_rc)) {
                                UL_LOG_WARN("LaunchHomebrewWindowedLibraryApplet: "
                                            "ecs::RegisterExternalContent rc=0x%08X — surfacing", reg_rc);
                                g_PendingWindowedAppletId = 0;
                                return reg_rc;
                            }

                            g_PendingEcsProgramId = hb_applet_takeover_program_id;
                            // Resolve applet_id from program_id (la's table)
                            // and stash for push_fn.
                            g_PendingWindowedAppletId = static_cast<u32>(la::GetAppletIdForProgramId(hb_applet_takeover_program_id));

                            UL_LOG_INFO("ECS hijack registered; AppletId=0x%04X (program_id=0x%016lX)",
                                        g_PendingWindowedAppletId, hb_applet_takeover_program_id);
                            // Deliberately NOT setting g_LastLibraryAppletLaunchedNotMenu — uMenu
                            // stays as the foreground LA and owns the launch from this point.
                            break;
                        }
                        case ul::smi::SystemMessage::LaunchHomebrewApplication: {
                            ul::loader::TargetInput temp_ipt;
                            UL_RC_TRY(reader.Pop(temp_ipt));

                            if(app::IsActive()) {
                                return ul::ResultApplicationActive;
                            }
                            if(!accountUidIsValid(&g_SelectedUser)) {
                                return ul::ResultInvalidSelectedUser;
                            }

                            u64 hb_application_takeover_program_id;
                            {
                                ul::ScopedLock lk(g_ConfigLock);
                                UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, hb_application_takeover_program_id));
                                if(hb_application_takeover_program_id == ul::os::InvalidApplicationId) {
                                    return ul::ResultNoHomebrewTakeoverApplication;
                                }
                            }

                            g_ActionQueue.push_back({
                                .type = ActionType::LaunchHomebrewApplication,
                                .launch_homebrew_application = {
                                    .app_id = hb_application_takeover_program_id,
                                    .app_target_input = temp_ipt
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::ChooseHomebrew: {
                            g_ActionQueue.push_back({
                                .type = ActionType::LaunchHomebrewLibraryApplet,
                                .launch_loader = {
                                    .target_input = ul::loader::TargetInput::Create(ul::HbmenuPath, ul::HbmenuPath, true, ChooseHomebrewCaption),
                                    .choose_mode = true
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenWebPage: {
                            char web_url[500] = {};
                            UL_RC_TRY(reader.PopData(web_url, sizeof(web_url)));

                            WebCommonConfig cfg;
                            UL_RC_TRY(webPageCreate(&cfg, web_url));
                            UL_RC_TRY(webConfigSetWhitelist(&cfg, ".*"));
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenWebPage,
                                .open_web_page = {
                                    .cfg = cfg
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenAlbum: {
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenAlbum
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::RestartMenu: {
                            UL_RC_TRY(reader.Pop(g_MenuRestartReloadThemeCache));
                            if(g_MenuRestartReloadThemeCache) {
                                g_WarnedAboutOutdatedTheme = false;
                            }
                            
                            g_ActionQueue.push_back({
                                .type = ActionType::RestartMenu
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::ReloadConfig: {
                            LoadConfig();
                            break;
                        }
                        case ul::smi::SystemMessage::UpdateMenuPaths: {
                            char menu_fs_path[FS_MAX_PATH];
                            UL_RC_TRY(reader.PopData(menu_fs_path, sizeof(menu_fs_path)));

                            char menu_path[FS_MAX_PATH];
                            UL_RC_TRY(reader.PopData(menu_path, sizeof(menu_path)));

                            ul::util::CopyToStringBuffer(g_CurrentMenuFsPath, menu_fs_path);
                            ul::util::CopyToStringBuffer(g_CurrentMenuPath, menu_path);
                            break;
                        }
                        case ul::smi::SystemMessage::UpdateMenuIndex: {
                            u32 menu_index;
                            UL_RC_TRY(reader.Pop(menu_index));

                            g_CurrentMenuIndex = menu_index;
                            break;
                        }
                        case ul::smi::SystemMessage::OpenUserPage: {
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenUserPage
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenMiiEdit: {
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenMiiEdit
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenAddUser: {
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenAddUser
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenNetConnect: {
                            g_ActionQueue.push_back({
                                .type = ActionType::OpenNetConnect
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::ListAddedApplications: {
                            UL_RC_TRY(reader.Pop(app_list_count));
                            break;
                        }
                        case ul::smi::SystemMessage::ListDeletedApplications: {
                            UL_RC_TRY(reader.Pop(app_list_count));
                            break;
                        }
                        case ul::smi::SystemMessage::OpenCabinet: {
                            u8 type;
                            UL_RC_TRY(reader.Pop(type));

                            g_ActionQueue.push_back({
                                .type = ActionType::OpenCabinet,
                                .open_cabinet = {
                                    .type = static_cast<NfpLaStartParamTypeForAmiiboSettings>(type)
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::StartVerifyApplication: {
                            u64 app_id;
                            UL_RC_TRY(reader.Pop(app_id));

                            auto &ctx = g_ApplicationVerifyContexts->emplace_back(app_id);
                            UL_RC_ASSERT(threadCreate(&ctx.thread, ApplicationVerifyMain, std::addressof(ctx), ctx.thread_stack, ApplicationVerifyContext::ThreadStackSize, 30, -2));
                            UL_RC_ASSERT(threadStart(&ctx.thread));
                            break;
                        }
                        case ul::smi::SystemMessage::ListInVerifyApplications: {
                            UL_RC_TRY(reader.Pop(app_list_count));
                            break;
                        }
                        case ul::smi::SystemMessage::NotifyWarnedAboutOutdatedTheme: {
                            g_WarnedAboutOutdatedTheme = true;
                            break;
                        }
                        case ul::smi::SystemMessage::TerminateMenu: {
                            g_ActionQueue.push_back({
                                .type = ActionType::TerminateMenu
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::OpenControllerKeyRemapping: {
                            u32 npad_style_set;
                            HidNpadJoyHoldType hold_type;
                            UL_RC_TRY(reader.Pop(npad_style_set));
                            UL_RC_TRY(reader.Pop(hold_type));

                            g_ActionQueue.push_back({
                                .type = ActionType::OpenControllerKeyRemapping,
                                .open_controller_key_remapping = {
                                    .npad_style_set = npad_style_set,
                                    .hold_type = hold_type
                                }
                            });
                            break;
                        }
                        case ul::smi::SystemMessage::Heartbeat: {
                            // v3.8.x SELF-HEAL WATCHDOG — uSystem side.
                            // Record the tick at which this heartbeat arrived.
                            // If g_WatchdogFailCount > 0 (prior recovery attempts
                            // were made) reset it — uMenu is alive again.
                            const u64 now_tick = armGetSystemTick();
                            g_LastMenuHeartbeatTick = now_tick;
                            if(g_WatchdogFailCount > 0) {
                                UL_LOG_INFO("[Watchdog] Heartbeat received after %u recovery attempt(s) — resetting fail count",
                                            g_WatchdogFailCount);
                                g_WatchdogFailCount = 0;
                            }
                            UL_LOG_INFO("[Watchdog] Heartbeat received (tick=%lu)", static_cast<unsigned long>(now_tick));
                            break;
                        }
                        case ul::smi::SystemMessage::RebootToStockQlaunch: {
                            // v2.3.6 (revised v2.3.6.1): Toggle the qlaunch override
                            // and reboot.  The handler lives on the SMI thread; we
                            // can't unlink/rename uSystem's own binary while it's
                            // running, so we operate on the on-disk file and the
                            // rename takes effect on the NEXT boot.
                            //
                            // We use ul::fs::* (already linked everywhere in uSystem)
                            // instead of raw newlib fopen/rename.  Earlier draft also
                            // pulled in spsmShutdown as a fallback — that linked an
                            // extra ~22 KB of spsm machinery and tipped uSystem into
                            // a black-screen boot regression on v2.3.6.  We rely
                            // solely on appletRequestToReboot() which uSystem
                            // already uses elsewhere and which Atmosphère honours.
                            constexpr const char *kActivePath   = "sdmc:/atmosphere/contents/0100000000001000/exefs.nsp";
                            constexpr const char *kDisabledPath = "sdmc:/atmosphere/contents/0100000000001000/exefs.nsp.disabled";

                            const bool active_present   = ul::fs::ExistsFile(kActivePath);
                            const bool disabled_present = ul::fs::ExistsFile(kDisabledPath);

                            if(active_present && !disabled_present) {
                                if(ul::fs::RenameFile(kActivePath, kDisabledPath)) {
                                    UL_LOG_INFO("RebootToStockQlaunch: override DISABLED (next boot = stock qlaunch)");
                                } else {
                                    UL_LOG_WARN("RebootToStockQlaunch: rename active->disabled failed");
                                }
                            } else if(!active_present && disabled_present) {
                                if(ul::fs::RenameFile(kDisabledPath, kActivePath)) {
                                    UL_LOG_INFO("RebootToStockQlaunch: override RE-ENABLED (next boot = uSystem/uMenu)");
                                } else {
                                    UL_LOG_WARN("RebootToStockQlaunch: rename disabled->active failed");
                                }
                            } else {
                                UL_LOG_WARN("RebootToStockQlaunch: ambiguous state active=%d disabled=%d",
                                    (int)active_present, (int)disabled_present);
                            }

                            // Ask AM to reboot; AM signals the kernel after we return.
                            const Result rc_req = appletRequestToReboot();
                            if(R_FAILED(rc_req)) {
                                UL_LOG_WARN("RebootToStockQlaunch: appletRequestToReboot rc=0x%x", rc_req);
                            }
                            break;
                        }
                        default: {
                            // ...
                            break;
                        }
                    }
                    return ul::ResultSuccess;
                },
                [&](const ul::smi::SystemMessage msg, smi::ScopedStorageWriter &writer) -> Result {
                    AMS_UNUSED(writer);
                    switch(msg) {
                        case ul::smi::SystemMessage::ListAddedApplications: {
                            ul::ScopedLock lock(g_LastAddedApplicationsLock);
                            if(app_list_count > g_LastAddedApplications.size()) {
                                return ul::ResultInvalidApplicationListCount;
                            }

                            for(u32 i = 0; i < app_list_count; i++) {
                                writer.Push(g_LastAddedApplications.at(i));
                            }

                            g_LastAddedApplications.clear();
                            break;
                        }
                        case ul::smi::SystemMessage::ListDeletedApplications: {
                            ul::ScopedLock lock2(g_LastDeletedApplicationsLock);
                            if(app_list_count > g_LastDeletedApplications.size()) {
                                return ul::ResultInvalidApplicationListCount;
                            }

                            for(u32 i = 0; i < app_list_count; i++) {
                                writer.Push(g_LastDeletedApplications.at(i));
                            }

                            g_LastDeletedApplications.clear();
                            break;
                        }
                        case ul::smi::SystemMessage::LaunchHomebrewWindowedLibraryApplet: {
                            // v3.1 Phase 2 Task 3 Slice 4 — push the resolved
                            // AppletId back to uMenu so it can pass to
                            // appletCreateLibraryAppletSelf locally.
                            writer.Push(g_PendingWindowedAppletId);
                            g_PendingWindowedAppletId = 0;
                            break;
                        }
                        case ul::smi::SystemMessage::ListInVerifyApplications: {
                            if(app_list_count > g_ApplicationVerifyContexts->size()) {
                                return ul::ResultInvalidApplicationListCount;
                            }

                            for(u32 i = 0; i < app_list_count; i++) {
                                writer.Push(g_ApplicationVerifyContexts->at(i).app_id);
                            }
                            break;
                        }
                        default: {
                            // ...
                            break;
                        }
                    }
                    return ul::ResultSuccess;
                }
            );
        }
    }

    bool HandleAction(Action &action) {
        // Cycle G4 (SP4.15): per-frame log throttle.  This function gets called
        // every MainLoop tick (≈60 Hz) for the head action; on hardware the
        // common case is "queued but blocked" — e.g. LaunchApplication waiting
        // for the active library applet (uMenu) to terminate.  The previous
        // unconditional log produced ~180 lines/sec of "Trying / Failed /
        // Action queue has" into RingFile, causing noticeable UI slowdown
        // and obscuring real events in the log.  Throttle: only log when
        // (action_type, queue size context) actually changes.
        static u32 s_last_logged_type = 0xFFFFFFFFu;
        if(static_cast<u32>(action.type) != s_last_logged_type) {
            UL_LOG_INFO("Trying to handle action of type %d in queue", static_cast<u32>(action.type));
            s_last_logged_type = static_cast<u32>(action.type);
        }
        switch(action.type) {
            case ActionType::LaunchApplication: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching application 0x%016lX...", action.launch_application.app_id);
                    UL_RC_ASSERT(app::Start(action.launch_application.app_id, false, g_SelectedUser));

                    return true;
                }
                break;
            }
            case ActionType::LaunchHomebrewLibraryApplet: {
                if(!la::IsActive()) {
                    if(action.launch_loader.choose_mode) {
                        UL_LOG_INFO("Launching homebrew chooser '%s' as library applet...", action.launch_loader.target_input.nro_path);
                    }
                    else {
                        UL_LOG_INFO("Launching homebrew '%s' as library applet (target once: %s)...", action.launch_loader.target_input.nro_path, action.launch_loader.target_input.target_once ? "true" : "false");
                    }
                    u64 hb_applet_takeover_program_id;
                    {
                        ul::ScopedLock lk(g_ConfigLock);
                        UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::HomebrewAppletTakeoverProgramId, hb_applet_takeover_program_id));
                    }
    
                    // AMS-1.11 clean-exit: release the previous ECS registration
                    // (which is uMenu's, since LaunchMenu was the most-recent
                    // ECS register) before allocating a new slot for uLoader.
                    if(g_PendingEcsProgramId != 0) {
                        const Result unreg_rc = ecs::UnregisterExternalContent(g_PendingEcsProgramId);
                        if(R_FAILED(unreg_rc)) {
                            UL_LOG_WARN("LaunchHomebrewLibraryApplet: "
                                        "UnregisterExternalContent(0x%016lX) "
                                        "rc=0x%08X", g_PendingEcsProgramId, unreg_rc);
                        }
                        g_PendingEcsProgramId = 0;
                    }

                    // TODO (new): consider not asserting and sending the error result to uMenu instead? same for various other asserts in this code...
                    UL_RC_ASSERT(ecs::RegisterLaunchAsApplet(hb_applet_takeover_program_id, 0, "/ulaunch/bin/uLoader/applet", &action.launch_loader.target_input, sizeof(action.launch_loader.target_input)));
                    g_PendingEcsProgramId = hb_applet_takeover_program_id;

                    g_LastLibraryAppletLaunchedNotMenu = true;
                    
                    // This will be used later to know if we should retrieve the output of uLoader or not
                    g_ExpectsLoaderChooseOutput = action.launch_loader.choose_mode;
                    return true;
                }
                break;
            }
            case ActionType::LaunchHomebrewApplication: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching homebrew '%s' over application 0x%016lX (target once: %s)...", action.launch_homebrew_application.app_target_input.nro_path, action.launch_homebrew_application.app_id, action.launch_homebrew_application.app_target_input.target_once ? "true" : "false");

                    // Query the NACP of the underlying application (we need to determine if it uses auto game recording)
                    if(!app::LoopQueryApplicationNacpMisc(action.launch_homebrew_application.app_id, g_LaunchHomebrewApplicationNacpMisc)) {
                        action.launch_homebrew_application.app_target_input.is_auto_game_recording = g_LaunchHomebrewApplicationNacpMisc.video_capture == 2;
                    }
                    else {
                        // Unable to query NACP, assume it uses auto game recording (safer to allocate less memory)
                        action.launch_homebrew_application.app_target_input.is_auto_game_recording = true;
                    }

                    // AMS-1.11 clean-exit: release the previous ECS registration
                    // before allocating a new slot for the application takeover.
                    if(g_PendingEcsProgramId != 0) {
                        const Result unreg_rc = ecs::UnregisterExternalContent(g_PendingEcsProgramId);
                        if(R_FAILED(unreg_rc)) {
                            UL_LOG_WARN("LaunchHomebrewApplication: "
                                        "UnregisterExternalContent(0x%016lX) "
                                        "rc=0x%08X", g_PendingEcsProgramId, unreg_rc);
                        }
                        g_PendingEcsProgramId = 0;
                    }

                    UL_RC_ASSERT(ecs::RegisterLaunchAsApplication(action.launch_homebrew_application.app_id, "/ulaunch/bin/uLoader/application", &action.launch_homebrew_application.app_target_input, sizeof(action.launch_homebrew_application.app_target_input), g_SelectedUser));
                    g_PendingEcsProgramId = action.launch_homebrew_application.app_id;

                    // Store target input of the launched application
                    g_LastHomebrewApplicationLaunchTarget = action.launch_homebrew_application.app_target_input;

                    return true;
                }
                break;
            }
            case ActionType::OpenWebPage: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching Web...");
                    UL_RC_ASSERT(la::OpenWeb(&action.open_web_page.cfg));

                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
            case ActionType::OpenAlbum: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching PhotoViewer (ShowAllAlbumFilesForHomeMenu)...");
                    UL_RC_ASSERT(la::OpenPhotoViewerAllAlbumFilesForHomeMenu());
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
            case ActionType::RestartMenu: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Restarting uMenu...");
                    UL_RC_ASSERT(LaunchMenu(ul::smi::MenuStartMode::StartupMenuPostBoot));

                    return true;
                }
                break;
            }
            case ActionType::OpenUserPage: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching MyPage (MyProfile)...");
                    UL_RC_ASSERT(la::OpenMyPageMyProfile(g_SelectedUser));
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
            case ActionType::OpenMiiEdit: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching MiiEdit...");
                    UL_RC_ASSERT(la::OpenMiiEdit());
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
            case ActionType::OpenAddUser: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching PlayerSelect (UserCreator)...");
                    UL_RC_ASSERT(la::OpenPlayerSelectUserCreator());
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    g_NextMenuLaunchAtStartup = true;
                    return true;
                }
                break;
            }
            case ActionType::OpenNetConnect: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching NetConnect...");
                    UL_RC_ASSERT(la::OpenNetConnect());

                    g_LastLibraryAppletLaunchedNotMenu = true;
                    g_NextMenuLaunchAtSettings = true;
                    return true;
                }
                break;
            }
            case ActionType::OpenCabinet: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching Cabinet...");
                    UL_RC_ASSERT(la::OpenCabinet(action.open_cabinet.type));
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
            case ActionType::TerminateMenu: {
                UL_LOG_INFO("Terminating uMenu...");
                UL_RC_ASSERT(la::Terminate());

                return true;
                break;
            }
            case ActionType::OpenControllerKeyRemapping: {
                if(!la::IsActive()) {
                    UL_LOG_INFO("Launching Controller (KeyRemapping)...");
                    UL_RC_ASSERT(la::OpenControllerKeyRemappingForSystem(action.open_controller_key_remapping.npad_style_set, action.open_controller_key_remapping.hold_type));
    
                    g_LastLibraryAppletLaunchedNotMenu = true;
                    return true;
                }
                break;
            }
        }

        // Cycle G4 (SP4.15): throttle.  HandleAction() itself already
        // throttles the "Trying" log to fire only on action-type change;
        // mirror that for "Failed" so we don't write 60 Hz of noise while
        // waiting for la::IsActive() to clear.
        static u32 s_last_failed_type = 0xFFFFFFFFu;
        if(static_cast<u32>(action.type) != s_last_failed_type) {
            UL_LOG_INFO("Failed to handle action in queue (type=%d, will retry)",
                        static_cast<u32>(action.type));
            s_last_failed_type = static_cast<u32>(action.type);
        }
        return false;
    }

    void MainLoop() {
        // UL_COMPUTE_LED: heartbeat tick — call at the top of every iteration.
        // LED pulses while this executes; a frozen LED = MainLoop has hung.
        // Pass busy=true if a launch/terminate sequence is in progress.
        ul::system::led::Tick(g_TerminatingNro || g_TerminatingApp);

        // DEV-ONLY: remote HOME-button trigger for automated host testing.
        //
        // WHY here and not in uMenu: uMenu's HTTP debug server dies when an
        // NRO goes foreground.  uSystem stays alive as the system applet and
        // owns HandleHomeButton(), so the trigger lives here.
        //
        // Mechanism: every ~30 iterations (≈300 ms at the 10 ms svcSleepThread
        // cadence) check whether sdmc:/ulaunch/cmd_home exists.  If it does,
        // remove it and call HandleHomeButton() exactly as a real HOME press
        // would (DetectShortPressingHomeButton → HandleHomeButton() path).
        //
        // Strict no-op when g_DevMode is false: the outer guard is evaluated
        // once per iteration and the branch is not taken — the counter and
        // stat() never run.
        if(g_DevMode) {
            static u32 s_dev_home_iter = 0;
            if(++s_dev_home_iter >= 30) {
                s_dev_home_iter = 0;
                // Re-assert no-sleep: an applet focus transition can reset the
                // self-controller AutoSleepDisabled flag, so keep pinning it OFF.
                appletSetAutoSleepDisabled(true);
                if(ul::fs::ExistsFile("sdmc:/ulaunch/cmd_home")) {
                    ul::fs::DeleteFile("sdmc:/ulaunch/cmd_home");
                    UL_LOG_INFO("[DEV] cmd_home consumed — triggering HandleHomeButton()");
                    HandleHomeButton();
                }

                // cmd_crash: NUCLEAR remote recovery. FTP-drop sdmc:/ulaunch/cmd_crash
                // to deliberately fatal uSystem -> Atmosphère fatal_auto_reboot (10s) ->
                // clean CFW boot -> uMenu in NORMAL mode. The always-works recovery from
                // ANY black/stuck/AMS-crash state when cmd_home/cmd_resetmenu can't help
                // (mirrors uMenu's /crash fatalThrow 0xCAFE). Needs g_DevMode (this block).
                if(ul::fs::ExistsFile("sdmc:/ulaunch/cmd_crash")) {
                    ul::fs::DeleteFile("sdmc:/ulaunch/cmd_crash");
                    UL_LOG_INFO("[DEV] cmd_crash consumed — fatalThrow(0xCAFE) to force auto-reboot recovery");
                    fatalThrow(0xCAFE);
                }

                // Fix 2 — cmd_resetmenu: force-recover uMenu from any black/stuck
                // state without a reboot.  FTP-drop sdmc:/ulaunch/cmd_resetmenu to
                // trigger.  Terminates any active library applet (la::) AND any
                // foreground/suspended application (app::), then launches uMenu in
                // NORMAL mode (MainMenu / mode 2).
                //
                // This bypasses the non-blocking state machines (g_TerminatingNro /
                // g_TerminatingApp) and does a synchronous force-kill so the operator
                // gets a single deterministic recovery cycle, not an iterating poll.
                // Safe to call from any state: if neither is active the terminate
                // calls are no-ops (the holders are already closed).
                if(ul::fs::ExistsFile("sdmc:/ulaunch/cmd_resetmenu")) {
                    ul::fs::DeleteFile("sdmc:/ulaunch/cmd_resetmenu");
                    UL_LOG_INFO("[DEV] cmd_resetmenu consumed — force-recovering uMenu");
                    if(g_DevMode) { HomeTrace("cmd_resetmenu: ENTER force-recovery"); }

                    // Clear any pending terminate state machines first — we're
                    // taking over recovery synchronously.
                    g_TerminatingNro = false;
                    g_TerminatingNroDeadlineTick = 0;
                    g_TerminatingApp = false;
                    g_TerminatingAppDeadlineTick = 0;

                    // Terminate active library applet (uMenu or NRO) if any.
                    if(la::IsActive()) {
                        if(g_DevMode) { HomeTrace("cmd_resetmenu: terminating active la::"); }
                        const auto la_rc = la::ForceTerminateNow();
                        if(R_FAILED(la_rc)) {
                            UL_LOG_WARN("[DEV/cmd_resetmenu] la::ForceTerminateNow failed: %s",
                                ul::util::FormatResultDisplay(la_rc).c_str());
                        }
                    }

                    // Terminate active/suspended application (app::) if any.
                    if(app::IsActive()) {
                        if(g_DevMode) { HomeTrace("cmd_resetmenu: terminating active app::"); }
                        const auto app_rc = app::ForceTerminateNow();
                        if(R_FAILED(app_rc)) {
                            UL_LOG_WARN("[DEV/cmd_resetmenu] app::ForceTerminateNow failed: %s",
                                ul::util::FormatResultDisplay(app_rc).c_str());
                        }
                        g_LastHomebrewApplicationLaunchTarget = {};
                    }

                    // Reset library-applet-launched-not-menu flag so the finalized-
                    // applet handler below doesn't try to collect output from a dead holder.
                    g_LastLibraryAppletLaunchedNotMenu = false;

                    // Reclaim foreground so LaunchMenu can run.
                    sys::SetForeground();

                    if(g_DevMode) { HomeTrace("cmd_resetmenu: BEFORE LaunchMenu(MainMenu)"); }
                    const auto launch_rc = LaunchMenu(ul::smi::MenuStartMode::MainMenu);
                    if(g_DevMode) { HomeTrace("cmd_resetmenu: AFTER LaunchMenu(MainMenu)"); }

                    if(R_FAILED(launch_rc)) {
                        UL_LOG_WARN("[DEV/cmd_resetmenu] LaunchMenu failed: %s",
                            ul::util::FormatResultDisplay(launch_rc).c_str());
                        if(g_DevMode) { HomeTrace("cmd_resetmenu: LaunchMenu FAILED"); }
                    }
                    else {
                        UL_LOG_INFO("[DEV/cmd_resetmenu] uMenu relaunched in NORMAL (MainMenu) mode");
                        if(g_DevMode) { HomeTrace("cmd_resetmenu: uMenu relaunched OK"); }
                    }
                }
            }
        }

        // -----------------------------------------------------------------------
        // Fix B/C/D — non-blocking NRO terminate poll.
        //
        // HandleHomeButton() arms g_TerminatingNro + g_TerminatingNroDeadlineTick
        // and returns immediately.  We poll here every MainLoop iteration so the
        // loop never blocks.
        //
        // Happy path: the NRO exits within ~500 ms and CheckTerminated() becomes
        // true.  We call ForceTerminateNow() (which Joins + Closes the holder per
        // the AMS-1.11 clean-exit contract) then LaunchMenu(MainMenu).
        //
        // Deadline path: the NRO (e.g. sphaira) ignores RequestExit.  After 500 ms
        // CheckTerminated() is still false; we call ForceTerminateNow() anyway —
        // it issues a hard kernel Terminate (cmd 25 at 500 ms internal timeout),
        // Joins, and Closes.  LaunchMenu follows regardless of the terminate result.
        //
        // Fix D: ForceTerminateNow() never asserts; on any non-success result it
        // logs and returns so we always reach LaunchMenu and return to desktop.
        // -----------------------------------------------------------------------
        if(g_TerminatingNro) {
            const bool nro_done     = la::CheckTerminated();
            const bool deadline_hit = (armGetSystemTick() >= g_TerminatingNroDeadlineTick);

            if(nro_done || deadline_hit) {
                if(g_DevMode) {
                    HomeTrace(deadline_hit && !nro_done
                        ? "POLL: deadline elapsed — calling ForceTerminateNow"
                        : "POLL: NRO exited cleanly — calling ForceTerminateNow");
                }
                if(deadline_hit && !nro_done) {
                    UL_LOG_WARN("[MainLoop] NRO terminate deadline elapsed — forcing hard terminate");
                }

                // Fix D: ignore result; log inside ForceTerminateNow on failure.
                if(g_DevMode) { HomeTrace("BEFORE ForceTerminateNow"); }
                const auto term_rc = la::ForceTerminateNow();
                if(g_DevMode) {
                    char _htbuf[96];
                    ::snprintf(_htbuf, sizeof(_htbuf),
                        "AFTER ForceTerminateNow rc=0x%08X", term_rc);
                    HomeTrace(_htbuf);
                }
                (void)term_rc; // already logged inside ForceTerminateNow

                g_TerminatingNro = false;
                g_TerminatingNroDeadlineTick = 0;

                // NRO-RELAUNCH FIX (v3.8.0 → v3.8.1) — FS-readiness probe added
                // after the sm-probe (Track A, 2026-06-19).
                //
                // CONFIRMED BY TRACE (v3.8.0 results):
                //   • ForceTerminateNow rc=0 — sphaira kernel-dead.
                //   • sm probe accepted after 1 × 10ms — sm was clear almost
                //     immediately; AMS sm deregistration is NOT the bottleneck.
                //   • LaunchMenu rc=0 — AM accepted the launch.
                //   • uMenu#2 poll[1..20/20] IsActive=YES — process alive for 2s.
                //   • umenu_boot_trace.log EMPTY; debug server :6010 never starts.
                //
                // CONCLUSION: uMenu#2 launches and stays alive but hangs before its
                // first SD write.  The first SD operation in uMenu#2's __appInit is
                // fsdevMountSdmc (= fsOpenSdCardFileSystem to fsp-srv).  Since sm
                // clears in ~10ms yet the hang persists, fsp-srv is the bottleneck:
                // sphaira's SD filesystem session (opened via fsdevMountSdmc in its
                // own __appInit) is not yet released by fsp-srv's server thread when
                // uMenu#2 tries to open its own session.
                //
                // FIX — four-phase drain (sm probe retained; FS probe added):
                //   Phase 1: HandleAppletMessage — consumes the la-exit event,
                //     unblocks AM's applet slot accounting.
                //   Phase 2: sm-readiness probe (retained from v3.8.0) — proves AMS
                //     sm deregistration is done.  Still useful as a fast early gate.
                //   Phase 3: FS-readiness probe (NEW, evidence-based) — calls
                //     fsOpenSdCardFileSystem on a TEMPORARY FsFileSystem struct
                //     (not uSystem's own fsdev mount — that is managed by libnx's
                //     global fs session, which stays alive throughout).  On success
                //     the SD session is immediately closed with fsFsClose.  This
                //     proves fsp-srv has released sphaira's SD session and is ready
                //     to hand one to uMenu#2.  Retry up to 30 × 10ms = 300ms.
                //   Phase 4: HandleAppletMessage — consume any compositor events
                //     that arrived during the probes.  Then call LaunchMenu.
                //
                // SAFETY: fsOpenSdCardFileSystem opens a NEW IFileSystem session
                // against fsp-srv (IPC cmd 18) distinct from uSystem's fsdev mount.
                // The fsFsClose immediately releases it.  uSystem's own SD mount
                // (fsdevMountSdmc / g_sdmcFs) is untouched.  __nx_fs_num_sessions=3
                // gives us a session budget; the probe borrows one slot for <1ms.

                HandleAppletMessage();   // Phase 1: consume library-applet exit event
                if(g_DevMode) { HomeTrace("NRO: Phase1 drain done — entering sm-clear poll"); }
                UL_LOG_INFO("[MainLoop] NRO terminated — polling sm-clear (max 300ms) before FS probe");

                // Phase 2: sm-readiness probe (retained from v3.8.0).
                // svcConnectToNamedPort does not touch libnx g_smSrv — safe while
                // uSystem is already sm-initialized.  Max wait: 30 × 10 ms = 300 ms.
                {
                    constexpr int   kMaxSmPollIter  = 30;       // 30 × 10 ms = 300 ms max
                    constexpr u64   kSmPollSleepNs  = 10'000'000ul;
                    bool sm_ready = false;
                    for(int _i = 0; _i < kMaxSmPollIter; _i++) {
                        Handle _sm_probe = INVALID_HANDLE;
                        // Try to open a NEW named-port connection to sm:.
                        // This blocks until AMS sm accepts the connection (port accept loop).
                        // On success, sm is alive and accepting; close immediately.
                        // We do NOT call any IPC on it — the accept itself proves sm is
                        // no longer in a blocking teardown loop for sphaira's services.
                        const Result sm_rc = svcConnectToNamedPort(&_sm_probe, "sm:");
                        if(R_SUCCEEDED(sm_rc)) {
                            svcCloseHandle(_sm_probe);
                            sm_ready = true;
                            if(g_DevMode) {
                                char _htbuf[96];
                                ::snprintf(_htbuf, sizeof(_htbuf),
                                    "NRO: sm port accepted after %d × 10ms polls (~%d ms)",
                                    _i + 1, (_i + 1) * 10);
                                HomeTrace(_htbuf);
                            }
                            UL_LOG_INFO("[MainLoop] sm port accepted after %d poll(s) (~%d ms) — proceeding to FS probe",
                                        _i + 1, (_i + 1) * 10);
                            break;
                        }
                        // svcConnectToNamedPort failed (should be rare on a live system).
                        // Log result in dev mode and retry after 10 ms.
                        if(g_DevMode) {
                            char _htbuf[96];
                            ::snprintf(_htbuf, sizeof(_htbuf),
                                "NRO: sm probe[%d] rc=0x%08X — retry", _i + 1, sm_rc);
                            HomeTrace(_htbuf);
                        }
                        if(_sm_probe != INVALID_HANDLE) { svcCloseHandle(_sm_probe); }
                        svcSleepThread(kSmPollSleepNs);
                    }
                    if(!sm_ready) {
                        if(g_DevMode) { HomeTrace("NRO: sm poll TIMED OUT (300ms) — proceeding to FS probe"); }
                        UL_LOG_WARN("[MainLoop] sm poll timed out after 300ms — proceeding to FS probe");
                    }
                }

                // Phase 3: FS-readiness probe (Track A, 2026-06-19).
                //
                // Open a raw fsOpenSdCardFileSystem session against fsp-srv and
                // immediately close it.  This succeeds only after fsp-srv has
                // released sphaira's SD IFileSystem session (object destruction
                // on the fsp-srv server thread after sphaira's process handle is
                // closed by the kernel).
                //
                // We use a LOCAL FsFileSystem struct — NOT uSystem's own fsdev
                // mount — so there is zero risk of corrupting the SD vfs that
                // uSystem itself uses (fsdevMountSdmc uses a separate global).
                // On success we call fsFsClose immediately to release the slot.
                // On failure we sleep 10ms and retry (up to 300ms).
                if(g_DevMode) { HomeTrace("NRO: entering FS-readiness probe (max 300ms)"); }
                UL_LOG_INFO("[MainLoop] Starting FS-readiness probe (max 300ms) before LaunchMenu");
                {
                    constexpr int   kMaxFsPollIter = 30;       // 30 × 10 ms = 300 ms max
                    constexpr u64   kFsPollSleepNs = 10'000'000ul;
                    bool fs_ready = false;
                    for(int _fi = 0; _fi < kMaxFsPollIter; _fi++) {
                        FsFileSystem _probe_fs;
                        const Result fs_rc = fsOpenSdCardFileSystem(&_probe_fs);
                        if(R_SUCCEEDED(fs_rc)) {
                            // SD session granted — fsp-srv is ready. Release immediately.
                            fsFsClose(&_probe_fs);
                            fs_ready = true;
                            if(g_DevMode) {
                                char _htbuf[128];
                                ::snprintf(_htbuf, sizeof(_htbuf),
                                    "NRO: FS probe OK after %d × 10ms polls (~%d ms)",
                                    _fi + 1, (_fi + 1) * 10);
                                HomeTrace(_htbuf);
                            }
                            UL_LOG_INFO("[MainLoop] FS probe: SD session opened+closed after %d poll(s) (~%d ms) — LaunchMenu is safe",
                                        _fi + 1, (_fi + 1) * 10);
                            break;
                        }
                        // fsp-srv rejected or blocked — retry after 10ms.
                        if(g_DevMode) {
                            char _htbuf[96];
                            ::snprintf(_htbuf, sizeof(_htbuf),
                                "NRO: FS probe[%d] rc=0x%08X — retry", _fi + 1, fs_rc);
                            HomeTrace(_htbuf);
                        }
                        svcSleepThread(kFsPollSleepNs);
                    }
                    if(!fs_ready) {
                        if(g_DevMode) { HomeTrace("NRO: FS probe TIMED OUT (300ms) — LaunchMenu anyway"); }
                        UL_LOG_WARN("[MainLoop] FS probe timed out after 300ms — proceeding with LaunchMenu anyway");
                    }
                }

                HandleAppletMessage();   // Phase 4: consume compositor-ready notifications
                if(g_DevMode) { HomeTrace("NRO: Phase4 drain done — calling LaunchMenu"); }

                if(g_DevMode) { HomeTrace("BEFORE LaunchMenu(MainMenu)"); }
                const auto launch_rc = LaunchMenu(ul::smi::MenuStartMode::MainMenu);

                if(g_DevMode) {
                    char _htbuf[128];
                    ::snprintf(_htbuf, sizeof(_htbuf),
                        "AFTER LaunchMenu rc=0x%08X", launch_rc);
                    HomeTrace(_htbuf);
                }

                if(R_FAILED(launch_rc)) {
                    // Fix D: log failure; do not assert.
                    UL_LOG_WARN("[MainLoop] LaunchMenu after NRO terminate failed: %s",
                        ul::util::FormatResultDisplay(launch_rc).c_str());
                    if(g_DevMode) { HomeTrace("LaunchMenu FAILED after NRO terminate"); }
                }
            }
            // else: NRO still running and deadline not yet reached — keep iterating.
        }

        // -----------------------------------------------------------------------
        // Fix 1 — non-blocking APPLICATION terminate poll.
        //
        // HandleHomeButton() arms g_TerminatingApp + g_TerminatingAppDeadlineTick
        // and returns immediately.  We poll here every MainLoop iteration.
        //
        // Happy path: the application exits within ~500 ms and CheckTerminated()
        // becomes true.  We call ForceTerminateNow() then LaunchMenu(MainMenu)
        // (NORMAL mode — NOT MenuApplicationSuspended) so uMenu starts cleanly.
        //
        // Deadline path: after 500 ms we call ForceTerminateNow() regardless,
        // which issues appletApplicationTerminate (hard kill) then Closes the
        // holder.  LaunchMenu follows regardless of the terminate result.
        //
        // Failures are logged, never asserted — we always reach LaunchMenu.
        // -----------------------------------------------------------------------
        // [REMOVED 2026-06-21] The post-game g_TerminatingApp terminate + PRIME-RECREATE
        // double-create handler lived here; it regressed HOME-from-game into am 2128-0035
        // (creating a 2nd uMenu into am's single live-library-applet slot). HOME-over-game now
        // SUSPENDS the game in HandleHomeButton (restored v3.7.0 behavior), so there is no
        // post-game terminate/re-create. g_TerminatingApp is now unused on the app path; the
        // g_TerminatingNro NRO-hang handler below is intentionally kept.

        // T3.0 ROLLBACK 2026-05-19: overlay action dispatch DISABLED.
        // Previous T3.0 build crashed uSystem at boot because the render
        // thread called hidGetTouchScreenStates without hidInitialize.
        // Action layer (this code) is innocent — but the flags are never
        // set by anyone now, so the dispatch would never fire even if
        // re-enabled.  Re-enables in T3.0.1 after hid init validates.
        //
        // if(ul::system::overlay::g_RequestClose.exchange(false, std::memory_order_acq_rel)) {
        //     HandleHomeButton();
        // }
        // if(ul::system::overlay::g_RequestMinimize.exchange(false, std::memory_order_acq_rel)) {
        //     HandleHomeButton();
        // }

        HandleGeneralChannelMessage();
        HandleAppletMessage();
        HandleMenuMessage();

        auto consumed_action = false;
        // Try to consume one action per loop.
        // Cycle G4 (SP4.15): only log queue depth when it CHANGES — the
        // previous unconditional log fired every frame an action was
        // pending, producing ~60 entries/sec of identical output that
        // crowded the log buffer and slowed the UI.
        static size_t s_last_logged_depth = 0;
        const size_t depth = g_ActionQueue.size();
        if(depth != s_last_logged_depth) {
            if(depth > 0) {
                UL_LOG_INFO("Action queue has %lu actions", depth);
            }
            s_last_logged_depth = depth;
        }
        for(u32 i = 0; i < g_ActionQueue.size(); i++) {
            auto &action = g_ActionQueue.at(i);
            consumed_action |= HandleAction(action);
            if(consumed_action) {
                // Action consumed, remove it from the queue
                g_ActionQueue.erase(g_ActionQueue.begin() + i);
                break;
            }
        }

        auto something_done = consumed_action;

        // -----------------------------------------------------------------------
        // SELF-HEAL WATCHDOG — MainLoop check (2026-06-20)
        //
        // If uMenu is running and has sent at least one heartbeat but the last
        // beat is stale by > kWatchdogStaleTicks (~30s), uMenu is silently hung
        // (display black, process alive, not crashing).  Force-recover it.
        //
        // Recovery: terminate any active la::/app::, then LaunchMenu(MainMenu).
        //
        // After kWatchdogMaxFails consecutive failed recoveries (the watchdog fires
        // again without a heartbeat in between), escalate to fatalThrow(0xCAFE) so
        // Atmosphère's fatal auto-reboot cleans up the state in ~50s.
        //
        // The check runs every MainLoop iteration (~10ms cadence) but the stale
        // threshold is 30s, so it almost never fires.  Skipped when g_DevMode is
        // false: the watchdog relies on the Heartbeat IPC message which uMenu only
        // sends when qd_AppletLifecycle.cpp is compiled in (QDESKTOP_MODE).
        // In non-dev / non-qdesktop builds the heartbeat never arrives, so
        // g_LastMenuHeartbeatTick stays 0 and the watchdog never activates.
        // -----------------------------------------------------------------------
        if(g_LastMenuHeartbeatTick != 0 && IsMenuRunning()) {
            const u64 now_wd_tick = armGetSystemTick();
            if((now_wd_tick - g_LastMenuHeartbeatTick) > kWatchdogStaleTicks) {
                g_WatchdogFailCount++;
                UL_LOG_WARN("[Watchdog] uMenu heartbeat STALE (>30s, fail #%u/%u) — force-recovering",
                            g_WatchdogFailCount, kWatchdogMaxFails);

                if(g_WatchdogFailCount >= kWatchdogMaxFails) {
                    // Too many consecutive recovery failures — nuclear option.
                    UL_LOG_WARN("[Watchdog] %u consecutive failures — fatalThrow(0xCAFE) → CFW auto-reboot",
                                kWatchdogMaxFails);
                    fatalThrow(0xCAFE);
                    // (unreachable — Atmosphère fatal handler takes over)
                }

                // Reset the heartbeat tick so the watchdog doesn't fire again
                // immediately while the recovery is in progress (LaunchMenu takes
                // a frame or two before uMenu starts sending beats again).
                g_LastMenuHeartbeatTick = armGetSystemTick();

                // Same force-recovery as cmd_resetmenu, but without the SD-poll
                // (this path must be non-blocking to keep the main loop alive).
                g_TerminatingNro = false;
                g_TerminatingNroDeadlineTick = 0;
                g_TerminatingApp = false;
                g_TerminatingAppDeadlineTick = 0;

                // Kill the active library applet (uMenu or NRO) if live.
                if(la::IsActive()) {
                    const auto la_rc = la::ForceTerminateNow();
                    if(R_FAILED(la_rc)) {
                        UL_LOG_WARN("[Watchdog] la::ForceTerminateNow failed: 0x%08X", la_rc);
                    }
                }
                if(app::IsActive()) {
                    const auto app_rc = app::ForceTerminateNow();
                    if(R_FAILED(app_rc)) {
                        UL_LOG_WARN("[Watchdog] app::ForceTerminateNow failed: 0x%08X", app_rc);
                    }
                    g_LastHomebrewApplicationLaunchTarget = {};
                }

                g_LastLibraryAppletLaunchedNotMenu = false;
                sys::SetForeground();

                const auto wd_launch_rc = LaunchMenu(ul::smi::MenuStartMode::MainMenu);
                if(R_FAILED(wd_launch_rc)) {
                    UL_LOG_WARN("[Watchdog] LaunchMenu failed: 0x%08X (fail count stays at %u)",
                                wd_launch_rc, g_WatchdogFailCount);
                }
                else {
                    UL_LOG_INFO("[Watchdog] uMenu relaunched in NORMAL mode — waiting for next heartbeat");
                }
            }
        }

        // Handle finalized active library applet (NRO self-exit path — not via HOME)

        if(!la::IsActive() && g_LastLibraryAppletLaunchedNotMenu) {
            // The library applet that we launched and was active just finished, and it's not uMenu

            UL_LOG_INFO("Active library applet finished...");

            // If we launched uLoader to choose a homebrew, collect the output now
            // (must be done before the NRO holder is fully stale).
            ul::loader::TargetOutput target_opt;
            if(g_ExpectsLoaderChooseOutput) {
                UL_LOG_INFO("Getting loader chosen output...");

                AppletStorage target_opt_st;
                UL_RC_ASSERT(la::Pop(&target_opt_st));
                UL_RC_ASSERT(appletStorageRead(&target_opt_st, 0, &target_opt, sizeof(target_opt)));
            }

            // Pick correct uMenu state to launch to, and launch uMenu
            auto menu_start_mode = ul::smi::MenuStartMode::MainMenu;
            if(g_NextMenuLaunchAtStartup) {
                menu_start_mode = ul::smi::MenuStartMode::StartupMenu;
                g_NextMenuLaunchAtStartup = false;
            }
            if(g_NextMenuLaunchAtSettings) {
                menu_start_mode = ul::smi::MenuStartMode::SettingsMenu;
                g_NextMenuLaunchAtSettings = false;
            }
            UL_RC_ASSERT(LaunchMenu(menu_start_mode));

            // If we collected output from uLoader, send it to the just-launched uMenu
            if(g_ExpectsLoaderChooseOutput) {
                ul::smi::MenuMessageContext msg_ctx = {
                    .msg = ul::smi::MenuMessage::ChosenHomebrew,
                    .chosen_hb = {}
                };
                memcpy(msg_ctx.chosen_hb.nro_path, target_opt.nro_path, sizeof(msg_ctx.chosen_hb.nro_path));
                PushMenuMessageContext(msg_ctx);
                g_ExpectsLoaderChooseOutput = false;
            }

            something_done = true;
        }

        // Final check

        const auto prev_applet_active = g_IsLibraryAppletActive;
        g_IsLibraryAppletActive = la::IsActive();
        if(!something_done && !prev_applet_active) {
            // If nothing was done but nothing is active, an application or library applet might have crashed, terminated, failed to launch...
            if(!app::IsActive() && !la::IsActive()) {
                UL_LOG_INFO("No application or library applet is active, "
                            "checking for application launch failure...");

                if(hosversionAtLeast(6,0,0)) {
                    auto terminate_rc = ul::ResultSuccess;
                    if(R_SUCCEEDED(nsGetApplicationTerminateResult(app::GetId(), &terminate_rc))) {
                        UL_LOG_WARN("Application 0x%016lX terminated with result %s", app::GetId(), ul::util::FormatResultDisplay(terminate_rc).c_str());
                    }
                }

                g_LastHomebrewApplicationLaunchTarget = {};

                // FG-FIX (2026-06-21): reclaim foreground before relaunching uMenu — the
                // prior foreground owner (terminated game/applet) is gone and am rejects a
                // foreground library-applet launch without it (2128-0035), which otherwise
                // turns this safety-net into the HOME-from-game crash-loop seen in the logs.
                {
                    const auto fg_rc = sys::SetForeground();
                    if(R_FAILED(fg_rc)) {
                        UL_LOG_WARN("[MainLoop] launch-failure-recovery SetForeground failed: %s — continuing",
                            ul::util::FormatResultDisplay(fg_rc).c_str());
                    }
                }

                // Reopen uMenu, notify failure
                UL_RC_ASSERT(LaunchMenu(ul::smi::MenuStartMode::MainMenu));
                PushSimpleMenuMessage(ul::smi::MenuMessage::PreviousLaunchFailure);
            }
        }

        svcSleepThread(10'000'000ul);
    }

    std::vector<NsExtApplicationRecord> ListAddedRecords(const std::vector<NsExtApplicationRecord> &old_records, const std::vector<NsExtApplicationRecord> &cur_records) {
        std::unordered_set<u64> prev_app_ids;
        for(const auto &rec: old_records) {
            prev_app_ids.insert(rec.id);
        }

        std::vector<NsExtApplicationRecord> added;
        for(const auto &rec: cur_records) {
            if(prev_app_ids.find(rec.id) == prev_app_ids.end()) {
                added.push_back(rec);
            }
        }
        return added;
    }

    std::vector<NsExtApplicationRecord> ListRemovedRecords(const std::vector<NsExtApplicationRecord> &old_records, const std::vector<NsExtApplicationRecord> &cur_records) {
        std::unordered_set<u64> cur_app_ids;
        for(const auto &rec: cur_records) {
            cur_app_ids.insert(rec.id);
        }

        std::vector<NsExtApplicationRecord> removed;
        for(const auto &rec: old_records) {
            if(cur_app_ids.find(rec.id) == cur_app_ids.end()) {
                removed.push_back(rec);
            }
        }
        return removed;
    }

    void EventManagerMain(void*) {
        UL_LOG_INFO("[EventManager] alive!");
        
        Event record_ev;
        UL_RC_ASSERT(nsGetApplicationRecordUpdateSystemEvent(&record_ev));
        UL_LOG_INFO("[EventManager] registered ApplicationRecordUpdateSystemEvent");

        Event gc_mount_fail_event;
        if(hosversionAtLeast(3,0,0)) {
            UL_RC_ASSERT(nsGetGameCardMountFailureEvent(&gc_mount_fail_event));
            UL_LOG_INFO("[EventManager] registered GameCardMountFailureEvent");
        }
        else {
            UL_LOG_INFO("[EventManager] cannot register GameCardMountFailureEvent, unsuported by firmware!");
        }

        s32 ev_idx;
        while(true) {
            auto wait_rc = ul::ResultSuccess;
            if(hosversionAtLeast(3,0,0)) {
                wait_rc = waitMulti(&ev_idx, UINT64_MAX, waiterForEvent(&record_ev), waiterForEvent(&gc_mount_fail_event));
            }
            else {
                wait_rc = waitMulti(&ev_idx, UINT64_MAX, waiterForEvent(&record_ev));
            }

            if(R_SUCCEEDED(wait_rc)) {
                if(ev_idx == 0) {
                    ul::ScopedLock lock(g_CurrentRecordsLock);
                    ul::ScopedLock lock2(g_LastDeletedApplicationsLock);
                    ul::ScopedLock lock3(g_LastAddedApplicationsLock);
                    g_LastAddedApplications.clear();
                    g_LastDeletedApplications.clear();
                    UL_LOG_INFO("[EventManager] Application records changed!");

                    std::vector<AccountUid> uids;
                    UL_RC_ASSERT(accountInitialize(AccountServiceType_System));
                    UL_RC_ASSERT(ul::acc::ListAccounts(uids));
                    accountExit();

                    const auto old_records = std::move(g_CurrentRecords);
                    g_CurrentRecords = ul::os::ListApplicationRecords();

                    const auto added_records = ListAddedRecords(old_records, g_CurrentRecords);
                    for(const auto &record: added_records) {
                        UL_LOG_INFO("[EventManager] > Added application 0x%016lX, caching it...", record.id);
                        g_LastAddedApplications.push_back(record.id);
                        ul::system::app::RequestCacheApplication(record.id);

                        for(const auto &uid: uids) {
                            const auto menu_path = ul::menu::MakeMenuPath(g_AmsIsEmuMMC, uid);
                            UL_LOG_INFO("[EventManager] > Ensuring application ID 0x%016lX entry in user menu (%s)", record.id, menu_path.c_str());
                            ul::menu::EnsureApplicationEntry(record, menu_path);
                        }
                    }

                    const auto removed_records = ListRemovedRecords(old_records, g_CurrentRecords);
                    for(const auto &record: removed_records) {
                        UL_LOG_INFO("[EventManager] > Deleted application 0x%016lX, removing cache...", record.id);
                        NotifyApplicationDeleted(record.id);
                        ul::system::app::RequestRemoveApplicationCache(record.id);

                        for(const auto &uid: uids) {
                            const auto menu_path = ul::menu::MakeMenuPath(g_AmsIsEmuMMC, uid);
                            ul::menu::DeleteApplicationEntryRecursively(record.id, menu_path);
                        }
                    }

                    // Only push this if uMenu is currently active
                    if(IsMenuRunning()) {
                        const ul::smi::MenuMessageContext msg_ctx = {
                            .msg = ul::smi::MenuMessage::ApplicationRecordsChanged,
                            .app_records_changed = {
                                .records_added_or_deleted = !added_records.empty() || !removed_records.empty(),
                            }
                        };
                        PushMenuMessageContext(msg_ctx);
                    }
                }
                else if(ev_idx == 1) {
                    eventClear(&gc_mount_fail_event);

                    const auto fail_rc = nsGetLastGameCardMountFailureResult();
                    UL_LOG_INFO("[EventManager] Gamecard mount failed with rc: 0x%X", fail_rc);

                    const ul::smi::MenuMessageContext msg_ctx = {
                        .msg = ul::smi::MenuMessage::GameCardMountFailure,
                        .gc_mount_failure = {
                            .mount_rc = fail_rc
                        }
                    };
                    PushMenuMessageContext(msg_ctx);
                }
            }

            svcSleepThread(100'000ul);
        }
    }

    size_t CaptureJpegScreenshot() {
        u64 size;
        const auto rc = capsscCaptureJpegScreenShot(&size, g_UsbViewerBufferDataOffset, PlainRgbaScreenBufferSize, ViLayerStack_Default, UINT64_MAX);
        if(R_SUCCEEDED(rc)) {
            return size;
        }
        else {
            return 0;
        }
    }

    void UsbViewerWriteThread(void*) {
        while(true) {
            rwlockWriteLock(&g_UsbRwLock);
            usbCommsWrite(g_UsbViewerBuffer, UsbPacketSize);
            rwlockWriteUnlock(&g_UsbRwLock);
            svcSleepThread(1'000'000ul);
        }
    }

    void UsbViewerRgbaThread(void*) {
        bool tmp_flag;
        while(true) {
            rwlockReadLock(&g_UsbRwLock);
            appletGetLastForegroundCaptureImageEx(g_UsbViewerBufferDataOffset, PlainRgbaScreenBufferSize, &tmp_flag);
            appletUpdateLastForegroundCaptureImage();
            rwlockReadUnlock(&g_UsbRwLock);
            svcSleepThread(1'000'000ul);
        }
    }

    void UsbViewerJpegThread(void*) {
        while(true) {
            rwlockReadLock(&g_UsbRwLock);
            g_UsbViewerBuffer->jpeg.size = (u32)CaptureJpegScreenshot();
            rwlockReadUnlock(&g_UsbRwLock);
            svcSleepThread(1'000'000ul);
        }
    }

    void CheckHomebrewTakeoverApplicationId() {
        ul::ScopedLock lk(g_ConfigLock);
        u64 hb_application_takeover_program_id;
        UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, hb_application_takeover_program_id));

        bool need_pick = (hb_application_takeover_program_id == ul::os::InvalidApplicationId);
        if(!need_pick) {
            // Simple command, not involving buffers (unlike GetApplicationControlData) that will fail if the ID is invalid
            const auto valid_app = R_SUCCEEDED(nsTouchApplication(hb_application_takeover_program_id));
            UL_LOG_INFO("Homebrew take-over application ID: 0x%016lX, valid: %d", hb_application_takeover_program_id, valid_app);
            if(!valid_app) {
                // The previously-chosen host was uninstalled — re-pick automatically.
                need_pick = true;
            }
        }

        if(need_pick) {
            // FULL-MODE AUTO-HOST (2026-06-14): Q OS owns "full application mode"
            // homebrew without the user ever choosing a game.  Horizon only grants
            // save-WRITE permissions to a type-1 application slot, so full-mode
            // homebrew must run inside SOME installed application's slot.  We pick
            // one automatically here — no specific game required, works with
            // whatever the user already has.  The host's launch slot is borrowed
            // ONLY for the instant of launch (RegisterLaunchAsApplication); its
            // save data is never touched.  Consoles with zero installed
            // applications stay on applet mode (uMenu falls back) until the
            // bundled Q OS forwarder lands.
            u64 picked = ul::os::InvalidApplicationId;
            for(const auto &rec : g_CurrentRecords) {
                if(R_SUCCEEDED(nsTouchApplication(rec.id))) {
                    picked = rec.id;
                    break;
                }
            }
            g_Config.SetEntry(ul::cfg::ConfigEntryId::HomebrewApplicationTakeoverApplicationId, picked);
            ul::cfg::SaveConfig(g_Config);
            if(picked != ul::os::InvalidApplicationId) {
                UL_LOG_INFO("Auto-selected homebrew full-mode host application: 0x%016lX (of %zu records)", picked, g_CurrentRecords.size());
            }
            else {
                UL_LOG_INFO("No installed application available as a full-mode host — applet fallback (%zu records)", g_CurrentRecords.size());
            }
        }
    }

    void Initialize() {
        UL_RC_ASSERT(appletLoadAndApplyIdlePolicySettings());
        UpdateOperationMode();

        // DEV gate: check for the debug flag file once at startup.
        // sdmc: is already mounted at this point (fsdevMountSdmc was called
        // in InitializeSystemModule before Initialize()).  ExistsFile uses
        // a single stat() call — cheap, synchronous, no heap alloc.
        g_DevMode = ul::fs::ExistsFile("sdmc:/ulaunch/debug.flag");
        if(g_DevMode) {
            UL_LOG_INFO("[DEV] debug.flag present — HOME-button remote trigger enabled "
                        "(cmd_home polling active)");
            // DEV: keep the console awake. Auto-sleep drops WiFi and strands all
            // remote work (debug server :6010 / FTP :5000) — and uMenu's own
            // appletSetAutoSleepDisabled only holds while uMenu is foreground, so
            // it sleeps the moment uMenu Finalizes (NRO launch / transition).
            // uSystem is always resident, so assert it here too and re-assert in
            // the MainLoop below. Never ships (g_DevMode = debug.flag only).
            appletSetAutoSleepDisabled(true);
        }

        g_AmsIsEmuMMC = ul::os::IsEmuMMC();

        // Clean old cache
        ul::fs::DeleteDirectory(ul::PreV100ApplicationCachePath);
        ul::fs::DeleteDirectory(ul::PreV100HomebrewCachePath);
        ul::fs::DeleteDirectory(ul::PreV100AccountCachePath);

        // Ensure root cache directory exists
        ul::fs::CreateDirectory(ul::RootCachePath);

        // Clean cache, everything except for theme cache
        ul::fs::CleanDirectory(ul::HomebrewCachePath);
        ul::fs::CleanDirectory(ul::ThemePreviewCachePath);

        g_CurrentRecords = ul::os::ListApplicationRecords();
        g_BootInitTick = armGetSystemTick();
        ul::system::app::InitializeControlCache(g_CurrentRecords);
        ul::menu::CacheHomebrew();
        CheckHomebrewTakeoverApplicationId();

        // BOOT-SPEED (2026-06-13): DebugLogApplicationParameters() was self-
        // described "Test" telemetry that issued 2 extra ns calls per installed
        // app on the boot critical path (contending for the single ns session),
        // producing only the [!Flags] debug dump.  Gated off to free the ns
        // service during launch.  Uncomment to restore the diagnostic.
        // DebugLogApplicationParameters();

        LoadConfig();

        UL_RC_ASSERT(sf::Initialize());

        // UL_COMPUTE_LED: JoyCon heartbeat — flashes while MainLoop is alive.
        // Frozen LED = uSystem is stuck. See ul/system/led/led_ComputeIndicator.hpp.
        ul::system::led::Initialize();

        UL_RC_ASSERT(threadCreate(&g_EventManagerThread, EventManagerMain, nullptr, nullptr, EventManagerThreadStackSize, 34, -2));
        UL_RC_ASSERT(threadStart(&g_EventManagerThread));

        bool viewer_usb_enabled;
        {
            ul::ScopedLock lk(g_ConfigLock);
            UL_ASSERT_TRUE(g_Config.GetEntry(ul::cfg::ConfigEntryId::UsbScreenCaptureEnabled, viewer_usb_enabled));
        }
        if(viewer_usb_enabled) {
            UL_RC_ASSERT(usbCommsInitialize());
            UL_RC_ASSERT(capsscInitialize());

            g_UsbViewerBuffer = reinterpret_cast<UsbPacketHeader*>(__libnx_aligned_alloc(ams::os::MemoryPageSize, UsbPacketSize));
            memset(g_UsbViewerBuffer, 0, UsbPacketSize);

            void(*thread_read_fn)(void*) = nullptr;
            g_UsbViewerBufferDataOffset = reinterpret_cast<u8*>(g_UsbViewerBuffer) + sizeof(UsbMode) + sizeof(UsbPacketHeader::jpeg);
            if(hosversionAtLeast(9,0,0) && (CaptureJpegScreenshot() > 0)) {
                g_UsbViewerBuffer->mode = UsbMode::Jpeg;
                thread_read_fn = &UsbViewerJpegThread;
            }
            else {
                g_UsbViewerBuffer->mode = UsbMode::Rgba;
                g_UsbViewerBufferDataOffset = reinterpret_cast<u8*>(g_UsbViewerBuffer) + sizeof(UsbMode);
                thread_read_fn = &UsbViewerRgbaThread;
                capsscExit();
            }

            UL_RC_ASSERT(threadCreate(&g_UsbViewerReadThread, thread_read_fn, nullptr, g_UsbViewerReadThreadStack, sizeof(g_UsbViewerReadThreadStack), 30, -2));
            UL_RC_ASSERT(threadStart(&g_UsbViewerReadThread));
            UL_RC_ASSERT(threadCreate(&g_UsbViewerWriteThread, &UsbViewerWriteThread, nullptr, g_UsbViewerWriteThreadStack, sizeof(g_UsbViewerWriteThreadStack), 28, -2));
            UL_RC_ASSERT(threadStart(&g_UsbViewerWriteThread));
        }

        // v3.1 Phase 2 Slice T1 — Tesla overlay model foundation.
        //
        // Creates uSystem's persistent vi:m max-Z managed layer that survives
        // applet transitions and renders chrome (cyan border + navy titlebar
        // + ✕/− glyphs) on top of uMenu, homebrew NROs, AND retail games.
        //
        // DISABLED 2026-05-19 (HW report): the persistent chrome stays
        // visible during applet launch transitions, making it look like
        // the launching app is loading inside a window (user-reported red-
        // ish chrome border + "flash like loading inside a window").  See
        // docs/Z-RIGHT-CLICK-OS-DESIGN.md and CHANGELOG.md v3.1.0.
        //
        // To re-enable when actively developing T3+ (touch input dispatch
        // back to uMenu) or future T-series work, flip UL_ENABLE_TESLA_OVERLAY
        // to 1.  The Initialize/Finalize calls are kept so the include +
        // overlay library remain referenced; no churn needed to flip back on.
        //
        // See docs/50_v3.1_phase2_implementation_plan.md §v3.1 architecture
        // pivot for the Tesla overlay rationale.
#define UL_ENABLE_TESLA_OVERLAY 0
#if UL_ENABLE_TESLA_OVERLAY
        const Result __qos_overlay_rc = ul::system::overlay::InitializeTestLayer();
        if(R_FAILED(__qos_overlay_rc)) {
            UL_LOG_WARN("uSystem: overlay::InitializeTestLayer rc=0x%08X — "
                        "overlay disabled this boot; uMenu boots normally",
                        __qos_overlay_rc);
        }
#else
        UL_LOG_INFO("uSystem: Tesla overlay GATED OFF "
                    "(UL_ENABLE_TESLA_OVERLAY=0) — no persistent chrome");
#endif
    }

}

extern "C" {

    extern u8 *fake_heap_start;
    extern u8 *fake_heap_end;

}

// TODO (low priority): stop using Atmosphere-libs?

namespace ams {

    namespace init {

        void InitializeSystemModule() {
            __nx_applet_type = AppletType_SystemApplet;
            __nx_fs_num_sessions = 3;

            UL_RC_ASSERT(sm::Initialize());
            UL_RC_ASSERT(fsInitialize());
            
            UL_RC_ASSERT(appletInitialize());

            UL_RC_ASSERT(nsInitialize());
            UL_RC_ASSERT(ldrShellInitialize());
            UL_RC_ASSERT(pmshellInitialize());
            UL_RC_ASSERT(setsysInitialize());

            // FS and log init is intentionally done at the end, otherwise the SD doesn't seem to be always ready...?
            UL_RC_ASSERT(fsdevMountSdmc());
            ul::InitializeLogging("uSystem");
        }

        void FinalizeSystemModule() {
            // UL_COMPUTE_LED: shut the LED off cleanly before service teardown.
            ul::system::led::Finalize();

            // v3.1 Phase 2 Slice T1: tear down the overlay layer first so
            // the render thread joins cleanly before vi/applet exit.
            // No-op when UL_ENABLE_TESLA_OVERLAY=0 (init never ran), but
            // FinalizeTestLayer is idempotent so calling it is safe.
            ul::system::overlay::FinalizeTestLayer();

            setsysExit();
            capsscExit();
            pmshellExit();
            ldrShellExit();
            nsExit();
            fsdevUnmountAll();
            fsExit();
            appletExit();
        }

        void Startup() {
            // libstratosphere heap: used for malloc/free/new/delete and everything using them
            init::InitializeAllocator(g_LibstratosphereHeap, LibstratosphereHeapSize);

            // libnx heap: used for internal malloc_r/etc called by stdlib stuff
            fake_heap_start = g_LibnxHeap;
            fake_heap_end = fake_heap_start + LibnxHeapSize;

            g_MenuMessageQueue = new std::queue<ul::smi::MenuMessageContext>();
            g_ApplicationVerifyContexts = new std::vector<ApplicationVerifyContext>();

            os::SetThreadNamePointer(os::GetCurrentThread(), "ul.system.Main");
        }

    }

    NORETURN void Exit(int rc) {
        AMS_UNUSED(rc);
        UL_RC_ASSERT(false && "Unexpected exit called by system applet (uSystem)");
        AMS_ABORT();
    }

    // uSystem handles basic qlaunch functionality since it is the back-end of the project, communicating with uMenu when neccessary

    void Main() {
        // Initialize everything
        Initialize();

        UL_LOG_INFO("Hello World from uSystem! Launching uMenu...");

        // After having initialized everything, launch our menu
        UL_RC_ASSERT(LaunchMenu(ul::smi::MenuStartMode::StartupMenuPostBoot));

        // Loop forever, since qlaunch should NEVER terminate (AM would crash in that case)
        while(true) {
            MainLoop();
        }
    }

}
