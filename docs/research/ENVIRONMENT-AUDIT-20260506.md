# Environment Audit — 2026-05-06

Read-only forensic audit of `/Volumes/SWITCH SD/` to determine what changed in the runtime environment between the Apr 26 hardware-validated boot (uMenu MD5 `5b22a1cda5d57332a880401abae0018b`) and the intermittent boot/crash behavior observed today.

## HOS firmware version

**HOS 20.0.0** — exact, definitive.

Evidence: every fatal_report header from Apr 23 through May 5 contains the line `Firmware:  20.0.0 (Atmosphère 1.11.1-master-d04c20a04)` (e.g. `/Volumes/SWITCH SD/atmosphere/fatal_reports/01777022653_0100000000000023.log`, `01777161128_…`, `01777246684_…` Apr 23/25/26; `01778001892_…`–`01778013102_…` May 4/5).

## Atmosphère version

**Atmosphère 1.11.1-master-d04c20a04** — embedded in every fatal_report. Bundle `package3` and `stratosphere.romfs` are dated `Apr 18 08:10:52 2026` (sizes 8388608 / 1350392). They have not been touched since Apr 18 — well before Apr 26. AMS_HATS pack on disk (`/Volumes/SWITCH SD/HATS_VERSION.txt`) is from Feb 1 and lists AMS 1.10.2; that file is stale and does not reflect the live install. The Apr 18 update is what brought AMS to 1.11.1 + FW 20.0.0 support.

## Hekate version

**Hekate v6.5.2** — confirmed two ways: (1) string `v6.5.2` appears in `/Volumes/SWITCH SD/bootloader/update.bin`; (2) `update.bin` (110004 bytes, mtime Apr 17 22:57:58) has the same size as `/Volumes/SWITCH SD/hekate_ctcaer_6.5.2.bin`. Hekate v6.5.x supports up through HOS 20.x (consistent with the firmware in use). The standalone `payload.bin` at SD root is the older 109808-byte build (Feb 1) and is not the live bootloader.

## Timeline of relevant SD changes (last 14 days)

| mtime | path | what |
|---|---|---|
| Apr 25 11:06 | `contents/420000000000000B/{exefs.nsp,boot2.flag,toolbox.json}` | sys-patch installed/activated |
| Apr 26 11:48 | `contents/0100000000001000/exefs.nsp.bak-pre-1.6.10-20260426-114840` | uMenu backup (= live `exefs.nsp`, MD5 `5b22a1c…`) |
| Apr 27 02:13 | `…fork-uSystem-v0.6.14-blackscreen-20260427-021313` | failed fork attempt |
| May 4 23:13–23:38 | 6 fatal_reports `0x10801` (svc::LimitReached) | first wave of SystemAppletMenu crashes |
| May 4 23:20–23:33 | `/config/ultrahand/*`, `/config/sys-con/*`, `/config/sphaira/*`, `/config/JKSV/*`, `/config/ultrahand/theme.ini` | mass overlay/sysmod config refresh |
| May 5 09:04 | `bootloader/patches.ini.bak-20260505-090418`, `atmosphere/exefs_patches.bak-…`, `kip_patches` shadow copies | sigpatch backup (suggests sigpatches edited that morning) |
| May 5 09:43 | `bootloader/hekate_ipl.ini` rewritten (autoboot=0, added STOCK OFW + SEMI-STOCK + sysMMC/emuMMC entries) | boot-menu structure changed |
| May 5 09:49 | `contents/00FF0000636C6BFF/`, `420000000007E51A/`, `420000000007E51B/`, `690000000000000D/` updated, with `…bak-pre-update-20260505-094928` companions | sys-clk, nx-ovlloader, ovlmenu, sys-con all replaced (live `exefs.nsp` mtimes are Nov 16 2024 / Nov 28 / Nov 6 — these are *rollbacks* to older sysmodule builds) |
| May 5 10:04–10:47 | uMenu cycled v2.3.5 → v2.3.6 → quarantined (live size 568284 = v2.3.5 = `5b22a1c…`) | uMenu A/B testing |
| May 5 10:39 | `420000000007E51A/flags/boot2.flag.disabled-bisect-step2` | nx-ovlloader auto-start *disabled* (flag renamed) |
| May 5 10:49 | `00FF0000636C6BFF/flags/boot2.flag.disabled-rollback-20260505`, `690000000000000D/flags/boot2.flag.disabled-rollback-20260505` | sys-clk and sys-con auto-start *disabled* |
| May 5 19:39 | `bootloader/nyx.ini` | Nyx UI prefs touched |
| May 6 09:16–14:16 | uMenu cycled six more times (`bak-pre-1ed9b81b`, `bak-pre-stabilization-final`, `aa267e1a-fw20tune-but-hekate-broken`, `bak-aa267e1a-newbuild-fails`); live binary still `5b22a1c…` | repeated build/test/rollback today |
| May 6 12:14 → 13:14 | `atmosphere/reboot_payload.bin` swapped (both copies have md5 `ac1f71cd10f401780b872c4dd537920c`, neither matches Hekate 6.5.1 or 6.5.2 standalone payloads) | reboot-to-payload chain altered |
| May 6 09:16 / 13:14 / 13:57 | three `override_config.ini` variants under `atmosphere/config/` (`.bak`, `.QUARANTINE-test-hekate`, `.DEAD-breaks-hekate-DO-NOT-RESTORE`) | HBL Album-tile override tuned, then quarantined for breaking Hekate |

Live `atmosphere/config/` directory contains NO active `override_config.ini` — only the three suffix-tagged variants. AMS is running with its compiled defaults for HBL routing.

## Did the firmware update?

**No.** Same Firmware = 20.0.0, same AMS = 1.11.1-master-d04c20a04 from Apr 23 fatal_report through May 5 fatal_report. `package3` and `stratosphere.romfs` mtimes are Apr 18 — untouched. `Nintendo/Contents/registered/` has zero NCAs. No Nintendo system update was applied.

The crash signatures themselves prove same-FW: result code `0xC880 (2128-0100)` from `am` was already crashing on Apr 23, Apr 25, Apr 26 (three pre-validation fatal_reports) on the **same** Firmware: 20.0.0 / AMS 1.11.1 stack. The May 4 wave shifted to `0x10801 (2001-0132 svc::LimitReached)`, then May 6 reverted to `0xC880`.

## Strict factual conclusion

The Apr 26 binary (uMenu MD5 `5b22a1cda5d57332a880401abae0018b`) and the firmware/Atmosphère/Hekate stack are unchanged. What changed is the **sysmodule-and-config environment surrounding the same binary**:

1. **May 5 09:49** — sys-clk, sys-con, nx-ovlloader, ovlmenu were swapped en masse to older builds (Nov 2024 mtimes preserved). Their `boot2.flag` files were then renamed `…disabled-bisect-step2` / `…disabled-rollback-20260505` between 10:39 and 10:49, taking them out of the boot chain.
2. **May 5 09:43** — `hekate_ipl.ini` replaced. The new file adds three boot entries (`100% STOCK OFW`, `SEMI-STOCK`, `CFW EMUMMC`) plus changes `autohosoff=2`, `bootwait=1`. Different boot-chain shape than Apr 26.
3. **May 5 09:04** — sigpatches (`exefs_patches`, `kip_patches`, `bootloader/patches.ini`) were backed up alongside live edits, indicating they were edited that morning.
4. **May 6 12:14 → 13:14** — `atmosphere/reboot_payload.bin` swapped; `override_config.ini` variants (HBL Album-tile redirect) were authored, quarantined, and tagged `DEAD-breaks-hekate-DO-NOT-RESTORE`, indicating one of them caused a Hekate failure during today's session.

The Apr 26 fatal_reports already show `0xC880 am` crashes on the validated build — Apr 26 was not a clean baseline; it had the same intermittent failure, three times that day. The escalation observed today is consistent with: **disabled sysmodules + replaced sigpatches + altered hekate_ipl.ini + swapped reboot_payload.bin**, on the same HOS 20.0.0 / AMS 1.11.1 / Hekate 6.5.2 / uMenu `5b22a1c…` stack, surfacing latent races that the original boot chain masked.
