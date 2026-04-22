# v0.2.1-pure — Stock uLaunch Rollback

**Date:** 2026-04-18  
**Target:** Nintendo Switch fw 20.0.0 + Atmosphère 1.11.1-master  
**Purpose:** Recovery ladder rung 1 — absolute baseline with zero Q OS modifications

## What This Is

Full rollback to XorTroll uLaunch upstream source. All Q OS patches removed except
QOS-PATCH-002 (paging guard — kept as it is a safe bug fix in EntryMenu.cpp).

## What Was Removed

- QOS-PATCH-005: DockElement hover-scale (v0.3.0)
- QOS-PATCH-006: D-pad grid wrap (v0.3.0)
- QOS-PATCH-007: QOS comment on HandleEntrySelection (comment stripped, function kept as stock)
- QOS-PATCH-008: FocusSurface state machine / RouteDpadInput (v0.3.0)
- QOS-PATCH-009: Cold Plasma Cascade wallpaper / PlasmaWallpaperElement (v0.4.0)
- QOS-PATCH-010: In-RAM cache cap + mem_Telemetry + LogMemoryStats (v0.5.0)
- NPDM system_resource_size: 0xC00000 (root cause of black-screen failure on fw 20.0.0)

Theme assets: stock XorTroll default-theme restored from v0.2.2-prep/stock-assets-backup/

## Root Cause Fixed

`"system_resource_size": "0xC00000"` in uSystem.json caused PM to silently reject
svcCreateProcess on fw 20.0.0 due to applet pool budget exhaustion.
This field is absent from uSystem.json in this build.

## SHA256 Hashes

| File | SHA256 |
|------|--------|
| SdOut/atmosphere/contents/0100000000001000/exefs.nsp | f3b09ce692bf07f515526e3ef85feebe2c986f9a64e6c4937b8e00c418fcbd60 |
| SdOut/qos-shell/bin/uMenu/main | 1dfb006743d2628b0eb979cd3a72719e271690531cabc03e288777173d3cf192 |
| SdOut/qos-shell/bin/uMenu/romfs.bin | 7a663deeea4d06682c5d058c2259118bb9c4da06d16dd013ad3faa3ed5d1fe00 |
| SdOut/switch/uManager.nro | 8477626b44e3a449f903b32dd8c928886b2e091703e00bb97b40c99337f43c78 |

## SD Card Layout

```
SD:/
  atmosphere/contents/0100000000001000/exefs.nsp   (uSystem sysmodule)
  qos-shell/bin/uMenu/main                          (uMenu NSO)
  qos-shell/bin/uMenu/main.npdm                     (uMenu NPDM)
  qos-shell/bin/uMenu/romfs.bin                     (uMenu RomFS with stock theme)
  qos-shell/bin/uSystem/exefs.nsp                   (uSystem NSP backup)
  qos-shell/bin/uLoader/applet/main                 (uLoader applet NSO)
  qos-shell/bin/uLoader/applet/main.npdm
  qos-shell/bin/uLoader/application/main            (uLoader application NSO)
  qos-shell/bin/uLoader/application/main.npdm
  switch/uManager.nro                               (uManager homebrew)
```

## Build Environment

- devkitA64 gcc 15.2.0
- libnx (devkitPro)
- Plutonium UI framework
- VERSION 1.2.3 (fork internal version)
