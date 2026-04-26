# Sphaira Catalog Entry — Q OS uMenu

> **Purpose:** Submission details for the Sphaira homebrew launcher catalog.
> Sphaira (https://github.com/ITotalJustice/sphaira) is a modern alternative homebrew launcher
> for the Nintendo Switch that includes its own App Store tab. Its App Store tab contacts the
> same ForTheUsers CDN (`https://switch.cdn.fortheusers.org/repo.json`) used by hb-appstore.
>
> **Key finding from source analysis (2026-04-25):**
> `sphaira/source/ui/menus/appstore.cpp` fetches `https://switch.cdn.fortheusers.org/repo.json`
> as its catalog source. The `assets/romfs/github/sphaira.json` self-reference file contains
> only `{"url": "https://github.com/ITotalJustice/sphaira"}` — it is NOT a separate catalog
> format. Sphaira does not maintain an independent third-party catalog; it piggybacks on the
> ForTheUsers CDN.
>
> **Therefore:** Getting Q OS uMenu into Sphaira's App Store tab requires exactly the same
> submission as hb-appstore (section 5 of `HBAPPSTORE-MANIFEST.md`). There is no separate
> Sphaira catalog PR.
>
> Generated: 2026-04-25T14:00:00Z

---

## 1. Sphaira App Store mechanics

### How Sphaira fetches the catalog

From `sphaira/source/ui/menus/appstore.cpp` (surveyed 2026-04-25):

```
GET https://switch.cdn.fortheusers.org/repo.json
```

The response is the same `libget` repo.json served to hb-appstore. Sphaira deserializes the
`packages[]` array into its own `Entry` struct with these fields:

```
name, title, author, category, license, description, changelog,
version, url, binary, filesize, extracted, md5, screens,
updated, app_dls, details
```

This is identical to the libget/hb-appstore field set. No Sphaira-specific extension fields exist.

### Installation flow inside Sphaira

1. User opens Sphaira → "App Store" tab.
2. Sphaira fetches `repo.json` and renders the package list.
3. User selects Q OS uMenu → Sphaira downloads `https://switch.cdn.fortheusers.org/zips/qos-umenu.zip`.
4. Sphaira verifies MD5, parses `manifest.install`, and extracts the ZIP to `sd:/`.
5. The ZIP layout (section 2 of `HBAPPSTORE-MANIFEST.md`) places files at the correct paths:
   - `atmosphere/contents/0100000000001000/exefs.nsp` → qlaunch override
   - `switch/uManager.nro` → installer visible in Homebrew Menu
   - `ulaunch/bin/uMenu/…` → uMenu binaries
   - `ulaunch/bin/uLoader/…` → uLoader binaries

**Compatibility note:** Sphaira performs the same extraction as hb-appstore. No separate
installer invocation is needed for the ZIP extraction. However, the user MUST reboot after
Sphaira extracts the ZIP for Atmosphère to load the new `exefs.nsp` override. Running
`uManager.nro` is optional (it handles clean uninstall and per-entry management); basic
install via Sphaira ZIP extraction is sufficient for first-time setup.

---

## 2. Catalog entry JSON (as it will appear in repo.json)

This is the `repo.json` entry that the ForTheUsers CDN will serve after the hb-appstore
submission is accepted. Sphaira reads this verbatim.

```json
{
  "name": "qos-umenu",
  "title": "Q OS uMenu",
  "author": "Q OS uMenu maintainers",
  "category": "advanced",
  "version": "1.2.3",
  "license": "GPL-2.0-only",
  "description": "Q OS desktop shell — hard fork of uLaunch with 1920×1080 desktop layout, auto-grid icons, dock, and layered top bar.",
  "details": "Q OS uMenu is a hard fork of XorTroll's uLaunch (GPL-2.0-only). It replaces the Nintendo Switch qlaunch system applet (TID 0x0100000000001000) via Atmosphère's exefs override mechanism.\n\nQ OS adds QDESKTOP_MODE: a 1920×1080 desktop layout with auto-grid application icons, a 6-slot dock, a layered top bar, login screen, and Q OS-branded special-icon dispatch.\n\nDirect upstream lineage: uLaunch (XorTroll/Stary2001, GPL-2.0-only) — the entire qlaunch-replacement architecture. Plutonium (XorTroll, GPL-2.0-only) — the UI framework. Atmosphère (Atmosphere-NX team) — the exefs override mechanism. libnx (devkitPro/switchbrew, ISC) — system bindings. This is a loveletter to all of them.\n\nInstallation: run uManager.nro from the Homebrew Menu after extraction to complete setup. Reboot required. Uninstall: delete sd:/atmosphere/contents/0100000000001000/ and sd:/ulaunch/.",
  "changelog": "v1.2.3: Q OS desktop layout (QDESKTOP_MODE), auto-grid icon placement, 6-slot dock, layered top bar, login screen, Q OS-branded special icons. Upstream uLaunch v1.2.3 base.",
  "url": "https://github.com/Jmesmykil/QOS",
  "binary": "/switch/uManager.nro",
  "filesize": 0,
  "extracted": 0,
  "md5": "",
  "screens": 1,
  "updated": "",
  "app_dls": 0
}
```

> `filesize`, `extracted`, `md5`, `updated`, and `app_dls` are computed by `repogen.py` at
> CDN-side processing time. They are shown as 0/"" here to document structure; the actual
> values will be filled by the ForTheUsers repo maintainers after they process the ZIP.

---

## 3. GitHub Release ZIP layout (what Sphaira fetches)

Same as `HBAPPSTORE-MANIFEST.md` section 2. Reproduced here for Sphaira-specific context:

```
qos-umenu-v1.2.3.zip
├── atmosphere/
│   └── contents/
│       └── 0100000000001000/
│           └── exefs.nsp           ← extracted to sd:/atmosphere/contents/0100000000001000/exefs.nsp
├── ulaunch/
│   ├── bin/
│   │   ├── uMenu/
│   │   │   ├── main               ← sd:/ulaunch/bin/uMenu/main
│   │   │   ├── main.npdm          ← sd:/ulaunch/bin/uMenu/main.npdm
│   │   │   └── romfs.bin          ← sd:/ulaunch/bin/uMenu/romfs.bin
│   │   ├── uLoader/
│   │   │   ├── applet/main + main.npdm
│   │   │   └── application/main + main.npdm
│   │   └── uSystem/
│   │       └── exefs.nsp
│   ├── lang/uMenu/*.json
│   ├── lang/uManager/*.json
│   └── themes/
└── switch/
    └── uManager.nro                ← sd:/switch/uManager.nro (visible in Homebrew Menu)
```

Atmosphère intercepts the `0100000000001000` title launch and redirects to `exefs.nsp`.
`uLoader` and `uSystem` are pulled from `ulaunch/bin/` at runtime by uSystem itself.

---

## 4. Submission action required

**No separate Sphaira PR is needed.** The single action is:

1. Complete the hb-appstore submission per `HBAPPSTORE-MANIFEST.md` section 5.
2. Once the ForTheUsers CDN ingests the package, Sphaira users automatically see Q OS uMenu
   in their App Store tab on next catalog refresh (Sphaira re-fetches `repo.json` at app launch).

If Sphaira ever migrates to an independent catalog format, revisit this document. As of
2026-04-25 based on `appstore.cpp` source analysis, the ForTheUsers CDN is the sole source.

---

## 5. Compatibility notes

| Concern | Status |
|---|---|
| Does Sphaira install via its own installer or does the user invoke uManager? | Sphaira extracts the ZIP directly to SD. `uManager.nro` is then available at `sd:/switch/uManager.nro` for optional post-install management. Basic function (qlaunch override active after reboot) works without running uManager. |
| Reboot required after Sphaira install? | Yes. Atmosphère reads `exefs.nsp` overrides at boot time. |
| Safe-return / uninstall from Sphaira? | Sphaira's App Store tab has a "Remove" action that uses the `manifest.install` file to delete all extracted files. This restores qlaunch to stock. Reboot required. |
| Atmosphère version minimum? | Atmosphère 1.0.0+. Standard exefs override mechanism — no special compat needed. |
| Firmware compatibility? | Inherits from upstream uLaunch v1.2.3. Tested on firmware versions supported by that base. |

---

Updated: 2026-04-25T14:00:00Z
