// qd_DevTools.cpp — Developer-mode diagnostic channel toggles for Q OS uMenu.
//
// Implements: ul::menu::qdesktop::dev::{IsNxlinkActive, TryEnableNxlink,
//             DisableNxlink, IsUsbSerialActive, TryEnableUsbSerial,
//             DisableUsbSerial, FlushAllChannels}.
//
// Build constraints: devkitA64 -std=gnu++23 -fno-rtti -fno-exceptions -Werror
// All hardware calls are wrapped in #ifdef __SWITCH__ so the host unit-test
// build remains clean.
//
// USB-serial mirroring strategy — one-shot snapshot model
// ─────────────────────────────────────────────────────────
// Real-time stdout tee to usbCommsWrite would require either:
//   (a) dup2 a pipe over fd 1 and spawn a drain thread, OR
//   (b) a tee hook installed into ul::tel::Emit before every write.
//
// Neither option is available without modifying util_Telemetry (which this
// file must not touch) or running a background thread (unsafe during the
// login-screen phase where USB is most useful).
//
// Instead, TryEnableUsbSerial uses the one-shot snapshot model:
//   1. On enable: call ul::tel::Flush() to drain the async ring to the
//      SD ring file, then fdatasync.  Read the entire current segment file
//      back and stream it via usbCommsWrite in 4 KiB chunks.  This gives
//      the developer everything logged since boot on the first click.
//   2. Subsequent live writes go to the SD ring as normal (via ul::tel::Emit).
//   3. The user clicks "USB serial" a second time (DisableUsbSerial) then
//      re-enables it to snapshot again after more activity.
//
// This is honest: no claim of real-time mirroring is made, and the user
// receives complete, readable log data without the complexity of a drain
// thread.  The header comment on TryEnableUsbSerial documents the model so
// callers set the right UI expectation.
//
// Known hardware interaction warning
// ────────────────────────────────────
// usbCommsInitialize() configures the USB device interface as a CDC-ACM
// serial device.  On the Nintendo Switch, the USB port is shared with USB
// Mass Storage (UMS) mode used by the UMS workflow.  If UMS is active when
// TryEnableUsbSerial is called, usbCommsInitialize will fail with
// ResultCode 0x272E02 (usbds already in use).  The caller receives false
// and the SD ring file is unaffected.  Do not call both simultaneously.

#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <ul/util/util_Telemetry.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#endif

namespace ul::menu::qdesktop::dev {

// ── File-static state ────────────────────────────────────────────────────────

#ifdef __SWITCH__
namespace {

// BSD socket subsystem init state.  Tracked so we call socketInitializeDefault
// only once per process lifetime; socketExit() is deliberately never called
// from this module because other libnx consumers (network services, etc.) may
// still need the socket subsystem.
static bool g_socket_initialized = false;

// Active nxlink socket file descriptor, or -1 when inactive.
static int g_nxlink_fd = -1;

// USB-CDC serial active flag.
static bool g_usb_serial_active = false;

// Path of the current telemetry segment file.  Populated lazily by
// TryEnableUsbSerial when it needs to snapshot the ring to USB.
// Format: /qos-shell/logs/uMenu.0.log (segment 0 is the default after Init).
static constexpr const char kRingSegPath[] = "/qos-shell/logs/uMenu.0.log";

// USB snapshot write chunk size — 4 KiB fits comfortably in the CDC bulk
// transfer budget and keeps stack pressure low.
static constexpr size_t kUsbChunkBytes = 4096u;

} // namespace
#endif // __SWITCH__

// ── IsNxlinkActive ───────────────────────────────────────────────────────────

bool IsNxlinkActive() {
#ifdef __SWITCH__
    return g_nxlink_fd >= 0;
#else
    return false;
#endif
}

// ── TryEnableNxlink ──────────────────────────────────────────────────────────

bool TryEnableNxlink() {
#ifdef __SWITCH__
    // Idempotent: already active → fast no-op.
    if (g_nxlink_fd >= 0) {
        return true;
    }

    // Lazy-init the BSD socket subsystem exactly once.
    if (!g_socket_initialized) {
        const Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qdesktop: dev::TryEnableNxlink — socketInitializeDefault "
                        "failed rc=0x%08X", static_cast<unsigned>(rc));
            return false;
        }
        g_socket_initialized = true;
        UL_TEL_INFO(Generic, "qdesktop: dev — socket subsystem initialized");
    }

    // Broadcast for a nxlink host on the LAN.  nxlinkConnectToHost has an
    // internal ~2 s timeout; if no host is listening it returns -1.  This is
    // the common case (no PC running nxlink) — log at INFO, not WARN.
    const int fd = nxlinkConnectToHost(true, true);
    if (fd < 0) {
        UL_TEL_INFO(Generic, "qdesktop: dev::TryEnableNxlink — no host found "
                    "(nxlinkConnectToHost returned %d)", fd);
        return false;
    }

    g_nxlink_fd = fd;
    UL_TEL_INFO(Generic, "qdesktop: dev — nxlink active fd=%d", g_nxlink_fd);
    return true;
#else
    return false;
#endif
}

// ── DisableNxlink ────────────────────────────────────────────────────────────

void DisableNxlink() {
#ifdef __SWITCH__
    if (g_nxlink_fd < 0) {
        // Already inactive — documented no-op.
        return;
    }
    close(g_nxlink_fd);
    UL_TEL_INFO(Generic, "qdesktop: dev — nxlink closed fd=%d", g_nxlink_fd);
    g_nxlink_fd = -1;
    // Do NOT call socketExit() — other subsystems may still use the socket
    // layer (network services, etc.).  The socket subsystem stays up for the
    // process lifetime once initialised.
#endif
}

// ── IsUsbSerialActive ────────────────────────────────────────────────────────

bool IsUsbSerialActive() {
#ifdef __SWITCH__
    return g_usb_serial_active;
#else
    return false;
#endif
}

// ── TryEnableUsbSerial ───────────────────────────────────────────────────────
//
// USB-serial model: one-shot snapshot (see file header for rationale).
// On success:
//   - usbCommsInitialize() configures the USB port as CDC-ACM.
//   - ul::tel::Flush() drains the async ring + fdatasync the SD file.
//   - The current ring segment is read and written to USB in 4 KiB chunks.
// Returns false without touching state if usbCommsInitialize fails.

bool TryEnableUsbSerial() {
#ifdef __SWITCH__
    // Idempotent: already active → fast no-op.
    if (g_usb_serial_active) {
        return true;
    }

    const Result rc = usbCommsInitialize();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: dev::TryEnableUsbSerial — usbCommsInitialize "
                    "failed rc=0x%08X (USB port may be in UMS mode)",
                    static_cast<unsigned>(rc));
        return false;
    }

    g_usb_serial_active = true;
    UL_TEL_INFO(Generic, "qdesktop: dev — USB serial CDC-ACM active");

    // ── One-shot snapshot: drain ring → SD, then stream SD → USB ─────────────
    ul::tel::Flush();

    FILE *fp = fopen(kRingSegPath, "rb");
    if (fp != nullptr) {
        // Stamp the beginning of the USB stream so the reader knows where
        // the snapshot starts.
        const char header[] = "[qd_devtools] USB snapshot begin\n";
        usbCommsWrite(header, sizeof(header) - 1u);

        static char chunk[kUsbChunkBytes];
        size_t n = 0u;
        while ((n = fread(chunk, 1u, kUsbChunkBytes, fp)) > 0u) {
            usbCommsWrite(chunk, n);
        }
        fclose(fp);

        const char footer[] = "[qd_devtools] USB snapshot end\n";
        usbCommsWrite(footer, sizeof(footer) - 1u);

        UL_TEL_INFO(Generic, "qdesktop: dev — USB snapshot complete from %s",
                    kRingSegPath);
    } else {
        UL_LOG_WARN("qdesktop: dev::TryEnableUsbSerial — ring file %s not "
                    "readable (first boot or path mismatch)", kRingSegPath);
    }

    return true;
#else
    return false;
#endif
}

// ── DisableUsbSerial ─────────────────────────────────────────────────────────

void DisableUsbSerial() {
#ifdef __SWITCH__
    if (!g_usb_serial_active) {
        // Already inactive — documented no-op.
        return;
    }
    usbCommsExit();
    g_usb_serial_active = false;
    UL_TEL_INFO(Generic, "qdesktop: dev — USB serial torn down");
#endif
}

// ── FlushAllChannels ─────────────────────────────────────────────────────────

void FlushAllChannels() {
    // 1. Drain the async SPSC ring and fdatasync the SD ring file.
    ul::tel::Flush();

#ifdef __SWITCH__
    // 2. If nxlink is active, send a timestamped flush marker so the host
    //    log viewer can correlate the manual flush event.
    if (g_nxlink_fd >= 0) {
        dprintf(g_nxlink_fd, "[flush] manual flush at %ld\n",
                static_cast<long>(::time(nullptr)));
    }

    // 3. If USB serial is active, send a short flush marker.
    if (g_usb_serial_active) {
        usbCommsWrite("[flush]\n", 8u);
    }
#endif

    const bool nxlink_up = IsNxlinkActive();
    const bool usb_up    = IsUsbSerialActive();
    UL_TEL_INFO(Generic,
                "qdesktop: dev::FlushAllChannels — ring drained, "
                "channels=%s%s",
                nxlink_up ? "nxlink " : "",
                usb_up    ? "usb_serial" : "");
}

} // namespace ul::menu::qdesktop::dev
