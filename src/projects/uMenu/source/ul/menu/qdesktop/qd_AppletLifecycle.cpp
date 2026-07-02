// qd_AppletLifecycle.cpp — see qd_AppletLifecycle.hpp for design notes.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_AppletLifecycle.hpp>
#include <ul/menu/qdesktop/qd_NxlinkServer.hpp>
#include <ul/menu/qdesktop/qd_RemoteShellServer.hpp>
#include <ul/menu/qdesktop/qd_DebugServer.hpp>
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/ul_Result.hpp>
#include <switch.h>
#include <atomic>

// v2.8.4 — defined in ui_MenuApplication.cpp:23 (process-global).
// We can't include ui_MenuApplication.hpp here (would pull Plutonium into
// this TU and create a circular include chain with the network servers),
// so just extern-declare what we need.
extern std::atomic<bool> g_uMenuTerminating;

namespace ul::menu::qdesktop {

namespace {

// libnx requires the cookie storage to outlive the hook registration —
// process-lifetime static is safe and matches the libnx examples.
AppletHookCookie g_AppletHookCookie {};
bool             g_Registered { false };

// The hook callback fires on libnx's appletMainLoop tick (each frame
// from the main UI thread).  Keep it short and non-blocking — heavy
// work belongs in the server thread, which we signal here via Stop().
void OnAppletEvent(AppletHookType type, void * /*param*/) {
    switch(type) {
        case AppletHookType_OnFocusState: {
            const AppletFocusState fs = appletGetFocusState();
            if (fs != AppletFocusState_InFocus) {
                // Focus lost — typically the user pressed the power
                // button (entering sleep) or the OS swapped us to the
                // background.  Either way, WiFi is about to drop and
                // our listen sockets will become invalid.  Hard-stop
                // both servers now so on wake the user sees a clean
                // "Remote Shell: OFF / Nxlink: OFF" state and can
                // re-enable from the hot-corner dropdown.
                //
                // Stop() is idempotent — calling it when the server
                // is already stopped is a no-op.
                if (g_RemoteShellServer.IsRunning()) {
                    UL_LOG_INFO("qdesktop: applet OnFocusState — stopping shell server");
                    g_RemoteShellServer.Stop();
                }
                if (g_NxlinkServer.IsRunning()) {
                    UL_LOG_INFO("qdesktop: applet OnFocusState — stopping nxlink server");
                    g_NxlinkServer.Stop();
                }
            }
            // If fs == AppletFocusState_InFocus we're returning to
            // foreground (post-wake or post-applet-return).  Do NOT
            // auto-restart the servers — matches hbmenu netloader's
            // "default OFF after any state change" semantics.  User
            // re-enables manually.
            break;
        }

        case AppletHookType_OnResume:
            // Wake completed.  Documented but deliberately empty —
            // see file header for the design rationale.  Logging
            // here would be noisy; users see the state via the
            // dropdown labels which read live from IsRunning().
            UL_LOG_INFO("qdesktop: applet OnResume — servers remain off until user re-enables");
            break;

        case AppletHookType_OnExitRequest:
            // ICON-PERSISTENCE-FIX (safety-net): the OS is requesting that we
            // exit.  On Switch, applets are commonly relaunched rather than
            // cleanly exited, so atexit() / static-destructor SaveToDisk may
            // never run.  Flush the icon-cache blob here as a best-effort
            // pre-exit write; even if the process is torn down milliseconds
            // later the .tmp → rename pattern in SaveToDisk gives us FAT32-safe
            // partial-write protection (a torn rename leaves the old blob
            // intact; no garbage is committed).
            // Note: this hook fires on the render thread during the
            // appletMainLoop() drain inside PumpAppletMessages().  SaveToDisk
            // uses only kernel IPC (fsFsOpenFile / fsFileWrite) and does NOT
            // hold GetSharedIconCacheMutex() — the brief ~5-20ms SD write is
            // acceptable here since we are about to exit anyway.
            UL_LOG_INFO("qdesktop: OnExitRequest — flushing icon cache blob before exit");
            ul::menu::qdesktop::GetSharedIconCache().SaveToDisk(
                "sdmc:/ulaunch/cache/icons.bgra");
            break;

        case AppletHookType_OnOperationMode:
        case AppletHookType_OnPerformanceMode:
        case AppletHookType_OnCaptureButtonShortPressed:
        case AppletHookType_OnAlbumScreenShotTaken:
        case AppletHookType_RequestToDisplay:
        case AppletHookType_Max:
        default:
            // No action — these don't affect our network-server lifecycle.
            break;
    }
}

} // anonymous namespace

void RegisterAppletHooks() {
    if (g_Registered) {
        return; // idempotent
    }
    appletHook(&g_AppletHookCookie, &OnAppletEvent, nullptr);
    g_Registered = true;
    UL_LOG_INFO("qdesktop: applet hooks registered (OnFocusState → stop net servers)");
}

// v2.8.6 — DIRECT POLL.  Replaces v2.8.4's hook-based approach.
//
// v2.8.4 attempted to use libnx's appletHook(OnFocusState) callback to stop
// the net servers when the Switch slept.  HW test 2026-05-18 showed the hook
// never fired even with the render-callback pump in place — the dropdown row
// 9 label still said "Remote Shell: ON" after wake, with the socket still
// listening.  Root cause hypothesis: libnx's appletHook dispatch path through
// appletMainLoop has a subtle init-order or queue-state issue that the public
// API doesn't surface.
//
// Bypass it.  Poll appletGetFocusState() directly each frame and detect the
// transition InFocus → !InFocus ourselves.  appletGetFocusState reads a cached
// value that libnx updates internally when the kernel delivers a focus-state
// message — so we still need appletMainLoop() to drain the queue, but we no
// longer DEPEND on the hook dispatcher to call us back.  This is the same
// pattern hbmenu uses (per Agent 5 research 2026-05-18: "hbmenu netloader has
// zero applet-message awareness; runs entirely synchronous inside the menu
// loop and on any error closes the socket and does not reopen").
//
// Cost: appletGetFocusState is a single libnx-internal load.  No syscall.
// appletMainLoop is still called (cheap when queue empty).  Frame cost ~50µs.
void PumpAppletMessages() {
    // v2.8.7 — diagnostic instrumentation.  v2.8.6's direct-poll approach
    // never produced a "focus lost" log line in HW testing, which leaves two
    // possibilities: (a) this function isn't being called at all because the
    // render-callback registration didn't take, or (b) it IS being called but
    // appletGetFocusState() never transitions away from InFocus during sleep
    // for library-applet-replacement uMenu.  These have different fixes; we
    // need to distinguish them.  Two heartbeat logs:
    //
    //   - "first call"     — fires exactly once.  Confirms callback was
    //                         registered AND Plutonium's render loop is calling
    //                         it.  Absence means main.cpp wiring is wrong.
    //   - "heartbeat"      — fires every ~4 seconds.  Reports current focus
    //                         state value.  Reveals whether the kernel ever
    //                         flips the state for our applet type.

    static bool s_first_call = true;
    if (s_first_call) {
        UL_LOG_INFO("qdesktop: PumpAppletMessages first call — render-callback wiring confirmed");
        s_first_call = false;
    }
    static u32 s_pump_frame = 0;
    ++s_pump_frame;

    // Remote-test debug layer (render thread): first call auto-starts the HTTP
    // debug server if sdmc:/ulaunch/debug.flag exists; every call publishes a
    // /state snapshot (and Increment 2: services screenshot captures).
    g_DebugServer.OnRenderFrame(s_pump_frame);
    if ((s_pump_frame & 0xFFu) == 0u) { // every 256 frames ≈ 4.3 s at 60 fps
        UL_LOG_INFO("qdesktop: PumpAppletMessages heartbeat frame=%u focus=%d shell_running=%d nxlink_running=%d",
                    s_pump_frame,
                    static_cast<int>(appletGetFocusState()),
                    g_RemoteShellServer.IsRunning() ? 1 : 0,
                    g_NxlinkServer.IsRunning() ? 1 : 0);
    }

    // v3.8.x SELF-HEAL WATCHDOG — uMenu-side heartbeat sender.
    //
    // Every ~1800 frames (~30s at 60fps) send SystemMessage::Heartbeat to
    // uSystem via the SMI protocol.  uSystem records the arrival tick; if
    // the tick goes stale > 30s while IsMenuRunning() is true, uSystem
    // force-recovers uMenu (watchdog in uSystem MainLoop).
    //
    // WHY here (PumpAppletMessages / render thread): s_pump_frame is the
    // only reliable per-frame counter available in this TU.  The IPC round-
    // trip is fast (~<1ms when uSystem is spinning normally) and occurs only
    // once every 30s, so render-thread jank is negligible.  If SendHeartbeat
    // fails (uSystem momentarily busy) we simply skip this beat — the 30s
    // watchdog timeout gives us many beats of margin.
    //
    // Frame-0 is skipped intentionally: uMenu is still initialising and
    // the SMI channel may not be ready.  The first heartbeat fires at
    // frame 1800 (~30s after startup), which is a conservative warm-up.
    // DISABLED 2026-06-20 — CRITICAL: SendHeartbeat() is a SYNCHRONOUS SMI round-trip
    // on the render thread. On hardware it BLOCKS FOREVER (uSystem's reply path is not
    // satisfied), freezing uMenu at exactly frame 1800 (~30s) — confirmed live: the
    // frame counter stuck at 1800, /icons errored, the render loop died. The self-heal
    // watchdog is only a bonus (no-sleep already keeps the device reachable + cmd_crash
    // recoverable), so the blocking beat is removed. uSystem's watchdog stays dormant
    // (it gates on g_LastMenuHeartbeatTick != 0, never set without a beat). Re-enable
    // ONLY with a non-blocking mechanism (fire-and-forget / SD timestamp uSystem polls),
    // never a synchronous SMI call on the render thread.
    // if (s_pump_frame > 0 && (s_pump_frame % 1800u) == 0u) { (void)ul::menu::smi::SendHeartbeat(); }

    // 1. Drain libnx's applet message queue.
    if (!appletMainLoop()) {
        bool was = g_uMenuTerminating.load(std::memory_order_acquire);
        if (!was) {
            UL_LOG_INFO("qdesktop: PumpAppletMessages — OS exit requested, flipping g_uMenuTerminating");
            g_uMenuTerminating.store(true, std::memory_order_release);
        }
    }

    // 2. Detect focus-state transition by direct poll.
    static AppletFocusState s_last_focus = AppletFocusState_InFocus;
    const AppletFocusState now_focus = appletGetFocusState();

    // v3.7.2 BOOT FOREGROUND FIX (revised v3.7.3 — remove one-shot latch).
    //
    // At boot / after any in-system relaunch (HOME-from-NRO) uSystem launches
    // uMenu as a library applet; the handoff leaves uMenu in Background applet-
    // focus, so the compositor never presents our frame → BLACK SCREEN.
    //
    // Original v3.7.2 design: request foreground every frame until the first
    // InFocus observation, then set s_boot_fg_done and stop forever.
    //
    // Why that failed for the HOME-from-NRO relaunch case (bug root-cause):
    //   appletGetFocusState() transitions to InFocus when AM's OWN state machine
    //   marks uMenu as the foreground applet.  But the compositor's physical
    //   display handoff — actually flipping uMenu's framebuffer onto the panel —
    //   is asynchronous and can lag AM by 50-300ms while the previous NRO's vi
    //   layer tears down.  The one-shot latch fires the moment AM reports InFocus
    //   and stops all further requests.  If the compositor is still draining the
    //   NRO's binder/parcel teardown at that exact instant, there is no
    //   subsequent request to push it over the edge → the screen stays black.
    //
    // Fix: do NOT use a one-shot latch.  Instead, request foreground on every
    // frame where we are NOT InFocus and where uMenu is not intentionally being
    // torn down.  Once InFocus is established, now_focus == InFocus so the
    // guard doesn't fire — there is no runaway loop.  If the compositor
    // later legitimately backgrounds uMenu (user launched an app, switch to
    // sleep), now_focus transitions away from InFocus, and we DO want
    // appletRequestToGetForeground to fire again when focus returns.
    // This also handles the case where appletMainLoop (above) signals an
    // exit request — g_uMenuTerminating is true and we skip the request.
    if (!g_uMenuTerminating.load(std::memory_order_acquire) &&
        now_focus != AppletFocusState_InFocus) {
        (void)appletRequestToGetForeground();
        // Log only on the first few frames to avoid 60 Hz spam.
        if (s_pump_frame <= 5u || (s_pump_frame & 0x3Fu) == 0u) {
            UL_LOG_INFO("qdesktop: requesting foreground (focus=%d, frame=%u)",
                        static_cast<int>(now_focus), s_pump_frame);
        }
    }

    if (now_focus != s_last_focus) {
        if (s_last_focus == AppletFocusState_InFocus &&
            now_focus  != AppletFocusState_InFocus) {
            UL_LOG_INFO("qdesktop: PumpAppletMessages — FOCUS LOST (state %d→%d), stopping net servers",
                        static_cast<int>(s_last_focus), static_cast<int>(now_focus));
            if (g_RemoteShellServer.IsRunning()) {
                g_RemoteShellServer.Stop();
            }
            if (g_NxlinkServer.IsRunning()) {
                g_NxlinkServer.Stop();
            }
        }
        else if (s_last_focus != AppletFocusState_InFocus &&
                 now_focus  == AppletFocusState_InFocus) {
            UL_LOG_INFO("qdesktop: PumpAppletMessages — FOCUS REGAINED (state %d→%d), servers stay off",
                        static_cast<int>(s_last_focus), static_cast<int>(now_focus));
        }
        s_last_focus = now_focus;
    }
}

void UnregisterAppletHooks() {
    if (!g_Registered) {
        return;
    }
    appletUnhook(&g_AppletHookCookie);
    g_Registered = false;
    UL_LOG_INFO("qdesktop: applet hooks unregistered");
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
