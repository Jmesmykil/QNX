# HBMenu Removal Migration

Stage 4 of `docs/45_HBMenu_Replacement_Design.md`.

Q OS replaces HBMenu's NRO-launcher UI with the vault file browser. Once the vault
is installed, the standalone HBMenu surface is redundant and can be removed to
reclaim 6–17 MB and eliminate a competing launcher surface.

---

## When to run

Run `migrate.sh` after Q OS installation, ideally before the first boot. It is
safe to run at any time — on a powered-off card, on an already-booted system via
UMS mode, or from macOS/Linux with the SD card mounted.

---

## What it does

`migrate.sh` removes three assets that belong to the HBMenu user surface:

| Asset | Location | Approx size |
|---|---|---|
| Root NRO | `<sd>/hbmenu.nro` | 5–8 MB |
| Theme and language files | `<sd>/switch/hbmenu/` | 1–3 MB |
| Rare custom Album-applet override | `<sd>/atmosphere/contents/010000000000100B/exefs/main.nso` | 0–6 MB |

The script shows a dry-run preview of every file it will touch — and the exact
byte count — before doing anything. It requires `--yes` to proceed.

After removal it writes a timestamped log to
`<sd>/qos-shell/migrations/hbmenu-<YYYYMMDD-HHMMSS>.log`.

---

## What it does NOT touch

The following are explicitly protected and never deleted:

- **`hbloader`** at `atmosphere/contents/010000000000100D/exefs/main.nso`
  — the NRO execution runtime. Without it, no `.nro` file runs at all, including
  all Q OS shell components. It must remain.
- Any other Atmosphère content (`atmosphere/contents/`, `atmosphere/config/`, etc.)
- Bootloader files (`bootloader/`)
- System titles or Nintendo content
- Anything else on the SD card not listed in the removal section above

The script validates that `atmosphere/`, `bootloader/`, and `switch/` are present
before touching anything, so accidental use on the wrong mount point is caught
early.

---

## How to revert

Run `restore.sh <sd-card-path>`. It downloads the latest release from
`https://github.com/switchbrew/nx-hbmenu/releases/latest`, unpacks it, and places
the files back at their canonical locations. The migration is one-way at the file
level, but fully reversible by re-installation.

The Q OS vault is not removed by `restore.sh`. Both surfaces can coexist: HBMenu
launches via Album, the vault launches via the Q OS desktop "Files" icon.

---

## Required tools

- `bash` (4.x or later; ships with macOS via Homebrew or is the system shell on Linux)
- `du` — disk usage (standard; available on macOS and all Linux distributions)
- `curl` — `restore.sh` only; downloads the release zip from GitHub
- `unzip` — `restore.sh` only; unpacks the downloaded zip

---

## Arguments and flags

### `migrate.sh`

```
migrate.sh <sd-card-path> [--yes]
```

| Argument / flag | Required | Description |
|---|---|---|
| `<sd-card-path>` | yes | Mount point of the Switch SD card, e.g. `/Volumes/SWITCH SD` or `/mnt/sd` |
| `--yes` | no | Execute removal. Without it the script runs in dry-run mode and exits 0. |

Exit codes: `0` = success or clean dry-run; `1` = validation failure; `2` = partial removal error.

### `restore.sh`

```
restore.sh <sd-card-path>
```

| Argument | Required | Description |
|---|---|---|
| `<sd-card-path>` | yes | Mount point of the Switch SD card |

Exit codes: `0` = success; `1` = validation or network failure; `2` = install error.

---

## Example session

```
# 1. Mount the SD card (macOS mounts it automatically when inserted).
#    Confirm the mount path:
diskutil list | grep -i switch

# 2. Preview what will be removed (no changes made):
bash tools/migrate-hbmenu/migrate.sh "/Volumes/SWITCH SD"

# Sample output:
#   Validating SD card at: /Volumes/SWITCH SD
#     atmosphere/ ... OK
#     bootloader/ ... OK
#     switch/     ... OK
#
#   REMOVAL CANDIDATES
#   -----------------------------------------------------------------------
#     [REMOVE]  /Volumes/SWITCH SD/hbmenu.nro  (6.2 MB)
#     [REMOVE]  /Volumes/SWITCH SD/switch/hbmenu/  (1.4 MB)
#     [skip]    /Volumes/SWITCH SD/atmosphere/contents/010000000000100B/...  (not present)
#
#   PROTECTED (NOT TOUCHED)
#   -----------------------------------------------------------------------
#     [KEEP]    /Volumes/SWITCH SD/atmosphere/contents/010000000000100D/exefs/main.nso
#
#   Total to reclaim: 7.6 MB
#
#   Dry run complete. No files were changed.
#   Run with --yes to execute the removal.

# 3. Execute the removal:
bash tools/migrate-hbmenu/migrate.sh "/Volumes/SWITCH SD" --yes

# 4. Verify the log was written:
cat "/Volumes/SWITCH SD/qos-shell/migrations/hbmenu-20261024-141500.log"

# 5. Eject:
diskutil eject "/Volumes/SWITCH SD"
```

### Restore example

```
# Re-install HBMenu from the latest GitHub release:
bash tools/migrate-hbmenu/restore.sh "/Volumes/SWITCH SD"
```
