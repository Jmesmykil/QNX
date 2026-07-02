# Q OS uLaunch Fork v0.2.1 — Pre-Stage Ready

Date staged: 2026-04-18
Agent: Rung-2 Lane (Sonnet)

---

## What is ready

The `v0.2.1-prep/` directory is a complete, patched source tree.
Once SDL2 is available system-wide, a single `bash BUILD-v0.2.1.sh` produces
`exefs.nsp` (uSystem) + `uMenu.nso` + `uManager.nro` with three Q OS patches applied.

### Patches applied (vs v0.2.0 baseline)

| ID | File | Change |
|----|------|--------|
| QOS-PATCH-001 | `libs/uCommon/include/ul/fs/fs_Stdio.hpp` | Dotfile filter in `UL_FS_FOR` — skips `.`-prefixed entries so no macOS/system hidden files appear as app icons |
| QOS-PATCH-002 | `projects/uMenu/source/ul/menu/ui/ui_EntryMenu.cpp` | Paging guard in `SwipeToNextPage` — clamps scroll to actual entry count, prevents cursor landing on empty cell past last app |
| QOS-PATCH-003 | `projects/uSystem/source/main.cpp` | Boot banner: emits `"Q OS uMenu v0.2.1 (fork of uLaunch by XorTroll, GPLv2, personal-use)"` at `InitializeSystemModule` via `UL_LOG_INFO` |

All three patch sites carry a `QOS-PATCH-NNN` comment + GPLv2 copyright attribution (XorTroll original + Q OS fork notice).

---

## What you need to do (two steps)

### Step 1 — Install SDL2 (interactive sudo, one time)

In a terminal:
```
sudo dkp-pacman -S switch-sdl2 switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx
```

CRITICAL version note: the build requires `switch-sdl2` revision **2.28.5-3**.
If pacman offers a newer revision, install from the pinned tarball instead:
```
# Download switch-sdl2-2.28.5-3-aarch64.pkg.tar.xz from:
# https://github.com/devkitPro/pacman-packages/tree/0ae8790f6e092cf8df937d143e70a785f7e27997/switch/SDL2
sudo dkp-pacman -U switch-sdl2-2.28.5-3-aarch64.pkg.tar.xz
```
Newer SDL2 causes the audio sysmodule to crash on game suspend (audren vs audout ABI).

### Step 2 — Build and deploy

```
cd /Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/archive/v0.2.1-prep
bash BUILD-v0.2.1.sh    # builds all three components, produces SdOut/
bash DEPLOY-v0.2.1.sh   # stages exefs.nsp to archive/v0.2.1/ + atmosphere-clone v0.1.0
```

When ready to push to Switch SD (after DEPLOY exits 0):
```
cp "/Users/nsa/Astral/QOS/tools/qos-ulaunch-fork/archive/v0.2.1/exefs.nsp" \
   "/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp"
diskutil eject "/Volumes/SWITCH SD"
```

---

## Directory layout

```
v0.2.1-prep/
  BUILD-v0.2.1.sh         — build script (step b-e)
  DEPLOY-v0.2.1.sh        — deploy/stage script
  README-v0.2.1-prep.md   — this file
  upstream/               — patched source tree (copy of src/ + 3 Q OS patches)
    Makefile
    projects/
      uSystem/source/main.cpp          ← PATCH-003 (banner)
      uMenu/source/ul/menu/ui/
        ui_EntryMenu.cpp               ← PATCH-002 (paging guard)
    libs/uCommon/include/ul/fs/
      fs_Stdio.hpp                     ← PATCH-001 (dotfile filter)
    ...all other upstream files unchanged...
```

---

## GPLv2 compliance note

XorTroll's copyright is preserved in every modified file header.
All Q OS-specific additions are also GPLv2.
Personal-use distribution: source disclosure not legally required (no public distribution).
