
#pragma once
#include <switch.h>

namespace ul::system::app {

    struct ApplicationSelectedUserArgument {
        static constexpr u32 Magic = 0xC79497CA;

        u32 magic;
        u8 unk_1; // Maybe bool is_user_selected?
        u8 pad[3];
        AccountUid uid;
        u8 unused[0x70];

        static inline constexpr ApplicationSelectedUserArgument Create(const AccountUid uid) {
            return {
                .magic = Magic,
                .unk_1 = 1,
                .uid = uid
            };
        }
    };
    static_assert(sizeof(ApplicationSelectedUserArgument) == 0x88);

    bool IsActive();
    Result Terminate();
    Result Start(const u64 app_id, const bool system, const AccountUid user_id, const void *data = nullptr, const size_t size = 0);
    bool HasForeground();
    Result SetForeground();
    Result Send(const void *data, const size_t size, const AppletLaunchParameterKind kind = AppletLaunchParameterKind_UserChannel);
    u64 GetId();

    // ---------------------------------------------------------------------------
    // Non-blocking APPLICATION terminate helpers (Fix 1 — HOME-over-APP black-screen).
    //
    // Mirrors the la:: non-blocking NRO terminate pattern exactly.
    // HandleHomeButton's app:: branch now uses these instead of the blocking
    // app::Terminate(), so uMenu is launched in NORMAL mode (MainMenu / mode 2)
    // instead of MenuApplicationSuspended (mode 3) which caused the black-screen.
    //
    //   RequestExitNonBlocking() — sends appletApplicationRequestExit once and
    //       returns immediately.  Called from HandleHomeButton.
    //
    //   CheckTerminated()        — returns true when appletApplicationCheckFinished
    //       reports the holder is done.  Poll from the MainLoop.
    //
    //   ForceTerminateNow()      — issues appletApplicationTerminate (hard kill)
    //       then Closes the holder.  Used after the 500 ms deadline elapses.
    // ---------------------------------------------------------------------------
    void   RequestExitNonBlocking();
    bool   CheckTerminated();
    Result ForceTerminateNow();

}
