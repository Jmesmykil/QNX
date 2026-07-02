#include <ul/system/la/la_LibraryApplet.hpp>
#include <ul/la/la_LibraryApplets.hpp>
#include <ul/ul_Result.hpp>

namespace ul::system::la {

    namespace {

        // SINGLE-HOLDER (pre-v3.7.47, restored 2026-06-20).
        //
        // g_LibraryAppletHolder owns ALL library applets — uMenu and NROs alike.
        // uMenu must exit before a game (Application) launches; HOS AM enforces
        // this at the IPC level (app::Start returns rc=0xD37C if a library-applet
        // holder is live).  The dual-holder resident-uMenu approach introduced in
        // v3.7.47–v3.8.2 has been reverted because it made game launching
        // impossible.
        AppletHolder g_LibraryAppletHolder;
        AppletId g_MenuAppletId = AppletId_None;
        AppletId g_LastAppletId = AppletId_None;

        struct LibraryAppletInfo {
            u64 program_id;
            AppletId applet_id;
        };

        #define _UL_LA_TABLE_ENTRY(applet) LibraryAppletInfo { ::ul::la::applet, AppletId_##applet }

        constexpr LibraryAppletInfo g_LibraryAppletTable[] = {
            _UL_LA_TABLE_ENTRY(LibraryAppletAuth),
            _UL_LA_TABLE_ENTRY(LibraryAppletCabinet),
            _UL_LA_TABLE_ENTRY(LibraryAppletController),
            _UL_LA_TABLE_ENTRY(LibraryAppletDataErase),
            _UL_LA_TABLE_ENTRY(LibraryAppletError),
            _UL_LA_TABLE_ENTRY(LibraryAppletNetConnect),
            _UL_LA_TABLE_ENTRY(LibraryAppletPlayerSelect),
            _UL_LA_TABLE_ENTRY(LibraryAppletSwkbd),
            _UL_LA_TABLE_ENTRY(LibraryAppletMiiEdit),
            _UL_LA_TABLE_ENTRY(LibraryAppletWeb),
            _UL_LA_TABLE_ENTRY(LibraryAppletShop),
            _UL_LA_TABLE_ENTRY(LibraryAppletPhotoViewer),
            _UL_LA_TABLE_ENTRY(LibraryAppletSet),
            _UL_LA_TABLE_ENTRY(LibraryAppletOfflineWeb),
            _UL_LA_TABLE_ENTRY(LibraryAppletLoginShare),
            _UL_LA_TABLE_ENTRY(LibraryAppletWifiWebAuth),
            _UL_LA_TABLE_ENTRY(LibraryAppletMyPage),
        };
        constexpr size_t LibraryAppletCount = sizeof(g_LibraryAppletTable) / sizeof(LibraryAppletInfo);

        // Helper: check whether the single AppletHolder has an active applet.
        inline bool HolderIsActive(AppletHolder *h) {
            if(h->StateChangedEvent.revent == INVALID_HANDLE) { return false; }
            if(!serviceIsActive(&h->s)) { return false; }
            return !appletHolderCheckFinished(h);
        }

        Result Create(const AppletId id, const s32 la_version) {
            // RESTORED v3.7.0 (2026-06-22): the heavy post-v3.7.0 cleanup that lived here
            // (RequestExitOrTerminate + cmd-25 appletHolderTerminate + DIAG-L2 + Close on a
            // FINISHED holder) hung uMenu#2 after HOME-from-game. v3.7.0 only terminates a LIVE
            // holder then re-creates; re-using a finished holder is fine (HW-proven in v3.7.0).
            if(IsActive()) {
                UL_RC_TRY(Terminate());
            }

            UL_RC_TRY(appletCreateLibraryApplet(&g_LibraryAppletHolder, id, LibAppletMode_AllForeground));

            // Treat -1/any negative pseudovalue as to not push these args
            if(la_version >= 0) {
                LibAppletArgs la_args;
                libappletArgsCreate(&la_args, (u32)la_version);
                libappletArgsSetPlayStartupSound(&la_args, true);
                UL_RC_TRY(libappletArgsPush(&la_args, &g_LibraryAppletHolder));
            }

            return ResultSuccess;
        }

        Result Launch(const AppletId created_id) {
            UL_RC_TRY(appletHolderStart(&g_LibraryAppletHolder));
            g_LastAppletId = created_id;
            return ResultSuccess;
        }

    }

    // IsActive — checks g_LibraryAppletHolder (uMenu OR NRO/system applets).
    bool IsActive() {
        return HolderIsActive(&g_LibraryAppletHolder);
    }

    Result Terminate() {
        // v2.8.0 — lowered RequestExitOrTerminate timeout from 15s to 2s.
        //
        // uMenu does NOT install an applet-message handler (no
        // appletMainLoop / appletGetMessage anywhere in src/projects/uMenu/).
        // So the kernel's RequestExit message (cmd 20) is never picked up
        // by the applet thread — the timeout ALWAYS fully elapses, then
        // libnx escalates to Terminate (cmd 25, the kernel kill).  The
        // original 15s was a graceful-exit budget for a polite uMenu we
        // don't ship.  Going to 2s keeps a tiny window for late teardown
        // (audio fade-out completion, BGM dispose) but collapses the
        // uSystem-MainLoop-blocked window by 13s.
        //
        // The 13s reduction directly attacks the post-theme-switch login
        // hang: while uSystem's MainLoop is parked in this function, no
        // SMI IPC handler runs.  uMenu B's first SetSelectedUser IPC
        // therefore waits up to 15s for a reply.  Combined with the
        // MaxPrivateSessions=4 fix (sf_IpcManager.hpp:33) the race window
        // closes completely.
        //
        // Future work: install a proper appletMainLoop in uMenu that picks
        // up RequestExit, sets g_uMenuTerminating, and breaks Plutonium's
        // Show() loop.  That collapses the wait to <100ms in the happy path.
        UL_RC_TRY(appletHolderRequestExitOrTerminate(&g_LibraryAppletHolder, 2'000'000'000ul));
        // 2026-05-06 AMS-1.11 clean-exit fix: appletHolderJoin (cmd 30
        // GetResult against ILibraryAppletAccessor) must run between
        // RequestExitOrTerminate (cmd 20/25) and Close (no-IPC handle
        // close).  AMS 1.11.0+ requires GetResult to consume the applet
        // exit-result before the IPC server frees the slot.  Without it
        // the slot stays allocated in the ServerManager pool until the
        // parent dies — which is when uSystem's 6-slot pool fills and we
        // abort at sf_hipc_server_session_manager.hpp:109 with
        // 2011-0102 ResultOutOfSessionMemory.  See
        // docs/research/AMS-1.11-CLEAN-EXIT-CONTRACT-20260506.md.
        appletHolderJoin(&g_LibraryAppletHolder);
        appletHolderClose(&g_LibraryAppletHolder);

        UL_RC_SUCCEED;
    }

    Result Start(const AppletId id, const s32 la_version) {
        UL_RC_TRY(Create(id, la_version));
        UL_RC_TRY(Launch(id));
        return ResultSuccess;
    }

    Result Start(const AppletId id, const s32 la_version, const void *in_data, const size_t in_size) {
        UL_RC_TRY(Create(id, la_version));

        if((in_data != nullptr) && (in_size > 0)) {
            UL_RC_TRY(libappletPushInData(&g_LibraryAppletHolder, in_data, in_size));
        }

        UL_RC_TRY(Launch(id));
        return ResultSuccess;
    }

    Result Start(const AppletId id, const s32 la_version, const void *in_data, const size_t in_size, const void *in_data_2, const size_t in_size_2) {
        UL_RC_TRY(Create(id, la_version));

        if((in_data != nullptr) && (in_size > 0)) {
            UL_RC_TRY(libappletPushInData(&g_LibraryAppletHolder, in_data, in_size));
        }
        if((in_data_2 != nullptr) && (in_size_2 > 0)) {
            UL_RC_TRY(libappletPushInData(&g_LibraryAppletHolder, in_data_2, in_size_2));
        }

        UL_RC_TRY(Launch(id));
        return ResultSuccess;
    }

    // Send / Read / Push / Pop operate on g_LibraryAppletHolder.
    Result Send(const void *data, const size_t size) {
        return libappletPushInData(&g_LibraryAppletHolder, data, size);
    }

    Result Read(void *data, const size_t size) {
        return libappletPopOutData(&g_LibraryAppletHolder, data, size, nullptr);
    }

    Result Push(AppletStorage *st) {
        return appletHolderPushInData(&g_LibraryAppletHolder, st);
    }

    Result Pop(AppletStorage *st) {
        return appletHolderPopOutData(&g_LibraryAppletHolder, st);
    }

    // v3.1 Phase 2 Slice 1 BackgroundIndirect implementation removed
    // 2026-05-19 — see header for rationale.

    // ---------------------------------------------------------------------------
    // Non-blocking NRO terminate helpers (Fix B/C/D — HOME-over-NRO hang).
    // See header for full rationale.
    // ---------------------------------------------------------------------------

    // Send a single RequestExit (cmd 20) and return immediately.
    // Does NOT wait; does NOT escalate to Terminate.  The caller is responsible
    // for polling CheckTerminated() and calling ForceTerminateNow() on deadline.
    void RequestExitNonBlocking() {
        // appletHolderRequestExit sends IPC cmd 20 to the applet's
        // ILibraryAppletAccessor interface.  It is non-blocking — it queues the
        // exit message and returns immediately regardless of whether the applet
        // picks it up.  libnx signature: void appletHolderRequestExit(AppletHolder*).
        appletHolderRequestExit(&g_LibraryAppletHolder);
    }

    // Returns true when the applet's state-changed event is signalled, meaning
    // the process has exited and it is safe to Join + Close the holder.
    bool CheckTerminated() {
        return appletHolderCheckFinished(&g_LibraryAppletHolder);
    }

    // Force-terminate (cmd 25 Terminate), then Join + Close the holder.
    // Used when the RequestExit deadline elapses and the NRO is still running.
    // Fix C: the timeout passed to RequestExitOrTerminate is 500 ms (down from
    // the old 2 s); since we only reach here after the deadline has already
    // elapsed and the process is presumably still alive, we request an immediate
    // kernel terminate and collect the result.
    //
    // Fix D: never UL_RC_ASSERT — on any non-success result we log and continue
    // so the caller can still call LaunchMenu and return to desktop.
    Result ForceTerminateNow() {
        // 500 ms timeout (Fix C) — used only for the RequestExitOrTerminate
        // escalation path inside ForceTerminateNow.  At this point the deadline
        // has already elapsed so we want a fast hard-kill, not another long wait;
        // 500 ms gives the kernel a brief window before the Terminate cmd fires.
        constexpr u64 kForceTerminateTimeoutNs = 500'000'000ul;

        const auto rc = appletHolderRequestExitOrTerminate(&g_LibraryAppletHolder, kForceTerminateTimeoutNs);
        if(R_FAILED(rc)) {
            // Fix D: log but do not assert — we still need to Join + Close so
            // the IPC slot is released (AMS-1.11 clean-exit contract).
            UL_LOG_WARN("[la] ForceTerminateNow: RequestExitOrTerminate failed: 0x%08X — proceeding with Join+Close", rc);
        }

        // Consume the exit result (AMS-1.11 requirement — see Terminate() comment).
        appletHolderJoin(&g_LibraryAppletHolder);
        appletHolderClose(&g_LibraryAppletHolder);

        UL_RC_SUCCEED;
    }

    u64 GetProgramIdForAppletId(const AppletId id) {
        for(u32 i = 0; i < LibraryAppletCount; i++) {
            const auto info = g_LibraryAppletTable[i];
            if(info.applet_id == id) {
                return info.program_id;
            }
        }

        UL_ASSERT_FAIL("Invalid applet ID: 0x%08X", id);
    }

    AppletId GetAppletIdForProgramId(const u64 id) {
        for(u32 i = 0; i < LibraryAppletCount; i++) {
            const auto info = g_LibraryAppletTable[i];
            if(info.program_id == id) {
                return info.applet_id;
            }
        }

        UL_ASSERT_FAIL("Invalid applet program ID: %016lX", id);
    }

    AppletId GetLastAppletId() {
        auto last_id_copy = g_LastAppletId;
        if(!IsActive()) {
            g_LastAppletId = AppletId_None;
        }
        return last_id_copy;
    }

    void SetMenuProgramId(const u64 id) {
        g_MenuAppletId = GetAppletIdForProgramId(id);
    }

    u64 GetMenuProgramId() {
        return GetProgramIdForAppletId(g_MenuAppletId);
    }

}
