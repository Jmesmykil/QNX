#include <ul/system/ecs/ecs_ExternalContent.hpp>
#include <ul/system/sf/sf_IpcManager.hpp>

namespace ul::system::ecs {

    namespace {

        inline Result ldrShellAtmosphereRegisterExternalCode(const u64 app_id, Handle *out_h) {
            return serviceDispatchIn(ldrShellGetServiceSession(), 65000, app_id,
                .out_handle_attrs = { SfOutHandleAttr_HipcMove },
                .out_handles = out_h,
            );
        }

        // 2026-05-06 AMS-1.11 clean-exit fix: the symmetric IPC cmd 65001 against
        // ldr:shell tells AMS Loader to release the external-content session that
        // was set up by RegisterExternalContent.  Without this call, the session
        // accumulates in AMS's 6-slot ServerManager pool — after 6 ECS-backed
        // applet launches, the 7th aborts uSystem with
        // 2011-0102 ams::sf::hipc::ResultOutOfSessionMemory.
        // (See docs/research/APPLET-TEARDOWN-AUDIT-20260506.md.)
        // The cmd ID is defined in libstratosphere ldr_shell_interface.hpp:27 as
        // AtmosphereUnregisterExternalCode.
        inline Result ldrShellAtmosphereUnregisterExternalCode(const u64 app_id) {
            return serviceDispatchIn(ldrShellGetServiceSession(), 65001, app_id);
        }

    }

    Result UnregisterExternalContent(const u64 program_id) {
        return ldrShellAtmosphereUnregisterExternalCode(program_id);
    }

    Result RegisterExternalContent(const u64 program_id, const std::string &exefs_path) {
        auto move_h = INVALID_HANDLE;
        UL_RC_TRY(ldrShellAtmosphereRegisterExternalCode(program_id, &move_h));

        FsFileSystem sd_fs;
        UL_RC_TRY(fsOpenSdCardFileSystem(&sd_fs));
        std::shared_ptr<::ams::fs::fsa::IFileSystem> remote_sd_fs = std::make_shared<::ams::fs::RemoteFileSystem>(sd_fs);
        auto subdir_fs = std::make_shared<::ams::fssystem::SubDirectoryFileSystem>(std::move(remote_sd_fs));
        ::ams::fs::Path exefs_fs_path;
        UL_RC_TRY(exefs_fs_path.Initialize(exefs_path.c_str(), exefs_path.length()));
        UL_RC_TRY(exefs_fs_path.Normalize(::ams::fs::PathFlags{}));
        UL_RC_TRY(subdir_fs->Initialize(exefs_fs_path));

        auto sd_ifs_ipc = sf::MakeSharedFileSystem(std::move(subdir_fs));
        UL_RC_TRY(sf::RegisterSession(move_h, ::ams::sf::cmif::ServiceObjectHolder(std::move(sd_ifs_ipc))));
        return ResultSuccess;
    }

    Result LaunchSystemProcess(const u64 program_id, const std::string &argv_str) {
        UL_RC_TRY(ldrShellSetProgramArguments(program_id, argv_str.c_str(), argv_str.length()));
        NcmProgramLocation loc = {
            .program_id = program_id,
            .storageID = NcmStorageId_BuiltInSystem
        };

        u64 pid;
        UL_RC_TRY(pmshellLaunchProgram(0, &loc, &pid));
        return ResultSuccess;
    }

}
