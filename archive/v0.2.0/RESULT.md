# v0.2.0 Baseline Build — RESULT

**Outcome: PARTIAL SUCCESS — exefs.nsp produced**
**Date: 2026-04-18**
**Agent: Rung-2 v0.2.0 Unblock Builder (Sonnet)**

---

## Primary Deliverable

| Field | Value |
|-------|-------|
| File | `archive/v0.2.0/exefs.nsp` |
| SHA256 | `76d9230fcae0e4b3bb7adcd67ab486ab971ca2a125ab8a0ec748c566d950463c` |
| Size | 586718 bytes (573 KB) |
| Convenience copy | `tools/qos-ulaunch-fork/qos-ulaunch-v0.2.0.nsp` |
| Deploy path | `/atmosphere/contents/0100000000001000/exefs.nsp` on Switch SD |

**uLoader also built:**
- `uLoader.nso` SHA256: `43d967e94c8039f38156b9ae6c5d4e8c263c107c0fdc10b4e2d58d23632b8779`

---

## Patches Applied (from task blockers)

### Fix 2 (HIGH) — Atmosphere-libs ABI break vs libnx 4.12.0-1
**File:** `src/libs/Atmosphere-libs/libstratosphere/include/stratosphere/svc/svc_stratosphere_shims.hpp:391`

Applied `reinterpret_cast<::DebugEventInfo *>` to resolve type mismatch between
`ams::svc::lp64::DebugEventInfo*` and bare `DebugEventInfo*` introduced in libnx 4.12.0-1.
Mirrors upstream commit `cb3b3a39` (ndeadly, 2026-02-03).
Direct header edit used because `Atmosphere-libs/.git` gitfile points to non-existent submodule path.

**Additional (unplanned, required for link):**
**File:** `src/libs/Atmosphere-libs/libstratosphere/libstratosphere.mk`
Added `/opt/homebrew` to `LIBDIRS` for `nx-hac-001` board so `jpeglib.h` resolves from
Homebrew `libjpeg-turbo` install. The capsrv JPEG decoder in libstratosphere requires this header;
portlibs are not installed.

### Fix 3a (LOW) — libnx-ipcext unused variable
**File:** `src/libs/libnx-ext/libnx-ipcext/source/ns-ext.c:85`
Added `(void)cmd_id;` after the variable declaration to suppress `-Werror=unused-variable`.

### Fix 3b (TRIVIAL) — python vs python3 in arc recipe
**File:** `src/Makefile`
Replaced `python` with `python3` in both lines of the `arc:` target.

---

## Remaining Blocker

### Fix 1 (CRITICAL — SDL2) — UNRESOLVED

- `pkg.devkitpro.org` returns HTTP 403 (Cloudflare) to headless `curl`
- `dkp-pacman` requires `sudo`; interactive terminal password entry not available to agents
- `/opt/devkitpro/portlibs/switch/` contains only a `bin/` subdirectory — no SDL2 headers or libs

**Impact:** `umenu` and `umanager` cannot be built. The `exefs.nsp` core sysmodule (uSystem) and
uLoader are unaffected and complete.

**To unblock umenu+umanager:** In an interactive terminal, run:
```
sudo dkp-pacman -S switch-sdl2 switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx
```
If version pinning to 2.28.5-3 is required (audio sysmodule crash risk on 2.28.5-4), fetch from:
`https://github.com/devkitPro/pacman-packages/tree/0ae8790f6e092cf8df937d143e70a785f7e27997/switch/SDL2`
and install with `sudo dkp-pacman -U <local-tarball>`.

---

## Build Matrix

| Component | Result | Notes |
|-----------|--------|-------|
| arc codegen | PASS | python3 patch applied |
| libnx-ext | PASS | |
| libnx-ipcext | PASS | (void)cmd_id; patch applied |
| libstratosphere | PASS | reinterpret_cast + /opt/homebrew jpeglib fix |
| libuCommon | PASS | |
| uSystem → exefs.nsp | PASS | PRIMARY DELIVERABLE |
| uLoader → uLoader.nso | PASS | |
| Plutonium | BLOCKED | SDL2/SDL_mixer.h missing |
| uMenu | BLOCKED | Plutonium dep |
| uManager | BLOCKED | Plutonium dep |

---

## Deployment Note

The `exefs.nsp` produced here is the bare upstream uLaunch v1.2.3 sysmodule compiled clean
against libnx 4.12.0-1 with the three patches above. No Q OS UX modifications have been made yet.
This is the baseline confirmation that the build pipeline works for Rung-2.

Next milestone: v0.3.0 — Q OS Dark Liquid Glass theme (`.ultheme` only, zero C++ required).
