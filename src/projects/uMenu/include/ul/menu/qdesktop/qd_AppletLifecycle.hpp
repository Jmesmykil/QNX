// qd_AppletLifecycle.hpp — applet sleep/wake hook for clean network-server teardown.
//
// v2.8.3 — Hooks libnx's appletHook(...) so we get an OnFocusState callback
// when the Switch enters sleep mode (focus lost while uMenu is the active
// library applet).  In that callback we hard-stop both QdNxlinkServer
// (port 28771) and QdRemoteShellServer (port 9999) — closing their listen
// sockets and joining their server threads — BEFORE the kernel invalidates
// the sockets out from under us.
//
// On wake (OnResume), we deliberately do NOTHING — the servers stay off,
// and the user re-enables them from the hot-corner dropdown if they want.
// This matches the canonical hbmenu netloader pattern: "any failure means
// off until manually re-armed."  Researched 2026-05-18 — the only other
// Switch homebrew TCP servers (sys-ftpd-light, mtheall/ftpd, hbmenu
// netloader, sphaira) either run as sysmodules with their own outer
// restart loops or default-off-after-failure.  Auto-restart on wake is
// not idiomatic and not what users expect.
//
// Pre-v2.8.3 behavior: WiFi disconnect on sleep invalidated the socket,
// accept() returned ECONNABORTED at every poll(), the v2.3.6 backoff
// targeting EHOSTUNREACH/ENETDOWN/ENETUNREACH (114/115/118) was bypassed
// because the actual errno is 113 (ECONNABORTED on devkitA64 newlib),
// the server thread spun at ~72 Hz across both servers, log fsync
// starved the main UI thread.  v2.8.2 fixed the backoff so it covers
// any non-transient errno; v2.8.3 eliminates the spin entirely by
// not leaving dead sockets running across sleep.
//
// Reference: docs/research/IPC-SESSION-POOL-EXHAUSTION-20260518.md
// describes the broader class of compound-bug audit that produced this.

#pragma once

#ifdef QDESKTOP_MODE

namespace ul::menu::qdesktop {

    // Register the applet-focus hook with libnx.  Call ONCE from main.cpp
    // after MenuApplication::Initialize() and before Load().  Idempotent:
    // subsequent calls are no-ops (libnx's hook list deduplicates).
    void RegisterAppletHooks();

    // Tear down the hook.  Currently unused (the hook lives for process
    // lifetime), but exposed for symmetry with future test harnesses.
    void UnregisterAppletHooks();

    // v2.8.4 — drain libnx's applet message queue.  Plutonium's main render
    // loop (ui_Application.cpp Show() at line 247) is a tight CallForRender
    // loop with NO appletMainLoop() call — so applet hooks registered via
    // RegisterAppletHooks() never fire on their own.  Register this function
    // as a Plutonium render callback so it runs once per frame on the main
    // thread, draining the queue and dispatching focus/resume/exit hooks.
    //
    // Cost: one IPC round-trip per frame (~50us, often a no-op early-exit
    // when the queue is empty).  Frame budget at 60 FPS is 16.6ms — this is
    // <0.5% of frame budget.
    //
    // Returns nothing, never blocks.  If appletMainLoop() returns false the
    // OS has requested an applet exit; we set g_uMenuTerminating to true so
    // the existing chrome render callbacks early-return cleanly.
    void PumpAppletMessages();

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
