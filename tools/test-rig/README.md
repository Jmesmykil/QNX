# Autonomous USB-Serial Test Rig — Quickstart

**Full design:** `docs/AUTONOMOUS-TEST-RIG-DESIGN.md`
**First milestone:** `docs/TEST-RIG-FIRST-MILESTONE.md`

---

## What this does

Eliminates the UMS push/eject/boot/visual-verify loop.
Once the Switch-side harness v2.0.0 is built, the AI can:

1. Build a new `uMenu.nso`
2. Push it to the Switch over USB-C serial
3. Restart uMenu
4. Capture a screenshot
5. Compare against expected pixels or run a vision-LLM check
6. Read the Switch log on failure

All without the creator touching the Switch.

---

## Pre-flight checklist

- [ ] pyserial installed: `pip3 install pyserial`
- [ ] Data-capable USB-C cable (not power-only): verify by checking
      `system_profiler SPUSBDataType | grep -A5 Nintendo` shows the Switch
- [ ] Switch NOT in UMS mode (eject `SWITCH SD` if mounted)
- [ ] Switch booted into CFW (Atmosphère), NOT Hekate Nyx
- [ ] Rig-mode flag planted on SD: `sdmc:/switch/qos-rig-mode.flag`
      (requires one UMS push to create — see below)
- [ ] Harness v2.0.0 built and copied to SD at
      `sdmc:/switch/qos-test-harness.nro`
      (v2.0.0 is not yet built — this is the current implementation task)

---

## Planting the rig-mode flag (one-time, UMS push)

```bash
# 1. Enter Hekate UMS mode (Tools → USB Tools → SD Card)
# 2. SD mounts as /Volumes/SWITCH SD on Mac
touch "/Volumes/SWITCH SD/switch/qos-rig-mode.flag"
diskutil eject "SWITCH SD"
# 3. Reboot Switch into CFW (not UMS)
```

---

## CLI usage (once rig is active)

```bash
cd /Users/nsa/QOS/tools/qos-ulaunch-fork

# Health check — first milestone
python3 tools/test-rig/mac-bridge.py ping
# Expected: [OK] PONG received  RTT=X.X ms

# Push a new .nso
python3 tools/test-rig/mac-bridge.py push build/uMenu.nso
# Expected: [OK] pushed NNNN bytes

# Restart uMenu after push
python3 tools/test-rig/mac-bridge.py restart-umenu
# Wait 5 seconds, then:

# Screenshot
python3 tools/test-rig/mac-bridge.py screenshot --out /tmp/test.png

# Compare against reference
python3 tools/test-rig/mac-bridge.py compare /tmp/test.png expected/login.png

# Read log on failure
python3 tools/test-rig/mac-bridge.py read-log --out /tmp/umenu.log
```

---

## Auto-detect vs manual port

The bridge globs `/dev/cu.usbmodem*` and picks the first match.
If you have multiple USB-serial devices, specify explicitly:

```bash
python3 tools/test-rig/mac-bridge.py --port /dev/cu.usbmodem14201 ping
```

---

## Implementation status (2026-04-25)

| Component | Status |
|-----------|--------|
| `mac-bridge.py` — `push_nso()` | Fully implemented |
| `mac-bridge.py` — `ping()` | Fully implemented |
| `mac-bridge.py` — `restart_umenu()` | Implemented (awaits Switch-side) |
| `mac-bridge.py` — `press_button()` | Implemented (awaits Switch-side) |
| `mac-bridge.py` — `screenshot()` | Implemented (awaits Switch-side) |
| `mac-bridge.py` — `read_log()` | Implemented (awaits Switch-side) |
| `mac-bridge.py` — `compare()` | Fully implemented (requires Pillow) |
| Switch-side harness v2.0.0 | **Not yet built** — next implementation task |

The Mac side is complete. The blocker is the Switch-side harness v2.0.0
which needs to add `run_rig_mode()` to
`/Users/nsa/QOS/tools/switch-nro-harness/src/main.rs`.
See `docs/TEST-RIG-FIRST-MILESTONE.md` for the PING/PONG first step.
