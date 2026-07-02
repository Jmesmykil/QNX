#include <ul/loader/loader_SelfProcess.hpp>
#include <ul/loader/loader_Target.hpp>
#include <ul/loader/loader_Input.hpp>
#include <ul/loader/loader_ProgramIdUtils.hpp>
#include <ul/ul_Result.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/util/util_Size.hpp>
#include <cstdio>

using namespace ul::util::size;

namespace {

    constexpr auto HbloaderSettingsSectionName = "hbloader";

    template<typename T>
    inline Result GetHbloaderSetting(const char *key, T &out_value) {
        u64 setting_size;
        UL_RC_TRY(setsysGetSettingsItemValue(HbloaderSettingsSectionName, key, std::addressof(out_value), sizeof(out_value), &setting_size));

        if(setting_size != sizeof(out_value)) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        
        return ul::ResultSuccess;
    }

    constexpr size_t HeapSize = 64_KB;
    u8 g_Heap[HeapSize] = {};

    // DEEP TRACE (2026-06-21): flush-on-write trace to pinpoint exactly where
    // uLoader#2 dies on the HOME-from-game relaunch (the am 2128-0035 self-exit).
    // Each call appends + flushes + closes so it survives an abort OR an async am
    // kill mid-init. Requires fsdev mounted (so only valid after fsdevMountSdmc and
    // before fsExit). The test harness clears this file before each run.
    inline void UldrTrace(const char *tag) {
        // RAW-FS trace (mirrors uMenu's working EarlyTrace) — fopen silently fails
        // on uLoader's tiny 64KB heap, so use the fs service directly. Needs only
        // fsInitialize (NOT fsdevMountSdmc), append+flush so it survives an abort.
        FsFileSystem fs;
        if(R_FAILED(fsOpenSdCardFileSystem(&fs))) return;
        fsFsCreateFile(&fs, "/ulaunch/uloader_trace.log", 0, 0);   // idempotent
        FsFile file;
        if(R_SUCCEEDED(fsFsOpenFile(&fs, "/ulaunch/uloader_trace.log",
                                    FsOpenMode_Write | FsOpenMode_Append, &file))) {
            s64 sz = 0;
            fsFileGetSize(&file, &sz);
            char line[256];
            const int n = snprintf(line, sizeof(line), "%s\n", tag);
            if(n > 0) { fsFileWrite(&file, sz, line, (u64)n, FsWriteOption_Flush); }
            fsFileClose(&file);
        }
        fsFsClose(&fs);
    }

}

extern "C" {

    u32 __nx_applet_exit_mode = 2;

    u32 __nx_fs_num_sessions = 1;
    u32 __nx_fsdev_direntry_cache_size = 1;
    bool __nx_fsdev_support_cwd = false;

    extern u8 *fake_heap_start;
    extern u8 *fake_heap_end;

    void __libnx_initheap() {
        fake_heap_start = g_Heap;
        fake_heap_end = g_Heap + HeapSize;
    }

    void __appInit() {}
    void __appExit() {}

}

int main() {
    UL_RC_ASSERT(smInitialize());

    UL_RC_ASSERT(fsInitialize());
    // DEEP-TRACE step 0: earliest possible — fires right after fsInitialize. If
    // uloader_trace.log is EMPTY after a HOME-from-game test, am killed the applet
    // process before ANY uLoader code ran (launch-time rejection).
    UldrTrace("0 fsInitialize OK (raw-fs trace live)");
    UL_RC_ASSERT(fsdevMountSdmc());

    // DEEP-TRACE step 1: proves uLoader#2's process actually started executing.
    // If uloader_trace.log is EMPTY after a HOME-from-game test, am killed the
    // applet before any uLoader code ran (launch-time rejection, not mid-init).
    UldrTrace("1 entry: sm/fs/sdmc OK");

    ul::InitializeLogging("uLoader");

    UL_RC_ASSERT(setsysInitialize());

    SetSysFirmwareVersion fw_ver;
    UL_RC_ASSERT(setsysGetFirmwareVersion(&fw_ver));
    // Atmosphere is always assumed to be present (was used to launch us actually :P)
    hosversionSet(MAKEHOSVERSION(fw_ver.major, fw_ver.minor, fw_ver.micro) | BIT(31));

    u64 applet_heap_size;
    UL_RC_ASSERT(GetHbloaderSetting("applet_heap_size", applet_heap_size));
    u64 applet_heap_reservation_size;
    UL_RC_ASSERT(GetHbloaderSetting("applet_heap_reservation_size", applet_heap_reservation_size));

    setsysExit();
    UldrTrace("2 setsys + hbloader settings OK");

    u64 self_program_id;
    UL_RC_ASSERT(ul::loader::GetSelfProgramId(self_program_id));
    ul::loader::DetermineSelfAppletType(self_program_id);
    UldrTrace("3 self program id + applet type OK");

    ul::loader::TargetInput target_ipt;
    UL_RC_ASSERT(ul::loader::ReadTargetInput(target_ipt));

    {
        char _b[224];
        snprintf(_b, sizeof(_b), "4 target read OK: nro='%.180s' once=%d", target_ipt.nro_path, (int)target_ipt.target_once);
        UldrTrace(_b);
    }

    UL_LOG_INFO("Targetting '%s' with argv '%s' (once: %d)", target_ipt.nro_path, target_ipt.nro_argv, target_ipt.target_once);

    // DEEP-TRACE step 5: last point with fs open. If the trace stops here but
    // uMenu's EarlyTrace stays empty, the failure is inside Target() (the hbloader
    // code-memory load + trampoline) OR am kills the process during/after it.
    UldrTrace("5 BEFORE fsExit + Target() jump to uMenu");

    fsdevUnmountAll();
    fsExit();
    smExit();

    ul::loader::Target(target_ipt, applet_heap_size, applet_heap_reservation_size);
}
