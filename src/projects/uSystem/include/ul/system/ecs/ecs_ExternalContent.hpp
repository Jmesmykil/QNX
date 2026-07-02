
#pragma once
#include <ul/system/la/la_LibraryApplet.hpp>
#include <ul/system/app/app_Application.hpp>
#include <ul/ul_Result.hpp>
#include <string>

namespace ul::system::ecs {

    Result RegisterExternalContent(const u64 program_id, const std::string &exefs_path);
    // 2026-05-06: symmetric release for RegisterExternalContent.  Wraps AMS
    // ldr:shell IPC cmd 65001.  MUST be called when the launched applet
    // exits or the ServerManager session pool slot leaks until uSystem dies
    // with 2011-0102.  See AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md.
    Result UnregisterExternalContent(const u64 program_id);
    Result LaunchSystemProcess(const u64 program_id, const std::string &argv_str);

    inline Result RegisterLaunchAsApplet(const u64 program_id, const u32 la_version, const std::string &exefs_path, const void *args, const size_t args_size) {
        UL_RC_TRY(RegisterExternalContent(program_id, exefs_path));
        // DUAL-HOLDER ROUTING (v3.8.2): la::Start() internally routes to
        // g_MenuLibraryAppletHolder when program_id == la::GetMenuProgramId(),
        // and to g_LibraryAppletHolder for all other IDs.  No explicit
        // branching is needed here — the routing is in la::Create()/HolderFor().
        const AppletId applet_id = la::GetAppletIdForProgramId(program_id);
        UL_RC_TRY(la::Start(applet_id, la_version, args, args_size));
        return ResultSuccess;
    }

    // v3.1 Phase 2 Slice 1 RegisterLaunchAsBackgroundIndirectApplet was
    // removed 2026-05-19 (architectural pivot to uMenu-side
    // appletCreateLibraryAppletSelf).

    inline Result RegisterLaunchAsApplication(const u64 program_id, const std::string &exefs_path, const void *args, const size_t args_size, const AccountUid uid) {
        UL_RC_TRY(RegisterExternalContent(program_id, exefs_path));
        UL_RC_TRY(app::Start(program_id, false, uid, args, args_size));
        return ResultSuccess;
    }

    inline Result RegisterLaunchAsSystemProcess(const u64 program_id, const std::string &exefs_path, const std::string &argv_str) {
        UL_RC_TRY(RegisterExternalContent(program_id, exefs_path));
        UL_RC_TRY(LaunchSystemProcess(program_id, argv_str));
        return ResultSuccess;
    }

}
