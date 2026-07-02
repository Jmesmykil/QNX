# v0.5.1-revertNPDM — v0.5.0 uSystem + NPDM Fix + v0.4.1 uMenu

**Date:** 2026-04-18  
**Target:** Nintendo Switch fw 20.0.0 + Atmosphère 1.11.1-master  
**Purpose:** Recovery ladder rung 3 — test v0.5.0 cache/telemetry features without PM-killing NPDM field

## Root Cause Fixed

`"system_resource_size": "0xC00000"` was present in uSystem.json in v0.5.0. This caused
Process Manager to silently reject svcCreateProcess on fw 20.0.0 because the applet pool
budget was exhausted by the 12MB extra system resource request.

This is the ONLY change from v0.5.0 to v0.5.1-revertNPDM.

## What This Is

- **uSystem**: v0.5.0 source (in-RAM app cache cap, mem_Telemetry, LogMemoryStats) rebuilt
  with `system_resource_size` removed from uSystem.json
- **uMenu**: v0.4.1 prebuilt binary (unchanged from v0.5.0 deployment)
  - Contains: QOS-PATCH-005 (DockElement), QOS-PATCH-006 (grid wrap), QOS-PATCH-007,
    QOS-PATCH-008 (FocusSurface), QOS-PATCH-009 (PlasmaWallpaper)

## What Changed vs v0.5.0

- Removed ONLY: `"system_resource_size": "0xC00000"` from uSystem.json
- Everything else in uSystem source is identical to v0.5.0
- uMenu is identical to v0.5.0 (v0.4.1 binary)

## SHA256 Hashes

| File | SHA256 |
|------|--------|
| SdOut/atmosphere/contents/0100000000001000/exefs.nsp | c16633e59491e2e895207a03028e216ea057bcf548f2c40fe701370c5e3a17e8 |
| SdOut/qos-shell/bin/uMenu/main | 471d81bc9ac073deab9e252a1b8463b4e2749131cedc1e25ef1b273b8f11c859 |
| SdOut/qos-shell/bin/uMenu/romfs.bin | 337c6593b195f748b271bf9b4f89f50f842b32a849b44bd7ae5500bd265c812e |
| SdOut/switch/uManager.nro | 8477626b44e3a449f903b32dd8c928886b2e091703e00bb97b40c99337f43c78 |

Cross-check:
- exefs.nsp SHA ≠ e940faba... (v0.5.0 broken binary) ✓
- exefs.nsp SHA ≠ f3b09ce6... (v0.2.1-pure binary) ✓
- uMenu/main SHA = 471d81bc... (v0.4.1 prebuilt) ✓

## SD Card Layout

```
SD:/
  atmosphere/contents/0100000000001000/exefs.nsp   (uSystem v0.5.1 — cache cap + telemetry, no system_resource_size)
  qos-shell/bin/uMenu/main                          (uMenu v0.4.1 — DockElement + PlasmaWallpaper + FocusSurface)
  qos-shell/bin/uMenu/main.npdm
  qos-shell/bin/uMenu/romfs.bin
  qos-shell/bin/uSystem/exefs.nsp
  qos-shell/bin/uLoader/applet/main
  qos-shell/bin/uLoader/applet/main.npdm
  qos-shell/bin/uLoader/application/main
  qos-shell/bin/uLoader/application/main.npdm
  switch/uManager.nro
```

## Recovery Ladder Position

1. v0.2.1-pure → If this boots, uLaunch base works on fw 20.0.0
2. v0.2.2-theme → If this boots, Q OS theme assets are safe
3. v0.5.1-revertNPDM (THIS) → If this boots, v0.5.0 features work; NPDM was the only blocker

If v0.5.1-revertNPDM fails: the bug is in v0.5.0 uSystem source (cache cap/telemetry), not the NPDM.
If v0.5.1-revertNPDM passes: v0.5.0 was ship-ready but for one JSON field.
