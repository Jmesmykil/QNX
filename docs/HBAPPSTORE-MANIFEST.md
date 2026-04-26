# hb-appstore Manifest — Q OS uMenu

> **Purpose:** Complete submission package for the ForTheUsers Homebrew App Store (hb-appstore).
> The hb-appstore uses the `libget` repo format. The live Switch catalog is served from
> `https://switch.cdn.fortheusers.org/repo.json`. Submissions are made by PR to the
> `fortheusers/hb-appstore` repository or via the web form at `https://hb-app.store/submit-or-request`.
>
> **Format source:** Live CDN `https://switch.cdn.fortheusers.org/repo.json` field survey
> (2026-04-25) + `fortheusers/hb-appstore` `gui/AppList.cpp` category enum survey +
> `vgmoose/get` `web/repogen.py` field extraction.
>
> **Version targeted:** Q OS uMenu v1.2.3 (Makefile `VERSION_MAJOR=1 VERSION_MINOR=2 VERSION_MICRO=3`).
> Update version string for every release before submitting.
>
> Generated: 2026-04-25T14:00:00Z

---

## 1. Complete `info.json` manifest body

This file lives at the root of the package directory processed by `repogen.py`.
It is **not** included inside the ZIP — it is read by the repo generator to populate `repo.json`.

```json
{
  "name": "qos-umenu",
  "title": "Q OS uMenu",
  "author": "Q OS uMenu maintainers",
  "category": "advanced",
  "version": "1.2.3",
  "description": "Q OS desktop shell — hard fork of uLaunch with 1920×1080 desktop layout, auto-grid icons, dock, and layered top bar.",
  "details": "Q OS uMenu is a hard fork of XorTroll's uLaunch (GPL-2.0-only). It replaces the Nintendo Switch qlaunch system applet (TID 0x0100000000001000) via Atmosphère's exefs override mechanism.\n\nQ OS adds QDESKTOP_MODE: a 1920×1080 desktop layout with auto-grid application icons, a 6-slot dock, a layered top bar, login screen, and Q OS-branded special-icon dispatch.\n\nDirect upstream lineage: uLaunch (XorTroll/Stary2001, GPL-2.0-only) — the entire qlaunch-replacement architecture. Plutonium (XorTroll, GPL-2.0-only) — the UI framework. Atmosphère (Atmosphere-NX team) — the exefs override mechanism. libnx (devkitPro/switchbrew, ISC) — system bindings. This is a loveletter to all of them.\n\nInstallation requires Atmosphère 1.x+ on a modded Nintendo Switch. Run uManager.nro from the Homebrew Menu to install. Uninstall by deleting sd:/atmosphere/contents/0100000000001000/ and sd:/ulaunch/.",
  "changelog": "v1.2.3: Q OS desktop layout (QDESKTOP_MODE), auto-grid icon placement, 6-slot dock, layered top bar, login screen, Q OS-branded special icons. Upstream uLaunch v1.2.3 base.",
  "url": "https://github.com/Jmesmykil/QOS",
  "license": "GPL-2.0-only"
}
```

### Field-by-field justification

| Field | Value | Source |
|---|---|---|
| `name` | `qos-umenu` | Package identifier — no spaces. Follows `lowercasename` convention observed in CDN (e.g., `BootSoundNX` → `bootsoundnx`). `qos-umenu` is unambiguous and searchable. |
| `title` | `Q OS uMenu` | Human-readable display name. Matches the project's public brand. |
| `author` | `Q OS uMenu maintainers` | Creator attribution. Matches convention for fork maintainers. |
| `category` | `advanced` | Confirmed from live CDN: `advanced` is the category for system modules (e.g., `BootSoundNX` which replaces boot behavior). uMenu replaces qlaunch — same tier as system modules. Alternatives considered: `tool` (for utilities that run as NROs, not system replacements) — rejected. `advanced` is the correct category for an NSP-based system replacement. |
| `version` | `1.2.3` | From `src/Makefile` `VERSION_MAJOR=1 VERSION_MINOR=2 VERSION_MICRO=3`. Update this for every release. |
| `description` | (see above) | Single-line summary per libget convention. ≤160 chars. |
| `details` | (see above) | Extended package info. First paragraph is the attribution loveletter body from `docs/UPSTREAM-ATTRIBUTION-COMMIT.md`. |
| `changelog` | (see above) | Per-version change record. Expand with each release. |
| `url` | `https://github.com/Jmesmykil/QOS` | Project URL. Must be the public GitHub repo — flip visibility before submitting. |
| `license` | `GPL-2.0-only` | From `docs/LICENSE-AUDIT.md`: GPLv2-only inherited from uLaunch + Plutonium + Atmosphère. No upgrade path. |

### `binary` field (in `repo.json`, not `info.json`)

`repogen.py` auto-detects the binary by scanning the ZIP for `.nro` files and populating `binary`.
For Q OS uMenu, `uManager.nro` is the installer NRO located at `switch/uManager.nro` in the ZIP.
The binary field will therefore be: `"binary": "/switch/uManager.nro"`.

The `exefs.nsp` files are system overrides, not user-launchable binaries — uManager.nro is the
entry point the user launches from the Homebrew Menu to install/uninstall.

---

## 2. ZIP payload structure

The existing `package` Makefile target (line 95–99 of `src/Makefile`) produces:
`uLaunch-v1.2.3.zip` containing three top-level directories from `SdOut/`:

```
qos-umenu-v1.2.3.zip
├── atmosphere/
│   └── contents/
│       └── 0100000000001000/
│           └── exefs.nsp          ← uSystem NSP (qlaunch replacement, TID 0x0100000000001000)
├── ulaunch/
│   ├── bin/
│   │   ├── uMenu/
│   │   │   ├── main               ← uMenu NSO
│   │   │   ├── main.npdm
│   │   │   └── romfs.bin          ← theme assets, lang files, Logo.png
│   │   ├── uLoader/
│   │   │   ├── applet/
│   │   │   │   ├── main           ← uLoader NSO (applet mode)
│   │   │   │   └── main.npdm
│   │   │   └── application/
│   │   │       ├── main           ← uLoader NSO (application mode)
│   │   │       └── main.npdm
│   │   └── uSystem/
│   │       └── exefs.nsp          ← uSystem NSP copy (ulaunch-side reference)
│   ├── lang/
│   │   ├── uMenu/                 ← locale JSON files (en, de, fr, es, it, ko, pt-BR, …)
│   │   └── uManager/              ← locale JSON files for installer
│   └── themes/                    ← empty dir; user themes go here
└── switch/
    └── uManager.nro               ← installer / uninstaller NRO (user-facing entry point)
```

**Note:** The `package` target currently names the ZIP `uLaunch-v$(VERSION).zip`.
The checklist item "Packaging script" (see `PUBLIC-RELEASE-CHECKLIST.md`) requires renaming
the output to `qos-umenu-v$(VERSION).zip` before the GitHub Release upload.

**Files excluded from ZIP by `repogen.py` convention** (must be in package dir root, not inside ZIP):
- `icon.png` — 256×256 PNG icon (see section 3)
- `screen.png` — primary screenshot (see section 3)
- `screen1.png` … `screen5.png` — additional screenshots
- `info.json` — metadata file (this document's section 1)
- `manifest.install` — auto-generated by `repogen.py`

---

## 3. Required asset list

| Asset | Required size | Format | Notes |
|---|---|---|---|
| `icon.png` | 256×256 px | PNG | Square app icon. **Current state:** `assets/qos-icon-256.jpg` exists (256×256 JPEG). Must be converted to PNG and rebrand-complete before submission. |
| `screen.png` | No fixed spec in libget; 720p (1280×720) standard | PNG | Primary screenshot. Q OS uMenu at 1920×1080 on Switch — scale or crop to 720p. |
| `screen1.png` | same | PNG | Optional. Second screenshot. |

**Icon current status:** `assets/qos-icon-256.jpg` — 256×256 JPEG, 10 KB. This is a JPEG, not PNG.
Convert: `sips -s format png assets/qos-icon-256.jpg --out assets/icon.png`
Then verify the rebrand pass (P1–P2 from `QOS-REBRAND-ASSET-INVENTORY.md`) is complete
so the icon shown is Q OS-original art, not upstream uLaunch art.

**1080p → 720p screenshot:** Switch runs uMenu at 1920×1080. hb-appstore renders screenshots
at whatever size is served (no enforced dimension in the CDN). Serve at 1280×720 to match
the display resolution of non-docked Switch and keep file size reasonable.

---

## 4. CDN asset URL convention

Once the package is accepted into the ForTheUsers repo, assets are served at:

```
https://switch.cdn.fortheusers.org/packages/qos-umenu/icon.png
https://switch.cdn.fortheusers.org/packages/qos-umenu/screen.png
https://switch.cdn.fortheusers.org/zips/qos-umenu.zip
```

The `repo.json` entry will be appended at:

```
https://switch.cdn.fortheusers.org/repo.json  →  packages[].name == "qos-umenu"
```

---

## 5. PR workflow

**Target repo:** `fortheusers/hb-appstore`
(The submission form at `hb-app.store/submit-or-request` also accepts submissions.
A direct GitHub PR is the developer path and gives faster review.)

**What lands in the PR:**

1. A new package directory under the packages tree (exact path TBD by maintainers — the CDN
   is separately managed; the PR provides metadata + the maintainers upload to CDN).
2. Alternatively, fill the web form — it generates the PR automatically.

**Recommended PR description template:**

```
## New package: Q OS uMenu (qos-umenu)

Hard fork of XorTroll/uLaunch — replaces the Nintendo Switch qlaunch system applet
with a Q OS desktop layout (1920×1080 auto-grid, dock, layered top bar).

- License: GPL-2.0-only (inherited from upstream uLaunch)
- Category: advanced (system module — exefs.nsp override via Atmosphère)
- Binary: /switch/uManager.nro (installer, user-facing entry point)
- Upstream credit: XorTroll/uLaunch, XorTroll/Plutonium, Atmosphere-NX/Atmosphère,
  devkitPro/libnx, devkitPro (toolchain)
- Source: https://github.com/Jmesmykil/QOS
- Release: https://github.com/Jmesmykil/QOS/releases/tag/qos-umenu-v1.2.3

Attaching: icon.png (256×256), screen.png (1280×720), qos-umenu-v1.2.3.zip
```

---

## 6. Attribution paragraph (first paragraph of `details` field)

Per `docs/UPSTREAM-ATTRIBUTION-COMMIT.md` — this paragraph must appear as the opening of the
`details` field in `info.json` and in the PR description:

> Q OS uMenu is a hard fork of XorTroll's uLaunch (https://github.com/XorTroll/uLaunch,
> GPL-2.0-only). The entire qlaunch-replacement architecture, the SMI IPC pattern between
> uSystem and uMenu, the NRO-loader path, and the uManager installer all descend from
> XorTroll's work. The UI framework is Plutonium (XorTroll, GPL-2.0-only). The exefs
> override mechanism is Atmosphère (Atmosphere-NX team). libnx (devkitPro/switchbrew, ISC)
> provides the system bindings. Q OS adds QDESKTOP_MODE on top. This is a loveletter to
> all of them.

---

Updated: 2026-04-25T14:00:00Z
