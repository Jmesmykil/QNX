# Public Release Checklist — Q OS uMenu

> **Purpose:** Ordered pre-flight checklist for the public hard-fork release of Q OS uMenu.
> Every item is concrete. Every item names who/what executes it and where the artifact lives.
> Zero items are marked TODO — each is either complete, blocked (see "Known blockers"), or
> actionable with a specific command/path.
>
> Reference docs:
> - `docs/UPSTREAM-ATTRIBUTION-COMMIT.md` — attribution commit body
> - `docs/HBAPPSTORE-MANIFEST.md` — hb-appstore submission package
> - `docs/SPHAIRA-CATALOG.md` — Sphaira catalog (piggybacks on hb-appstore submission)
> - `docs/QOS-REBRAND-ASSET-INVENTORY.md` — full asset replacement scope
> - `docs/LICENSE-AUDIT.md` — license resolution (GPLv2, personal-use, creator-confirmed)
>
> Generated: 2026-04-25T14:00:00Z

---

## Phase 0 — Blockers (resolve before anything else)

See "Known Blockers" section at the end of this document.
All Phase 0 items must resolve before Phase 1 begins.

---

## Phase 1 — Legal & Attribution

- [ ] **1.1 License audit final**
  - Who: Human creator (review) + agent (verify file presence)
  - What: Confirm `src/LICENSE` at repo root is unmodified GPLv2 full text (no edits, no
    additional clauses). Verify with:
    `md5sum /Users/nsa/QOS/tools/qos-ulaunch-fork/src/LICENSE`
    Compare against canonical GPLv2 MD5: `b234ee4d69f5fce4486a80fdaf4a4263`.
  - Status: Structurally complete per `LICENSE-AUDIT.md` (creator-confirmed 2026-04-18).
    Run final verification before tag.

- [ ] **1.2 Attribution commit landed**
  - Who: Human creator (git commit) — per R1, agents do not commit without explicit request.
  - What: Commit message body is fully drafted in `docs/UPSTREAM-ATTRIBUTION-COMMIT.md`.
    Branch must be the public-fork branch. The commit subject is:
    `Hard-fork attribution: credit to uLaunch, Plutonium, Atmosphère, libnx, devkitPro`
  - Pre-commit checklist inside `UPSTREAM-ATTRIBUTION-COMMIT.md` must be fully checked first:
    items 1–7 (LICENSE file, LICENSE-AUDIT.md, asset license file, name spellings,
    copyright headers in Q OS delta files, CONTRIBUTING.md, README.md Origin section).
  - Status: Draft written 2026-04-25T14:00:00Z. Not yet committed. Blocked on items 1.3–1.4.

- [ ] **1.3 Asset license file created under `assets/`**
  - Who: Agent (write file) + human creator (confirm license choice: CC-BY-SA-4.0 or
    All Rights Reserved for Q OS original art)
  - What: Create `assets/LICENSE-ASSETS.md` documenting:
    - Q OS original art assets (post-rebrand pass) → creator-chosen license
    - Upstream art assets retained in commit history → GPL-2.0-only (XorTroll)
  - Path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/assets/LICENSE-ASSETS.md`
  - Status: Not yet created. Blocked on creator confirming art license choice.

- [ ] **1.4 Copyright headers added to Q OS delta files**
  - Who: Agent (edit files in `src/`)
  - What: All files under `src/projects/uMenu/source/qd_*` and any other Q OS-authored files
    must have a header: `// Copyright (C) 2026 Q OS uMenu maintainers — GPL-2.0-only`
  - Status: Not yet applied. This is an src/ edit — coordinate with Track A/B/E agents
    before touching `src/`.

---

## Phase 2 — Asset Rebrand

- [ ] **2.1 Q OS-original art assets replace all upstream PNGs (P1–P4)**
  - Who: Artist agent using ImageMagick / Photoshop MCP + human review
  - What: Replace all 56 PNGs listed in `docs/QOS-REBRAND-ASSET-INVENTORY.md`:
    - **P1 (5 hero assets) — ✅ DONE 2026-04-25T16:33:00Z** via ImageMagick 7.1.2-17.
      Background.png (17 284 B, 1920×1080), EntryMenuBackground.png (13 498 B, 1920×837),
      InputBarBackground.png (3 230 B, 1797×60), Cursor.png (11 494 B, 444×444),
      Selected.png (1 787 B, 416×416).
      Placed in both `src/default-theme/ui/` and `src/projects/uMenu/romfs/default/ui/`.
      Originals staged in `assets/qos-rebrand/`.
    - P2 (8 special icons) — Settings, Album, Themes, Controllers, MiiEdit, WebBrowser, Amiibo, Empty
    - P3 (defaults + chrome) — DefaultApplication, DefaultHomebrew, Folder, TopMenuBackground variants
    - P4 (status overlays) — Border, Suspended, Corrupted, Gamecard, NeedsUpdate, NotLaunchable, etc.
    - P5 (system symbols) — Battery/Connection icons: keep-or-rebuild decision by creator
    - P6 (Settings chrome) — Blocked on Track A promotion completing
  - Source path: `src/projects/uMenu/romfs/default/ui/` (read via Makefile `cp -r default-theme/`)
  - Drop new PNGs into `assets/qos-rebrand/` first; swap in a single atomic commit.
  - Status: P1 complete. P2–P4 not started. P6 blocked on Track A.

- [ ] **2.2 `icon.png` (256×256 PNG) for hb-appstore ready**
  - Who: Agent (sips convert) or Artist agent
  - What: Convert `assets/qos-icon-256.jpg` to PNG and place as `assets/icon.png`:
    ```
    sips -s format png /Users/nsa/QOS/tools/qos-ulaunch-fork/assets/qos-icon-256.jpg \
         --out /Users/nsa/QOS/tools/qos-ulaunch-fork/assets/icon.png
    ```
    Verify: `sips -g pixelWidth -g pixelHeight -g format assets/icon.png`
    Expected: 256×256, format png.
    If a Q OS-rebranded icon is generated in 2.1, use that instead.
  - Status: Source JPEG exists (256×256, 10 KB). PNG conversion not yet done.

- [ ] **2.3 Screenshot `screen.png` (1280×720) captured and placed**
  - Who: Human creator (capture on hardware or emulator) + agent (resize if needed)
  - What: Boot Q OS uMenu on Switch (or Ryujinx), screenshot the desktop in QDESKTOP_MODE.
    Resize to 1280×720 if at 1920×1080:
    ```
    sips -z 720 1280 /path/to/screenshot.png \
         --out /Users/nsa/QOS/tools/qos-ulaunch-fork/assets/screen.png
    ```
    Place at `assets/screen.png`. This file is not inside the ZIP — it is served from the CDN.
  - Status: Not yet captured.

---

## Phase 3 — Repo Visibility

- [ ] **3.1 Repo visibility flipped public**
  - Who: Human creator (repo owner — only the owner can change visibility)
  - What (CLI — preferred): Run as the repo owner:
    ```
    gh repo edit Jmesmykil/QOS --visibility public --accept-visibility-change-consequences
    ```
    What (web UI — alternative): Go to https://github.com/Jmesmykil/QOS/settings →
    "Danger Zone" → "Change visibility" → Public.
  - Current state: **PRIVATE** (confirmed 2026-04-25T14:00:00Z via
    `gh repo view Jmesmykil/QOS --json isPrivate,name,url,visibility`
    returning `{"isPrivate":true,"name":"QOS","url":"https://github.com/Jmesmykil/QOS","visibility":"PRIVATE"}`).
    This is a hard blocker — hb-appstore will reject a submission linking to a private repo.
    The ForTheUsers CDN maintainers require a publicly accessible GPLv2 source URL.
  - DO NOT execute the flip command above — creator-only action.
  - Verify after flip:
    `gh repo view Jmesmykil/QOS --json isPrivate,url`
    Expected: `{"isPrivate":false,"url":"https://github.com/Jmesmykil/QOS"}`

---

## Phase 4 — Documentation

- [ ] **4.1 Install / uninstall / recovery docs added to README.md**
  - Who: Agent (write to `README.md` at repo root)
  - What: Add "Installation", "Uninstallation", and "Safe Return" sections:
    - Installation: Atmosphère requirement, Sphaira/hb-appstore one-click, or manual ZIP extraction + reboot.
    - Uninstallation: Delete `sd:/atmosphere/contents/0100000000001000/` and `sd:/ulaunch/`, reboot.
    - Safe return: Holding the correct button combination at Hekate boot to skip Atmosphère and boot stock (per Hekate docs), or booting stock from Nyx's multi-boot menu.
  - Path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/README.md`
  - Status: Not yet added. `README.md` exists but contains no install/uninstall section.

- [ ] **4.2 CONTRIBUTING.md created**
  - Who: Agent (write file)
  - What: Two-section file: (1) Framework-level changes → contribute to upstream uLaunch at
    https://github.com/XorTroll/uLaunch. (2) Desktop-layer changes (QDESKTOP_MODE, qd_*) →
    contribute to this fork at https://github.com/Jmesmykil/QOS.
  - Path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/CONTRIBUTING.md`
  - Status: Not yet created.

---

## Phase 5 — Packaging Script

- [x] **5.1 Makefile `package` target renamed to produce `qos-umenu-vX.Y.Z.zip`**
  - Who: Agent (edit `src/Makefile`)
  - What: Line 9 of `src/Makefile` changed:
    Before: `OUT_DIR_ZIP := uLaunch-v$(VERSION)`
    After:  `OUT_DIR_ZIP := qos-umenu-v$(VERSION)`
    This renames both the `.7z` and `.zip` outputs without changing build logic.
    `OUT_DIR` (`SdOut`) is untouched — that is the staging directory name, not a release artifact.
  - Path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/Makefile` line 9
  - Status: ✅ Applied 2026-04-25. Confirmed via `grep OUT_DIR_ZIP src/Makefile`.

- [ ] **5.2 `info.json` placed at package root for `repogen.py`**
  - Who: Agent (write file)
  - What: Place the `info.json` from `docs/HBAPPSTORE-MANIFEST.md` section 1 at:
    `/Users/nsa/QOS/tools/qos-ulaunch-fork/info.json`
    (repo root, alongside the ZIP — this is where `repogen.py` expects it)
  - Status: Not yet created.

- [ ] **5.3 `CREDITS.md` added to ZIP root (required by `UPSTREAM-ATTRIBUTION-COMMIT.md`)**
  - Who: Agent (write file, then update Makefile `package` target to include it)
  - What: Create `CREDITS.md` at `src/CREDITS.md` containing the attribution commit body
    from `docs/UPSTREAM-ATTRIBUTION-COMMIT.md`. Add to Makefile `package` target:
    `@cp CREDITS.md $(OUT_DIR)/CREDITS.md`
    so it lands at the root of the ZIP.
  - Path: `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/CREDITS.md`
  - Status: Not yet created. Coordinate with src agents.

---

## Phase 6 — GitHub Release

- [ ] **6.1 Release branch/tag created**
  - Who: Human creator (git tag) — per R1
  - What:
    ```
    git -C /Users/nsa/QOS/tools/qos-ulaunch-fork tag -a qos-umenu-v1.2.3 \
        -m "Q OS uMenu v1.2.3 — public hard-fork release"
    git -C /Users/nsa/QOS/tools/qos-ulaunch-fork push origin qos-umenu-v1.2.3
    ```
  - Status: Not yet tagged. Blocked on all Phase 1–5 items.

- [ ] **6.2 Build artifacts produced**
  - Who: Agent or CI (run `make -C /Users/nsa/QOS/tools/qos-ulaunch-fork/src package`)
  - What: Produces `qos-umenu-v1.2.3.zip` (and `.7z`) in `src/`. Requires devkitPro/devkitA64
    toolchain installed. Build environment check:
    `which aarch64-none-elf-gcc`
    If absent: install devkitPro per https://devkitpro.org/wiki/Getting_Started.
  - Status: Last known build: `qos-ulaunch-v0.2.0.nsp` (present at repo root, 2026-04-18).
    Full ZIP package not yet produced under the Q OS name.

- [ ] **6.3 GitHub Release created with ZIP uploaded**
  - Who: Agent (gh CLI)
  - What:
    ```
    gh release create qos-umenu-v1.2.3 \
        /Users/nsa/QOS/tools/qos-ulaunch-fork/src/qos-umenu-v1.2.3.zip \
        --repo Jmesmykil/QOS \
        --title "Q OS uMenu v1.2.3" \
        --notes-file /Users/nsa/QOS/tools/qos-ulaunch-fork/docs/UPSTREAM-ATTRIBUTION-COMMIT.md
    ```
    Also attach `qos-umenu-v1.2.3.7z` for users who prefer 7-zip.
  - Status: Not yet. Blocked on 6.1 + 6.2.

---

## Phase 7 — Store Submissions

- [ ] **7.1 hb-appstore PR opened**
  - Who: Human creator (GitHub PR) or agent (gh CLI)
  - What: Open a PR or submit via web form at `https://hb-app.store/submit-or-request`.
    PR template is in `docs/HBAPPSTORE-MANIFEST.md` section 5.
    Attach: `icon.png` (256×256 PNG), `screen.png` (1280×720 PNG), `qos-umenu-v1.2.3.zip`,
    `info.json` (from `docs/HBAPPSTORE-MANIFEST.md` section 1).
    Link: https://github.com/Jmesmykil/QOS/releases/tag/qos-umenu-v1.2.3
  - Status: Not yet. Blocked on Phase 3 (repo public) + Phase 6 (release tagged + ZIP uploaded).
  - Record PR URL here when opened: ___________________________________

- [ ] **7.2 Sphaira — no separate submission needed**
  - Who: N/A
  - What: Per `docs/SPHAIRA-CATALOG.md` section 4: Sphaira reads the same ForTheUsers CDN
    (`https://switch.cdn.fortheusers.org/repo.json`). Once hb-appstore PR is accepted and CDN
    is updated, Q OS uMenu automatically appears in Sphaira's App Store tab on next catalog refresh.
  - Status: Automatic after 7.1 lands.

---

## Phase 8 — Verification

- [ ] **8.1 Smoke-test fresh install on a clean SD card**
  - Who: Human creator (hardware test on Switch)
  - What: On a second SD card (or after backing up the current one):
    1. Boot stock Switch with Atmosphère only (no ulaunch/ dir, no contents/0100000000001000/).
    2. Launch hb-appstore or Sphaira.
    3. Find Q OS uMenu in the catalog, install.
    4. Reboot.
    5. Confirm qlaunch is replaced: Switch boots to Q OS uMenu desktop, not stock HOME Menu.
    6. Run `uManager.nro` — confirm version string `v1.2.3` is shown.
    7. Uninstall via uManager or Sphaira "Remove". Reboot. Confirm stock HOME Menu returns.
  - Status: Not yet. Blocked on all preceding phases.

- [ ] **8.2 Recovery path verified**
  - Who: Human creator (hardware test)
  - What: With Q OS uMenu installed, confirm safe-return works:
    - Hekate Nyx multi-boot menu appears at power-on (HOME + VOL+ at boot).
    - Stock Horizon boots correctly from Nyx "Launch → Stock".
    - No brick scenario: if uMenu fails to boot, Hekate catches the reboot loop.
  - Status: Not yet. Part of fresh-install smoke-test.

---

## Known Blockers

> These are confirmed blockers found during the pre-flight scan on 2026-04-25.
> Each is a hard gate — nothing in Phases 3–8 can proceed past it.

### BLOCKER 1 — Repo is private (HARD BLOCKER for all store submissions)

- **Finding:** `gh repo view Jmesmykil/QOS --json isPrivate` returns `{"isPrivate":true}`.
- **Impact:** hb-appstore and Sphaira both link to the source repo in the package entry.
  The ForTheUsers CDN maintainers will reject a submission with a private source URL.
  Users cannot verify the GPLv2 source requirement with a private repo.
- **Fix:** Human creator flips visibility at https://github.com/Jmesmykil/QOS/settings.
  Command to verify after: `gh repo view Jmesmykil/QOS --json isPrivate`
- **Checklist item:** Phase 3, item 3.1.

### BLOCKER 2 — Art rebrand P1 ✅ resolved; P2–P4 remain (softer blocker)

- **Finding (original):** All six priority groups not started as of 2026-04-25T14:00:00Z.
- **Update 2026-04-25T16:33:00Z:** P1 (5 hero assets — Background, EntryMenuBackground,
  InputBarBackground, Cursor, Selected) replaced with Q OS originals via ImageMagick 7.1.2-17.
  Assets are in `assets/qos-rebrand/` and live in both `src/default-theme/ui/` and
  `src/projects/uMenu/romfs/default/ui/`. P2–P4 (icons, chrome, status overlays) remain upstream.
- **Impact (remaining):** P2–P4 are brand-confusion risk but are not hard submission blockers.
  P6 (Settings chrome) remains blocked on Track A. P1 was the most-visible blocker — resolved.
- **Fix (remaining):** Agent continues P2–P4 pass per `QOS-REBRAND-ASSET-INVENTORY.md` workflow.
- **Checklist item:** Phase 2, items 2.1–2.3.

### BLOCKER 3 — ✅ RESOLVED 2026-04-25T16:33:00Z

- **Finding:** `src/Makefile` line 9 was: `OUT_DIR_ZIP := uLaunch-v$(VERSION)`.
- **Fix applied:** Changed to `OUT_DIR_ZIP := qos-umenu-v$(VERSION)` (line 9 only).
  `OUT_DIR` (`SdOut`) is untouched. Build logic unchanged.
  Next `make package` will produce `qos-umenu-v1.2.3.zip` and `qos-umenu-v1.2.3.7z`.
- **Checklist item:** Phase 5, item 5.1 — ✅ marked complete.

### Non-blocking observation — `icon.png` is JPEG, not PNG

- **Finding:** `assets/qos-icon-256.jpg` is a JPEG file (confirmed via `file` command).
  The hb-appstore CDN serves `icon.png` — the file must be PNG format, not JPEG.
- **Impact:** Not a hard blocker (conversion is a one-liner), but submitting a JPEG named
  `icon.png` will render incorrectly or be rejected by the CDN processing pipeline.
- **Fix:** `sips -s format png assets/qos-icon-256.jpg --out assets/icon.png`
- **Checklist item:** Phase 2, item 2.2.

---

Updated: 2026-04-25T16:33:00Z (Blockers 2-P1 + 3 resolved by agent pass)
