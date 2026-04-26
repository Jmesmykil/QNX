# Test Rig — First Milestone: USB Serial Echo (PING / PONG)

**Author:** K+5 session, 2026-04-25T00:00:00Z
**Goal:** Prove the USB-C serial channel works end-to-end before building
any complex rig commands.

Mac sends `0xEE PING`. Switch responds `0xEF PONG`. Done.

---

## Why this milestone first

Every subsequent rig command (push NSO, restart uMenu, screenshot) relies on
the CDC-ACM serial channel being reliable. PING/PONG costs ~20 lines of new
Rust and proves three things in one shot:

1. USB CDC-ACM enumerates on Mac (`/dev/cu.usbmodem*` appears).
2. The harness serial read loop executes on the Switch.
3. The harness serial write loop executes and the bytes arrive on Mac.

If PING/PONG does not work, nothing else will. Fix it first.

---

## Step 1: Enumerate USB serial on Mac — no Switch code needed yet

Before writing a single line of Switch code, confirm the CDC-ACM driver works.

**What to do:**

Boot Switch into Atmosphère CFW. In the uMenu login screen, press the
"USB serial" dev-tools toggle (bottom-left, third button). This calls
`usbCommsInitialize()` in `qd_DevTools.cpp::TryEnableUsbSerial` — the same
CDC-ACM init the harness will call.

Then on Mac:

```bash
ls /dev/cu.usbmodem*
```

**Expected output:**

```
/dev/cu.usbmodem14201
```

(The exact number varies per boot.)

**If no device appears:**

- Confirm the cable carries data. Test with a known-data cable
  (the one used for Switch firmware updates works).
- Confirm UMS is not active. If `/Volumes/SWITCH SD` is mounted, eject it
  first: `diskutil eject "SWITCH SD"`, then reboot Switch.
- Confirm the "USB serial" toggle label reads "active" not "inactive". If
  it reads "inactive", check the uMenu.0.log (UMS conflict RC `0x272E02` is
  the most common cause).

**Estimated time: 10–15 minutes.**
**Hard dependency on hardware + cable — cannot be simulated.**

---

## Step 2: Switch-side harness echo loop — ~1 hour of Rust

Add a new function `run_rig_mode` to
`/Users/nsa/QOS/tools/switch-nro-harness/src/main.rs` that:

1. Calls `usbCommsInitialize()` (via nx crate or raw ffi).
2. Reads one byte at a time in a loop.
3. If the byte is `0xEE` (PING), writes `0xEF` (PONG) back.
4. If the byte is anything else, writes `0xFE 0x05 0x0B UNKNOWN_CMD` back.
5. Runs until `appletMainLoop()` returns false.

The mode is gated on `sdmc:/switch/qos-rig-mode.flag` at startup:

```rust
// In main(), before run_test_suite():
let rig_flag = "sdmc:/switch/qos-rig-mode.flag";
if fs::get_entry_type(rig_flag).is_ok() {
    run_rig_mode(&mut log);
    // run_rig_mode is diverging (!) — returns only on error or app exit
}
// else: fall through to existing assertion suite
run_test_suite(&mut log);
```

**The nx crate (`nx = { git = ..., tag = "0.5.0" }`) is already in
`Cargo.toml`.** It provides `nx::usb` or raw svc wrappers for libnx
`usbCommsInitialize` / `usbCommsRead` / `usbCommsWrite`. Check the nx
crate source for the exact API surface before writing the call sites —
do not guess the function signatures.

**Cargo.toml version bump:** change `version = "1.0.2"` to `version = "2.0.0"`.
Create `tools/switch-nro-harness/VERSION` with content `2.0.0`.

**Build command:**

```bash
cd /Users/nsa/QOS/tools/switch-nro-harness
cargo nx build --release
# Produces: target/aarch64-nintendo-switch-freestanding/release/qos-test-harness.nro
```

**Estimated time: 1–2 hours** (mostly reading nx crate USB API + implementing
the 30-line loop).

---

## Step 3: Mac-side PING verification — ~5 minutes

With the new harness NRO on the SD and rig-mode flag planted, run:

```bash
python3 /Users/nsa/QOS/tools/qos-ulaunch-fork/tools/test-rig/mac-bridge.py ping
```

**Expected output:**

```
[INFO] Opening /dev/cu.usbmodem14201 at 115200 baud
[OK] PONG received  RTT=3.2 ms
```

**If `[ERR] No /dev/cu.usbmodem* device found`:**
The harness either did not start in rig mode (flag missing or wrong path)
or `usbCommsInitialize` failed. Check:

```bash
# Mount SD via UMS and read the harness log
cat "/Volumes/SWITCH SD/switch/qos-test-harness-rig.log"
```

**If `[ERR] Short read: expected 1 bytes, got 0`:**
Timeout — the harness is running but not responding to `0xEE`. Confirm the
opcode dispatch in `run_rig_mode` handles byte `0xEE` specifically.

**Estimated time: 5 minutes** (just running the command).

---

## Pass Criteria

| Check | Expected |
|-------|----------|
| `/dev/cu.usbmodem*` appears after USB toggle / harness starts | Yes |
| `mac-bridge.py ping` exits 0 | Yes |
| RTT < 500 ms | Yes (physical USB latency is typically 2–10 ms; 500 ms allows for harness startup) |

All three pass = **PING/PONG milestone complete.**

After this milestone, implement `PUSH_NSO` on the Switch side next —
the Mac side is already done.

---

## Total Estimated Time to First PING/PONG

| Step | Time |
|------|------|
| Step 1 — Enumerate USB serial (hardware) | 10–15 min |
| Step 2 — Write harness echo loop (Rust) | 1–2 hours |
| Step 3 — Run ping command (verification) | 5 min |
| **Total** | **~2 hours** |

The 2-hour estimate assumes nx crate USB bindings are straightforward. If
`usbCommsInitialize` is not exposed by the nx crate and requires raw libnx
FFI, add 30–60 minutes for the extern block.

---

## Cross-References

- `docs/AUTONOMOUS-TEST-RIG-DESIGN.md` — full architecture
- `tools/test-rig/mac-bridge.py` — Mac bridge (ping() fully implemented)
- `tools/switch-nro-harness/src/main.rs` — Switch-side to extend
- `tools/switch-nro-harness/Cargo.toml` — bump to 2.0.0
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp` — reference
  for usbCommsInitialize usage pattern
