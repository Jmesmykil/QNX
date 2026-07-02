
#pragma once
#include <switch.h>

namespace ul::system::la {

    // ---------------------------------------------------------------------------
    // SINGLE-HOLDER ARCHITECTURE (pre-v3.7.47, restored 2026-06-20)
    //
    // One AppletHolder (g_LibraryAppletHolder) owns ALL library applets:
    // uMenu and all NROs/system applets.  This is the stock uLaunch design.
    //
    // uMenu MUST exit before a game (Application) can launch — the HOS AM
    // constraint: a library-applet holder cannot coexist with an Application.
    // rc=0xD37C from app::Start() is the AM rejection when the holder is kept
    // resident.  Therefore uMenu must terminate itself (Plutonium Finalize)
    // before LaunchApplication fires, and uSystem relaunches it via LaunchMenu
    // after the game exits.
    //
    // IsActive()  — true when g_LibraryAppletHolder is alive (uMenu or NRO).
    // ---------------------------------------------------------------------------

    bool    IsActive();

    Result Terminate();
    Result Start(const AppletId id, const s32 la_version);
    Result Start(const AppletId id, const s32 la_version, const void *in_data, const size_t in_size);
    Result Start(const AppletId id, const s32 la_version, const void *in_data, const size_t in_size, const void *in_data_2, const size_t in_size_2);
    Result Send(const void *data, const size_t size);
    Result Read(void *data, const size_t size);
    Result Push(AppletStorage *st);
    Result Pop(AppletStorage *st);

    u64 GetProgramIdForAppletId(const AppletId id);
    AppletId GetAppletIdForProgramId(const u64 id);

    AppletId GetLastAppletId();

    void SetMenuProgramId(const u64 id);
    u64 GetMenuProgramId();

    // v3.1 Phase 2 Slice 1 BackgroundIndirect surface (Create/Start/Is/
    // Terminate/Push/PopBackgroundIndirect) was REMOVED 2026-05-19 after
    // HW test confirmed that a SystemApplet cannot launch library applets
    // from background — AM rejects appletCreateLibraryApplet when the
    // caller isn't the foreground applet.  Architecture pivoted to uMenu-
    // side appletCreateLibraryAppletSelf (libnx applet.h:1183).  See
    // docs/archive/v3.1-bg-indirect-pivot/.

    // ---------------------------------------------------------------------------
    // Non-blocking NRO terminate helpers (Fix B/C/D — HOME-over-NRO hang).
    //
    // These operate on g_LibraryAppletHolder ONLY.
    //
    //   RequestExitNonBlocking() — sends appletHolderRequestExit once and
    //       returns immediately.  Called from HandleHomeButton.
    //
    //   CheckTerminated()        — returns true when appletHolderCheckFinished
    //       reports the holder is done.  Poll from the MainLoop.
    //
    //   ForceTerminateNow()      — issues the kernel Terminate cmd (cmd 25)
    //       and immediately Joins + Closes g_LibraryAppletHolder.
    //       Used after the 500 ms deadline elapses.
    // ---------------------------------------------------------------------------
    void    RequestExitNonBlocking();
    bool    CheckTerminated();
    Result  ForceTerminateNow();

}
