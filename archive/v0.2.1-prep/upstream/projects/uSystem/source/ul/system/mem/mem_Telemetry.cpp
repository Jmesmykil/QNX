#include <ul/system/mem/mem_Telemetry.hpp>
#include <ul/ul_Result.hpp>
// QOS-PATCH-010: live memory telemetry for fw 20.0.0 applet-pool OOM analysis.
// svcGetInfo IDs as documented in switchbrew (confirmed libnx InfoType enum).
// svcGetSystemInfo SystemInfoType_TotalPhysicalMemorySize=0,
//                  SystemInfoType_UsedPhysicalMemorySize=1,  pool_id=1 (Applet).
// 2026-04-18

namespace ul::system::mem {

    void LogMemoryStats(const char *tag) {
        // ── Per-process heap (InfoType 4=HeapRegionAddress, 5=HeapRegionSize) ──
        u64 heap_addr  = 0;
        u64 heap_size  = 0;
        svcGetInfo(&heap_addr, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&heap_size,  InfoType_HeapRegionSize,   CUR_PROCESS_HANDLE, 0);

        // ── Per-process total/used memory ──
        u64 proc_total = 0;
        u64 proc_used  = 0;
        svcGetInfo(&proc_total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&proc_used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);

        // ── System-wide applet pool (pool_id=1) ──
        u64 applet_total = 0;
        u64 applet_used  = 0;
        svcGetSystemInfo(&applet_total, SystemInfoType_TotalPhysicalMemorySize, INVALID_HANDLE, 1);
        svcGetSystemInfo(&applet_used,  SystemInfoType_UsedPhysicalMemorySize,  INVALID_HANDLE, 1);

        UL_LOG_INFO("[MEM] tag=%s heap=%lluKB/%lluKB applet_pool=%lluMB/%lluMB process=%lluMB/%lluMB",
            tag,
            proc_used  / 1024ULL,
            proc_total / 1024ULL,
            applet_used  / (1024ULL * 1024ULL),
            applet_total / (1024ULL * 1024ULL),
            proc_used  / (1024ULL * 1024ULL),
            proc_total / (1024ULL * 1024ULL)
        );

        (void)heap_addr;
        (void)heap_size;
    }

}
