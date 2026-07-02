# v0.2.2-theme — Stock Binary + Q OS Cosmic-Purple Theme

**Date:** 2026-04-18  
**Target:** Nintendo Switch fw 20.0.0 + Atmosphère 1.11.1-master  
**Purpose:** Recovery ladder rung 2 — verify theme assets work without code patches

## What This Is

Same uSystem binary as v0.2.1-pure. uMenu rebuilt with Q OS cosmic-purple theme assets
re-applied. All Q OS code patches remain absent (same source as v0.2.1-pure).

Only the romfs.bin differs from v0.2.1-pure — it contains the Q OS themed PNGs.

## What Changed vs v0.2.1-pure

- default-theme/ui/Background.png → Q OS cosmic-purple deep space gradient
- default-theme/ui/Main/EntryMenuBackground.png → Q OS styled
- default-theme/ui/Main/EntryMenuLeftIcon.png → Q OS styled (new file)
- default-theme/ui/Main/EntryMenuRightIcon.png → Q OS styled (new file)
- default-theme/ui/Main/InputBarBackground.png → Q OS styled
- default-theme/ui/Main/OverIcon/Border.png → Q OS styled
- default-theme/ui/Main/OverIcon/Cursor.png → Q OS styled
- default-theme/ui/Main/OverIcon/Selected.png → Q OS styled
- default-theme/ui/Main/TopMenuBackground/*.png → Q OS styled
- default-theme/ui/Settings/InputBarBackground.png → Q OS styled
- default-theme/ui/Settings/SettingEditableIcon.png → Q OS styled
- default-theme/ui/Settings/SettingNonEditableIcon.png → Q OS styled
- default-theme/ui/UI.json → Q OS color values
- default-theme/theme/ directory → Q OS theme manifest (new in snapshot)

## What Stayed the Same vs v0.2.1-pure

- uSystem binary: identical (exefs.nsp SHA unchanged)
- All Q OS code patches: still absent
- uLoader, uManager: identical

## SHA256 Hashes

| File | SHA256 |
|------|--------|
| SdOut/atmosphere/contents/0100000000001000/exefs.nsp | f3b09ce692bf07f515526e3ef85feebe2c986f9a64e6c4937b8e00c418fcbd60 |
| SdOut/qos-shell/bin/uMenu/main | 0d5dfcb4d1247705db78830477450b27abdfd0bc8194e637ac0af8a8fec5f83e |
| SdOut/qos-shell/bin/uMenu/romfs.bin | a3e5ba6298f487ecf1a0d835b75911183be1f9803ef5c354271bc4b74eda70f2 |
| SdOut/switch/uManager.nro | 8477626b44e3a449f903b32dd8c928886b2e091703e00bb97b40c99337f43c78 |

Note: exefs.nsp SHA matches v0.2.1-pure — uSystem binary is identical.
uMenu/main SHA differs from v0.2.1-pure — romfs.bin contains Q OS theme assets.

## SD Card Layout

```
SD:/
  atmosphere/contents/0100000000001000/exefs.nsp   (uSystem sysmodule — same as v0.2.1-pure)
  qos-shell/bin/uMenu/main                          (uMenu NSO — stock code, Q OS theme)
  qos-shell/bin/uMenu/main.npdm
  qos-shell/bin/uMenu/romfs.bin                     (uMenu RomFS with Q OS cosmic-purple theme)
  qos-shell/bin/uSystem/exefs.nsp
  qos-shell/bin/uLoader/applet/main
  qos-shell/bin/uLoader/applet/main.npdm
  qos-shell/bin/uLoader/application/main
  qos-shell/bin/uLoader/application/main.npdm
  switch/uManager.nro
```
