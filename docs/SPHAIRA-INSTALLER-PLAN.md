# Sphaira Installer Pipeline — Q OS CFW Pack

> **Scope:** Plan only. No code, no SD writes, no public posts.
> Goal: Switch user opens Sphaira → finds Q OS in its appstore → one-tap installs the
> full Q OS CFW pack (Atmosphère + Hekate + sysmodules + sigpatches + uLaunch fork
> + configs).
> Generated: 2026-05-05. Primary sources cited inline.
>
> **Distinct from `HBAPPSTORE-MANIFEST.md` and `SPHAIRA-CATALOG.md`,** which scope
> the *uMenu-only* package (`qos-umenu`). This plan scopes a *full HATS-equivalent
> CFW pack* (`qos-cfw`) layered or shipped whole.

---

## 1. Sphaira's installer mechanism (verified from source)

Primary sources surveyed 2026-05-05:
- `https://github.com/ITotalJustice/sphaira/blob/master/sphaira/source/ui/menus/appstore.cpp`
- `https://github.com/ITotalJustice/sphaira/blob/master/sphaira/include/app.hpp`
- `https://github.com/ITotalJustice/sphaira/wiki/Install`
- `https://github.com/ITotalJustice/sphaira/wiki/Locations`

### 1.1 Catalog source — single hardcoded URL

Sphaira's appstore tab GETs exactly one URL on launch:

```
URL_BASE = "https://switch.cdn.fortheusers.org"
URL_JSON = "https://switch.cdn.fortheusers.org/repo.json"
```

The JSON shape is the `libget` repo format used by hb-appstore. No "extra repos"
field, no second URL, no INI override. `app.hpp` defines `CONFIG_PATH =
"/config/sphaira/config.ini"` and `locations.ini` (per Locations wiki) but
`locations.ini` is for FTP/HTTP **upload** destinations, not catalog sources.

Cached repo lives at `sd:/switch/sphaira/cache/appstore/repo.json`.

### 1.2 ZIP/manifest URL pattern

`BuildZipUrl()` and `BuildManifestUrl()` produce:

```
GET {URL_BASE}/zips/{name}.zip               → the package payload
GET {URL_BASE}/packages/{name}/manifest.install   → file list for clean uninstall
```

`{name}` is the `name` field from the catalog entry (e.g. `qos-cfw`).

### 1.3 Accepted file formats by surface

| Sphaira surface | Accepts | Used for |
|---|---|---|
| **Appstore tab** | `.zip` (extracted to `sd:/`, governed by `manifest.install`) | Homebrew + system overrides + raw `atmosphere/`/`bootloader/` trees |
| **File browser → install** | `.nsp`, `.xci`, `.nsz`, `.xcz` | Title installs (eShop-format apps) |
| **File browser → run** | `.nro` | Standalone homebrew |

The appstore path is the only one that ships a `bootloader/` + `atmosphere/`
+ `switch/` tree atomically. NSP installer is irrelevant for a CFW pack — it
installs licensed titles, not exefs overrides.

### 1.4 Side-loadable JSON repo URL — DOES NOT EXIST

`URL_JSON` is a `constexpr` literal, not config-loaded. Sphaira cannot be
pointed at a self-hosted catalog without a source patch. **This is the single
most important constraint in this plan.**

---

## 2. Q OS release artifact format

Two artifact strategies. We ship both; the catalog entry chooses one.

### 2.1 `qos-umenu.zip` — uMenu overlay only (already specified)

Per `HBAPPSTORE-MANIFEST.md` §2 + `SPHAIRA-CATALOG.md` §3. Layers over an
existing Atmosphère install. No Atmosphère, no Hekate, no sysmodules. ~5 MiB.

### 2.2 `qos-cfw.zip` — full CFW pack (this document's focus)

HATS-equivalent all-in-one. Target SD layout after extraction (mirrors
`/Volumes/SWITCH SD/HATS_VERSION.txt` reference pack 2026-02-01):

```
qos-cfw-v2.3.5.zip
├── HATS_VERSION.txt → QOS_VERSION.txt   ← human-readable manifest
├── atmosphere/
│   ├── package3
│   ├── stratosphere.romfs
│   ├── reboot_payload.bin
│   ├── hbl.nsp
│   ├── exefs_patches/                   ← (ours: any custom patches)
│   ├── kip_patches/                     ← loader.kip + fs_mitm patches
│   ├── contents/
│   │   ├── 0100000000000352/             ← sys-clk    (charlesthobe)
│   │   ├── 0100000000000353/             ← sys-patch  (impeeza)  (sigpatches mod)
│   │   ├── 0100000000000354/             ← sys-con    (cathery)
│   │   ├── 010000000000000d/             ← nx-ovlloader+ (ppkantorski)
│   │   ├── 4200000000000010/             ← ovlreloader (RetroNX-Team)
│   │   └── 0100000000001000/             ← uSystem qlaunch override
│   │       └── exefs.nsp
│   ├── config/                           ← override.ini + system_settings.ini
│   └── hosts/                            ← 90DNS or empty
├── bootloader/
│   ├── hekate_ipl.ini                    ← Q OS-themed launch entries
│   ├── res/                              ← Hekate icons + bg
│   ├── sys/                              ← weft.bin, nyx.bin
│   ├── update.bin                        ← Hekate auto-update payload
│   ├── patches.ini                       ← warmboot patches
│   └── payloads/                         ← optional: Lockpick_RCM, etc.
├── switch/                               ← Homebrew Menu apps
│   ├── uManager.nro                      ← installer/uninstaller (user-facing)
│   ├── Sphaira.nro                       ← (NOT redistributed; user fetches)
│   ├── nx-shell.nro
│   └── Tinwoo.nro                        ← optional
├── ulaunch/
│   ├── bin/uMenu/{main, main.npdm, romfs.bin}
│   ├── bin/uLoader/{applet, application}/{main, main.npdm}
│   ├── bin/uSystem/exefs.nsp
│   ├── lang/{uMenu, uManager}/*.json
│   └── themes/                            ← default-theme-music-vX.Y.Z.ultheme
├── config/
│   └── sphaira/                           ← optional default Sphaira config
├── CREDITS.md                             ← attribution loveletter (full text)
└── LICENSE                                ← GPLv2 full text (mandatory under §6)
```

### 2.3 Layered overlay vs. full pack — ship both

| Mode | Pros | Cons | Use when |
|---|---|---|---|
| **Overlay** (`qos-umenu.zip`) | Small, no licensing risk on bootloader payloads | User must already have CFW | User has Atmosphère installed |
| **Full pack** (`qos-cfw.zip`) | One-tap from stock-modded state | Heavy, redistributes Atmosphère + Hekate (GPLv2 — fine, §6) | First-time installs |

Two catalog entries: `qos-umenu` (overlay) and `qos-cfw` (full). User picks.

### 2.4 What Atmosphère expects to find

The `0100000000001000` exefs override is the qlaunch hook (uSystem replaces
the system applet). uMenu and uLoader binaries are loaded by uSystem at
runtime from `sd:/ulaunch/bin/`. Path layout matches `SdOut/` from
`make package` (`src/Makefile` lines 8–9, 95–106).

---

## 3. Versioning strategy

### 3.1 Semver mapping

| Component | Source of truth | Current |
|---|---|---|
| Q OS uMenu (uLaunch fork) | `src/Makefile` `VERSION_MAJOR.MINOR.MICRO` | 2.3.4 (wire) / 2.3.5 (target) |
| Q OS CFW pack | This doc proposes `pack/VERSION` file at repo root | TBD |
| Atmosphère | Vendored under `vendor/atmosphere` (or pinned tag) | Pin at release time |
| Hekate-ext | Pinned tag in CI workflow | Pin at release time |

`qos-cfw` follows the uMenu version (single Q OS release tag covers both).
v2.3.5 is the first public CFW pack.

### 3.2 Sphaira/libget manifest `version` field

Sphaira shows `entry.version` in the appstore tile and uses it for "update
available" comparison against the cached install. Format: bare semver string
(`"2.3.5"`), no `v` prefix. Same convention as `BootSoundNX`, `Goldleaf` on
the live CDN.

### 3.3 Compatibility matrix (advertise in `details` field)

| Q OS pack version | Switch firmware | Atmosphère version | Hekate version |
|---|---|---|---|
| 2.3.5 | up to 21.2.0 | 1.10.2 | ext-V6.5.1 |

Firmware ceiling: pin to whatever Atmosphère's `target_firmware` supports.
Update on every release. Mirrors HATS pack convention (HATS_VERSION.txt
header line).

---

## 4. Public hosting / index registration

Three options. Recommendation last.

### 4.1 Option A — PR to ForTheUsers CDN (canonical)

- **Action:** PR to `fortheusers/hb-appstore` or web form at
  `https://hb-app.store/submit-or-request`. Per `HBAPPSTORE-MANIFEST.md` §5.
- **Pros:** Zero user friction. Q OS appears in Sphaira's appstore on next
  refresh after CDN ingest. Already the documented path for `qos-umenu`.
- **Cons:** Subject to CDN maintainer review. Heavy `qos-cfw.zip` (CFW packs
  may be ~100 MiB+) may be rejected — the CDN is sized for homebrew, not
  full CFW packs. **Verify with maintainers before assuming acceptance.**
- **Status:** Recommended for `qos-umenu` (overlay, ~5 MiB). For `qos-cfw`,
  ask first.

### 4.2 Option B — Self-hosted JSON repo — NOT VIABLE

Sphaira's `URL_JSON` is hardcoded (§1.4). Would require a Sphaira fork.
Out of scope.

### 4.3 Option C — Standalone bootstrap NRO

- **Action:** Ship `qos-installer.nro` users put in `sd:/switch/`. Run it
  from Hbmenu → it pulls latest `qos-cfw.zip` from a known GitHub Release
  and extracts to SD.
- **Pros:** Fully under Q OS control. Bypasses CDN size limits. Allows
  bundling sigpatches (which CDN won't host — see §6.3) under user-consent
  prompts. Self-update path lives in the NRO.
- **Cons:** User has to side-load the NRO once first. Not "in Sphaira" — it
  bootstraps independently of Sphaira.

### 4.4 Recommendation

**Hybrid: A + C.**

1. `qos-umenu` (overlay, ~5 MiB) → Option A (ForTheUsers CDN). Sphaira shows it.
2. `qos-cfw` (full pack, ~100 MiB+, plus sigpatches) → Option C (bootstrap NRO
   off GitHub Releases). Reason: CDN size + sigpatches DMCA risk (§6.3) make
   A unsuitable. NRO can prompt the user about sigpatches consent before
   downloading.
3. List `qos-installer` itself in the appstore (Option A) as the entrypoint
   for Option C. Then "find Q OS in Sphaira's appstore → one-tap install"
   still works for full CFW pack — the one tap installs the bootstrap NRO,
   and the user runs it from Hbmenu.

This keeps the Sphaira-discoverability story intact without forcing the
heavyweight pack through someone else's CDN.

---

## 5. Build pipeline

Reuses `make package` target in `src/Makefile` (verified line 103: `package:
arc usystem uloader umenu umanager default-theme-music`). Output already
correct: `qos-umenu-v$(VERSION).zip` + `.7z`.

### 5.1 Existing CI

`/Users/astral/QOS/.github/workflows/switch-builds.yml` builds Rust components
(`qos.bin`, mock NROs). Does **not** build the uLaunch fork. We add a new
workflow.

### 5.2 New workflow: `qos-cfw-release.yml`

Triggers: `push` of tag `qos-v*`.

```
jobs:
  build-umenu:
    runs-on: ubuntu-latest
    container: devkitpro/devkita64:latest
    steps:
      - checkout
      - make -C tools/qos-ulaunch-fork/src package
      - upload-artifact: qos-umenu-vX.Y.Z.zip + .7z

  assemble-cfw-pack:
    needs: build-umenu
    runs-on: ubuntu-latest
    steps:
      - download-artifact qos-umenu zip
      - download Atmosphère release (pinned tag) → vendor/atmosphere
      - download Hekate-ext release (pinned tag) → vendor/hekate-ext
      - download sysmodule releases (sys-clk, sys-patch, sys-con, nx-ovlloader+,
        ovlreloader) — pinned tags
      - run scripts/assemble-cfw-pack.sh → produces qos-cfw-vX.Y.Z.zip
      - generate manifest.install (file list for Sphaira clean uninstall)
      - generate info.json (catalog entry per HBAPPSTORE-MANIFEST.md §1, but
        with name='qos-cfw' for the full pack)

  build-installer-nro:
    runs-on: ubuntu-latest
    container: devkitpro/devkita64:latest
    steps:
      - build qos-installer NRO from src/projects/qos-installer/ (does not
        exist yet — see §8)
      - upload-artifact: qos-installer.nro

  publish-release:
    needs: [build-umenu, assemble-cfw-pack, build-installer-nro]
    runs-on: ubuntu-latest
    steps:
      - gh release create $TAG \
          qos-umenu-vX.Y.Z.zip qos-umenu-vX.Y.Z.7z \
          qos-cfw-vX.Y.Z.zip qos-cfw-vX.Y.Z.7z \
          qos-installer.nro \
          info.json manifest.install
```

### 5.3 `manifest.install` generation

Per Sphaira's `BuildManifestUrl()`: a list of every file the zip extracts,
one path per line, used for clean uninstall. Generate via:

```
unzip -l qos-cfw-vX.Y.Z.zip | awk '/^[ ]/{print $4}' > manifest.install
```

(Exact awk column depends on `unzip -l` output format on the runner.)

### 5.4 Repo to push to — PENDING DECISION

Flag: **Jamesmykil to decide.** Options:

- `Jmesmykil/QOS` (current monorepo) — release tag `qos-cfw-v2.3.5`.
- `Jmesmykil/qos-cfw` (new repo) — separates CFW pack release artifacts
  from monorepo source pushes.

Do not pick. Once decided, fill the gh CLI invocation in §5.2 and CDN
submission URL in §4.1.

---

## 6. Distribution legality / scope

### 6.1 License inventory of redistributed components

| Component | License | Source URL on GitHub | Redist OK? |
|---|---|---|---|
| Atmosphère | GPLv2 | Atmosphere-NX/Atmosphere | YES (with LICENSE) |
| Hekate-ext | GPLv2 | sthetix/hekate-ext | YES (with LICENSE) |
| uLaunch fork (this repo) | GPLv2 | Jmesmykil/QOS | YES (with LICENSE — `src/LICENSE` exists, top-level LICENSE missing — see §8) |
| sys-clk | GPLv3 | charlesthobe/sys-clk | YES |
| sys-patch | GPLv3 | impeeza/sys-patch | YES |
| sys-con | GPLv2 | cathery/sys-con | YES |
| nx-ovlloader+ | GPLv2 | ppkantorski/nx-ovlloader-plus | YES |
| ovlreloader | GPLv2 | RetroNX-Team/ovlreloader | YES |
| Plutonium / Atmosphere-libs | GPLv2 (statically linked into uMenu) | XorTroll/Plutonium, Atmosphere-NX/Atmosphere-libs | propagates GPLv2 (already audited in `LICENSE-AUDIT.md`) |

### 6.2 Attribution required (per `LICENSE-AUDIT.md` + `UPSTREAM-ATTRIBUTION-COMMIT.md`)

Ship GPLv2 LICENSE in zip root, `CREDITS.md` listing every component +
author + license + upstream URL, and keep upstream copyright headers intact.

### 6.3 Files we MUST NOT redistribute

- **Nintendo signed binaries** — any `.nca`, `.tik`, `.cert`, `prodinfo`,
  Horizon firmware files. Out of scope; we are not a system updater.
- **Sigpatches IPS files** — `atmosphere/exefs_patches/es_*.ips`,
  `loader_*.ips`, `fs_*.ips` from sources like `ITotalJustice/patches` or
  derivatives. The original `ITotalJustice/patches` repo was DMCA-takedown
  removed in 2024. Current providers (NexlifyHub/SystemEnhancer in HATS,
  impeeza's mirrors) operate at varying risk. Q OS should not host these in
  the CFW pack zip on GitHub Releases — the takedown precedent applies to
  Q OS the same way.

### 6.4 Sigpatches handling — RECOMMENDATION

Do not bundle sigpatches in `qos-cfw.zip` on GitHub or on ForTheUsers CDN.
Instead, the bootstrap NRO (§4.3) prompts the user, then on consent fetches
from a user-configurable URL set in `sd:/switch/qos-installer/config.ini`
(default: a known community provider, swappable when a provider is taken
down). The Q OS pack stays clean. This is what AIO-Switch-Updater and
HATS-Tools already do — HATS lists "Sigpatches 1.9.8 - NexlifyHub/SystemEnhancer"
as a fetched component, not a baked-in file.

---

## 7. Smoke test plan

User-visible verification after Sphaira install. Each test is one user
action.

| Test | Expected outcome | Failure mode |
|---|---|---|
| **T1 boot counter** | `sd:/qos/bootseq.log` line count increments by 1 on next reboot | If file missing or no increment, exefs override didn't take. |
| **T2 uMenu opens** | Switch boots from Hekate → Atmosphère → uMenu desktop visible at 1920×1080 with QDESKTOP_MODE | If stock HOME Menu shows, exefs.nsp not loaded. |
| **T3 uManager NRO runs** | `Hbmenu → uManager.nro` shows version `2.3.5` and "Q OS uMenu installed" status | If "Not installed", paths wrong. |
| **T4 Sphaira self-update** | After Q OS install, open Sphaira → check for updates → Sphaira itself can still self-update | If broken, our pack stomped sphaira's overlay path. |
| **T5 Hbmenu fallback** | Hold the configured key combo at boot → returns to vanilla Hbmenu without Q OS uMenu | If frozen, we broke the safe-return path. |
| **T6 stock return** | Hekate Nyx → Launch → Stock — boots vanilla Horizon | If Atmosphère still active in stock launch, bootloader config wrong. |
| **T7 uninstall via Sphaira** | Sphaira appstore → Q OS → Remove → reboot → stock HOME Menu | If files remain, manifest.install incomplete. |

T1–T3 are minimum acceptance. T4 is the regression check (don't break
Sphaira). T5–T7 are recovery / no-brick checks.

---

## 8. Dependencies on existing work

Items the user already flagged. Not blocking the plan but called out:

| Item | Status | Plan dependency |
|---|---|---|
| v2.3.5 stable boot confirmed | DONE per user | None. v2.3.5 is the first public release version. |
| Album → Photos fix | not done | Should land before public smoke test (T2 reveals the regression). |
| Switch glyph fix | not done | Cosmetic — does not block T1–T7. Can ship 2.3.5 with glyph note in changelog. |
| Settings system applet launch path | not done | Affects T2 if user opens Settings from uMenu. Same as glyph: changelog note acceptable. |
| Repo public visibility | private (per `PUBLIC-RELEASE-CHECKLIST.md` §3.1) | HARD blocker for §4.1 (CDN won't accept private source URL). |
| Top-level `LICENSE` file | missing (only `src/LICENSE` exists) | Add `LICENSE` at `/Users/astral/QOS/tools/qos-ulaunch-fork/LICENSE` (symlink or copy of `src/LICENSE`) before tagging release. Required by §6.2. |
| `qos-installer` NRO source | not yet written | Required for §4.4 Option C. New crate under `src/projects/qos-installer/`. Out of scope for this plan; flag as next phase. |
| Top-level CFW pack assembly script | not yet written | `scripts/assemble-cfw-pack.sh` referenced in §5.2. Out of scope for this plan; flag as next phase. |

---

## Next concrete action

**Jamesmykil chooses the release repo** (`Jmesmykil/QOS` vs new `Jmesmykil/qos-cfw`)
**and confirms Option A+C hybrid (§4.4).** Once those two decisions are made,
the next agent task is to write `scripts/assemble-cfw-pack.sh` and the
`qos-cfw-release.yml` GitHub Actions workflow per §5.2.
