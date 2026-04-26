# PLAN — uMenu v0.7: deko3d + ImGui Renderer Port

**Date:** 2026-04-18
**Status:** PLANNING (parked until v0.6.x field test returns green)
**Supersedes:** Prior Tesla/libultrahand plan (infeasible — see §1)
**Estimate:** 3–4 weeks

---

## §1 Why Not Tesla

Tesla (`libtesla` / libultrahand) is architecturally incompatible with uMenu:

- Tesla is `OverlayApplet`-only. uMenu is `LibraryApplet` (launched via
  `ILibraryAppletCreator::CreateLibraryApplet` at Album TID `100D`).
- Tesla framebuffer is 448×720 RGBA4444 via `viCreateManagedLayer`. uMenu needs
  1280×720 full-screen.
- Tesla uses `hid:sys` input-focus steal (`hidsysEnableAppletToGetInput`) — that
  API is overlay-only. LibraryApplet uses `hidGetNpadStateHandheld`.
- Tesla's widget coordinate system assumes overlay dimensions.

Porting Tesla to LibraryApplet = rebuild from scratch. Not a port.

## §2 Why deko3d + ImGui

Confirmed production references on fw 20.x:

| Project | Role | Renderer | Applet mode |
|---------|------|----------|-------------|
| sphaira (ITotalJustice) | Full launcher / title manager | deko3d + nanovg-deko3d | Confirmed applet mode |
| ftpd (mtheall) | FTP server with full UI | deko3d + ImGui (1920×1080) | Yes |
| scturtle/imgui_deko3d_example | Canonical reference | deko3d + ImGui (1280×720) | Yes |

Key properties:
- Bypasses Mesa/nouveau entirely. No `-lEGL -lGLESv2 -lglapi -ldrm_nouveau`.
  Removes the `_malloc_r NULL` crash class at its source.
- libnx auto-selects `nvdrv:a` for `AppletType_LibraryApplet` — zero service
  configuration change.
- Memory footprint: 16MB image pool + 128KB code + 1MB data ≈ 17MB GPU,
  ~6% of uMenu's 296MB heap. Proven working.

## §3 Scope

### In scope
- Replace SDL2 + Plutonium + Mesa stack with deko3d + ImGui
- Port all uMenu screens (`MainMenu`, `EntryMenu`, `MenuApplicationSpecs`,
  `UserCreationMenu`, etc.) to ImGui widget idiom
- Port `LoadApplicationIconTexture` to deko3d image-upload path (`copyBufferToImage`)
- Remove the v0.6.5 `RendererSoftwareFlags` hack (no longer needed)
- Remove the v0.6.6 capture buffer lazy-alloc (deko3d owns its memory pools
  directly, no 3.67MB static buffer)
- Keep SMI protocol (uMenu↔uSystem IPC) unchanged
- Keep existing romfs asset layout (icons, fonts, language JSONs)

### Out of scope
- Replacing Plutonium with `tsl::` (Tesla) — infeasible (§1)
- Changing NPDM service access list (already wildcards, covers nothing needed)
- Touching uSystem, uLoader, or uManager rendering paths
- Theme engine (post-v0.7)

## §4 Work Breakdown (3–4 weeks)

### Week 1 — Foundation
1. **Build deps:** add `-ldeko3d -lnouveau_deko3d -lnx -lm` to Makefile;
   remove `-lEGL -lGLESv2 -lglapi -ldrm_nouveau`. Add `-I$(LIBNX)/include/deko3d`
   and `-I$(CURDIR)/source/imgui`.
2. **Vendor ImGui 1.89+** into `source/imgui/` (docking branch).
3. **Port `imgui_impl_deko3d.cpp`** from scturtle's canonical reference:
   `FB_NUM=2`, `FB_WIDTH=1280`, `FB_HEIGHT=720`, `CODEMEMSIZE=128KB`,
   `CMDMEMSIZE=1MB`, image pool = 16MB.
4. **Bootstrap**: replace `pu::ui::render::Initialize()` with
   `ImGui_ImplDeko3d_Init()` in `main.cpp`; keep existing init-log scaffolding.
5. **Exit criteria:** empty ImGui window renders at 1280×720 in LibraryApplet slot,
   HOME-press returns to HOS cleanly without 2168-0002.

### Week 2 — Widget Shim Layer
1. Build `qos::ui::Element` base class (imitates `pu::ui::elm::Element`
   surface area — `OnRender`, `OnInput`, `SetX/Y/W/H`, visibility).
2. Implement shims: `QImage` (was `pu::ui::elm::Image`), `QButton`, `QLabel`,
   `QMenu`, `QContextMenu`, `QDialog`.
3. Shims draw via `ImGui::GetBackgroundDrawList()` / `ImGui::GetForegroundDrawList()`
   for pixel-perfect positioning (Plutonium parity).
4. Font loader: use `ImGui::GetIO().Fonts->AddFontFromMemoryTTF()` with
   `plGetSharedFontByType(PlSharedFontType_Standard)` — shared fonts stay in
   system pool, free against our heap budget.
5. **Exit criteria:** MainMenu screen renders with placeholder widgets, buttons
   respond to `hidGetNpadStates`.

### Week 3 — Icon + Asset Pipeline
1. Port `LoadApplicationIconTexture` to deko3d:
   - Stage icon bytes into a `DkMemBlock` with `CpuUncached|GpuCached|Image` flag
   - Build `DkImageLayout` for 256×256 RGBA8
   - Use `copyBufferToImage` on the transfer queue
   - Cache ImTextureID for reuse
2. Port `g_TemporaryControlData` (144KB NsApplicationControlData) — stays on CPU,
   only the icon bytes go to GPU.
3. Port wallpaper loader (background images) — same pattern, 1280×720 RGBA8 = 3.5MB
   allocation, fits in 16MB image pool with room for icon cache.
4. Port `FlushIconCache` to `dkQueueWaitIdle` + cache invalidate.
5. **Exit criteria:** full game tile grid renders with real icons from installed titles.

### Week 4 — Screen Port + Polish
1. Port remaining screens in this order:
   `LanguagesMenu`, `ThemesMenu`, `UserCreationMenu`, `HomebrewMenu`,
   `SettingsMenu`, `EntryMenu`, `MenuApplicationSpecs`.
2. Wire SMI protocol message-pump to ImGui frame loop (unchanged from v0.6).
3. Hardware test cycle: boot, HOME-press, app launch, HOME-return, Q OS Manager
   launch, suspend/resume.
4. **Exit criteria:** v0.7.0-rc1 green on hardware, no Mesa link, no
   `_malloc_r NULL` crash, HOME-return works without SW-renderer fallback.

## §5 Memory Budget (confirmed)

```
Heap declared (__nx_heap_size):       296 MB
  ImGui + widget shims:                  ~2 MB
  NsApplicationControlData cache:       4 MB  (144KB × 28 slots max)
  SMI protocol buffers:                 ~1 MB
  misc (stdlib, logs):                 ~5 MB
  CPU-side free:                      ~284 MB

GPU (deko3d):
  Image pool (textures + 2 framebufs): 16 MB
  Code pool (shaders):                128 KB
  Data pool (vertices + uniforms):      1 MB
  Command buffers (2 × 1MB):            2 MB
  GPU total:                          ~19.1 MB (6.4% of declared heap)
```

## §6 Risk Register

| Risk | Probability | Mitigation |
|------|-------------|------------|
| deko3d not stable on fw 20.0.0 LibraryApplet | Low | sphaira confirmed working as of v0.6.1 |
| Icon upload stalls on first render | Medium | Pre-upload during splash screen, use semaphores |
| ImGui + SMI thread contention | Medium | SMI stays on its own thread, ImGui is main-thread, lock-free MPSC queue |
| Font glyph atlas overflow (shared font = 32MB) | Medium | Use `PlSharedFontType_Standard` only, don't load all 7 fonts |
| 14MB applet pool insufficient for 16MB GPU | Low | Actual budget is higher for LibraryApplet (not overlay); sphaira proves it |
| Plutonium XML layouts can't be ported 1:1 | High | Layouts become ImGui code; accepted scope cost |

## §7 Decision Points (creator approval gates)

1. **Start v0.7?** After v0.6.x hardware test returns green. If v0.6.5 SW-renderer
   fix is sufficient for beta, v0.7 can be parked further.
2. **Sphaira vs ftpd reference?** Default: ftpd (ImGui is closer to Plutonium's
   immediate-mode feel than NanoVG's retained path-render style).
3. **16MB vs 32MB image pool?** Default: 16MB (sphaira production figure). Bump
   only if wallpaper + icons + framebuffers OOM in practice.

## §8 Rollback Plan

v0.6.x branch (`RendererSoftwareFlags` + capture buffer lazy-alloc) stays on the
staging lane as the stable fallback. v0.7 merges to main only after two
consecutive clean hardware cycles (HOME-press + app launch + Q OS Manager).

## §9 References

- deko3d on GitHub: https://github.com/devkitPro/deko3d
- sphaira (applet-mode launcher): https://github.com/ITotalJustice/sphaira
- ftpd imgui_deko3d: https://github.com/mtheall/ftpd/blob/master/source/switch/imgui_deko3d.cpp
- scturtle ImGui+deko3d ref: https://github.com/scturtle/imgui_deko3d_example
- nanovg-deko3d: https://github.com/Adubbz/nanovg-deko3d
- libnx nvdrv service selection: libnx/nx/source/services/nv.c
