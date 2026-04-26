# 48 — Dev Tools Test Harness (NXLink + USB Serial + Log Flush)

**Author:** Captured 2026-04-24 evening
**Why:** Creator directive — "make sure we successfully honestly test the hbmenu pieces we are using for serial USB and nxlink." Agents have been claiming the dev-tool wiring works because the code compiles. The compile is necessary but not sufficient. This doc is the honest test procedure.

---

## What's wired today

`src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp` provides three toggles invoked from the login screen (`ui_StartupMenuLayout`):

| Toggle | API the toggle calls | Underlying libnx call |
|---|---|---|
| **NXLink** | `ul::menu::qdesktop::dev::TryEnableNxlink` | `socketInitializeDefault` + `nxlinkConnectToHost(true, true)` |
| **USB serial** | `ul::menu::qdesktop::dev::TryEnableUsbSerial` | `usbCommsInitialize` (CDC-ACM) |
| **Flush logs** | `ul::menu::qdesktop::dev::FlushAllChannels` | `ul::tel::Flush()` + `fdatasync` SD ring |

The login layout has three small buttons on the bottom-left that call these. State labels next to each button update each frame via `RefreshDevToolLabels()` to show "active" / "inactive".

---

## NXLink — honest test procedure

### What it actually does on the wire

`nxlinkConnectToHost(true, true)` does:
1. `socketInitializeDefault` — opens the BSD socket service.
2. UDP broadcast on port 28771 with the magic packet `nxboot`.
3. Listens for a response; if a host on the LAN responds with the listener's IP+port, opens a TCP connection.
4. Calls `dup2` so `stdout` and `stderr` file descriptors are the TCP socket — anything `printf` / `dprintf` writes goes to the listener.

If no response within ~2 s, returns negative — `TryEnableNxlink` reports false.

### Pre-flight checks (Mac side)

```bash
# 1. devkitPro tools must be installed and in PATH.
which nxlink && nxlink --help | head -5
# Expected: prints "/opt/devkitpro/tools/bin/nxlink" and a usage banner.

# 2. Switch and Mac must be on the same Wi-Fi network.
#    Check by pinging an arbitrary device — any response is fine.
ping -c 1 192.168.1.1   # adjust to your subnet

# 3. macOS firewall — System Settings → Network → Firewall — must allow
#    incoming UDP on port 28771. Easiest: temporarily disable firewall
#    for the test, then re-enable.
```

### Test run

**Mac terminal (host listener):**

```bash
# Start the listener BEFORE flipping the toggle on the Switch.
# -s = "server" mode: wait for the Switch broadcast, accept, stream stdout.
nxlink -s
# Expected output: "Waiting for switch ..."
```

**On the Switch (Q OS login screen):**

1. Boot to login screen.
2. Press the dev-tools toggle for "Nxlink" (bottom-left, second row).
3. Watch the label next to the button.
   - **Green / "active"** = `TryEnableNxlink` returned true. Socket is dup'd to nxlink.
   - **Red / "inactive"** = no host found, or socket error. Look for the `UL_LOG_WARN("qdesktop: dev::TryEnableNxlink ...` line in `/qos-shell/logs/uMenu.0.log` after next session for the failure code.

**Mac terminal — expected within 2 s:**

```
[nxlink] Connection from 192.168.1.42 (Switch IP)
[nxlink] Streaming stdout from Switch...
```

**Then on the Switch:** press the "Flush logs" toggle. Any `printf` issued by uMenu should appear in the Mac terminal. If the only output is the boot-sequence marker line and nothing else — telemetry's stdout redirection isn't reaching nxlink. That is itself a bug to file (telemetry writes to `/qos-shell/logs/uMenu.0.log` not stdout — nxlink only catches `printf`/`puts`/`dprintf` to fd 1 / 2).

### What "honest pass" means

| Observation | Verdict |
|---|---|
| Mac listener prints "Connection from" + Switch IP | NXLink session **established** ✅ |
| Above + at least one line of Switch-originated output appears | NXLink streaming **functional** ✅ |
| Listener prints "Connection from" but no further output | Socket up, no producers writing to stdout — telemetry path not wired to printf, separate fix |
| Listener never prints "Connection from" | Toggle is failing — read `qd_DevTools.cpp:TryEnableNxlink` log warning for socket init or connect failure |

### Common gotchas

- **macOS firewall blocking UDP 28771** — easiest to disable firewall temporarily for the test.
- **Wi-Fi router with AP isolation enabled** — Switch and Mac can't see each other on the LAN. Check router admin page; turn off AP isolation.
- **Switch in airplane / handheld undocked / sleeping** — `socketInitializeDefault` may fail. Power the Switch on, ensure Wi-Fi connected (check Switch Settings → Internet).

---

## USB serial — honest test procedure

### What it actually does on the wire

`usbCommsInitialize` configures the Switch's USB-C port as a CDC-ACM virtual serial device. The Mac sees it as `/dev/cu.usbmodem*` (or `/dev/tty.usbmodem*`). `usbCommsWrite(buf, len)` writes raw bytes to the endpoint.

`qd_DevTools.cpp::TryEnableUsbSerial` calls `usbCommsInitialize` and (on success) snapshots the current `/qos-shell/logs/uMenu.0.log` content and streams it over USB in 4 KiB chunks.

### Known blocker — UMS conflict

The Switch's USB-C port can be in **only one of three modes** at a time:
1. UMS (Mass Storage) — what we use for SD card transfer.
2. CDC-ACM (this serial mode).
3. Charging only (default).

If UMS is active (because the user used the dock SD-eject helper or hekate UMS option), `usbCommsInitialize` returns `0x272E02` ("usbds already in use") and `TryEnableUsbSerial` returns false.

**Procedure: make sure UMS is OFF before testing USB serial.** Reboot the Switch from Hekate without entering UMS — the USB-C port is then free.

### Pre-flight checks (Mac side)

```bash
# 1. List currently attached USB serial devices.
ls /dev/cu.usbmodem* 2>/dev/null
# Expected before toggle: empty (no USB serial active yet).

# 2. screen, picocom, or minicom must be installed.
which screen picocom 2>/dev/null
# Expected: at least one of them resolves. screen ships with macOS.

# 3. Connect Switch to Mac via the same USB-C cable used for charging.
#    Standard charging cable supports data when not in UMS mode.
```

### Test run

**On the Switch (Q OS login screen):**

1. Boot to login screen (NOT in UMS mode).
2. Press the "USB Serial" dev-tools toggle.
3. Watch the label.
   - **active** = `usbCommsInitialize` succeeded.
   - **inactive** + log line `qdesktop: dev::TryEnableUsbSerial returned 0x272E02` = UMS conflict.
   - **inactive** + log line with other RC = library/driver issue, capture RC for diagnosis.

**Mac terminal — within ~2 s of toggle:**

```bash
# The Switch should now appear as a USB serial device.
ls /dev/cu.usbmodem*
# Expected: one new /dev/cu.usbmodem<N> entry, e.g. /dev/cu.usbmodem14201

# Open the serial port. Replace the path with the one ls showed.
screen /dev/cu.usbmodem14201 115200
# Or: picocom -b 115200 /dev/cu.usbmodem14201
```

**Expected output in `screen`:**

```
[USB-SERIAL] Q OS uMenu telemetry snapshot
[USB-SERIAL] /qos-shell/logs/uMenu.0.log — <N> bytes
<contents of the current log ring file>
[USB-SERIAL] snapshot end
```

`screen` exit: `Ctrl-A` then `K` then `Y`.

### What "honest pass" means

| Observation | Verdict |
|---|---|
| `/dev/cu.usbmodem*` appears within 2 s of toggle | USB CDC-ACM **enumerates** ✅ |
| Above + screen/picocom shows the snapshot bytes | USB serial **functional** ✅ |
| `/dev/cu.usbmodem*` appears but screen shows nothing | Enumeration up, snapshot stream broken — read `qd_DevTools.cpp:TryEnableUsbSerial` for the dump path |
| `/dev/cu.usbmodem*` never appears | `usbCommsInitialize` failed — check log for RC and address |

### Common gotchas

- **UMS still active** (most common cause). Reboot from Hekate without touching the UMS option.
- **Cable is power-only** (some USB-C cables omit data lines). Try a different cable known to support data.
- **macOS Sequoia + restrictive USB security** — System Settings → Privacy & Security → may need to allow the device. Approve when prompted.

---

## Flush Logs — honest test procedure

### What it actually does

`FlushAllChannels()` calls `ul::tel::Flush()` (drains async SPSC ring + `fsync` ring file) and writes a timestamped marker line to any active channels (nxlink stdout, USB serial endpoint, ring file).

This is the only safe way to confirm Switch-side telemetry has reached the SD card before unplugging or ejecting.

### Test run

1. Press "Flush logs" toggle on the login screen.
2. After a session of activity (login, navigate desktop, open vault, etc.), reboot to UMS and check the SD card:

```bash
# Mount SD on Mac, then:
ls -la "/Volumes/SWITCH SD/qos-shell/logs/"
# Expected: uMenu.0.log present, modified time recent, size > 1 KB
#           (more than just BOOT entries).

# Read the tail to confirm content beyond BOOT lines:
tail -30 "/Volumes/SWITCH SD/qos-shell/logs/uMenu.0.log"
```

### What "honest pass" means

| Observation | Verdict |
|---|---|
| Log file size > 5 KB and tail shows INFO/WARN lines | Flush **functional** ✅ |
| Log file size ≤ 1 KB, only BOOT lines | **REGRESSION** — atexit + periodic flush isn't actually persisting async messages. As of 2026-04-24 SP4.1, this is the observed state on hardware despite the source path looking correct. **Open bug — diagnose before claiming flush works.** |

### Known bug as of SP4.1 (2026-04-24)

After 18 boots with the regression-fix + SP4.1 telemetry wiring, the only entries in `/qos-shell/logs/uMenu.0.log` are 18 BOOT lines. Zero INFO, WARN, or CRIT entries. The atexit + 180-frame periodic flush is in source (`main.cpp:73`, `ui_MainMenuLayout.cpp:1249`) but isn't producing output on hardware. The async SPSC ring is being created at `tel::Init`, the drain thread starts, `EmitSync` for WARN/CRIT exists at `util_Telemetry.cpp:351` — but nothing past BOOT reaches disk.

**Possible causes (untriaged):**
1. The drain thread isn't actually running (`threadCreate` returns success but the thread function never executes).
2. The mutex around `g_ring` is contended — drain thread can't acquire while main thread holds it.
3. RAII destructors during applet shutdown skip atexit (kernel-kill on Home press).
4. UL_LOG_INFO is being optimized out at -O2 because the logger function appears to have no side effects from the compiler's view.

**Diagnostic next-action:** read `util_Telemetry.cpp::DrainThreadFn` (around line 175) and add an `EmitSync` synchronous WARN at the top of the drain function so we can see whether the thread is even running.

---

## Pass / fail decision matrix

For the creator's "successfully honestly test" gate, all three must pass:

```
[ ] NXLink: Mac listener sees "Connection from" + at least one byte of stdout.
[ ] USB serial: /dev/cu.usbmodem* appears AND screen/picocom shows the snapshot.
[ ] Flush logs: SD ring file > 1 KB AND tail shows non-BOOT entries.
```

If any one fails, the dev-tool toggle is NOT verified — file the failure mode and fix before claiming the dev-tools subsystem is shipping.

---

## Cross-references

- `docs/45_HBMenu_Replacement_Design.md` §5 — vault dev-tool windows (Stage 5)
- `docs/47_Integratable_Tasks_Catalog.md` (in flight) — what other tools we can absorb
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp` — implementation
- libnx headers: `<switch/services/usb_comms.h>`, `<switch/runtime/nxlink.h>`, `<switch/services/bsd.h>`

