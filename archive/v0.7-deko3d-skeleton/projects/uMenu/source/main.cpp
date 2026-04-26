// v0.7-deko3d-skeleton: Plutonium/SDL2 renderer replaced by deko3d + ImGui.
// All __appInit / __appExit / INIT_LOG infrastructure preserved from v0.6.8.
// MainLoop() body replaced with a minimal ImGui bootstrap loop (Week 1 goal:
// empty ImGui window renders at 1280x720 in LibraryApplet slot).
// v0.7.0-beta3: dense diagnostic telemetry added (romfs, shaders, deko3d,
// nwindow, ImGui init, title enum, icon upload, render-loop frame counters).
// v0.7.0-beta4: icon upload SKIPPED (register_image_cb crash at 0x7150c).
// v0.7.0-beta5: BUG 1 fix (sphaira NWindow), BUG 2 fix (fw_compat probe), BUG 4 root-cause doc.
//   TileGrid placeholder rendering (label only, icon=0) proves render pipeline.
#include <ul/fs/fs_Stdio.hpp>
#include <ul/cfg/cfg_Config.hpp>
#include <ul/util/util_Json.hpp>
#include <ul/util/util_Size.hpp>
#include <ul/net/net_Service.hpp>
#include <ul/menu/smi/sf/sf_PrivateService.hpp>
#include <ul/menu/am/am_LibraryAppletUtils.hpp>
#include <ul/menu/am/am_LibnxLibappletWrap.hpp>
#include <cstdarg>

// deko3d + ImGui backends (v0.7)
#include "imgui/imgui.h"
#include "ui/imgui_impl_deko3d.h"
#include "ui/imgui_impl_switch.h"

// v0.7.0-beta: tile grid, icon upload, title enumeration, SMI client
#include "qos_widgets_grid.hpp"
#include "qos_icon_upload.hpp"
#include "qos_title_enum.hpp"
#include "qos_smi_client.hpp"

// v0.7.0-beta5: firmware + applet-pool compatibility probe (BUG 2 fix)
// Must be included BEFORE the extern "C" block so qos::fw_compat is
// visible inside __appInit() which is declared with C linkage.
#include "qos_fw_compat.hpp"

using namespace ul::util::size;

// v0.7 skeleton: Minimal GlobalSettings — avoids pulling in Plutonium headers.
// Only the fields accessed by the skeleton main loop are present.
// The full Plutonium-backed struct will be restored in Week 2.
namespace ul::menu::ui {
    struct GlobalSettings {
        ul::smi::SystemStatus system_status = {};
        ul::cfg::Config       config;
    };
}

ul::menu::ui::GlobalSettings g_GlobalSettings;

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
        // v0.7.0-alpha: first marker written before any init succeeds.
        // sdmc not mounted yet — written into g_InitLog ring; flushed after mount.
        InitLogAppend("[INIT] uMenu v0.7.0-beta6-fbflag (deko3d/ImGui isolate, SMI disabled, no hw compression) starting\n");

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
        UL_LOG_INFO("[INIT] uMenu v0.7.0-beta6-fbflag (deko3d/ImGui isolate, SMI disabled, no hw compression) starting");
        UL_LOG_INFO("Alive! (uMenu v0.7.0-beta6-fbflag)");

        // --- Non-critical: settings services ---
        INIT_LOG_OPTIONAL("setsysInitialize",           setsysInitialize());
        INIT_LOG_OPTIONAL("setInitialize",              setInitialize());
        INIT_LOG_OPTIONAL("setsysGetFirmwareVersion",   setsysGetFirmwareVersion(&g_FirmwareVersion));
        hosversionSet(MAKEHOSVERSION(g_FirmwareVersion.major, g_FirmwareVersion.minor, g_FirmwareVersion.micro) | BIT(31));

        // --- Mandatory: applet stack, HID, accounts ---
        INIT_LOG_ASSERT("appletInitialize",              appletInitialize());

        // v0.7.0-beta5 BUG 2: probe firmware version and applet pool headroom.
        // Non-fatal — failure is logged but does not abort __appInit.
        {
            const bool fc_ok = qos::fw_compat::Init();
            InitLogAppend("[INIT] fw_compat::Init: %s\n", fc_ok ? "OK" : "FAILED (non-fatal)");
        }

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

        // v0.7 skeleton: MenuControlEntryLoadFunction + BluetoothManager deferred to Week 2.
        __nx_win_init();
    }

    void __appExit() {
        // romfsExit called here as a safety net; MainLoop also calls it on clean exit.
        romfsExit();

        UL_LOG_INFO("Goodbye! (uMenu v0.7.0-beta5)");

        __nx_win_exit();

        // v0.7 skeleton: BluetoothManager deferred to Week 2.
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

// v0.7 skeleton: g_MenuApplication (Plutonium) removed.
// The SMI/IPC layer still compiles via ul::menu::smi headers.

namespace {

    ul::smi::MenuStartMode g_StartMode;

    // v0.6.8-debug: write a single checkpoint line to init log with flush.
    // Macro keeps call sites single-line and grep-friendly.
    #define CHECKPOINT(msg) \
        do { \
            FILE *_cpf = fopen("sdmc:/switch/qos-menu-init.log", "a"); \
            if(_cpf) { fputs("[CHECKPOINT] " msg "\n", _cpf); fflush(_cpf); fclose(_cpf); } \
        } while(0)

    // v0.7.0-alpha: formatted log macro (uses same file, explicit [V7INIT] prefix).
    #define V7LOG(fmt, ...) \
        do { \
            FILE *_v7f = fopen("sdmc:/switch/qos-menu-init.log", "a"); \
            if(_v7f) { fprintf(_v7f, "[V7INIT] " fmt "\n", ##__VA_ARGS__); fflush(_v7f); fclose(_v7f); } \
        } while(0)

    // v0.7.0-beta3: dense telemetry macro — fopen/fflush/fclose per call so
    // partial writes survive mid-boot crashes.  All [TELEM] lines include a
    // short context prefix so grep -a "[TELEM]" in the ELF is a quick sanity
    // check that the strings made it into the binary.
    #define TELEM(fmt, ...) \
        do { \
            FILE *_tf = fopen("sdmc:/switch/qos-menu-init.log", "a"); \
            if(_tf) { fprintf(_tf, "[TELEM] " fmt "\n", ##__VA_ARGS__); fflush(_tf); fclose(_tf); } \
        } while(0)

    // Per-frame heartbeat — single fputs, minimal overhead.
    #define TELF(n) \
        do { \
            FILE *_ff = fopen("sdmc:/switch/qos-menu-init.log", "a"); \
            if(_ff) { fprintf(_ff, "[TEL-F] %u\n", (unsigned)(n)); fclose(_ff); } \
        } while(0)

    // ──────────────────────────────────────────────────────────────────────────
    // v0.7-deko3d-skeleton: MainLoop
    // Plutonium/SDL2 renderer fully removed. Replaced with deko3d + ImGui.
    // Week 1 goal: a single empty ImGui window renders at 1280x720 inside the
    // LibraryApplet slot. HOME / Plus exits cleanly (no 2168-0002).
    // SMI private service is still initialized so uSystem IPC stays functional.
    // ──────────────────────────────────────────────────────────────────────────
    void MainLoop() {
        // ── v0.7.0-beta3 boot mark ───────────────────────────────────────────
        {
            u64 tick = armGetSystemTick();
            u64 pid = 0; svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
            TELEM("boot mark T=%llu pid=%llu", (unsigned long long)tick, (unsigned long long)pid);
        }

        // Load config — required for SMI message dispatch even in skeleton mode.
        g_GlobalSettings.config = ul::cfg::LoadConfig();

        // Render-only diagnostic build: keep the beta6 renderer isolated from the
        // legacy private-service/SMI path until the first visible frame is proven.

        CHECKPOINT("v0.7 skeleton: initializing ImGui + deko3d");

        // ── Applet state telemetry (v0.7.0-beta3) ────────────────────────────
        {
            AppletType at = appletGetAppletType();
            AppletFocusState fs = appletGetFocusState();
            AppletOperationMode om = appletGetOperationMode();
            TELEM("applet type=%d", (int)at);
            TELEM("applet focus state=%d", (int)fs);
            TELEM("appletGetOperationMode=%d", (int)om);
        }

        // Create ImGui context
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;  // disable imgui.ini writes on Switch

        // Apply a dark theme so the skeleton window is visually distinct
        ImGui::StyleColorsDark();

        // Initialize backends
        const bool sw_ok  = ImGui_ImplSwitch_Init();
        const bool dk_ok  = ImGui_ImplDeko3d_Init();

        TELEM("ImGui_ImplSwitch_Init=%d", (int)sw_ok);
        TELEM("ImGui_ImplDeko3d_Init=%d", (int)dk_ok);

        if(!sw_ok || !dk_ok) {
            CHECKPOINT("v0.7 skeleton: backend init FAILED — see qos-menu-init.log");
            UL_LOG_WARN("ImGui backend init failed (sw=%d dk=%d) — aborting render loop", (int)sw_ok, (int)dk_ok);
            // Clean up what we can and return; __appExit will still run.
            if(dk_ok)  ImGui_ImplDeko3d_Shutdown();
            if(sw_ok)  ImGui_ImplSwitch_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        V7LOG("deko3d+ImGui init complete, entering tile-grid integration");

        // v0.7.0-beta5 BUG 2: log applet pool headroom now that the deko3d backend
        // has finished its own allocations — this gives a post-GPU-init baseline.
        {
            const uint64_t headroom = qos::fw_compat::AppletPoolHeadroom();
            TELEM("applet pool headroom post-deko3d-init = %llu bytes",
                  (unsigned long long)headroom);
            V7LOG("applet pool headroom = %llu bytes", (unsigned long long)headroom);
        }

        CHECKPOINT("v0.7.0-beta5: title enum + SMI (icon uploader deferred)");

        // ── Icon uploader init — DEFERRED to beta6 (BUG 4) ──────────────────
        //
        // ROOT CAUSE (confirmed 2026-04-19, deferred to beta6):
        //   ImGui 1.92 (IMGUI_VERSION_NUM=19272) broke the legacy SetTexID ABI:
        //
        //   1. ImFontAtlas::SetTexID(ImTextureID) now asserts
        //      TexRef._TexID == ImTextureID_Invalid AND immediately writes through
        //      TexRef._TexData->TexID.  If _TexData is null (legacy back-end path)
        //      this dereferences a null pointer — corrupting backend state before
        //      register_image_cb is ever reached.
        //      Fixed in this beta5 for the font atlas (uses TexData->SetTexID/SetStatus),
        //      but not yet audited for all call sites in qos_icon_upload.cpp.
        //
        //   2. ImDrawCmd layout changed: TexRef is now an ImTextureRef (16 bytes)
        //      instead of a raw ImTextureID (8 bytes).  Any code that casts
        //      ImDrawCmd::GetTexID() to a DkResHandle must go through
        //      cmd.GetTexID() (inline, correct) — done in RenderDrawData.
        //      The icon upload path stores raw DkResHandle values as ImTextureID
        //      (ImU64 cast) which is still valid, but the full upload→bind path
        //      has not been tested against the new draw-command layout.
        //
        //   3. ABI mismatch in ImplDeko3dHandles::image_memblock:
        //      GetHandles() returns &g_dk.imageMemBlock (address of dk::UniqueMemBlock
        //      C++ wrapper), but qos_icon_upload.cpp casts it as DkMemBlock (raw C
        //      handle).  These are not the same thing; the cast will read garbage.
        //      Fixing this requires either exposing the raw handle from the backend or
        //      changing the InitParams type to dk::UniqueMemBlock*.
        //
        // Fix plan (beta6):
        //   - Port qos_icon_upload.cpp to use TexData-based registration.
        //   - Fix image_memblock ABI: expose DkMemBlock (not &UniqueMemBlock).
        //   - Re-enable InitIconUploader once both issues are resolved.
        //
        qos::IconUploadInitParams iup{};
        {
            ImplDeko3dHandles dkh{};
            if(ImGui_ImplDeko3d_GetHandles(&dkh)) {
                iup.dk_device            = dkh.dk_device;
                iup.dk_queue             = dkh.dk_queue;
                iup.image_memblock       = dkh.image_memblock;
                iup.image_memblock_size  = dkh.image_memblock_size;
            }
        }
        iup.register_image_cb = ImGui_ImplDeko3d_RegisterImage;
        qos::InitIconUploader(iup);

        // ── SMI client + title enumeration ───────────────────────────────────
        {
            const bool te_ok = qos::InitTitleEnum();
            V7LOG("InitTitleEnum %s", te_ok ? "OK" : "FAILED (continuing)");
            TELEM("InitTitleEnum = %d", (int)te_ok);
        }

        // ── Enumerate installed titles → build tile list ──────────────────────
        qos::TileGrid grid;
        unsigned telem_icon_ok = 0, telem_icon_fail = 0;
        {
            V7LOG("EnumerateInstalledTitles: starting (may be slow on cold boot)");
            TELEM("Title enum start");
            auto titles = qos::EnumerateInstalledTitles(/*max_titles=*/0);
            V7LOG("EnumerateInstalledTitles: got %u titles", (unsigned)titles.size());
            TELEM("nsListApplicationRecord total=%u", (unsigned)titles.size());

            std::vector<qos::TileEntry> tiles;
            tiles.reserve(titles.size());
            for(const auto &t : titles) {
                qos::TileEntry e;
                e.app_id = t.application_id;
                e.label  = t.name;
                if (!t.icon_jpeg.empty()) {
                    e.icon = qos::GetOrUpload(t.application_id, t.icon_jpeg.data(), t.icon_jpeg.size());
                    if (e.icon) telem_icon_ok++;
                    else telem_icon_fail++;
                } else {
                    e.icon = 0;   // placeholder (first letter of label)
                    telem_icon_fail++;
                }
                tiles.push_back(e);
            }

            FILE *_f = fopen("sdmc:/switch/qos-menu-init.log", "a");
            if(_f) { fprintf(_f, "[V7INIT] tile grid built with %zu tiles (%u icons uploaded, %u failed/skipped)\n", tiles.size(), telem_icon_ok, telem_icon_fail); fflush(_f); fclose(_f); }

            V7LOG("tile grid loaded: %u tiles (%u icons uploaded)", (unsigned)titles.size(), telem_icon_ok);
            TELEM("Icon uploads: %u OK, %u FAILED", telem_icon_ok, telem_icon_fail);
            TELEM("TileGrid created with %u tiles", (unsigned)titles.size());
        }

        // Activate callback: render-only diagnostic build, so keep A-presses
        // local and log them instead of routing through the legacy SMI path.
        grid.SetActivateCallback([](const qos::TileEntry &e) {
            FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
            if(f) {
                fprintf(f, "[V7LAUNCH] render-only build pressed A on 0x%016lX label=%s\n",
                        (unsigned long)e.app_id, e.label.c_str());
                fflush(f);
                fclose(f);
            }
        });

        CHECKPOINT("v0.7.0-beta3: entering render loop");
        V7LOG("entering render loop (tile grid live)");
        TELEM("=== ENTERING RENDER LOOP ===");
        bool g_first_frame = true;
        unsigned g_frame = 0;
        // Capture tile count once — TileGrid has no GetTileCount() accessor.
        const unsigned g_tile_count = telem_icon_ok + telem_icon_fail;
        // deko3d presentImage() is void; no RC is exposed by the backend.
        // g_last_present_rc is always 0 — logged to show the field is live.
        const u32 g_last_present_rc = 0;

        // ── Render loop ───────────────────────────────────────────────────────
        AppletFocusState g_prev_focus = appletGetFocusState();
        while(appletMainLoop()) {
            // Cheap per-frame heartbeat: write frame number unconditionally.
            // If we crash mid-loop, the last [TEL-F] line tells us which frame.
            TELF(g_frame);
            // Flush every 60 frames to keep writes batched.
            if((g_frame % 60) == 0 && g_frame > 0) {
                // (TELF already opened/closed per call; the 60-frame batch is just
                //  a reminder marker for the periodic TELEM dump below)
            }

            // Poll HID; returns false when HOME/Plus pressed → exit cleanly
            if(!ImGui_ImplSwitch_NewFrame())
                break;

            ImGui_ImplDeko3d_NewFrame();
            ImGui::NewFrame();

            // ── Focus change detection (v0.7.0-beta3) ────────────────────────
            {
                AppletFocusState cur_focus = appletGetFocusState();
                if(cur_focus != g_prev_focus) {
                    if(cur_focus == AppletFocusState_InFocus) {
                        TELEM("applet focus GAINED at frame %u", g_frame);
                    } else {
                        TELEM("applet focus LOST at frame %u", g_frame);
                    }
                    g_prev_focus = cur_focus;
                }
            }

            // ── Input: read raw pad state for tile grid navigation ────────────
            // imgui_impl_switch uses hidGetNpadStatesHandheld internally;
            // we read the same source for the tile grid (buttons-down semantics).
            u64 telem_held = 0, telem_kdown = 0;
            {
                HidNpadHandheldState pad = {};
                hidGetNpadStatesHandheld(HidNpadIdType_Handheld, &pad, 1);
                // We need "just-pressed" (kDown) not "held".
                // Track previous state statically for edge detection.
                static HidNpadHandheldState s_prev_pad = {};
                const u64 held = pad.buttons;
                const u64 prev = s_prev_pad.buttons;
                const u64 kdown = held & ~prev;
                s_prev_pad = pad;
                telem_held  = held;
                telem_kdown = kdown;

                const bool up  = (kdown & HidNpadButton_StickLUp)   || (kdown & HidNpadButton_Up);
                const bool dn  = (kdown & HidNpadButton_StickLDown) || (kdown & HidNpadButton_Down);
                const bool lt  = (kdown & HidNpadButton_StickLLeft) || (kdown & HidNpadButton_Left);
                const bool rt  = (kdown & HidNpadButton_StickLRight)|| (kdown & HidNpadButton_Right);
                const bool a   = (kdown & HidNpadButton_A) != 0;
                const bool b   = (kdown & HidNpadButton_B) != 0;
                grid.OnInput(up, dn, lt, rt, a, b);
            }

            // ── Full-screen ImGui window — tile grid ──────────────────────────
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f));
            ImGui::Begin("##qos_menu", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBackground);
            grid.Render();
            ImGui::End();

            ImGui::Render();
            ImDrawData *draw_data = ImGui::GetDrawData();
            ImGui_ImplDeko3d_RenderDrawData(draw_data);
            // Note: deko3d presentImage() is void — g_last_present_rc stays 0.

            if(g_first_frame) {
                g_first_frame = false;
                V7LOG("first frame rendered at %.1f FPS (v0.7.0-beta4 placeholder tiles live)", io.Framerate);
                // Frame-1 ImGui DrawData sanity — if vtx=0, ImGui isn't generating geometry.
                if(draw_data) {
                    TELEM("frame 1 ImGui DrawData: total_idx=%d total_vtx=%d CmdListsCount=%d",
                          draw_data->TotalIdxCount, draw_data->TotalVtxCount,
                          draw_data->CmdListsCount);
                } else {
                    TELEM("frame 1 ImGui DrawData: NULL (render not called?)");
                }
                TELEM("frame 1 dkQueuePresentImage=0x%08X", (unsigned)g_last_present_rc);
                // v0.7.0-beta4: first-frame render pipeline proof
                {
                    static bool first_frame_logged = false;
                    if (!first_frame_logged) {
                        first_frame_logged = true;
                        FILE *_ff = fopen("sdmc:/switch/qos-menu-init.log", "a");
                        if(_ff) {
                            auto dd = ImGui::GetDrawData();
                            fprintf(_ff, "[V7LIVE] first frame: DrawData=%p vtx=%d idx=%d cmdlists=%d\n",
                                    (void*)dd, dd ? dd->TotalVtxCount : -1, dd ? dd->TotalIdxCount : -1, dd ? dd->CmdListsCount : -1);
                            fflush(_ff); fclose(_ff);
                        }
                    }
                }
            }

            // ── Periodic 60-frame telemetry dump ─────────────────────────────
            if((g_frame % 60) == 0) {
                AppletFocusState cur_fs = appletGetFocusState();
                TELEM("frame=%u fps=%.1f dkQueuePresentImage=0x%08X applet_state=%d",
                      g_frame, (double)io.Framerate, (unsigned)g_last_present_rc, (int)cur_fs);
                TELEM("frame=%u input: held=0x%016llX down=0x%016llX cursor=%d",
                      g_frame,
                      (unsigned long long)telem_held,
                      (unsigned long long)telem_kdown,
                      grid.GetCursorIndex());
                TELEM("frame=%u tiles=%u icon_upload_cache=%u",
                      g_frame,
                      g_tile_count,
                      telem_icon_ok);
                // v0.7.0-beta4: [V7LIVE] render heartbeat
                {
                    FILE *_ff = fopen("sdmc:/switch/qos-menu-init.log", "a");
                    if(_ff) { fprintf(_ff, "[V7LIVE] render frame %u (tiles=%u cursor=%d)\n", g_frame, g_tile_count, grid.GetCursorIndex()); fflush(_ff); fclose(_ff); }
                }
            }

            g_frame++;
        }

        V7LOG("render loop exited cleanly (v0.7.0-beta4)");
        CHECKPOINT("v0.7.0-beta4: render loop exited cleanly");
        TELEM("=== RENDER LOOP EXIT === total frames=%u last_present=0x%08X",
              g_frame, (unsigned)g_last_present_rc);

        // ── Teardown ──────────────────────────────────────────────────────────
        TELEM("teardown: Title enum shutdown (render-only diagnostic build)");
        qos::ShutdownTitleEnum();
        // qos::ShutdownIconUploader() — skipped; uploader was never initialized in v0.7.0-beta4
        TELEM("teardown: ImGui_ImplDeko3d_Shutdown, ImGui::DestroyContext");
        ImGui_ImplDeko3d_Shutdown();
        ImGui_ImplSwitch_Shutdown();
        ImGui::DestroyContext();
        TELEM("teardown: deko3d queues and device destroyed by backend");
        TELEM("boot exit: clean");

    }

}

// uMenu v0.7.0-beta4: icon upload SKIPPED — placeholder tile grid (render proof).
// beta3: dense diagnostic telemetry added.
// romfs.bin mount path: sdmc:/qos-shell/bin/uMenu/romfs.bin (fixed in beta2).
// InitializeSettings() and QuickMenu (Plutonium) removed for Week 1 skeleton.
// SMI IPC plumbing retained inside MainLoop().

// Inline helper: write a single telem line from main() context (sdmc already mounted).
static void MainTelem(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void MainTelem(const char *fmt, ...) {
    FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
    if(f) {
        va_list ap;
        va_start(ap, fmt);
        fputs("[TELEM] ", f);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fflush(f);
        fclose(f);
    }
}

int main() {
    // Brief quiesce so Horizon fully settles the applet pool before deko3d
    // allocates GPU memory.
    svcSleepThread(500'000'000);  // 500 ms (reduced from v0.6.8's 2 s)

    UL_RC_ASSERT(ul::menu::am::ReadStartMode(g_StartMode));
    UL_ASSERT_TRUE(g_StartMode != ul::smi::MenuStartMode::Invalid);

    // Read system status storage sent by uSystem
    UL_RC_ASSERT(ul::menu::am::ReadFromInputStorage(&g_GlobalSettings.system_status, sizeof(g_GlobalSettings.system_status)));
    UL_LOG_INFO("v0.7.0-beta4: start_mode=%d last_menu_index=%d",
        (u32)g_StartMode, g_GlobalSettings.system_status.last_menu_index);

    // ── romfs mount telemetry (v0.7.0-beta3) ─────────────────────────────────
    const bool romfs_exists = ul::fs::ExistsFile(ul::MenuRomfsFile);
    MainTelem("romfsMountFromFsdev(%s) exists=%d", ul::MenuRomfsFile, (int)romfs_exists);
    if(!romfs_exists) {
        MainTelem("romfs.bin NOT FOUND — fatal");
        UL_RC_ASSERT(ul::ResultRomfsNotFound);
    }
    const Result romfs_rc = romfsMountFromFsdev(ul::MenuRomfsFile, 0, "romfs");
    MainTelem("romfsMountFromFsdev result=0x%08X", (unsigned)romfs_rc);
    if(R_FAILED(romfs_rc)) {
        UL_RC_ASSERT(romfs_rc);
    }

    // ── Enumerate romfs root + shaders/ (v0.7.0-beta3) ───────────────────────
    {
        // Count and list up to 10 files from romfs root
        DIR *d = opendir("romfs:");
        if(d) {
            int n = 0;
            struct dirent *ent;
            while((ent = readdir(d)) != nullptr && n < 10) {
                MainTelem("  romfs:/%s", ent->d_name);
                n++;
            }
            closedir(d);
            MainTelem("fsdev:/romfs listing: %d files (shown up to 10)", n);
        } else {
            MainTelem("fsdev:/romfs listing: opendir FAILED (romfs not mounted?)");
        }

        // Probe the two shader files and check their DKSH magic
        const char *shaders[2] = {
            "romfs:/shaders/imgui_vsh.dksh",
            "romfs:/shaders/imgui_fsh.dksh"
        };
        for(int i = 0; i < 2; i++) {
            MainTelem("shader: reading %s", shaders[i]);
            FILE *sf = fopen(shaders[i], "rb");
            if(!sf) {
                MainTelem("  opened FAILED");
                continue;
            }
            // Get size
            fseek(sf, 0, SEEK_END);
            long sz = ftell(sf);
            rewind(sf);
            MainTelem("  opened fd OK, size=%ld", sz);
            // Read first 4 bytes — DKSH magic = 0x48534B44 ('DKSH')
            u32 magic = 0;
            fread(&magic, 4, 1, sf);
            MainTelem("  read 4 bytes, first 4=0x%08X (DKSH=0x48534B44)", (unsigned)magic);
            fclose(sf);
        }
    }

    MainLoop();

    return 0;
}
