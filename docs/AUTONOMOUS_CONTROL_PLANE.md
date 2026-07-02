# Q OS uMenu — Autonomous Control Plane

> **Status: HW-verified 2026-06-19.** uMenu ships (dev builds) an HTTP debug server that
> lets a remote operator drive the Switch hands-off: see → act → observe → deploy →
> reboot → recover from crashes, with **zero physical button presses** for the proven paths.
>
> **This is a development capability only.** The auto-start and the `/crash` route are
> compiled out of release builds (`UL_DEBUG_SERVER_DEV=0`); see "Release hygiene" below.

## Endpoints

| Service | Port | Notes |
|---|---|---|
| Debug HTTP server | `LAN-IP:6010` | In `uMenu` (`qd_DebugServer.cpp`). Dies with uMenu. |
| sys-ftpd | `LAN-IP:5000` | creds `qos`/`qos`. **Independent sysmodule — survives a uMenu crash.** |

uMenu binary on SD: `/ulaunch/bin/uMenu/main` (the `.nso`). Rollback copy: `main.bak-ftp`.

## Routes

| Route | Does |
|---|---|
| `GET /ping` `/state` | liveness; version / frame / focus |
| `GET /screenshot` | composed-frame JPEG via `caps:sc` (real 1280×720) |
| `GET /observe` | telemetry: temp, battery, wifi, fw, serial, MAC, users |
| `GET /press/<BTN>` | synthetic button (A,B,X,Y,DUP…) via hid:dbg **HDLS** virtual Pro Controller |
| `GET /touch/<x>/<y>` | synthetic touch (0–1279 / 0–719) via HDLS |
| `GET /reboot` | reboot-to-payload via `reboot_to_hekate.nro` → CFW (see ⚠️ below) |
| `GET /crash` | **dev-only**: `fatalThrow` to test crash auto-recovery |

HDLS is system-wide (drives uMenu *and* games) but **dies with uMenu**, so it cannot
dismiss an Atmosphère fatal — that is what `fatal_auto_reboot_interval` is for.

## The loop (no human)

```
edit → make umenu → FTP verified-swap deploy → reboot (/crash, proven) →
  fusee/autoboot → uMenu → debug auto-starts (debug.flag) → /state + /screenshot → repeat
```

Self-start: `sdmc:/ulaunch/debug.flag` makes the server auto-start on boot (even on the
login screen, which shows a **`Debug: ON`** field beside Nxlink/USB). Dev-gated.

## Boot configuration (all on the SD, reversible)

- `bootloader/hekate_ipl.ini`: `autoboot=4` (entry 4 = **CFW (EMUMMC)**), `bootwait=1`
  (keeps the **VOL- escape** to the Hekate menu).
- `atmosphere/config/system_settings.ini`: `fatal_auto_reboot_interval = u64!0x2710` (10 s),
  `power_menu_reboot_function = str!payload`. Backup: `system_settings.ini.bak-qos-20260619`.
- `atmosphere/reboot_payload.bin` = **fusee** (~110 KB). (Was once a broken stock-OFW payload;
  repaired 2026-05-17.)

## ⚠️ Reboot reality — READ THIS

- **`reboot_to_hekate.nro`** → Hekate → autoboot → CFW. **Proven**, but shows a "press +" prompt.
- **`/crash` → fatal → `fatal_auto_reboot`** → reboot **to payload** (fusee) → CFW.
  **Proven, prompt-free** (~45 s). The fatal reaches CFW *because the fatal reboots to payload*.
- **Plain `bpcRebootSystem()` does a NORMAL reboot → STOCK OFW** (needs RCM re-injection).
  It is **NOT** reboot-to-payload. This cost a re-injection on 2026-06-19. **Never use a bare
  `bpcRebootSystem()` to get back to CFW.**
- **Clean prompt-free reboot (TODO):** `amsBpcSetRebootPayload(<hekate/fusee>) + bpcRebootSystem()`
  — exactly what `reboot_to_hekate.nro` does internally, minus the prompt. Implement and test
  **deliberately, with VOL- ready** — do not gamble it on a live session.

## Release hygiene (mandatory before any public push)

- Build with `UL_DEBUG_SERVER_DEV = 0` → auto-start + `/crash` compiled out.
- Do **not** ship `sdmc:/ulaunch/debug.flag`.
- Ship with debug **OFF** by default (manual hot-corner toggle only).
- The manual-toggle server + routes are documented for users as a "remote test interface."
