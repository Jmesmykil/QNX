
#pragma once
// QOS-PATCH-010: memory telemetry helper for fw 20.0.0 applet-pool OOM diagnosis.
// Logs heap + applet-pool + process memory at tagged checkpoints via UL_LOG_INFO.
// 2026-04-18

namespace ul::system::mem {

    // Queries svcGetInfo + svcGetSystemInfo and emits a single log line:
    //   [MEM] tag=<tag> heap=<used>/<total>KB applet_pool=<used>/<total>MB process=<used>/<total>MB
    void LogMemoryStats(const char *tag);

}
