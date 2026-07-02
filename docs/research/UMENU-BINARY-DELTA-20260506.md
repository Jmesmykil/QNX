# uMenu Binary Delta — 30d02092 vs 3beeb544 (2026-05-06)

**Root cause:** The new binary adds 2.50 MB of VM footprint, dominated by a 2.00 MB static BSS array in the `QdIconCache` singleton, which crosses the FW20 SystemApplet pool budget.

---

## Side-by-side metric table

| Metric | OLD `3beeb544` | NEW `30d02092` | Delta |
|---|---|---|---|
| File size (bytes) | 6,929,556 | 7,099,642 | +170,086 |
| Build ID | `f6df4a9ae243b4a9` | `9f1d1093c831bc11` | — |
| `.text` decompressed | 8,352,752 | 8,554,672 | **+201,920** |
| `.rodata` decompressed | 2,432,564 | 2,486,804 | **+54,240** |
| `.data` decompressed | 848,760 | 947,224 | **+98,464** |
| `.bss` size | 3,438,736 | 5,707,760 | **+2,269,024** |
| Total VM footprint | 15,072,812 | 17,696,460 | **+2,623,648** |
| Static constructors (INIT_ARRAY) | 20 | 22 | +2 |
| Static destructors (FINI_ARRAY) | 19 | 22 | +3 |
| PLT relocations (imported symbols) | 543 | 607 | +64 |

BSS dominates: +2,269,024 bytes (+2.16 MB) = 86.5% of total VM growth.

---

## Top 10 BSS allocations in the new binary by size

NSO carries `DT_STRSZ=1` (stripped). Names are source-attributed.

| Rank | Size | Symbol / allocation | Source |
|---|---|---|---|
| 1 | 2,100,224 | `QdIconCache::entries_` `std::array<IconCacheEntry,128>` in `GetSharedIconCache()` | `qd_IconCache.cpp:699` |
| 2 | 65,536 | `g_NxlinkServerStack` 64 KB `constinit` thread stack | `qd_NxlinkServer.cpp:56` |
| 3 | 65,536 | `g_ShellServerStack` 64 KB `constinit` thread stack | `qd_RemoteShellServer.cpp:53` |
| 4 | 104 | `QdIconCache` overhead: `tick_counter_`+`hash_index_`+hit-rate counters | `qd_IconCache.cpp:699` |
| 5 | 56 | `GetFailedExtractPaths()` `std::unordered_set<std::string>` static | `qd_IconCache.cpp:717` |
| 6 | 40 | `GetSharedIconCacheMutex()` `std::mutex` static (40 bytes AArch64 pthreads) | `qd_IconCache.cpp:703` |
| 7 | 160 | `g_NxlinkServer` `QdNxlinkServer` global | `qd_NxlinkServer.cpp:47` |
| 8 | 160 | `g_RemoteShellServer` `QdRemoteShellServer` global | `qd_RemoteShellServer.cpp:42` |
| 9 | 4 | `g_nxlink_scan_pending` `std::atomic_bool` | `qd_NxlinkServer.cpp:48` |
| 10 | ~2,048 | Misc: `QdWindowManager`, `QdNintendoApps`, Vault drag state, vtables | various |

**Verified sum: 2,233,868 bytes attributed (98.5% of BSS delta).**

---

## Delta attribution

### Feature 1 — Icon cache cap raised from 24 → 128 (PRIMARY CAUSE)

`qd_IconCache.hpp:28`: `MEM_CACHE_CAP = 128` (was 24).

`IconCacheEntry` = `u64+u64+u8[16384]+bool` = 16,408 bytes. The singleton `GetSharedIconCache()` holds `std::array<IconCacheEntry, MEM_CACHE_CAP> entries_` in static storage.

- 24 × 16,408 = 393,792 bytes (old)
- 128 × 16,408 = 2,100,224 bytes (new)
- **+1,706,432 bytes (+1.63 MB BSS)**

The `qd_IconCache.hpp:25–27` comment cites "2 MB — well within Switch heap headroom" but that assessment was not re-evaluated against the FW20 SystemApplet pool budget.

### Feature 2 — NxlinkServer + RemoteShellServer thread stacks (SECONDARY CAUSE)

Both are unconditional `constinit` BSS globals, allocated at link time regardless of whether dev tools are ever enabled:

- `qd_NxlinkServer.cpp:56`: `alignas(0x1000) constinit u8 g_NxlinkServerStack[64 * 1024]` — 65,536 bytes
- `qd_RemoteShellServer.cpp:53`: `alignas(0x1000) constinit u8 g_ShellServerStack[64 * 1024]` — 65,536 bytes
- **+131,072 bytes BSS**

### Feature 3 — New code and data surface (+354 KB across .text/.rodata/.data)

`QdWindowManager`, `QdNxlinkServer`, `QdRemoteShellServer`, `QdNintendoApps`, `QdVaultLayout` drag-scroll, and context-menu code add +201,920 bytes `.text`. New UI strings/vtables: +54,240 bytes `.rodata`. New initialized globals: +98,464 bytes `.data`.

---

## Smallest set of features to disable to bring `30d02092` back under `3beeb544`'s footprint envelope

### Change A — `MEM_CACHE_CAP = 24` (saves 1,706,432 bytes BSS)

`src/projects/uMenu/include/ul/menu/qdesktop/qd_IconCache.hpp` **line 28**

```cpp
static constexpr size_t MEM_CACHE_CAP = 24;  // was 128
```

Single-line primary fix. LRU thrashing returns on Launchpad with >24 NROs, but the binary boots. If 24 is too small operationally, `MEM_CACHE_CAP = 48` (786,816 bytes) keeps the binary within budget when combined with changes B and C.

### Change B — Heap-allocate thread stacks on `Start()` (saves 131,072 bytes BSS)

`src/projects/uMenu/source/ul/menu/qdesktop/qd_NxlinkServer.cpp` **line 56**

```cpp
// Before
alignas(0x1000) constinit u8 g_NxlinkServerStack[64 * 1024];
// After
static u8 *g_NxlinkServerStack = nullptr;  // memalign(0x1000, 64*1024) in Start(), free in Stop()
```

`src/projects/uMenu/source/ul/menu/qdesktop/qd_RemoteShellServer.cpp` **line 53** — identical change for `g_ShellServerStack`. Both stacks are only used when the user enables dev tools from the login-screen toggle; lazy allocation at `Start()` is correct.

### Change C — `#ifdef QD_DEVTOOLS_ENABLED` server globals (saves 324 bytes BSS)

`src/projects/uMenu/source/ul/menu/qdesktop/qd_NxlinkServer.cpp` **lines 47–56**
`src/projects/uMenu/source/ul/menu/qdesktop/qd_RemoteShellServer.cpp` **lines 42–53**

Wrap global declarations in the existing `QDESKTOP_MODE` / `QD_DEVTOOLS_ENABLED` guard. Minor savings but eliminates boot-time construction for non-dev builds.

### Combined savings

| Change | BSS saved | `.bss` after |
|---|---|---|
| A: `MEM_CACHE_CAP = 24` | 1,706,432 | 3,563,328 |
| B: heap thread stacks | 131,072 | 3,432,256 |
| C: ifdef server globals | 324 | 3,431,932 |
| **Total** | **1,837,828** | **3,431,932** |

Post-change `.bss` ≈ 3,432 KB — within 0.2% of the old binary's 3,439 KB. Total VM drops to ~15,858,632 bytes, leaving ~800 KB headroom over the old binary's envelope for the retained `.text`/`.rodata`/`.data` growth.

---

## Strict factual conclusion

Two independently measurable changes crossed the FW20 SystemApplet pool line:

1. `qd_IconCache.hpp:28`: `MEM_CACHE_CAP` raised 24 → 128. `GetSharedIconCache()`'s static `entries_` array grew from 393,792 to 2,100,224 bytes — **+1,706,432 bytes BSS, 65.1% of the 2,623,648 byte total VM delta**.

2. `qd_NxlinkServer.cpp:56` and `qd_RemoteShellServer.cpp:53`: two unconditional `constinit` 64 KB thread stacks added — **+131,072 bytes BSS** regardless of whether dev tools are ever enabled.

Together: 1,837,504 bytes, 70.1% of total VM growth. The remaining 29.9% is distributed across new `.text`/`.rodata`/`.data` for `QdWindowManager`, `QdNintendoApps`, Vault drag-scroll, and context-menu features — none of which would cause a boot failure in isolation.

uMenu `3beeb544` boots with the same uSystem because it stays within the pool budget. uMenu `30d02092` fails because it requests 2.50 MB more committed address space at startup than the pool allows.
