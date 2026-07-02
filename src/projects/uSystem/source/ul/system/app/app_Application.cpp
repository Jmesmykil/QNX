#include <ul/system/app/app_Application.hpp>
#include <ul/system/app/app_ControlCache.hpp>
#include <ul/ul_Result.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/util/util_String.hpp>

namespace ul::system::app {

    namespace {

        AppletApplication g_ApplicationHolder;
        u64 g_LastApplicationId;

        ApplicationNacpMisc g_StartApplicationNacpMisc;

        inline void EnsureSaveData(const u64 app_id, const u64 owner_id, const AccountUid user_id, const FsSaveDataType type, const FsSaveDataSpaceId space_id, const u64 savedata_size, const u64 savedata_journal_size) {
            if(savedata_size > 0) {
                const FsSaveDataAttribute attr = {
                    .application_id = app_id,
                    .uid = user_id,
                    .system_save_data_id = 0,
                    .save_data_type = type,
                    .save_data_rank = FsSaveDataRank_Primary,
                    .save_data_index = 0
                };
                const FsSaveDataCreationInfo cr_info = {
                    .save_data_size = (s64)savedata_size,
                    .journal_size = (s64)savedata_journal_size,
                    .available_size = 0x4000, // Fixed value on all savedata creation in qlaunch
                    .owner_id = owner_id,
                    .flags = 0,
                    .save_data_space_id = (u8)space_id
                };
                const FsSaveDataMetaInfo meta_info = {
                    .size = (type == FsSaveDataType_Bcat) ? 0u : 0x40060u,
                    .type = (type == FsSaveDataType_Bcat) ? FsSaveDataMetaType_None : FsSaveDataMetaType_Thumbnail
                };

                // Note: qlaunch uses a dedicated command (FindSaveData...), we just check if it exists by trying to open it
                FsFileSystem dummy_fs;
                if(R_SUCCEEDED(fsOpenSaveDataFileSystem(&dummy_fs, space_id, &attr))) {
                    fsFsClose(&dummy_fs);
                }
                else {
                    // Not yet created, create it then
                    const auto rc = fsCreateSaveDataFileSystem(&attr, &cr_info, &meta_info);
                    if(R_FAILED(rc)) {
                        UL_LOG_WARN("Failed to create save data for application ID 0x%016lX: %s", app_id, util::FormatResultDisplay(rc).c_str());
                    }
                }
            }
        }

    }

    bool g_ApplicationHasFocus;

    bool IsActive() {
        if(!eventActive(&g_ApplicationHolder.StateChangedEvent)) {
            return false;
        }
        if(!serviceIsActive(&g_ApplicationHolder.s)) {
            return false;
        }

        return !appletApplicationCheckFinished(&g_ApplicationHolder);
    }

    Result Terminate() {
        UL_RC_TRY(appletApplicationTerminateAllLibraryApplets(&g_ApplicationHolder));
        UL_RC_TRY(appletApplicationRequestExit(&g_ApplicationHolder));

        const auto rc = eventWait(&g_ApplicationHolder.StateChangedEvent, 15'000'000'000ul);
        if(rc == KERNELRESULT(TimedOut)) {
            UL_RC_TRY(appletApplicationTerminate(&g_ApplicationHolder));
        }

        const auto app_rc = serviceDispatch(&g_ApplicationHolder.s, 30);
        UL_LOG_WARN("Application terminated with result 0x%X", app_rc);

        appletApplicationClose(&g_ApplicationHolder);
        g_ApplicationHasFocus = false;
        return rc;
    }

    Result Start(const u64 app_id, const bool system, const AccountUid user_id, const void *data, const size_t size) {
        appletApplicationClose(&g_ApplicationHolder);

        if(system) {
            UL_RC_TRY(appletCreateSystemApplication(&g_ApplicationHolder, app_id));
        }
        else {
            bool have_misc = app::LoopQueryApplicationNacpMisc(app_id, g_StartApplicationNacpMisc);
            if(!have_misc) {
                // BOOT-SPEED hardening (2026-06-14): the deferred app-cache may not
                // have reached this title yet on a very fast launch.  Fetch the NACP
                // misc synchronously so EnsureSaveData is NEVER skipped on a cold
                // miss (which would leave a freshly-installed title without its save
                // partition created).
                have_misc = app::FetchApplicationNacpMiscSync(app_id, g_StartApplicationNacpMisc);
            }
            if(have_misc) {
                // Ensure it's launchable
            
                // TODO: does this do anything at all? qlaunch does not seem to use this...
                UL_RC_TRY(nsTouchApplication(app_id));

                // Ensure Account savedata
                EnsureSaveData(app_id, g_StartApplicationNacpMisc.save_data_owner_id, user_id, FsSaveDataType_Account, FsSaveDataSpaceId_User, g_StartApplicationNacpMisc.user_account_save_data_size, g_StartApplicationNacpMisc.user_account_save_data_journal_size);

                // Ensure Device savedata
                EnsureSaveData(app_id, g_StartApplicationNacpMisc.save_data_owner_id, {}, FsSaveDataType_Device, FsSaveDataSpaceId_User, g_StartApplicationNacpMisc.device_save_data_size, g_StartApplicationNacpMisc.device_save_data_journal_size);
                
                // Ensure Temporary savedata
                EnsureSaveData(app_id, g_StartApplicationNacpMisc.save_data_owner_id, {}, FsSaveDataType_Temporary, FsSaveDataSpaceId_Temporary, g_StartApplicationNacpMisc.temporary_storage_size, 0);

                // Ensure Cache savedata
                EnsureSaveData(app_id, g_StartApplicationNacpMisc.save_data_owner_id, {}, FsSaveDataType_Cache, FsSaveDataSpaceId_User, g_StartApplicationNacpMisc.cache_storage_size, g_StartApplicationNacpMisc.cache_storage_journal_size);

                // Ensure Bcat savedata
                EnsureSaveData(app_id, 0x010000000000000C, {}, FsSaveDataType_Bcat, FsSaveDataSpaceId_User, g_StartApplicationNacpMisc.bcat_delivery_cache_storage_size, 0x200000);
            }

            UL_RC_TRY(appletCreateApplication(&g_ApplicationHolder, app_id));
        }

        if(accountUidIsValid(&user_id)) {
            const auto selected_user_arg = ApplicationSelectedUserArgument::Create(user_id);
            UL_RC_TRY(Send(&selected_user_arg, sizeof(selected_user_arg), AppletLaunchParameterKind_PreselectedUser));
        }

        if(size > 0) {
            UL_RC_TRY(Send(data, size));
        }

        UL_RC_TRY(appletUnlockForeground());
        UL_RC_TRY(appletApplicationStart(&g_ApplicationHolder));
        UL_RC_TRY(SetForeground());

        g_LastApplicationId = app_id;
        return ResultSuccess;
    }

    bool HasForeground() {
        return g_ApplicationHasFocus;
    }

    Result SetForeground() {
        UL_RC_TRY(appletApplicationRequestForApplicationToGetForeground(&g_ApplicationHolder));
        g_ApplicationHasFocus = true;
        return ResultSuccess;
    }

    Result Send(const void *data, const size_t size, const AppletLaunchParameterKind kind) {
        AppletStorage st;
        UL_RC_TRY(appletCreateStorage(&st, size));
        util::OnScopeExit st_close([&]() {
            appletStorageClose(&st);
        });

        UL_RC_TRY(appletStorageWrite(&st, 0, data, size));
        UL_RC_TRY(appletApplicationPushLaunchParameter(&g_ApplicationHolder, kind, &st));
        return ResultSuccess;
    }

    u64 GetId() {
        return g_LastApplicationId;
    }

    // ---------------------------------------------------------------------------
    // Non-blocking APPLICATION terminate helpers (Fix 1 — HOME-over-APP).
    // Mirrors la:: RequestExitNonBlocking / CheckTerminated / ForceTerminateNow.
    // See header for full rationale.
    // ---------------------------------------------------------------------------

    // Send a single RequestExit (IApplicationAccessor cmd equivalent) and return
    // immediately.  Does NOT wait; does NOT escalate to Terminate.  The caller is
    // responsible for polling CheckTerminated() and calling ForceTerminateNow() on
    // deadline.
    void RequestExitNonBlocking() {
        // appletApplicationRequestExit sends the exit request to the running
        // application's IApplicationAccessor and returns immediately regardless
        // of whether the application processes it.
        appletApplicationRequestExit(&g_ApplicationHolder);
    }

    // Returns true when the application's state-changed event is signalled,
    // meaning the process has exited and it is safe to Terminate + Close the
    // holder.
    bool CheckTerminated() {
        return appletApplicationCheckFinished(&g_ApplicationHolder);
    }

    // Force-terminate the application (hard kill), then Close the holder.
    // Used when the RequestExit deadline elapses and the application is still
    // running.  Never UL_RC_ASSERT — on any non-success result we log and
    // continue so the caller can still call LaunchMenu and return to desktop.
    Result ForceTerminateNow() {
        // Hard-kill via appletApplicationTerminate.  This is the same call that
        // the blocking app::Terminate() escalates to after eventWait times out.
        const auto rc = appletApplicationTerminate(&g_ApplicationHolder);
        if(R_FAILED(rc)) {
            UL_LOG_WARN("[app] ForceTerminateNow: appletApplicationTerminate failed: 0x%08X — proceeding with Close", rc);
        }

        appletApplicationClose(&g_ApplicationHolder);
        g_ApplicationHasFocus = false;
        UL_RC_SUCCEED;
    }

}
