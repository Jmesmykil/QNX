# Hekate Reboot Regression — Fix Options 2026-05-06

**Target:** qos-ulaunch-fork, creator-owned  
**Analysis date:** 2026-05-06  
**Source of truth:** static analysis only — no SD reads required at decision time

---

## Context Summary

`RebootToHekate()` in `src/projects/uMenu/source/ul/menu/qdesktop/qd_Power.cpp` is
unmodified. The file has six fallback-warning paths; the regression causes one of them
to fire. The payload (`sdmc:/atmosphere/reboot_payload.bin`, 134 608 bytes) is present
on the SD card. The deployed `exefs.nsp` (md5 `aa267e1a`, 590 398 bytes, built
2026-05-06 05:43) was built from the dirty working tree which sets `uSystem.json`
`system_resource_size = 0x0` and `main_thread_stack_size = 0x80000`.

**Why this breaks Hekate reboot:** `bpcamsInitialize()` calls
`svcConnectToNamedPort("bpc:ams")`. That SVC has zero dependency on
`system_resource_size`. However, uSystem with `system_resource_size = 0x0` runs out
of heap during its own `__init__` / `appletInitialize` chain (the v2.x
`ApplicationControlCache` BSS added ~11 MB of static objects). When uSystem hits
`am 2001-0132 svc::LimitReached`, Atmosphère recovers by calling
`appletRequestToReboot()` — a plain HOS reboot — before uMenu is fully up. By the
time the user triggers action 6, either (a) bpc:ams is already torn down so
`bpcamsInitialize()` returns a non-success rc and hits the last fallback, or (b)
`IsRebootToHekateSupported()`'s static probe ran during an unstable startup window
and cached `false` forever, causing the button to silently fire
`power::RebootToHekate()` via the unchecked `FireAction(6)` dispatch — which calls
`bpcInitialize()` + `bpcRebootSystem()` without staging any payload.

The diagnostic log will show exactly which fallback fired. Match the observed line to
the decision matrix below.

---

## Three Ranked Fix Options

### Option A — SD-copy no-build rollback (fastest, zero risk)

**Name:** Copy known-good `exefs.nsp` backup over active binary on the mounted SD.

**Mechanism:** The file
`sdmc:/atmosphere/contents/0100000000001000/exefs.nsp.bak-pre-v2.3.6-20260505-100427`
(md5 `5b22a1cd`, 568 284 bytes) is the last working uSystem binary. It was built
from commit `9246dc1b` (SP4.15.1 hotfix) with `system_resource_size = 0xC00000` and
`stack = 0x100000`. Copying it over `exefs.nsp` restores the known-good state without
a rebuild. The v2.3.5-rollback-keep copy (same md5 `5b22a1cd`) is a second verified
copy of the same binary.

**Pre-built patch — Edit tool call:**

Not a source edit. The action is a filesystem copy when the SD is mounted:

```
src:  /Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp.bak-pre-v2.3.6-20260505-100427
dst:  /Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp
```

No rebuild. No source change. Reboot console after copy.

**Choose when:** Any of the six fallback warnings fired. This is the unconditional
safe path.

**Expected side effects:** The `RebootToStockQlaunch` SMI handler added in this
session's `uSystem/source/main.cpp` will NOT be present (it's post-v2.3.5). The
Settings panel "Boot to Nintendo Home Menu" button will be missing or inert. Accept
this temporarily; add the handler back in the next clean build cycle.

**Rebuild scope:** None.

---

### Option B — Revert uSystem.json to SP4.15.1 values and rebuild

**Name:** Restore `system_resource_size = 0xC00000` and `stack = 0x100000`, rebuild
uSystem only.

**Mechanism:** The current dirty-tree values (`0x0` / `0x80000`) match the SP4.13
baseline, which worked only because that binary was much smaller. v2.x BSS growth
means the SP4.13 values cause `svc::LimitReached` on FW 20. Restoring the SP4.15.1
values gives uSystem a working heap budget. The full analysis is in
`AM-NSS-FW20-IPC-CONFIRM.md` finding 7 and `SESSION-REGRESSION-AUDIT-20260506.md` R1.

**Pre-built patch:**

File: `src/projects/uSystem/uSystem.json`

```
old_string:
    "main_thread_stack_size": "0x0080000",
    "system_resource_size": "0x0",

new_string:
    "main_thread_stack_size": "0x0100000",
    "system_resource_size": "0xC00000",
```

Then rebuild uSystem and deploy `exefs.nsp` to
`sdmc:/atmosphere/contents/0100000000001000/exefs.nsp`.

**Choose when:** You see `bpcamsInitialize failed rc=0x...` or
`bpcamsSetRebootPayload failed rc=0x...` in the log (the last two fallback warnings).
Those indicate bpc:ams is up but the IPC call itself fails — consistent with heap
exhaustion inside uSystem corrupting the IPC session before the call completes.

**Expected side effects:** The `RebootToStockQlaunch` handler lands alongside this
change (it is in the same dirty tree). The Settings "Boot to Nintendo Home Menu"
button will exist and function. `am 2001-0132` panic is resolved. Login-screen
reactivity (R2) is also restored.

**Rebuild scope:** uSystem only (`make -j$(nproc)` in `src/projects/uSystem`). uMenu
does not need rebuilding; its `uMenu.json` has `system_resource_size = 0x0` and that
is correct and unchanged.

---

### Option C — Increase uMenu system_resource_size (targeted bpcams-only fix)

**Name:** Add `system_resource_size` to uMenu.json so the library-applet process has
heap headroom when uSystem's heap is starved.

**Mechanism:** This targets the scenario where uSystem itself is otherwise healthy
(its heap recovers before the user triggers action 6) but the IPC transfer-memory
path for `bpcamsSetRebootPayload` fails due to the library-applet's own transfer
memory being constrained. The `SfBufferAttr_HipcMapAlias` attribute on the
`bpcamsSetRebootPayload` dispatch means the kernel maps the payload buffer via
transfer memory — which IS gated on the process having alias-region space, not
`system_resource_size`. However, `malloc(payload_size)` in `RebootToHekate()` (~130
KB for the 134 608-byte payload) does come from the applet-side heap. If uMenu's heap
is under pressure, this malloc fails.

uMenu.json already has `system_resource_size = "0x0"` (unchanged). uMenu runs in pool
partition 1 (Applet pool). Its heap is from `svcSetHeapSize`, not the system-resource
pool. So adjusting `system_resource_size` in uMenu.json would not directly fix a
malloc failure — malloc comes from the regular heap, not the system-resource region.
This option is therefore a LOWER-CONFIDENCE fix and is listed third.

**Pre-built patch (if log shows `malloc failed`):**

File: `src/projects/uMenu/uMenu.json`

```
old_string:
    "system_resource_size": "0x0",

new_string:
    "system_resource_size": "0x200000",
```

This reserves 2 MB of extra kernel pages for uMenu's system-resource region, reducing
the risk of the applet address space being fragmented at malloc time.

**Choose when:** Log shows exactly `malloc(N) failed; falling back` where N is
~130 000–135 000. In that case Option A or B also fix it (they restore uSystem health,
which removes pressure on the library-applet heap budget). Option C alone is not
sufficient if uSystem is still crashing.

**Expected side effects:** Slightly longer uMenu startup time (kernel pre-reserves
2 MB of applet pool at launch). No behavioral change otherwise.

**Rebuild scope:** uMenu only.

---

## Decision Matrix

| Observed log line | Most probable root cause | Apply |
|---|---|---|
| `cannot open sdmc:/atmosphere/reboot_payload.bin; falling back` | Payload missing from SD or sdmc not mounted | Check SD: `ls /Volumes/SWITCH\ SD/atmosphere/reboot_payload.bin` (file is present — 134 608 bytes — so this warning would mean SD not mounted at reboot time; apply Option A then verify SD mount) |
| `fseek/ftell failed; falling back` | SD card I/O error or filesystem corruption | Apply Option A. Check SD health separately |
| `payload file empty or ftell error; falling back` | Corrupted `reboot_payload.bin` | Replace `reboot_payload.bin` from a known-good copy, then apply Option A |
| `malloc(N) failed; falling back` | uMenu heap exhausted | Apply Option A (immediate). Then Option B for a clean build. Option C if A+B still fails |
| `fread partial; got N of M bytes; falling back` | SD I/O failure mid-read | Apply Option A. Check SD health |
| `bpcamsInitialize failed rc=0x...; falling back to plain reboot` | bpc:ams port not registered — uSystem crashed before Atmosphère registered the port | Apply Option A immediately, then Option B for rebuild. This is the most likely warning given the `system_resource_size=0x0` regression |
| `bpcamsSetRebootPayload failed rc=0x...; falling back to plain reboot` | bpc:ams session open but IPC dispatch failed | Apply Option B (source fix + rebuild). The session connected but transfer-memory mapping failed — consistent with heap budget issues inside uSystem |
| No warning in log, plain reboot fires silently | `IsRebootToHekateSupported()` static probe cached `false` at startup; `FireAction(6)` calls `RebootToHekate()` which falls through its bpcRebootSystem plain path after bpcInitialize succeeds | Apply Option A. The cached `false` in the static bool means every subsequent call returns immediately after bpcRebootSystem. The static is reset on next clean uMenu launch after uSystem is restored |

---

## Pre-Built Patches — Ready to Copy-Paste

### Option A (no rebuild — copy on mounted SD)

No Edit call needed. Shell command when SD is mounted:

```
cp "/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp.bak-pre-v2.3.6-20260505-100427" \
   "/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp"
```

Verify after copy:

```
md5 "/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp"
# Expected: 5b22a1cda5d57332a880401abae0018b
```

### Option B (source fix + rebuild)

**Edit call 1 of 1:**

file_path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uSystem/uSystem.json`

old_string:
```
    "main_thread_stack_size": "0x0080000",
    "system_resource_size": "0x0",
```

new_string:
```
    "main_thread_stack_size": "0x0100000",
    "system_resource_size": "0xC00000",
```

Then: rebuild uSystem, copy `exefs.nsp` to SD.

### Option C (uMenu patch — only if B fails and log shows malloc failure)

**Edit call 1 of 1:**

file_path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/uMenu.json`

old_string:
```
    "system_resource_size": "0x0",
```

new_string:
```
    "system_resource_size": "0x200000",
```

Then: rebuild uMenu, copy `main` to `sdmc:/ulaunch/bin/uMenu/main`.

---

## Rebuild Scope Reference

| Option | Files to rebuild | SD target |
|---|---|---|
| A | None | `exefs.nsp` (copy from backup) |
| B | `src/projects/uSystem/` only | `sdmc:/atmosphere/contents/0100000000001000/exefs.nsp` |
| C | `src/projects/uMenu/` only | `sdmc:/ulaunch/bin/uMenu/main` |
