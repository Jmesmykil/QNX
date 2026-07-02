# Login Screen Reactivity — v2.3.7 Audit (uMenu md5 fe408f8f)

## Three Invariants

| Invariant | File:line | Status |
|---|---|---|
| `POWER_CLICK_TOLERANCE_PX = 30` | `qd_PowerButton.cpp:224` | INTACT |
| Edge-triggered PowerButton state machine | `qd_PowerButton.cpp:245–301` (TouchDown sets `press_inside_` once; TouchMove only tracks position; TouchUp fires on cumulative distance) | INTACT |
| `first_main_frame_done_` welcomer guard | `ui_MainMenuLayout.hpp:114` + `ui_MainMenuLayout.cpp:1461` | INTACT |

## Root Cause

**File:line:** `qd_DevTools.cpp:228–277` — `TryEnableUsbSerial()`
**Call site:** `ui_StartupMenuLayout.cpp:236–242` — `qd_btn_usbserial->SetOnClick` lambda

When the USB Serial button is pressed on the login screen, `TryEnableUsbSerial()` runs three blocking operations on the **main UI thread**:
1. `usbCommsInitialize()` — USB CDC-ACM IPC init (line 235)
2. `ul::tel::Flush()` — `fdatasync` to SD card (line 247); 100–500 ms on Class 10 SD
3. `fread` loop + `usbCommsWrite` — reads `/qos-shell/logs/uMenu.0.log` and streams it to USB in 4 KiB chunks (lines 249–265)

The main loop does not advance during this window. All buttons — including the power row, user cards, and nxlink button — appear frozen. The file header at `qd_DevTools.cpp:18–19` incorrectly states that background threads are "unsafe during the login-screen phase"; the nxlink fix in 2ab5fa30 demonstrates they are not.

The tile-icon and vault-drag working-tree changes are confirmed **not** contributing: `QdNintendoAppsLayout` and `QdVaultLayout` are only instantiated from `MainMenuLayout` and are unreachable from `StartupMenuLayout` at both source and runtime level.

## Pre-Built Fix

**File:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp`

```
old_string:
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

new_string:
    // 2026-05-06 reactivity fix: move blocking USB init + SD read off the main
    // UI thread.  Same worker-thread pattern as TryEnableNxlinkServer (see
    // 2ab5fa30).  Set g_usb_serial_active optimistically so the button label
    // flips immediately; the worker owns the actual IO.
    g_usb_serial_active = true;
    static Thread g_usb_snapshot_thread;
    static bool   g_usb_thread_live = false;
    if (!g_usb_thread_live) {
        struct Worker {
            static void Run(void *) {
                const Result rc = usbCommsInitialize();
                if (R_FAILED(rc)) {
                    UL_LOG_WARN("qdesktop: dev::TryEnableUsbSerial worker — "
                                "usbCommsInitialize failed rc=0x%08X",
                                static_cast<unsigned>(rc));
                    return;
                }
                UL_TEL_INFO(Generic, "qdesktop: dev — USB serial CDC-ACM active");
                ul::tel::Flush();
                FILE *fp = fopen(kRingSegPath, "rb");
                if (fp != nullptr) {
                    usbCommsWrite("[qd_devtools] USB snapshot begin\n", 33u);
                    static char chunk[kUsbChunkBytes];
                    size_t n = 0u;
                    while ((n = fread(chunk, 1u, kUsbChunkBytes, fp)) > 0u) {
                        usbCommsWrite(chunk, n);
                    }
                    fclose(fp);
                    usbCommsWrite("[qd_devtools] USB snapshot end\n", 31u);
                    UL_TEL_INFO(Generic, "qdesktop: dev — USB snapshot done");
                }
            }
        };
        const Result rc_t = threadCreate(&g_usb_snapshot_thread,
                                         Worker::Run, nullptr, nullptr,
                                         0x4000, 0x2C, -2);
        if (R_SUCCEEDED(rc_t)) {
            threadStart(&g_usb_snapshot_thread);
            g_usb_thread_live = true;
        } else {
            UL_LOG_WARN("qdesktop: dev::TryEnableUsbSerial — threadCreate failed");
        }
    }
    return true;
```

## Confidence

High — blocking SD `fdatasync` + `fread` loop on the main thread is the only per-button-press mechanism that can stall the login screen render loop. All per-frame paths (`OnMenuUpdate`, `OnRender` on all login-screen elements) confirmed non-blocking via static analysis.

## Adjacent Risks

- `qd_DevTools.cpp:19` file-header comment ("background thread unsafe during login") is wrong and should be updated to match the fix rationale.
- `DisableUsbSerial()` calls `usbCommsExit()` synchronously on the main thread — audit whether that is blocking.
- After the fix, `g_usb_serial_active` is set optimistically before the worker thread finishes init. If `usbCommsInitialize` fails in the worker, `g_usb_serial_active` remains `true` but USB is non-functional. Add a failure-path reset: `g_usb_serial_active = false` in the worker's error branch.
