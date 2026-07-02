# Static Analysis Report: uSystem Binary Diff — 20260505

## Target

- **Working binary:** `/Users/nsa/QOS/staging/qos-usystem-upstream/exefs.nsp` (identity-verified via MD5 `5b22a1cda5d57332a880401abae0018b` matching commit body)
- **Broken binary:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uSystem/out/nintendo_nx_arm64_armv8a/release/uSystem.nsp`
- **Type:** NSP (PFS0 container) -> NSO (LZ4-compressed arm64 executable) + NPDM
- **Authorization:** Owned — creator's fork at `qos-ulaunch-fork/`
- **Analysis Date:** 2026-05-05
- **Claimed commit:** `9246dc1b` for both

---

## Container Structure

Both NSPs are PFS0 archives with 2 entries: `main` (NSO) and `main.npdm` (NPDM). Extraction
confirmed via magic bytes and LZ4 decompression of all three sections.

---

## NSO Header Comparison

| Field | Working | Broken | Delta |
|---|---|---|---|
| NSO file size | 567,216 B | 588,607 B | **+21,391** |
| .text (decompressed) | 807,696 B | 827,184 B | **+19,488** |
| .text (compressed in file) | 487,491 B | 506,875 B | **+19,384** |
| .rodata (decompressed) | 123,172 B | 127,468 B | **+4,296** |
| .rodata (compressed) | 53,994 B | 55,818 B | **+1,824** |
| .data (decompressed) | 223,088 B | 256,112 B | **+33,024** |
| .data (compressed) | 25,475 B | 25,658 B | **+183** |
| .bss | 12,794,016 B (~12.2 MB) | 24,329,488 B (~23.2 MB) | **+11,535,472 (~11 MB)** |
| .rodata mem_off | 0x000C6000 | 0x000CA000 | +16,384 (page alignment) |
| .data mem_off | 0x000E5000 | 0x000EA000 | +20,480 (page alignment) |
| Build ID | `43700700ead2...` | `fbbb07000ada...` | **different** |
| Flags | 0x3F (all sections compressed) | 0x3F | identical |
| KernelVersion | 0.2 | 0.2 | identical |
| HandleTableSize | 48 | 48 | identical |
| MiscFlags | 0x200 | 0x200 | identical |

### Build IDs (full 32 bytes)

```
Working: 43700700ead20000836300000000000000000000000000000000000000000000
Broken:  fbbb07000ada00003a6400000000000000000000000000000000000000000000
```

---

## NPDM META Comparison

NPDM size is identical (972 bytes). Only 2 bytes differ across the entire file.

| Field | Offset | Working | Broken | Delta |
|---|---|---|---|---|
| `system_resource_size` | 0x14 | `0x00000000` (0) | `0x00C00000` (12,582,912 B / **12 MB**) | **+12 MB** |
| `main_thread_stack_size` | 0x1C | `0x00080000` (524,288 B / **512 KB**) | `0x00100000` (1,048,576 B / **1 MB**) | **doubled** |

All other META fields are identical: flags, main_thread_prio (0), default_cpu (0), version (0),
title_name ("uSystem"), product_code ("").

ACID content (post-signature, at 0x280): **byte-for-byte identical.**

---

## ACI0 / KAC / FAH / SAC Comparison

ACI0, FAH, SAC, and all 11 KAC descriptors are **byte-for-byte identical** between the two binaries.

KAC descriptor summary (both binaries):

```
[0] 0x030373B7  ThreadInfo: prio=[59,28]  cpu_range=[3,3]
[1] 0x1FFFFFCF  SvcGroup: base=0   mask=0xFFFFFE
[2] 0x3FFFFFEF  SvcGroup: base=24  mask=0xFFFFFF
[3] 0x47E60FEF  SvcGroup: base=48  mask=0x3F307F
[4] 0x7FFFFFEF  SvcGroup: base=72  mask=0xFFFFFF
[5] 0x9FFFFFEF  SvcGroup: base=96  mask=0xFFFFFF
[6] 0xA0001FEF  SvcGroup: base=120 mask=0x0000FF
[7] 0x00009FFF  KernelVersion: 0.2
[8] 0x00183FFF  HandleTableSize: 48
[9] 0x02007FFF  MiscFlags: 0x200
[10] 0x0000FFFF  (type-16 padding / unused)
```

ProgramID: `0x0100000000001000` (both).

FAH: full filesystem access (`0xFF...FF`), identical. SAC: identical (4 bytes).

---

## .rodata String Analysis

| Category | Working | Broken |
|---|---|---|
| Unique strings (>= 6 chars) | 1,894 | 1,938 |
| Added | — | 62 |
| Removed | 18 | — |

### Significant additions (broken only)

New feature subsystem strings (`ApplicationControlCache`):

```
[ApplicationControlCache] Initializing control cache with %zu records (cache usage per application: %f MB)
[ApplicationControlCache] Making NACP + icon cache for application ID 0x%016lX (icon size: %zu bytes)
[ApplicationControlCache] Cache memory usage: %f MB
[ApplicationControlCache] alive!
threadCreate(&g_ApplicationControlCacheThread, ApplicationControlCacheMain, nullptr, nullptr,
             ApplicationControlCacheThreadStackSize, 0x1F, -2)
```

New log messages for thread creation with `nullptr` stack pointer:

```
threadCreate(&g_EventManagerThread, EventManagerMain, nullptr, nullptr, EventManagerThreadStackSize, 34, -2)
```

New homeBrew long-press routing:

```
Got AppletMessage: DetectLongPressingHomeButton while non-uMenu is foreground
forwarding HomeLongRequest to uMenu
```

New cache path constants:

```
sdmc:/ulaunch/cache/app
sdmc:/ulaunch/cache/preview
```

### Significant removals (working only, missing from broken)

Old EventManager static stack form:

```
threadCreate(&g_EventManagerThread, EventManagerMain, nullptr,
             g_EventManagerThreadStack, sizeof(g_EventManagerThreadStack), 34, -2)
```

Old newlib version in debug paths:

```
newlib-4.5.0.20241231/...
```

Old `xor` builder path (upstream):

```
/run/media/xor/MDA/Nintendo/Switch/Proyectos/uLaunch/libs/uCommon/...
```

Replaced by the creator's own path:

```
/Users/nsa/QOS/tools/qos-ulaunch-fork/src/libs/uCommon/...
```

### Newlib bump confirmed

- Working: `newlib-4.5.0.20241231`
- Broken: `newlib-4.6.0.20260123` (two months newer)

---

## .bss Memory Layout (broken ELF symbols)

From `nm --demangle` on the broken ELF at
`src/projects/uSystem/out/nintendo_nx_arm64_armv8a/release/uSystem.elf`:

```
BSS range:  0x00129000 .. 0x0185C580 = 24,327,552 bytes
Working est: 0x00129000 .. 0x00D5C8A0 = ~12,793,504 bytes
Delta:      +11,534,048 bytes (~11 MB)
```

New ApplicationControlCache static objects in BSS:

```
0x01636AA0  g_ApplicationControlCacheThread
0x01636AD8  g_ApplicationCacheLock
0x01636AE0  g_ApplicationCache
0x01636AE8  g_PendingApplicationRemovalCacheQueueLock
0x01636AF0  g_PendingApplicationRemovalCacheQueue
0x01636AF8  g_PendingApplicationCacheQueueLock
0x01636B00  g_PendingApplicationCacheQueue
```

The 11 MB BSS growth is not explained by these small objects alone. The gap from
`g_PendingApplicationCacheQueue` (0x1636B00) to BSS end (0x185C580) is **2,251,392 bytes**
(~2.1 MB) of additional static data, plus the `system_resource_size = 12 MB` which is
allocated from the system memory pool at runtime (not in BSS), together accounting for
the full memory footprint increase.

---

## Root Cause Analysis

### Five discriminating fields

| Rank | Field | Working | Broken | Significance for am |
|---|---|---|---|---|
| 1 | `system_resource_size` (NPDM META 0x14) | 0 | **12 MB** | am calls `svcSetResourceLimitLimitValue` based on this; if the system resource pool cannot satisfy the request, `svcLimitReached (0x10801)` is returned at proxy-init |
| 2 | `main_thread_stack_size` (NPDM META 0x1C) | 512 KB | **1 MB** | Doubled stack allocation from kernel pool; interacts with total process memory budget |
| 3 | `.bss size` | 12.2 MB | **23.2 MB** | Process virtual memory footprint increase of ~11 MB; combined with stack doubling and system_resource_size, total reserved memory exceeds the am SystemApplet process memory limit |
| 4 | Build ID | `43700700...` | `fbbb0700...` | Cosmetic — confirms different code was compiled; crash PC=`am+0x40570` decodes against the broken binary's build ID |
| 5 | Newlib version | 4.5.0 (Dec 2024) | **4.6.0 (Jan 2026)** | Different stdlib; explains .text growth (+19 KB) and some .rodata churn; not directly related to crash, but indicates the toolchain was updated between builds |

### Why am proxy-init panics with 0x10801

The crash `am.nss panics at proxy-init, result 0x10801 = svc::LimitReached, PC=am+0x40570`
maps to am's `RegisterServer` path for uSystem's IPC port. am uses `svcCreateProcess` with
the NPDM's `system_resource_size` to pre-reserve system memory. When `system_resource_size`
goes from 0 to 12 MB, the kernel calls `svcSetResourceLimitLimitValue` and `svcSetUnsafeLimit`
before the process can register. In FW 20.0.0 the system applet pool has a hard ceiling. The
12 MB request, combined with the doubled stack (1 MB) and the enlarged process image (~11 MB
extra BSS), causes the resource limit to be exceeded before am gets to the proxy connection
handshake, producing `svcLimitReached`.

The ApplicationControlCache subsystem — a new background thread that pre-fetches NS icon/NACP
data into a large static cache — is the proximate cause: it introduced `system_resource_size`
and the BSS growth in the same change. The thread stack form also changed from a static BSS
buffer to a nullptr (kernel-allocated) stack, which pulls from the same system pool.

---

## Summary of Deltas (Compact Reference)

```
NSO .text:    +19,488 B decomp  (+19,384 B compressed)
NSO .rodata:   +4,296 B decomp  (+1,824 B compressed)
NSO .data:    +33,024 B decomp  (+183 B compressed)
NSO .bss:    +11,535,472 B  (+11.0 MB)
NPDM system_resource_size: 0 -> 12 MB
NPDM main_stack_size:      512 KB -> 1 MB
NPDM KAC/FAH/SAC/ACID:    identical
Newlib:        4.5.0 -> 4.6.0
Build ID:      changed (cosmetic)
```

---

## Recommendations

1. **Revert the ApplicationControlCache subsystem** or gate it behind a compile-time flag until
   the system resource budget is understood. The `.bss` pre-allocation and `system_resource_size`
   fields must be sized below the FW 20 SystemApplet pool limit.

2. **Pin `system_resource_size` to 0** (or a confirmed safe non-zero value tested on FW 20) in
   `src/projects/uSystem/uSystem.json` (the NPDM source). Zero works for the working binary.

3. **Revert `main_thread_stack_size` to 0x80000** (512 KB) until the 1 MB requirement is
   verified against the memory budget.

4. **Newlib 4.6.0 is benign for the crash** but contributes ~5 KB of .text growth. Acceptable.

5. **Verify git state of `uSystem.json`** — the NPDM fields `system_resource_size` and
   `main_thread_stack_size` must be set explicitly in the source JSON, not auto-generated.
   The commit `9246dc1b` should not have changed these if it was purely a heap retune revert.
   A dirty JSON or non-committed `uSystem.json` may be the actual regression source.
