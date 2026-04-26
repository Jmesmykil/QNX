# Autonomous USB-Serial Test Rig — Design Document

**Author:** K+5 session, 2026-04-25T00:00:00Z
**Purpose:** Eliminate the 5–10 min human-gated UMS loop and let the AI iterate on
`uMenu.nso` autonomously over USB-C serial.

---

## 1. Architecture Diagram

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                        Mac (host)                               │
  │                                                                 │
  │  tools/test-rig/mac-bridge.py                                   │
  │  ┌──────────────────────────────────────────────────────────┐   │
  │  │  push_nso(path)   → 0x01 PUSH_NSO <len32> <bytes>       │   │
  │  │  restart_umenu()  → 0x02 RESTART_UMENU                  │   │
  │  │  tap(x, y)        → 0x03 TAP <x16> <y16>                │   │
  │  │  press_button(btn)→ 0x04 PRESS_BUTTON <btn8>             │   │
  │  │  screenshot()     → 0x05 SCREENSHOT_REQ                  │   │
  │  │  read_log()       → 0x06 READ_LOG                        │   │
  │  │                                                           │   │
  │  │  (reads back)     ← 0x80 PUSH_NSO_OK                    │   │
  │  │                   ← 0x81 SCREENSHOT_DATA <len32> <png>   │   │
  │  │                   ← 0x82 LOG_DATA <len32> <bytes>        │   │
  │  │                   ← 0xFE ERROR <code8> <msg_len8> <msg>  │   │
  │  │                   ← 0xFF DONE                            │   │
  │  └──────────────────────────────────────────────────────────┘   │
  │            │                                                     │
  │            │  pyserial  /dev/cu.usbmodem<N>  115200 baud        │
  └────────────┼────────────────────────────────────────────────────┘
               │
               │ USB-C data cable (must carry data lines — not power-only)
               │
  ┌────────────┼────────────────────────────────────────────────────┐
  │            │         Nintendo Switch (Erista, handheld)         │
  │            ▼                                                     │
  │  USB-C port  ←  CDC-ACM serial via usbCommsInitialize()         │
  │            │                                                     │
  │  Atmosphère sysmodule layer:                                     │
  │  ┌─────────────────────────────────────────────────────────┐    │
  │  │  sys-patch TID 420000000000000B  (already on SD)        │    │
  │  └─────────────────────────────────────────────────────────┘    │
  │            │                                                     │
  │  Applet layer:                                                   │
  │  ┌─────────────────────────────────────────────────────────┐    │
  │  │  uMenu (qlaunch replacement)                            │    │
  │  │  • renders desktop / launcher UI                        │    │
  │  │  • exposes USB-serial toggle via qd_DevTools.cpp        │    │
  │  └──────────────────┬──────────────────────────────────────┘    │
  │                     │ IPC / shared-memory notification          │
  │  ┌──────────────────▼──────────────────────────────────────┐    │
  │  │  qos-test-harness.nro  (v2.x — new serial command loop) │    │
  │  │  • started from qlaunch or hbmenu as "host" applet      │    │
  │  │  • opens CDC-ACM via usbCommsInitialize                  │    │
  │  │  • dispatches incoming command bytes:                    │    │
  │  │      PUSH_NSO    → write .nso to sdmc:/atmosphere/...   │    │
  │  │      RESTART     → appletRequestExitToSelf + relaunch   │    │
  │  │      TAP         → hidSetSupportedNpadStyleSet + touch   │    │
  │  │      PRESS_BUTTON→ emulated HID gamepad report          │    │
  │  │      SCREENSHOT  → viCaptureScreen → PNG → USB write    │    │
  │  │      READ_LOG    → read ring file → USB write           │    │
  │  └─────────────────────────────────────────────────────────┘    │
  └─────────────────────────────────────────────────────────────────┘
```

---

## 2. Hardware Requirements

| Item | Requirement |
|------|------------|
| USB-C cable | Must carry **data lines** — power-only cables are common and will silently fail. Verify by checking that `/dev/cu.usbmodem*` appears on Mac before the harness runs. |
| Switch model | Erista (HAC-001) — this design targets handheld mode with USB-C accessible. Mariko (HAC-001-01) uses a different USB controller; adapt if needed. |
| Atmosphère | 1.7.1 or later — required for `usbCommsInitialize` stability in applet context. |
| Hekate | 6.x or later — used for boot chaining and UMS mode. |
| sys-patch | TID `420000000000000B` already installed on SD at `/atmosphere/contents/420000000000000B/`. This is what's on the SD right now (confirmed: `toolbox.json` present, `boot2.flag` present). **Note:** The SD currently has `sys-patch`, NOT `sys-con`. See §10 for the critical distinction. |
| Devkit cable | NOT the dock — use the direct USB-C port on the bottom of the Switch. |
| Exclusive USB host | While the serial rig is active, **nothing else may hold the USB-C port**: UMS must be off, no charge-only mode via some adapters. One device, one mode. |

---

## 3. Software Stack — Mac Side

### 3.1 Python Dependencies

```bash
# pyserial — confirmed installed 2026-04-25
pip3 install pyserial   # version 3.5 now installed

# Optional: Pillow for PNG comparison
pip3 install Pillow
```

### 3.2 mac-bridge.py — Command Surface

Location: `tools/test-rig/mac-bridge.py`

The bridge is both a library (importable from Python scripts) and a CLI:

```bash
# CLI usage
python3 tools/test-rig/mac-bridge.py push uMenu.nso
python3 tools/test-rig/mac-bridge.py restart-umenu
python3 tools/test-rig/mac-bridge.py tap 640 360
python3 tools/test-rig/mac-bridge.py press-button A
python3 tools/test-rig/mac-bridge.py screenshot --out /tmp/test.png
python3 tools/test-rig/mac-bridge.py read-log --out /tmp/umenu.log
```

### 3.3 Serial Port Discovery

The Mac enumerates CDC-ACM devices as `/dev/cu.usbmodem<N>`. The bridge auto-discovers by globbing and picking the first match. If multiple USB-serial devices are attached, pass `--port /dev/cu.usbmodemXXXX` to override.

```bash
ls /dev/cu.usbmodem*    # should show exactly one entry when Switch is connected
```

### 3.4 Iteration Loop — AI Workflow

```
1.  Edit C++ source (e.g. src/projects/uMenu/source/...)
2.  Build uMenu.nso:
        cd /Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu
        make -j$(nproc)
3.  Push to Switch:
        python3 tools/test-rig/mac-bridge.py push uMenu.nso
    (bridge writes the .nso to sdmc:/atmosphere/titles/<qlaunch_tid>/exefs/main.nso
     via the harness, then ACKs)
4.  Restart uMenu:
        python3 tools/test-rig/mac-bridge.py restart-umenu
5.  Wait 3 seconds for relaunch, then take a screenshot:
        python3 tools/test-rig/mac-bridge.py screenshot --out /tmp/iter.png
6.  Compare /tmp/iter.png against expected:
        python3 tools/test-rig/mac-bridge.py compare /tmp/iter.png expected/login.png
    OR pass to vision-LLM:
        python3 tools/test-rig/mac-bridge.py vision-check /tmp/iter.png \
            "Does the login screen show 'Q OS' in the top-left?"
7.  If pass: mark done. If fail: read log:
        python3 tools/test-rig/mac-bridge.py read-log --out /tmp/iter.log
    Then fix source, go to step 1.
```

---

## 4. Software Stack — Switch Side

### 4.1 qos-test-harness.nro — Serial Command Loop Extension

The existing harness at `/Users/nsa/QOS/tools/switch-nro-harness/` is at **v1.0.2**
(886 assertions across 37 categories). The serial command loop is a NEW capability that
goes in **v2.0.0** — a major version bump because it changes the harness from a
passive test runner into an active command dispatcher.

**SSOT version file:** `tools/switch-nro-harness/VERSION` does not exist yet; create
it with content `2.0.0` when the implementation begins.

**Key new functions in v2.0.0 main.rs:**

```rust
/// Entry point for the autonomous rig mode.
/// Opened by detecting sdmc:/switch/qos-rig-mode.flag at startup.
/// Runs the serial command loop instead of the normal assertion suite.
fn run_rig_mode(log: &mut impl Write) -> ! {
    // 1. Call usbCommsInitialize() via nx::usb (or raw syscall wrapper).
    // 2. Send 0xFF DONE (READY marker) so Mac knows the channel is up.
    // 3. Loop: read command byte, dispatch, send response.
    // 4. On any fatal error: write 0xFE ERROR, then call appletRequestExitToSelf.
}
```

The mode is activated by the **flag file** `sdmc:/switch/qos-rig-mode.flag`. When
that file is present at harness startup, `run_rig_mode` is called instead of the
normal assertion suite. This makes the change zero-impact on existing CI runs.

### 4.2 Rig Mode Detection — Startup Flow

```
main()
  └─ check sdmc:/switch/qos-rig-mode.flag
       ├─ exists → run_rig_mode()    ← NEW autonomous path
       └─ missing → run_test_suite() ← existing v1.0.2 path (unchanged)
```

### 4.3 Touch Injection

The Switch HID service does **not** provide a public API for injecting synthetic
touch events from a userland NRO without the `hid:tmp` system handle (privileged
sysmodule territory). The correct approach for v1 is:

1. **Button injection via `hiddbg`** — `hiddbgAttachHdlsWorkBuffer` +
   `hiddbgSetHdlsState` allows injecting virtual gamepad button presses from
   a userland process. This is what homebrew input-emulators use.
2. **Touch injection is out of scope for v1** — see §9 (Out of Scope). Use button
   injection only in v1 (A/B/X/Y/Plus/Minus/DPad).

For button injection, the harness calls:

```rust
// Pseudocode — actual nx crate bindings may differ; consult nx::hid module
hiddbg::attach_hdls_work_buffer();
let mut state = HiddbgHdlsState::default();
state.buttons = button_mask;  // e.g. KEY_A
hiddbg::set_hdls_state(&handle, &state);
// hold for one frame, then release
state.buttons = 0;
hiddbg::set_hdls_state(&handle, &state);
```

### 4.4 Screenshot Capture

libnx provides `viCaptureScreen` for RGBA framebuffer capture:

```c
// C pseudocode — translate to Rust nx bindings
ViDisplay display;
viOpenDefaultDisplay(&display);

NvMap nvmap;
// ... allocate framebuffer via nvmap, call viGetDisplayFramebufferHandle ...
// viCaptureScreen copies current fb to a caller-provided buffer.

// Then encode RGBA → PNG using a minimal pure-Rust PNG encoder
// (e.g. the `miniz_oxide` crate already used in aarch64-switch-rs examples,
//  or a hand-rolled DEFLATE-less 24-bit PNG header + raw scanlines).
```

For v1, raw RGBA pixels are acceptable if PNG encoding is not worth the binary size.
The Mac side can convert with Pillow:
```python
from PIL import Image
img = Image.frombytes("RGBA", (1280, 720), raw_rgba_bytes)
img.save("/tmp/test.png")
```

**Switch screen resolution:** 1280×720 in handheld, 1920×1080 docked. Design for
1280×720 (handheld = only mode for this rig).

### 4.5 NSO Push Target Path

uMenu (qlaunch) is launched by Atmosphère as the `qlaunch` title. Its exefs main is:

```
sdmc:/atmosphere/exefs_patches/...   ← patches, NOT the binary
sdmc:/atmosphere/titles/<TID>/exefs/main.nso  ← IPS replacement binary
```

The canonical qlaunch TID on Erista running Atmosphère is `0100000000001000`.

So `push_nso()` must write the received bytes to:

```
sdmc:/atmosphere/titles/0100000000001000/exefs/main.nso
```

The harness must create the directory if it does not exist.

---

## 5. Wire Protocol

All multi-byte integers are **little-endian**. The channel is 115200 baud, 8N1,
no hardware flow control (usbCommsInitialize defaults).

### 5.1 Command Bytes (Mac → Switch)

| Opcode | Name | Payload | Description |
|--------|------|---------|-------------|
| `0x01` | `PUSH_NSO` | `u32 len` + `len` bytes | Write bytes to `sdmc:/atmosphere/titles/0100000000001000/exefs/main.nso`. Switch ACKs with `0x80 PUSH_NSO_OK` or `0xFE ERROR`. |
| `0x02` | `RESTART_UMENU` | none | Calls `appletRequestExitToSelf` from the harness. Atmosphère relaunches qlaunch automatically. Switch sends `0xFF DONE` then the harness exits. |
| `0x03` | `TAP` | `u16 x` + `u16 y` | **v1.1 only** — out of scope for v1. |
| `0x04` | `PRESS_BUTTON` | `u8 btn` | Inject one button press+release via `hiddbg`. Button codes: `0x01`=A, `0x02`=B, `0x04`=X, `0x08`=Y, `0x10`=Plus, `0x20`=Minus, `0x40`=DUp, `0x80`=DDown. ACK: `0xFF DONE`. |
| `0x05` | `SCREENSHOT_REQ` | none | Capture framebuffer, encode, send back as `0x81 SCREENSHOT_DATA`. |
| `0x06` | `READ_LOG` | none | Read `sdmc:/qos-shell/logs/uMenu.0.log`, send back as `0x82 LOG_DATA`. |
| `0xEE` | `PING` | none | Echo `0xEF PONG`. Used for channel health-check. First milestone target. |

### 5.2 Response Bytes (Switch → Mac)

| Opcode | Name | Payload | Description |
|--------|------|---------|-------------|
| `0x80` | `PUSH_NSO_OK` | `u32 bytes_written` | NSO was written successfully. |
| `0x81` | `SCREENSHOT_DATA` | `u8 fmt` + `u32 len` + `len` bytes | `fmt=0x00` = raw RGBA 1280×720 (3686400 bytes). `fmt=0x01` = PNG. |
| `0x82` | `LOG_DATA` | `u32 len` + `len` bytes | Raw log file content. |
| `0xEF` | `PONG` | none | Response to `PING`. |
| `0xFE` | `ERROR` | `u8 code` + `u8 msg_len` + `msg_len` bytes | Error with short ASCII message. Codes: `0x01`=UMS_CONFLICT, `0x02`=FS_ERROR, `0x03`=USB_INIT_FAIL, `0x04`=SCREENSHOT_FAIL, `0x05`=UNKNOWN_CMD. |
| `0xFF` | `DONE` | none | Command completed successfully (for commands with no data response). |

### 5.3 Framing Contract

- Max NSO size: 8 MiB (`0x800000` bytes). Mac must not send larger.
- Max log read: 1 MiB. Switch truncates silently if ring exceeds this.
- Screenshot raw RGBA: exactly `1280 * 720 * 4 = 3686400` bytes.
- No checksum in v1 — USB bulk transfer provides its own error detection at the
  physical layer. If corruption is observed in practice, add CRC32 in v1.1.

---

## 6. Boot Sequence

The rig requires the Switch to be in a known state before the Mac can connect.
This boot sequence achieves that without any creator interaction after power-on.

### 6.1 Hekate Auto-Boot Configuration

The current `hekate_ipl.ini` has `autoboot=0` (manual selection). For the rig,
the creator must temporarily change this to auto-boot CFW (SYSMMC):

```ini
[config]
autoboot=1         ; ← change from 0
autoboot_list=0
bootwait=3         ; 3 s window to override manually

[CFW (SYSMMC)]
pkg3=atmosphere/package3
kip1patch=nosigchk
emummc_force_disable=1
icon=bootloader/res/sysnand.bmp
```

**After rig testing, restore `autoboot=0`** to prevent unintentional CFW auto-boot.

### 6.2 Atmosphère Boot Chain

```
Power on
  → Hekate (payload via RCM or modchip)
  → [3 s bootwait, skip if no input]
  → boot entry 1: CFW (SYSMMC)
  → Atmosphère loads, mounts sdmc
  → sys-patch TID 420000000000000B starts (already has boot2.flag)
  → qlaunch (uMenu) starts as the HOME menu applet
  → uMenu checks sdmc:/switch/qos-rig-mode.flag
       ← MISSING in normal mode: uMenu renders UI
       ← PRESENT for rig mode: uMenu launches qos-test-harness.nro as an album applet
  → qos-test-harness.nro runs run_rig_mode()
  → calls usbCommsInitialize()
  → sends 0xEF PONG (READY signal)
  → Mac bridge detects PONG and reports "Switch ready"
```

**Total time from power-on to "Switch ready":** approximately 25–35 seconds on Erista.

### 6.3 Activating Rig Mode

```bash
# Mount SD (UMS mode via Hekate UMS option) to create the flag,
# then eject and reboot into CFW.
touch "/Volumes/SWITCH SD/switch/qos-rig-mode.flag"
# Eject SD, reboot Switch.

# After rig session, remove the flag to restore normal uMenu:
rm "/Volumes/SWITCH SD/switch/qos-rig-mode.flag"
```

The flag file is read-only checked (never written) by the harness, so it survives
reboots. Remove it manually when rig testing is complete.

---

## 7. Failure Recovery

**Rule: every failure path must end with the Switch in a state where the creator
can navigate to Nyx without RCM.** (Per `feedback_safe_return_must_be_100_percent`.)

### 7.1 USB Cable Disconnect

- Switch side: `usbCommsWrite` returns a negative result code. The harness detects
  this, writes an error to the SD log, calls `usbCommsExit`, then falls through to
  the normal assertion suite (safe return to idle NRO).
- Mac side: `serial.Serial.write()` raises `SerialException`. `mac-bridge.py`
  catches this, prints `[ERR] USB disconnected`, exits with code 1.
- **Recovery:** reconnect cable, reboot Switch (no RCM needed — Hekate reloads
  normally from the SD).

### 7.2 sys-con / usbComms Hangs

If `usbCommsInitialize` never returns (kernel deadlock):
- The harness has a **30-second watchdog** via `svcSetHeapSize` + a timer thread.
  If the main thread does not post a heartbeat within 30 s, the watchdog calls
  `__nx_applet_exit` to force-exit.
- Atmosphère reboots qlaunch automatically after an NRO applet exits.
- **Recovery:** Switch returns to uMenu login screen. No RCM needed.

### 7.3 uMenu Hangs (infinite loop / crash)

- Atmosphère's `pm` (process manager) detects applet crash and sends the user to
  the error screen. The error screen has a "Return to HOME" button which relaunches
  qlaunch from the last-known-good binary on SD.
- **Recovery:** press "Return to HOME". If qlaunch binary is corrupt from a bad push,
  copy the last-known-good `.nso` from `staging/` via UMS then reboot.

### 7.4 NSO Push of a Crashing Binary

The push writes `main.nso` to SD before the restart command is sent. If the new
binary crashes, the recovery path is:

```bash
# Mount SD via UMS
cp /Users/nsa/QOS/tools/qos-ulaunch-fork/staging/last-known-good.nso \
   "/Volumes/SWITCH SD/atmosphere/titles/0100000000001000/exefs/main.nso"
# Eject, reboot.
```

**The harness should maintain a "last-known-good" shadow copy** at
`sdmc:/atmosphere/titles/0100000000001000/exefs/main.nso.bak` and restore it
automatically if the pushed binary crashes within 5 seconds of relaunch.

### 7.5 Hekate Nyx as Final Fallback

If any of the above recovery steps fail, hold `Vol+` during power-on to enter
Hekate Nyx directly. From Nyx: Tools → USB Tools → SD card (UMS mode) → repair
the SD filesystem or restore files.

**This path requires no RCM.** Hekate Nyx is always reachable via `Vol+`.

---

## 8. Iteration Loop — AI Reference

```
Step 1: EDIT
    Modify C++ source under:
      qos-ulaunch-fork/src/projects/uMenu/source/ul/menu/

Step 2: BUILD
    cd /Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu
    make -j$(sysctl -n hw.ncpu) 2>&1 | tee /tmp/build.log
    # Artifact: build/uMenu.nso  (or check build output dir)
    # If build fails: read /tmp/build.log, fix, repeat Step 2.

Step 3: PUSH
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        push build/uMenu.nso
    # Expected output: "[OK] pushed 1234567 bytes"
    # On ERROR 0x01 (UMS_CONFLICT): the SD is still in UMS mode.
    #   Run: diskutil eject "SWITCH SD"
    #   Then reboot Switch into CFW (not UMS).
    #   Then retry push.

Step 4: RESTART
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        restart-umenu
    # Expected output: "[OK] restart sent"
    # Then wait 5 seconds for uMenu to reload.

Step 5: SCREENSHOT
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        screenshot --out /tmp/iter-$(date +%s).png
    # Expected output: "[OK] screenshot saved to /tmp/iter-<ts>.png"

Step 6: VERIFY
    # Option A — pixel diff against a known-good reference:
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        compare /tmp/iter-<ts>.png expected/login-baseline.png \
        --threshold 0.02
    # "PASS: 98.7% match (threshold 2%)"  OR  "FAIL: 45.3% match"

    # Option B — vision-LLM check (requires ANTHROPIC_API_KEY):
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        vision-check /tmp/iter-<ts>.png \
        "Does this Switch screenshot show the Q OS login screen with the circular avatar area and three dev-tool buttons in the bottom-left?"

Step 7: ON FAILURE — read log
    python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py \
        read-log --out /tmp/iter.log
    # Then read /tmp/iter.log for the crash or assertion failure.
    # Fix source, go back to Step 1.

Step 8: ON PASS — commit
    # Mark the feature done in the iteration log.
    # git add + ready for creator commit.
```

---

## 9. Out of Scope for v1

These capabilities are intentionally excluded from v1 to keep the scope bounded.
They come in v1.1 after the PING/PONG milestone proves the channel works.

| Feature | Reason deferred |
|---------|----------------|
| Touch injection (`TAP` command) | Requires `hid:tmp` sysmodule handle — not available to userland NROs. Needs a custom sysmodule or hiddbg touch surface (undocumented). |
| Video stream | USB CDC-ACM bandwidth (~12 Mbps theoretical, ~3 Mbps practical) is too slow for continuous 1280×720 video. Would require USB 3.0 bulk or Wi-Fi. |
| Networking / nxlink integration | Orthogonal channel. Use USB-C exclusively for the autonomous rig. |
| Gamepad axis input | Analog stick emulation via hiddbg requires more nuanced state machine than button press/release. |
| PNG encoding on Switch | Adds ~150 KB to NRO size. Raw RGBA is sufficient for v1; Pillow on Mac converts. |
| Multi-NRO staging | Pushing multiple NROs per iteration. v1 supports uMenu.nso only. |

---

## 10. Critical Finding — sys-patch vs. sys-con

**The SD card has `sys-patch` (TID `420000000000000B`), NOT `sys-con`.**

The task brief mentioned "sys-con (TID `420000000000000B`) which exposes a USB-serial
bridge." This is incorrect. TID `420000000000000B` is **sys-patch** (a signature-patch
sysmodule). sys-con is a controller-input sysmodule with a completely different TID
(`430000000000000B`) and does NOT provide a USB-serial bridge.

The USB-serial capability we rely on is not sys-con at all — it comes from **libnx's
`usbCommsInitialize()`** which the NRO calls directly. This is actually better news:
no additional sysmodule is required. The harness NRO opens CDC-ACM itself.

**The design above is correct and unaffected by this distinction.** The harness
handles USB-serial without any custom sysmodule. sys-patch on the SD is irrelevant
to the rig.

---

## 11. Required Pre-Flight Before First Run

1. **Confirm data cable:** plug Switch into Mac via USB-C, run
   `system_profiler SPUSBDataType | grep -A5 "Nintendo"`. If Nintendo appears in
   the USB tree (even as a charge device), the cable carries data lines.

2. **Confirm no UMS conflict:** ensure Switch is not in UMS mode. If SD is mounted
   at `/Volumes/SWITCH SD`, eject it (`diskutil eject "SWITCH SD"`) before booting
   into CFW for the rig session.

3. **Build the harness v2.0.0:** the serial command loop does not yet exist in the
   current `v1.0.2` binary. This is the primary implementation task before the rig
   can be used end-to-end. See `docs/TEST-RIG-FIRST-MILESTONE.md` for the first step.

4. **Create rig-mode flag on SD:** this requires one UMS push (SD mounted) to plant
   `switch/qos-rig-mode.flag`. After that, all subsequent iterations are over USB.

5. **Install pyserial on Mac:** `pip3 install pyserial` — already done 2026-04-25.

---

## 12. File Locations Summary

| File | Purpose |
|------|---------|
| `tools/test-rig/mac-bridge.py` | Mac-side bridge — CLI + Python API |
| `tools/test-rig/README.md` | Quickstart for the AI |
| `docs/AUTONOMOUS-TEST-RIG-DESIGN.md` | This document |
| `docs/TEST-RIG-FIRST-MILESTONE.md` | PING/PONG milestone — first step |
| `tools/switch-nro-harness/src/main.rs` | Switch-side harness source (extend to v2.0.0) |
| `tools/switch-nro-harness/Cargo.toml` | Bump to `version = "2.0.0"` when v2 lands |
| `sdmc:/switch/qos-rig-mode.flag` | Flag to activate rig mode on Switch |
| `sdmc:/atmosphere/titles/0100000000001000/exefs/main.nso` | Push target for uMenu |
