# NSP / NSZ / XCI Local Installer — Design (Q OS uMenu, absorb wave 2)

**Status:** DESIGN + SAFE SCAFFOLD ONLY. The write path (NCM/ES/NS) is **GATED**
and returns `NotImplemented`. No live install is enabled. Requires James's
explicit authorization (PD-11) + the open questions in §10 resolved before any
NAND-writing code is turned on.

**Component:** `QdNspInstaller` —
`include/ul/menu/qdesktop/qd_NspInstaller.hpp` +
`source/ul/menu/qdesktop/qd_NspInstaller.cpp`.

**Goal:** install a Switch title from a container file **already on the SD card**
into the console's content storage, so uMenu can install titles without
Goldleaf/Tinfoil/DBI — making it a more self-contained OS.

---

## 1. Legal / clean-room posture (read first)

This installer is deliberately constrained to the **legitimate-install shape**:

- **Local SD files only.** No network fetch of titles, tickets, or keys.
- **No title-key dumping. No key derivation.** We never read `prod.keys`, never
  compute a title key, never decrypt NCA content. NCAs are moved **encrypted,
  byte-for-byte**, into content storage; Horizon/FS decrypt them at runtime using
  keys the console **already owns** — exactly how a purchased eShop install works.
- **Tickets imported verbatim, never forged.** If (and only if) the NSP carries a
  `.tik`, it is handed to ES unchanged. We do not generate, "fix", or
  common-key-wrap a ticket. A title needing a rights-id the console lacks simply
  won't launch — by design, not worked around.
- **No proprietary code.** Only public libnx APIs + the publicly-documented
  PFS0/NCA container shapes. No leaked SDK headers, no Nintendo source.

This is the same content pipeline libnx and Atmosphère expose publicly; the
posture here is "behave like a legitimate installer for content the user already
has on their card," nothing more.

---

## 2. Container formats

| Format | Envelope | What's inside | This change |
|--------|----------|---------------|-------------|
| **NSP** | PFS0 | NCAs + `*.cnmt.nca` (content meta) + optional `*.tik`/`*.cert` | **SCAFFOLDED** — PFS0 parse is fully implemented; write path gated |
| **NSZ** | PFS0 | Same, but NCAs are **zstd-compressed** `.ncz` | **DEFERRED** — parser detects it (promotes kind→Nsz) and `Validate` refuses with a clear message. Needs a streaming zstd-decompress stage feeding the placeholder writer |
| **XCI** | HFS0 (+ XCI header @ 0x100) | "secure" partition holds a PFS0-like NCA set; also `normal`/`update`/`logo` partitions | **DEFERRED** — `DetectKind` recognizes the HFS0 magic; install refused. Needs HFS0 walk + secure-partition extraction, then it rejoins the NSP path |

The on-disk **PFS0 layout** (documented, implemented in the parser):

```
[ Pfs0Header 0x10 ]
    u32 magic = "PFS0" (0x30534650 LE)
    u32 file_count
    u32 string_table_size
    u32 reserved (0)
[ Pfs0FileEntry 0x18 ] × file_count
    u64 data_offset          // relative to the DATA region base
    u64 data_size
    u32 string_table_offset  // name offset into the string table
    u32 reserved
[ string_table : string_table_size bytes, NUL-separated names ]
[ DATA region ]              // data_offset is measured from HERE
```

`data_region_base = 0x10 + file_count*0x18 + string_table_size`. The parser
converts every entry to an **absolute** offset within the `.nsp` and classifies
it by name suffix (`.cnmt.nca` → CNMT, `.nca` → content, `.ncz` → compressed,
`.tik`/`.cert` → ticket material). It hard-bounds `file_count ≤ 4096` and
`string_table_size ≤ 1 MiB` so a malformed/hostile file can't OOM or loop.

---

## 3. The full clean-room install pipeline

```
 Parse ─▶ Validate ─▶ PlaceContent ─▶ ImportTicket ─▶ Register ─▶ Verify ─▶ Done
   │          │            │ (GATED)       │ (GATED)      │ (GATED)    │
 [SAFE]    [SAFE]          └──────────────┬┴─────────────┘            [SAFE]
                                          ▼
                                    Rollback (GATED)  ◀── on any failure
```

| State | Touches | libnx APIs | Status |
|-------|---------|-----------|--------|
| **Parse** | SD read only | `fopen`/`fread`/`fseek` | **IMPLEMENTED** |
| **Validate** | SD + query-only NCM | `ncmInitialize`, `ncmOpenContentStorage`, `ncmContentStorageGetFreeSpaceSize`, `ncmContentStorageClose`, `ncmExit` | **IMPLEMENTED** |
| **PlaceContent** | **NCM content storage (NAND/SD)** | `ncmContentStorageGeneratePlaceHolderId`, `…CreatePlaceHolder`, `…WritePlaceHolder`, `…Register`, `…Delete*` | **GATED** |
| **ImportTicket** | **ES (NAND)** | `esImportTicket` *(see §6 gap)* | **GATED** |
| **Register** | **content-meta DB + ns records (NAND)** | `ncmContentMetaDatabaseSet/Commit`, `nsPushApplicationRecord`, `nsInvalidateApplicationControlCache` *(see §6 gap)* | **GATED** |
| **Verify** | query-only NCM | `ncmContentStorageHas`, `ncmContentMetaDatabaseHas` | **IMPLEMENTED** envelope; inner CNMT enumeration gated (needs NCA reader) |
| **Rollback** | **NCM/NS delete (NAND)** | `ncmContentStorageDelete`, `…DeletePlaceHolder`, `ncmContentMetaDatabaseRemove`, `nsDeleteApplicationRecord` | **GATED** |

### 3.1 Step detail (what the reviewed implementation will do)

**Parse** — `OpenAndParse(path, &container)`. Read PFS0 header → entry table →
string table; produce `NspContainer{ entries[], kind, nca_count, has_cnmt,
has_ticket, cnmt_entry_index, total_content_bytes }`. Read-only.

**Validate** — refuse deferred formats (NSZ/XCI); require a CNMT and ≥1 NCA;
query destination free space and require `free ≥ total_content + 10%`. Failing
the free-space check **here** is the whole point — it prevents an `ENOSPC`
half-write into NAND later.

**PlaceContent** *(GATED)* — for each NCA: derive its `NcmContentId` from the
16-hex filename stem (an installed NCA's name *is* its content-id), then
`GeneratePlaceHolderId` → `CreatePlaceHolder(content_id, phid, size)` →
stream the NCA's bytes from the `.nsp` (`fseek` to `entry.data_offset`) in large
blocks via `WritePlaceHolder` → `Register(content_id, phid)`. Each created
`phid` and registered `content_id` is recorded in `created_placeholders_` /
`registered_contents_` so Rollback deletes exactly what we wrote.

**ImportTicket** *(GATED)* — only if `container.has_ticket`. Read the `.tik`
(0x2C0 bytes) + `.cert` into RAM; `esImportTicket(tik, tik_sz, cert, cert_sz)`.
No ticket → skip (not an error; standard-crypto titles have none).

**Register** *(GATED)* — (A) write the content-meta DB row:
`ncmOpenContentMetaDatabase` → build `NcmContentMetaKey{id, version,
type=Application}` + the packaged-content blob from the CNMT →
`ncmContentMetaDatabaseSet` → `Commit`. (B) push the ns application record: build
a `ContentStorageRecord{ meta_key, storage_id }[]` →
`nsPushApplicationRecord(app_id, 0x3, records, count)` →
`nsInvalidateApplicationControlCache(app_id)` so HOME refreshes the tile.

**Verify** — read-only: for every content-id the CNMT lists,
`ncmContentStorageHas`; plus `ncmContentMetaDatabaseHas` for the meta key. AND
them. (Doubles as an "already installed?" probe.)

**Rollback** *(GATED)* — newest-first delete of `registered_contents_` (via
`ncmContentStorageDelete`), then `created_placeholders_` (via
`…DeletePlaceHolder`), then the meta row (`…Remove`) and the ns record
(`nsDeleteApplicationRecord`) if those steps had run. Only ever touches things
**this object created** — never pre-existing console data.

---

## 4. NCM data model (from this tree's `ncm_types.h`, verified)

- `NcmStorageId` — destination: `…_SdCard` (5) or `…_BuiltInUser` (4). Installing
  to SD is the safer default (no system NAND risk); see §10 open question.
- `NcmContentId { u8 c[0x10] }` — 16-byte content id; the NCA filename stem in
  hex. `NcmPlaceHolderId { Uuid }` — a transient write target.
- `NcmContentMetaKey { u64 id; u32 version; u8 type; u8 install_type; u8 pad[2] }`
  — the meta-DB primary key; `type = NcmContentMetaType_Application (0x80)`.
- `NcmContentInfo { NcmContentId; u32 size_low; u8 size_high; u8 attr; u8
  content_type; u8 id_offset }` + `ncmContentInfoSizeToU64` helper for the 40-bit
  size. `NcmPackagedContentInfo { u8 hash[0x20]; NcmContentInfo }` is the CNMT's
  per-content record.
- `NcmContentMetaHeader { u16 ext_hdr_size; u16 content_count; u16
  content_meta_count; u8 attributes; u8 storage_id }` — top of the CNMT blob,
  followed by an `NcmApplicationMetaExtendedHeader { u64 patch_id; u32
  required_system_version; u32 required_application_version }` for Applications.

The CNMT bytes the meta-DB row needs are produced by parsing the **CNMT NCA's
PFS0 section**, which requires the NCA reader (gated, part of PlaceContent).

---

## 5. Error handling, rollback, free-space

- **Result discipline.** Built with `-fno-exceptions -fno-rtti` (Makefile), so
  every failure is a libnx `Result`, never a throw. Safe-path failures use
  `MAKERESULT(Module_Libnx, LibnxError_{BadInput,IoError})`; the gate sentinel is
  `QdNspInstaller_NotImplemented = MAKERESULT(Module_Libnx, 350)` — a bespoke
  description **outside** this libnx's `LibnxError_*` enum (it has no
  `NotImplemented`), so it's unmistakable in logs and can't collide with a real
  error or with success. If that sentinel ever reaches the UI, **nothing was
  written.**
- **Free-space gate before any write** (Validate), mirroring `qd_SaveBackup`'s
  `statvfs` floor pattern. A query failure → refuse (don't risk a mid-write
  `ENOSPC`).
- **Rollback is precise + gated.** It deletes only the placeholders/contents/
  records tracked in `created_placeholders_` / `registered_contents_`. Rollback
  is itself NAND-mutating, so it is gated too.
- **Resource ledger.** The open `FILE*` during parse and the install worker
  thread are `UL_LEDGER_TRACK`/`UNTRACK`-wrapped so Monitor shows them, matching
  `qd_CheatsInstaller`.
- **Threading.** Off-UI worker + mutex-guarded `NspInstallProgress` snapshot
  polled each frame — identical pattern to `qd_CheatsInstaller`. 512 KiB worker
  stack (headroom for the future NSZ zstd window).

---

## 6. libnx gaps — CLOSED by clean-room IPC wrappers

This tree's libnx (`/opt/devkitpro/libnx/include/switch/services/`) **does not
expose** (verified):

1. **Any `es` service binding** — there is no `es.h` and no `esImportTicket`.
2. **`nsPushApplicationRecord` / `nsDeleteApplicationRecord`** — `ns.h` has
   `nsListApplicationRecord` (cmd, struct `NsApplicationRecord` present),
   `nsCountApplicationContentMeta`, `nsListApplicationContentMetaStatus`, etc.,
   but **not** the record *push/delete*.

NCM, by contrast, *is* fully bound in this libnx — only ES + the NS record
push/delete are missing.

### 6.1 The wrappers (added this change — two new, self-contained units)

Both are clean-room, built **only** on public libnx plumbing (`smGetService`,
`serviceDispatch`/`serviceDispatchIn`, `serviceClose`, `serviceIsActive`) — the
exact pattern already used by `source/ul/menu/smi/sf/sf_PrivateService.cpp`. No
proprietary headers, no leaked SDK. Command ids are the publicly-documented
switchbrew values and must be re-confirmed against the running firmware before
ungating.

**`qd_EsIpc.{hpp,cpp}`** — `namespace ul::menu::qdesktop::es`:
- `Initialize()` → `smGetService(&srv, "es")` (idempotent; `es` has no per-session
  init cmd). May be **permission-denied** for uMenu — surfaces as a normal Result.
- `ImportTicket(tik, tik_size, cert, cert_size)` → **`es` cmd 1** (`ImportTicket`).
  ABI: **no raw input**, **two In map-alias buffers** (`SfBufferAttr_HipcMapAlias
  | SfBufferAttr_In`) = ticket bytes, then cert-chain bytes. Imports VERBATIM.
- `Finalize()` / `IsActive()`. Session tracked in the resource ledger
  (`QdResKind::Service`, tag `"es"`).

**`qd_NsAmIpc.{hpp,cpp}`** — `namespace ul::menu::qdesktop::nsam`:
- `Initialize()` → libnx `nsInitialize()`, then resolves
  `IApplicationManagerInterface` **firmware-tolerantly**: try
  `nsGetApplicationManagerInterface(&srv)` ([3.0.0+], a subsession **we own +
  close**); fall back to `nsGetServiceSession_ApplicationManagerInterface()`
  (pre-3.0.0, a session **libnx owns** — we copy it and must **not** close it,
  only `nsExit()`).
- `ContentStorageRecord` — locally defined (`0x18` bytes, `static_assert`ed):
  `NcmContentMetaKey meta_key (0x10)` + `u8 storage_id` + `u8 padding[7]`.
- `PushApplicationRecord(app_id, last_modified_event, records, count)` →
  **IApplicationManagerInterface cmd 16**. ABI: raw in = packed `{ u8
  last_modified_event; u64 application_id; }`, **one In map-alias buffer** =
  `ContentStorageRecord[]`. Installers use `last_modified_event = 0x3`.
- `DeleteApplicationRecord(app_id)` → **cmd 21**. ABI: raw in = `u64
  application_id`, no buffers.
- `Finalize()` / `IsActive()`. Session tracked (`QdResKind::Service`, tag
  `"ns:am"`).

### 6.2 How they are wired into the installer (still GATED)

`qd_NspInstaller.cpp` `#include`s both wrappers and defines a single file-scope
**master gate** `constexpr bool kLiveInstallEnabled = false;`. The real wrapper
calls live inside `if (kLiveInstallEnabled) { ... }` blocks in `StepImportTicket`
(es), `StepRegister` (ns:am push, + the NCM meta-DB row), and `StepRollback`
(ns:am delete). Because the constant is statically `false`, those branches are
**dead code** — the compiler **type-checks the full IPC integration**, but it can
**never execute**. Every live path still returns `QdNspInstaller_NotImplemented`
and writes **nothing**. Ungating = (a) James's PD-11 authorization, (b) §10 open
questions resolved, (c) flip `kLiveInstallEnabled` — never as part of "make it
build."

> **Coordination note:** these three `.cpp` files ship **parked**
> (`qd_NspInstaller.cpp.parked`, `qd_EsIpc.cpp.parked`, `qd_NsAmIpc.cpp.parked`)
> so the Makefile's `qdesktop/*.cpp` auto-glob excludes them from the main-tree
> build. The `.hpp` headers are in place (headers don't auto-compile). Un-park the
> three `.cpp` to integrate. The full set was verified to compile + link cleanly
> un-parked, and the tree was verified to build cleanly with them re-parked.

---

## 7. Files in this change (nothing existing modified outside this scope)

- `docs/NSP-INSTALLER-DESIGN.md` — this document.
- `include/ul/menu/qdesktop/qd_NspInstaller.hpp` — API + safety contract.
  **Fix:** the progress-lock member is now declared `mutable ::Mutex` (the libnx
  u32), not unqualified `Mutex` — inside `namespace ul::menu::qdesktop` an
  unqualified `Mutex` resolved to uCommon's RAII `ul::Mutex` (pulled in via
  `ul_Result.hpp` → `ul_Include.hpp`), which is what produced the 12
  `cannot convert 'ul::Mutex*' to 'Mutex*'` errors against libnx
  `mutexLock`/`mutexUnlock`. `qd_CheatsInstaller.hpp` avoids this only because it
  does not include `ul_Include.hpp`; explicit `::Mutex` is the robust fix.
- `source/ul/menu/qdesktop/qd_NspInstaller.cpp` — PFS0 parser (real) + gated
  state machine. Adds `mutexInit(&progress_lock_)` in `StartInstall` (parity with
  `qd_CheatsInstaller`) and the gated `kLiveInstallEnabled` wrapper wiring (§6.2).
- `include/ul/menu/qdesktop/qd_EsIpc.hpp` + `source/.../qd_EsIpc.cpp` — clean-room
  `es` ImportTicket wrapper (§6.1).
- `include/ul/menu/qdesktop/qd_NsAmIpc.hpp` + `source/.../qd_NsAmIpc.cpp` —
  clean-room `ns:am` IApplicationManagerInterface record push/delete (§6.1).

All follow uMenu conventions: `qd_` prefix, everything inside
`#ifdef QDESKTOP_MODE`, `namespace ul::menu::qdesktop[::es|::nsam]`,
`UL_LOG_INFO/WARN`, `UL_LEDGER_TRACK/UNTRACK`, libnx `Result` codes.

**Build status (this change):** `make umenu` was run and verified GREEN with all
three `.cpp` un-parked (`qd_EsIpc.cpp`, `qd_NsAmIpc.cpp`, `qd_NspInstaller.cpp`
all compiled; `uMenu.elf` linked; `uMenu.nso`/`uMenu.nsp` built; 0 errors), and
GREEN again with the three `.cpp` re-parked (forced relink, files excluded by the
glob, 0 errors).

---

## 8. Integration notes (do these in a SEPARATE, reviewed change)

> Per the task's hard constraint, this change touches **no** existing/shared
> file. The wiring below is documented here, not performed.

### 8.1 Makefile — NO EDIT REQUIRED

`projects/uMenu/Makefile` builds `SOURCES` by **directory glob**:

```
SOURCES := source ... source/ul/menu/qdesktop
```

Every `.cpp` in `source/ul/menu/qdesktop/` is compiled automatically, so
`qd_NspInstaller.cpp` is picked up with **no Makefile change**. `QDESKTOP_MODE`
is already defined globally (`CFLAGS += -DQDESKTOP_MODE`). Nothing to add.

### 8.2 Menu wiring (when a UI is desired)

A future `qd_NspInstallerLayout` (its own new file, separate change) would:

1. Browse `sdmc:` for `*.nsp` / `*.nsz` / `*.xci` (reuse the Vault file-manager
   list) and call `QdNspInstaller::OpenAndParse` to preview entries — **all safe
   today**.
2. Show the parsed entry list + total size + "has ticket?" badge.
3. Behind James's authorization, call `StartInstall(path, NcmStorageId_SdCard)`
   and poll `GetProgress()` each frame for the phase/percent bar (same shape as
   `QdCheatsLayout` ↔ `QdCheatsInstaller`).
4. Register the layout in the launcher/hot-corner the same way `QdCheatsLayout`
   is registered (a `qos`-menu entry). No registration is added in this change.

---

## 9. Risks

1. **Bricking / NAND corruption (highest).** A bad `Register` (malformed
   content-meta row or ns record) can corrupt the title database and, worst case,
   destabilize boot. This is why Register/PlaceContent/Rollback are all gated and
   why installing to **SD** (not BuiltInUser/system) is the proposed default.
2. **Half-installed title.** A crash/`ENOSPC` between PlaceContent and Register
   leaves orphaned NCAs in NCM. Mitigated by the free-space gate + precise
   Rollback — but Rollback must be implemented and tested *before* PlaceContent is
   ungated, never after.
3. **Wrong-target / overwrite.** Installing a patch/AOC, or re-installing an
   existing app, must be reasoned about (version compare, update vs base) before
   the record push — a careless `nsPushApplicationRecord` can shadow an existing
   install.
4. **libnx gap forces hand-rolled IPC** (§6) for ES + ns records — clean-room but
   unverified surface that itself needs review + HW test.
5. **uMenu's restrictive NPDM.** As a qlaunch-replacement, uMenu may lack the FS/
   NCM permissions a dedicated installer (hbloader's 0xFF flags) gets — the same
   class of permission limit already seen in `qd_SaveBackup` (RO-mount fallback).
   NCM `Open*`/`Register` may simply be denied at runtime; must be probed on HW
   before trusting the write path.

---

## 10. v1 recommendation + open questions for James (resolve before ungating)

### 10.1 Standing v1 recommendation

- **Install to SD only (`NcmStorageId_SdCard`).** Smallest blast radius; keeps
  the system NAND title DB out of the write path entirely. Do **not** offer
  `BuiltInUser`/system NAND in v1 — it materially raises the Risk #1 brick
  surface for no user-facing benefit (SD installs launch fine).
- **Base Applications only (`NcmContentMetaType_Application`, `0x80`).** Defer
  Patch/Update/AddOnContent: those change the version-compare + record-push
  semantics and can shadow/ço-mingle with existing installs. A base-app push is
  the simplest, most reviewable first target.
- **NSP only** for v1 (NSZ/XCI stay refused at Validate — they need a
  zstd-decompress stage / HFS0 walk respectively).
- The clean-room `es` + `ns:am` wrappers (§6) live **in this fork** (qdesktop
  subsystem), not a shared lib — they are uMenu-specific and gated.

### 10.2 Open questions (must be answered to flip `kLiveInstallEnabled`)

1. **Confirm SD-only + base-apps-only is acceptable for v1** (the recommendation
   above), or is a wider scope wanted?
2. **NCA reader.** PlaceContent + the inner-CNMT enumeration in Verify both need
   an NCA/PFS0-section reader (to derive content-ids and the meta-DB blob). That
   is the next gated unit and a prerequisite for any real install/verify.
3. **uMenu NPDM permissions (probe on HW first).** As a qlaunch replacement,
   uMenu may be **denied** `es` and/or the NCM `Open*`/`Register` and ns
   record-push commands at runtime (same permission class already seen in
   `qd_SaveBackup`'s RO-mount fallback). `es::Initialize` / `nsam::Initialize`
   are written to surface a denial as a plain Result; this MUST be tested on the
   Erista before trusting the write path.

> Until 10.2 is resolved and the gate is explicitly authorized (PD-11), the
> installer ships as: **parse + validate + free-space + verify-envelope only.**
> `kLiveInstallEnabled` stays `false`. **No NAND/NCM/ES/NS is written.**
