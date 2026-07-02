# v0.5.0 — 2026-04-18 — fw 20.0.0 OOM Fix (QOS-PATCH-010)

## Root Cause

Firmware 20.0.0 reduced the sysmodule applet-pool steal cap from 40 MB (fw 17–19)
to 14 MB. uLaunch v1.2.0's in-RAM NACP+icon cache targeted 40 MB
(unbounded std::unordered_map<u64, ApplicationNacpMisc>). At boot, pgl rejected
svcCreateProcess / svcSetResourceLimitLimitValue with error 2128-0100
(0xC880 = Module 128 pgl, Description 100 ResultOutOfMemory) because the NPDM
SystemResourceSize exceeded the 14 MB fw 20 hard ceiling.

## Changes

### projects/uSystem/source/ul/system/app/app_ControlCache.cpp
- Added `#include <deque>`.
- Introduced `constexpr size_t MaxCachedApplications = 50` (~10 MB at ~200 KB/entry,
  leaving 4 MB headroom under the 14 MB fw 20 cap).
- Added `std::deque<u64> *g_ApplicationCacheInsertionOrder` for FIFO eviction tracking.
- `InitializeControlCache`: allocates the new deque alongside existing queues.
- `AddNextApplicationCache`: evicts oldest entries (FIFO via deque) before inserting
  when the map reaches MaxCachedApplications.
- `AddNextApplicationCache`: pushes app_id to deque after successful insertion.
- `RemoveNextApplicationCache`: removes the evicted app_id from the deque on
  explicit removal to keep the structures in sync.

### projects/uSystem/include/ul/system/app/app_ControlCache.hpp
- Added QOS-PATCH-010 comment block explaining the fw 20 regression.

### projects/uSystem/source/ul/system/mem/mem_Telemetry.cpp (new)
### projects/uSystem/include/ul/system/mem/mem_Telemetry.hpp (new)
- `ul::system::mem::LogMemoryStats(tag)`: queries svcGetInfo (heap, process total/used)
  and svcGetSystemInfo (applet pool total/used, pool_id=1). Emits one UL_LOG_INFO line.

### projects/uSystem/source/main.cpp
- Added `#include <ul/system/mem/mem_Telemetry.hpp>`.
- `LogMemoryStats("boot")` before InitializeControlCache.
- `LogMemoryStats("post-cache-init")` after InitializeControlCache + CacheHomebrew.
- `LogMemoryStats("pre-menu-spawn")` before LaunchMenu in Main().

### projects/uSystem/uSystem.json
- Added `"system_resource_size": "0xC00000"` (12 MiB — 2 MiB under fw 20 cap).
  pgl reads this field from the NPDM when spawning the process; absence or excess
  caused the 0xC880 reject under fw 20.

## SHAs

| Artifact | SHA-256 |
|----------|---------|
| exefs.nsp (v0.5.0, uSystem) | e940fabadb0fade5dd3deba0e6a896958956281167c6cbf47a715c6ebc9b7d7a |
| uMenu/main (v0.5.0) | 471d81bc9ac073deab9e252a1b8463b4e2749131cedc1e25ef1b273b8f11c859 |
| exefs.nsp (v0.4.x / before patch) | 0fcf62886e172655e47b0cf63fb3bdb9e0a48a498afe2c4c43744f2318ebe62b |

## Expected Behaviour After This Patch

- pgl accepts the NPDM SystemResourceSize (0xC00000 < 14 MB fw 20 cap).
- In-RAM NACP cache is bounded at 50 entries. Library icons and .nacp.meta files
  remain on disk; only the hot in-RAM map is capped.
- Three LogMemoryStats checkpoints in uSystem logs allow real-hardware validation
  of applet-pool headroom before and after cache init and before menu spawn.
- No functional regression: the FIFO eviction policy is additive to the existing
  g_PendingApplicationRemovalCacheQueue path.
