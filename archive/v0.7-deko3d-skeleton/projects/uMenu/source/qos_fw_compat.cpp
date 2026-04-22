// qos_fw_compat.cpp — firmware/applet pool compatibility probe
// uMenu v0.7.0-beta6 — BUG 2 fix (INVALID_HANDLE correction)
//
// Implementation details:
//   svcGetInfo(u64* out, u32 id0, Handle handle, u64 id1)
//     id0 = SystemInfoType_TotalPhysicalMemorySize (0) → total applet pool
//     id0 = SystemInfoType_UsedPhysicalMemorySize  (1) → used applet pool
//     handle = INVALID_HANDLE (0) for system-wide pool queries on FW 20
//     id1 = PhysicalMemorySystemInfo_Applet (1)
//
//   FW 20 behavior (confirmed by hw telemetry 2001-0116 on beta5):
//     The pseudo-handle 0xFFFF8001 was rejected with KERN_InvalidHandle (0x0000E801).
//     System-wide applet-pool queries require INVALID_HANDLE (0) as the handle argument.
//
//   Enum values verified against:
//     /opt/devkitpro/libnx/include/switch/kernel/svc.h (2026-04-19)
//       SystemInfoType_TotalPhysicalMemorySize = 0
//       SystemInfoType_UsedPhysicalMemorySize  = 1
//       PhysicalMemorySystemInfo_Applet        = 1
#include "qos_fw_compat.hpp"

#include <switch.h>
#include <cstdio>

namespace qos {
namespace fw_compat {

namespace {

static bool    s_initialized    = false;
static bool    s_svcinfo_ok     = false;
static uint64_t s_applet_total  = 0;
static uint64_t s_applet_used   = 0;

// svcGetInfo handle for system-wide pool queries.
// FW 20 rejects the 0xFFFF8001 pseudo-handle with KERN_InvalidHandle (2001-0116);
// system-wide applet-pool queries require INVALID_HANDLE (0) per hw telemetry.
static constexpr Handle kSystemHandle = INVALID_HANDLE;

// id1 value for "applet" pool partition
static constexpr uint64_t kAppletPartition = 1u; // PhysicalMemorySystemInfo_Applet

} // anonymous namespace

bool Init() {
    if (s_initialized) {
        return s_svcinfo_ok;
    }
    s_initialized = true;

    // ── Firmware version ────────────────────────────────────────────────────
    // hosversionGet() is valid after hosversionSet() is called in __appInit
    // (which already ran setsysGetFirmwareVersion + hosversionSet).
    const uint32_t hosver = hosversionGet();
    const int fw_major = HOSVER_MAJOR(hosver);
    const int fw_minor = HOSVER_MINOR(hosver);
    const int fw_micro = HOSVER_MICRO(hosver);

    {
        FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (f) {
            fprintf(f, "[V7INIT] fw_compat::Init: HOS version %d.%d.%d (hosver=0x%08X)\n",
                    fw_major, fw_minor, fw_micro, (unsigned)hosver);
            fflush(f);
            fclose(f);
        }
    }

    // ── Applet pool: total physical memory ──────────────────────────────────
    u64 total = 0;
    Result rc_total = svcGetInfo(&total,
        SystemInfoType_TotalPhysicalMemorySize, kSystemHandle,
        kAppletPartition);

    if (R_FAILED(rc_total)) {
        FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (f) {
            fprintf(f,
                "[V7INIT] fw_compat::Init: svcGetInfo(TotalPhysical,Applet) FAILED 0x%08X "
                "(%04d-%04d) — headroom unavailable\n",
                (unsigned)rc_total,
                R_MODULE(rc_total) + 2000,
                R_DESCRIPTION(rc_total));
            fflush(f);
            fclose(f);
        }
        // Non-fatal: the rest of the menu still works without pool telemetry.
        s_svcinfo_ok = false;
        return false;
    }

    // ── Applet pool: used physical memory ───────────────────────────────────
    u64 used = 0;
    Result rc_used = svcGetInfo(&used,
        SystemInfoType_UsedPhysicalMemorySize, kSystemHandle,
        kAppletPartition);

    if (R_FAILED(rc_used)) {
        FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (f) {
            fprintf(f,
                "[V7INIT] fw_compat::Init: svcGetInfo(UsedPhysical,Applet) FAILED 0x%08X "
                "(%04d-%04d) — headroom unavailable\n",
                (unsigned)rc_used,
                R_MODULE(rc_used) + 2000,
                R_DESCRIPTION(rc_used));
            fflush(f);
            fclose(f);
        }
        s_svcinfo_ok = false;
        return false;
    }

    s_applet_total = total;
    s_applet_used  = used;
    s_svcinfo_ok   = true;

    const uint64_t headroom = (total >= used) ? (total - used) : 0u;

    // ── Telemetry output ─────────────────────────────────────────────────────
    {
        FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (f) {
            fprintf(f,
                "[TELEM] fw_compat applet_pool_total=%llu applet_pool_used=%llu "
                "applet_pool_headroom=%llu\n",
                (unsigned long long)total,
                (unsigned long long)used,
                (unsigned long long)headroom);
            fflush(f);
            fclose(f);
        }
    }

    return true;
}

uint64_t AppletPoolHeadroom() {
    if (!s_initialized || !s_svcinfo_ok) {
        return 0u;
    }
    return (s_applet_total >= s_applet_used)
        ? (s_applet_total - s_applet_used)
        : 0u;
}

} // namespace fw_compat
} // namespace qos
