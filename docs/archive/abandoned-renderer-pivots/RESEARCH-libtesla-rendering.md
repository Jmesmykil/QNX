# RESEARCH — libtesla Rendering Stack for Q OS uMenu v0.7

**Date:** 2026-04-18
**Context:** fw 20.0.0 14MB applet pool wall blocks SDL2/Mesa-based UI rendering. Tesla overlays render full interactive UI in ~6 MB on the same firmware — that's the path forward.

---

## 1. Repo + Architecture (+ License)

**Canonical repos (2026-active):**

| Repo | Status | License | Role |
|---|---|---|---|
| `WerWolv/libtesla` | stale (last commit 2023-05-27, v1.3.2) | GPL-2.0 | original single-header library (3,659 lines) |
| `ppkantorski/libultrahand` | ACTIVE | GPL-2.0 (libtesla) + CC-BY-4.0 (libultra extension) | tesla.hpp at 13,794 lines — **this is what to use** |
| `ppkantorski/Ultrahand-Overlay` | ACTIVE | GPL-2.0 | reference implementation overlay |
| `WerWolv/nx-ovlloader` | upstream | GPL-2.0 | host sysmodule that bootstraps overlays |
| `ppkantorski/nx-ovlloader` | ACTIVE v2.0.0+ | GPL-2.0 | active fork with fw 20/21 heap tuning |
| `WerWolv/Tesla-Menu` | stable | GPL-2.0 | overlay picker menu |

**License compatibility:** All GPL-2.0, compatible with uLaunch fork's GPLv2. libultra's CC-BY-4.0 sublicense requires attribution only.

**Architecture tiers:**
1. `nx-ovlloader` (sysmodule): allocates process heap via `svcSetHeapSize`, loads `.ovl` NRO
2. Loaded `.ovl` calls `tsl::loop<YourOverlay>(argc, argv)` as `main()`
3. libtesla owns vi layer + rendering + font + input inside that loop

---

## 2. Rendering Pipeline (Services, Framebuffer, Format)

**Services used** (all from `include/tesla.hpp`):

| Service | Call | Purpose |
|---|---|---|
| `vi:m` | `viInitialize(ViServiceType_Manager)` | Managed display session |
| `vi` | `viOpenDefaultDisplay()` | Display handle |
| `vi` | `viCreateManagedLayer(&display, 0, 0, &__nx_vi_layer_id)` | Overlay layer |
| `vi` | `viAddToLayerStack()` × 8 stack types | Visible across all display contexts |
| `vi` | `nwindowCreateFromLayer()` + `framebufferCreate()` | FB via libnx wrapper |
| `hid:sys` | `hidsysEnableAppletToGetInput(true, pid)` | Steal input focus |
| `pmdm` | `pmdmntGetProcessId*()` | PID lookup for HID focus |

**NO nvdrv. NO Mesa. NO nouveau. NO EGL.** Pure vi + libnx Framebuffer API.

**Framebuffer spec:**

| Parameter | Value |
|---|---|
| Width | 448 px |
| Height | 720 px |
| Format | `PIXEL_FORMAT_RGBA_4444` (2 bytes/px) |
| Buffers | 2 (double-buffered) |
| Screen reference | 1920 × 1080 |
| Layer scaling | `LayerWidth = 1080 × (448/720)` = 672 px visual column |

**Byte arithmetic:** 448 × 720 × 2 × 2 = **1,290,240 bytes ≈ 1.26 MB** for double buffer.

Contrast: uMenu's attempted 1920×1080 RGBA8888 = 8,294,400 bytes (8.29 MB). This is the wall.

**Why it bypasses the applet FB pool:** `viCreateManagedLayer` on a LibraryApplet creates a **stray layer** via `IManagerDisplayService`. FB memory allocated from process heap (set by `svcSetHeapSize`), not shared applet capture buffers.

---

## 3. Text Rendering

**Engine:** stbtt (STB TrueType), inlined directly in tesla.hpp. No FreeType.

**Font sources** via `plGetSharedFontByType`:
- `PlSharedFontType_Standard` — Latin/JP
- `PlSharedFontType_ChineseSimplified` / `ChineseTraditional`
- `PlSharedFontType_KO`
- `PlSharedFontType_NintendoExt` — Nintendo symbol glyphs

**Glyph cache:** `MAX_CACHE_SIZE = 600` glyphs (libultrahand), split shared + notification cache.

**Layout:** Monospace + kerning via stbtt advance-width. No HarfBuzz shaping. No subpixel AA (RGBA4444 too low precision).

**libultrahand improvement:** NEON SIMD `fillRowSpanNEON` for ~60fps wallpaper rendering at stock clocks.

---

## 4. Bitmap/Image Support

**libtesla (WerWolv original):** none — text + primitives only.
**libultrahand (ppkantorski):** adds rounded rects + multi-threaded CPU blit, still no PNG/JPG decoder.

**Implication:** For uMenu icon rendering, vendor `stb_image.h` (~1,100 lines, single-header, permissive license). Each decoded icon becomes a CPU bitmap blitted into the framebuffer.

---

## 5. Input Handling

**Services:** `hid` + `hid:sys`. No nvhid or USB path.

**Default activation combo:** L + D-pad Down + R-stick click. Configurable via `/config/tesla/config.ini` `key_combo=` field.

**Focus steal:** `hidsysEnableAppletToGetInput(true, overlayPid)` on open, `(false, ...)` on close.

**Input struct:** `tsl::hid::Event` with `keysDown`, `keysHeld`, `touchPos`, both analog sticks.

**Key constraint for uMenu:** `hidsysEnableAppletToGetInput` requires `hid:sys` privilege (nx-ovlloader has it, plain LibraryApplets don't). uMenu should use normal `hidGetNpadStateHandheld`/`hidGetNpadStates` — the hid:sys shim is overlay-only.

---

## 6. Memory Footprint (Concrete Bytes)

**nx-ovlloader v2.0.0 heap budget:**

| HOS version | `svcSetHeapSize` |
|---|---|
| HOS 19 and older | 8 MB (0x800000) |
| **HOS 20.x** | **6 MB (0x600000)** |
| HOS 21+ | 4 MB (0x400000) |

Override: `/config/nx-ovlloader/heap_size.bin`.

**Breakdown within 6 MB (fw 20.0.0):**

| Segment | Size |
|---|---|
| nx-ovlloader + NRO stubs | ~64 KB |
| Double FB (RGBA4444, 448×720) | ~1.26 MB |
| Nintendo shared fonts | ~2-3 MB **mapped from system pool, not counted against heap** |
| stbtt glyph cache (600 glyphs) | ~150-300 KB |
| libtesla code + BSS | ~200-400 KB |
| **Remaining for overlay UI state** | **~2-4 MB** |

**The shared-font trick:** `plGetSharedFontByType` maps font bytes from a system-managed shared region. **The font bytes DO NOT count against `svcSetHeapSize` quota.** This is why Tesla text rendering is affordable.

---

## 7. Using libtesla as a Library (Not Just Overlay Entrypoint)

**Summary:** `tsl::loop<T>()` is not drop-in linkable — it owns the event loop. BUT `tsl::gfx::Renderer` (inner class, ~lines 1100-2200 of tesla.hpp) IS extractable.

**What uMenu already has in its favor:**
- Already `AppletType_LibraryApplet` (`main.cpp` line 41) — same privilege class as an overlay
- Can attempt `viCreateManagedLayer` if NPDM grants `vi:m`, or fall back to `viCreateLayer` (stray, lower priority)

**Required uMenu changes:**
- Extract `tsl::gfx::Renderer`, open its own vi layer directly (no nx-ovlloader mediation)
- Drive from existing Plutonium main loop (or replace Plutonium entirely)
- Remove SDL2/EGL/Mesa init entirely — Plutonium's `pu::ui::Application::Load()` calls these unconditionally, so Plutonium coexistence is NOT viable

---

## 8. Alternative Frameworks

**libultrahand:** the active 2026 Tesla ecosystem. Same rendering model as libtesla, adds NEON + multi-threaded draws + theme system. **Use this.**

**deko3d:** out of scope for LibraryApplet qlaunch replacement. Requires NVN permissions typically reserved for licensed dev tools.

**ImGui + custom Switch backend:** uMenu source already vendors ImGui (`src/libs/imgui/`). Alternative path: write `imgui_impl_vitesla.cpp` that opens a vi layer + drives ImGui draw lists via software raster. +2 weeks over Tesla-renderer path.

---

## 9. uMenu v0.7 Port Plan — File-by-File

**Root problem:** `main.cpp` line 44 `__nx_heap_size = 296_MB` is aspirational. Real ceiling on fw 20.0.0 is ~14 MB. SDL2 + Mesa init alone exceeds this before first draw call.

**Crash path:** `pu::ui::render::Renderer::New()` → SDL2 init → EGL init → Mesa/nouveau load → `nouveau_bo_new` for 1920×1080 RGBA8 → malloc NULL → Data Abort.

### Option A — Tesla Renderer Extraction (RECOMMENDED)

| File | Change |
|---|---|
| `src/projects/uMenu/source/main.cpp` | Remove `__nx_heap_size = 296_MB`, set to `16_MB`. Remove `__nx_win_init/exit`. Remove `pu::ui::render::Renderer::New()`. Add Tesla renderer init. |
| `src/projects/uMenu/Makefile` | Remove `-lpu -lSDL2* -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lwebp -lpng -ljpeg`. Add `-ltesla` or inline tesla.hpp. |
| `ui_Common.hpp` | Remove `#include <pu/Plutonium>`, add `#include <tesla.hpp>`. Map pu::ui::Color → Tesla color types. |
| `ui_MenuApplication.hpp` | Replace `pu::ui::Application` extension with custom app class wrapping `tsl::gfx::Renderer`. |
| `ui_RawRgbaImage.cpp` | Replace `SDL_CreateTexture` with Tesla FB blit. Add stb_image for PNG decode. |
| `ui_BackgroundScreenCapture.cpp` | **Line 15: `CaptureBufferSize = 0x384000` (3.67 MB static alloc). Make lazy/conditional.** Independent v0.6.6 fix regardless of Tesla port. |
| `ui_MenuApplication.cpp` | Rewrite layout tree as `tsl::elm::*` subclasses. |
| `ui_MainMenuLayout.cpp` | ~700 lines, full rework. Application grid + entry icons + user avatar. |
| `ui_SettingsMenuLayout.cpp` | Easiest layout — Tesla's TrackBar/ToggleListItem directly map. |
| `ui_StartupMenuLayout.cpp` | User picker — medium complexity, Tesla custom elements needed. |
| `ui_ThemesMenuLayout.cpp` | Theme thumbnails — hard without bitmap decode. |
| `ui_LockscreenMenuLayout.cpp` | Easy — text + rectangles only. |
| `ui_QuickMenu.cpp` | Easy — already overlay-like. |

**Plutonium coexistence NOT VIABLE:** Plutonium's `SDL_Init` initializes EGL+Mesa unconditionally in its `Load()`. Full Plutonium removal required.

### Option B — ImGui + Custom vi Framebuffer Backend

Keep ImGui (already vendored), remove SDL2 backend, write `imgui_impl_vitesla.cpp` driving a vi-layer framebuffer with software raster.
- Advantages: richer widget set than Tesla
- Disadvantages: +2 weeks over Option A, no existing Switch reference

### Effort Estimate

| Phase | Work | Duration |
|---|---|---|
| P1 | Remove SDL2/Mesa/EGL/Plutonium, Tesla renderer init, boot clean | 2-3 days |
| P2 | Port ui_Common + ui_MenuApplication base | 2 days |
| P3 | Port simple layouts (Lockscreen, QuickMenu, Settings) | 3-4 days |
| P4 | Port MainMenuLayout + icon decode via stb_image | 5-7 days |
| P5 | Port background capture, startup, themes | 4-5 days |
| P6 | Memory audit + fw 20.0.0 hw validation | 2-3 days |
| **Total** | | **~3-4 weeks** |

---

## 10. Top-3 Recommendations

1. **Use libultrahand (ppkantorski), not WerWolv/libtesla.** Active fork, NEON-optimized, multi-threaded draws, theme system. CC-BY-4.0 sublicense on libultra layer requires only attribution.

2. **Target 448×720 RGBA4444 framebuffer.** 1.26 MB double-buffer fits in fw 20.0.0's 6 MB budget with room for icons. 1080p RGBA8888 reproduces the exact crash. If 1080p is mandatory, use RGBA4444 at 960×540 (1.98 MB).

3. **Replace the 3.67 MB capture buffer with lazy/conditional alloc — independent of Tesla port.** `ui_BackgroundScreenCapture.cpp` line 40 does `new u8[0x384000]()` unconditionally at startup. Making it conditional on `IsSuspended() + user-enabled` frees 3.67 MB immediately. **Ship as v0.6.6 regardless of renderer decision.**

---

## 3 Architectural Decisions Required Before v0.7

1. **`vi:m` in uMenu's NPDM** — check `projects/uMenu/uMenu.json` service_access list. If missing, add it, or fall back to `viCreateLayer` (stray layer, no cross-context stacking priority).

2. **Audio subsystem** — Plutonium uses SDL2_mixer. Options: drop BGM in v0.7 (fastest) OR add libnx audren (+1 week).

3. **Icon image pipeline** — Tesla has no PNG/JPG decoder. Options: vendor stb_image.h (single-header, ~1100 lines) OR drop per-app icons in v0.7 (render colored rectangles + text labels).

---

## Source Index

- `https://github.com/ppkantorski/libultrahand` — active tesla.hpp
- `https://github.com/ppkantorski/Ultrahand-Overlay` — reference overlay impl
- `https://github.com/ppkantorski/nx-ovlloader` — host sysmodule v2.0.0+
- `https://github.com/ppkantorski/nx-ovlloader/releases/tag/v2.0.0` — fw 20 heap constant
- `https://github.com/WerWolv/nx-ovlloader/blob/5da25ae529e1a2988180a1a318177bf6f6de15d5/source/main.c` — upstream main.c line 74-75
- `https://switchbrew.org/wiki/Display_services` — vi + stray layer semantics
- `https://switchbrew.org/wiki/Applet_Manager_services` — AppletType privilege map
- `https://switchbrew.org/wiki/SVC#svcSetHeapSize` — heap size syscall
