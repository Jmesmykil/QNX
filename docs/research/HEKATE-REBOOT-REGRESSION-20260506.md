# Hekate Reboot Regression — 2026-05-06

> **SUPERSEDED 2026-05-16 — keep for historical post-mortem only.**
> This doc documents the May 6 regression hypothesis. The 2026-05-16 root cause turned out to be different:
> the in-process `bpc:ams` chainload from `qd_Power.cpp::RebootToHekate()` no longer triggers Hekate on HOS 20.0.0
> (it returns OK but boots stock firmware). The fix is composition — hot-corner action 6 now delegates to Tomvita's
> `sdmc:/switch/reboot_to_hekate.nro` via `smi::LaunchHomebrewLibraryApplet` + `FadeOutToNonLibraryApplet` + `Finalize`
> (commit `8a3bbe92`, branch `fix-hekate-reboot-via-nro-delegate`, HW-confirmed 2026-05-16T~21:07).
> The "last known-good uSystem" referenced below is also superseded — the canonical working uSystem is upstream
> uLaunch v1.2.0 build sha256 `ce48dc7d4dc46e64b1b9a564541309454f0670ec1b721f9742eeaddc5b1f8a98` (May 5 build).
> See `~/.claude/projects/-Users-astral/memory/project_qos_hekate_reboot_recipe.md` for the canonical working recipe.

## Root cause

**File:** `src/projects/uSystem/uSystem.json`  
**Lines 6-7 (dirty):**
```
"main_thread_stack_size": "0x0080000",
"system_resource_size": "0x0",
```
**Was (HEAD):**
```
"main_thread_stack_size": "0x0100000",
"system_resource_size": "0xC00000",
```

The deployed `exefs.nsp` (590 398 bytes, modified 2026-05-06 05:43, md5 `aa267e1a`) was built from
today's dirty tree. uSystem's main-thread stack was halved (1 MB → 512 KB) and its
system-resource pool was zeroed. The new `RebootToStockQlaunch` SMI handler
(`src/projects/uSystem/source/main.cpp`) allocates on the SMI thread; with
`system_resource_size = 0x0` the kernel returns no heap pages from the system-resource
region. When the SMI dispatcher calls `ul::fs::ExistsFile` / `ul::fs::RenameFile` those
routines internally call `fopen`, which requires a stdio buffer from the per-process heap.
With `system_resource_size = 0x0` the alloc fails silently and the handler falls through
to `appletRequestToReboot()` — a plain HOS reboot — instead of staging the Hekate payload.

The hot-corner action 6 dispatch chain is otherwise intact:  
`QdHotCornerRightDropdown::FireAction(6)` → `power::RebootToHekate()` in `qd_Power.cpp` (no
changes today) → `bpcamsSetRebootPayload` + `bpcRebootSystem`. That path does NOT go
through uSystem at all. The regression is therefore:

**The newly deployed `exefs.nsp` (uSystem built with `system_resource_size=0`) crashes
the SMI thread early in boot or causes uSystem to emit `appletRequestToReboot()` before
uMenu is fully initialised, so by the time the user triggers action 6 the Atmosphère
bpc:ams service is no longer available (IPC handle was torn down), causing
`bpcamsInitialize()` inside `RebootToHekate()` to fail and fall through to the plain
`bpcRebootSystem()` path — which reboots to HOS.**

Evidence: `exefs.nsp.bak-pre-v2.3.6-20260505-100427` (568 284 bytes) on the SD is the
last known-good uSystem. The pre-2.3.6 `exefs.nsp` was built with `system_resource_size =
0xC00000`.

## Fix

Revert `uSystem.json` to the pre-2.3.6 resource sizes:

**File:** `src/projects/uSystem/uSystem.json`

```
old_string:
    "main_thread_stack_size": "0x0080000",
    "system_resource_size": "0x0",

new_string:
    "main_thread_stack_size": "0x0100000",
    "system_resource_size": "0xC00000",
```

Then rebuild uSystem and redeploy `exefs.nsp` to
`sdmc:/atmosphere/contents/0100000000001000/exefs.nsp`.

A faster no-build fix: copy the backup already on the SD card —
`exefs.nsp.bak-pre-v2.3.6-20260505-100427` — over `exefs.nsp` to restore the
last-known-good uSystem binary immediately.

## Confidence

**High.** The only binary deployed today that touches the bpc:ams IPC surface is
`exefs.nsp` (uSystem). `qd_Power.cpp` and `qd_HotCornerRightDropdown.cpp` are
unmodified. The `system_resource_size = 0x0` change is the sole structural difference
between today's uSystem build and the pre-2.3.6 binary.

## Side effects

The `RebootToStockQlaunch` handler added in `uSystem/source/main.cpp` will also be broken
(same IPC heap starvation). The Settings panel "Boot to Nintendo Home Menu" button will
fire `appletRequestToReboot()` without renaming `exefs.nsp`, causing a plain HOS reboot
with uSystem still active — not a stock qlaunch boot as intended.
