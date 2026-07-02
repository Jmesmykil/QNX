# Switch Community NRO Architectural Audit (v3.1 windowed-homebrew compatibility)

> Pre-implementation reference for v3.1.  All NROs listed are evaluated against
> the universal VM-in-window model: NRO renders full-screen into the applet
> slot; uMenu captures via vi:s CaptureScreenshot and composites into a window.
> NROs are NOT modified.

Audit date: 2026-05-18  
Sources: GitHub repository source code via `gh api`, verified against live
HEAD commits as of audit date.

---

## Summary table

| NRO | FB model | Default res | Input | Applet-aware | Windowable verdict |
|---|---|---|---|---|---|
| sphaira | deko3d swapchain (nanovg-dk) | 1280×720 handheld / 1920×1080 docked — dynamic | `padInitializeAny` | YES — hooks OnFocusState, OnOperationMode, OnPerformanceMode | YES (capture-based) |
| NX-Shell | OpenGL 4.3 / EGL + ImGui | 1280×720 fixed | `padInitializeDefault` | NO — only `appletMainLoop` sentinel | YES |
| JKSV | SDL2 (hardware accel) | 1280×720 fixed | `padInitializeDefault` | NO — only `appletMainLoop` + applet-type warning | YES |
| Goldleaf | SDL2 via Plutonium | 1920×1080 default | `padInitializeWithMask` (Plutonium) | NO — Plutonium hides it | YES |
| TinWoo (DDinghoya fork) | SDL2 via Plutonium | 1920×1080 default | `padInitializeWithMask` (Plutonium) | NO — Plutonium hides it | YES |
| nx-hbmenu | deko3d swapchain (linear work-buffer) | 1280×720 fixed | `padInitializeAny` + touch | NO — only `appletMainLoop` | YES |
| RetroArch | OpenGL/EGL (primary) + Vulkan optional | 1280×720 handheld / 1920×1080 docked — dynamic | Legacy `hidInitialize*` (not padPoll) | YES — polls `appletGetFocusState` every frame | CONDITIONAL (see notes) |
| NXThemesInstaller | OpenGL 4.3 / GLFW + ImGui | 1280×720 fixed | GLFW gamepad (not libnx pad API) | YES — `appletGetAppletType` check on startup | YES |
| AIO Switch Updater | OpenGL / GLFW via Borealis | 1280×720 fixed | Borealis pad layer | NO — Borealis handles it | YES |
| sys-clk-manager | OpenGL / GLFW via Borealis | 1280×720 fixed | Borealis pad layer | NO — Borealis handles it | YES |
| Ultrahand-Overlay | libtesla / `.ovl` sysmodule | 1920×1080 overlay layer | Tesla input layer | YES — `.ovl` applet type | NOT APPLICABLE — overlay, not NRO |
| EdiZon SE | Linear framebuffer `framebufferCreate` | 1280×720 fixed | Legacy `hidKeysDown/Held` (pre-libnx2) | NO — only `appletMainLoop` | CONDITIONAL (see hazards) |
| DBI | Closed source (no public source repo) | Unknown (likely 720p based on community reports) | Unknown | Unknown | UNKNOWN |
| ftpd | deko3d swapchain + ImGui | 1920×1080 init / dynamic on mode change | `padInitializeDefault` | YES — `appletGetFocusState` hook, dynamic resize | YES |
| breeze (tomvita/breeze-beta) | deko3d swapchain (nanovg-dk) | 1280×720 fixed | Legacy `hidGetTouchScreenStates` | YES — `AppletType_LibraryApplet`, focus handling | CONDITIONAL (see notes) |
| SimpleModDownloader | SDL2 via Plutonium | 1920×1080 default | `padInitializeWithMask` (Plutonium) | NO — Plutonium hides it | YES |
| linkalho | OpenGL / GLFW via Borealis | 1280×720 fixed | Borealis pad layer | NO — Borealis handles it | YES |

---

## Per-NRO findings

### 1. sphaira

**Author:** ITotalJustice  
**Repo:** https://github.com/ITotalJustice/sphaira

**Framebuffer model:** deko3d swapchain + NanoVG-dk vector renderer.  
Initialised in `app.cpp` via `dk::DeviceMaker`, two framebuffers, `dk::SwapchainMaker`.  
Source: https://github.com/ITotalJustice/sphaira/blob/master/sphaira/source/app.cpp  
Source: https://github.com/ITotalJustice/sphaira/blob/master/sphaira/include/app.hpp

**Resolution / DPI assumptions:**  
Logical coordinate space is fixed at `SCREEN_WIDTH = 1280.f`, `SCREEN_HEIGHT = 720.f`
(defined in `sphaira/include/ui/types.hpp`).  
Physical swapchain size is dynamic: `GetFrameBufferSize()` calls `appletGetOperationMode()`
and returns 1280×720 (handheld) or 1920×1080 (docked).  
`nvgScale(vg, m_scale.x, m_scale.y)` compensates so all UI coordinates remain 1280×720
regardless of dock state.  
Source: https://github.com/ITotalJustice/sphaira/blob/master/sphaira/include/ui/types.hpp  
Source: https://github.com/ITotalJustice/sphaira/blob/master/sphaira/source/app.cpp (`GetFrameBufferSize()`)

**Touch:** Raw `hidGetTouchScreenStates` in physical pixel space, corrected by
`m_scale` before use. Touch coordinates in handheld mode will map 1:1; in docked
mode they are scaled down from 1920×1080 physical to 1280×720 logical.

**Input model:** `padInitializeAny(&m_pad)` — picks up any controller.  
Source: `app.cpp`, line `padInitializeAny(&m_pad)` in init block.

**Applet self-awareness:**  
YES — registers an applet hook (`appletHook`) and responds to
`AppletHookType_OnFocusState`, `AppletHookType_OnOperationMode`,
`AppletHookType_OnPerformanceMode`, and `AppletHookType_OnCaptureButtonShortPressed`.  
On mode change, it destroys and recreates the deko3d framebuffer resources.

**Frame pacing:**  
Target 60 fps, clamped between 15 fps and 120 fps:
```
constexpr double target_delta = 1000.0 / 60.0;
constexpr double min_delta    = 1000.0 / 120.0;
constexpr double max_delta    = 1000.0 / 15.0;
```
Uses `armTicksToNs` + timestamp delta. Main loop via `appletMainLoop()`.

**Q OS windowing compatibility verdict: YES**  
deko3d swapchain renders into the vi surface normally — `vi:s CaptureScreenshot`
will capture the full rasterised frame. The operation-mode hook will fire when
uMenu composites it into a fake handheld context, potentially causing a swapchain
rebuild; however, since uMenu controls the applet slot, the NRO receives
`AppletOperationMode_Handheld` (1280×720 path), which is the simpler path.
The only caution: the capture-button hook is wired but currently just logs — no
action taken.

---

### 2. NX-Shell

**Author:** Joel16  
**Repo:** https://github.com/joel16/NX-Shell

**Framebuffer model:** OpenGL 4.3 Core Profile via EGL (`EGL_CONTEXT_MAJOR_VERSION_KHR = 4`,
`EGL_CONTEXT_MINOR_VERSION_KHR = 3`) on `nwindowGetDefault()`.  
Dear ImGui renders on top (OpenGL3 backend, `imgui_impl_switch.cpp`).  
Source: https://github.com/joel16/NX-Shell/blob/master/source/gui.cpp

**Resolution / DPI assumptions:**  
`ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ...)` hardcoded in
`source/window.cpp`. The EGL surface inherits from `nwindowGetDefault()` which
defaults to 1280×720 unless explicitly resized. No `appletGetOperationMode` call
detected.

**Input model:** `padInitializeDefault(&bd->pad)` in `imgui_impl_switch.cpp`.
Standard `HidNpadButton_*` mapping.

**Applet self-awareness:** Minimal — only `appletMainLoop()` as the loop sentinel.
No mode-change hooks, no focus polling.

**Frame pacing:** `eglSwapBuffers` drives vsync. No explicit sleep or frame timer.
Rate governed by display VBLANK (60 fps in normal operation).

**Q OS windowing compatibility verdict: YES**  
Fixed 720p, EGL renders to the default NWindow surface. Capture will work cleanly.
No dynamic swapchain rebuild that could interfere. The ImGui 1280×720 hardcode means
the UI won't auto-scale to 1080p if ever needed, but for VM-in-window that is
irrelevant.

---

### 3. JKSV

**Author:** J-D-K  
**Repo:** https://github.com/J-D-K/JKSV

**Framebuffer model:** SDL2 hardware-accelerated renderer (`sdl::initialize`
wraps `SDL_CreateRenderer` with hardware flags).  
Source: https://github.com/J-D-K/JKSV/blob/master/source/JKSV.cpp  
Source: https://github.com/J-D-K/JKSV/blob/master/include/graphics/screen.hpp

**Resolution / DPI assumptions:**  
`graphics::SCREEN_WIDTH = 1280`, `graphics::SCREEN_HEIGHT = 720` — compile-time
constants, not dynamic.  
Source: https://github.com/J-D-K/JKSV/blob/master/include/graphics/screen.hpp

**Input model:** `padInitializeDefault` (wrapped in `input::initialize()`).
Source: https://github.com/J-D-K/JKSV/blob/master/source/input.cpp

**Applet self-awareness:** `appletGetAppletType()` is checked once on startup to
display a warning popup if running as an applet (not a full application). The loop
uses `appletMainLoop()` as sentinel. No mode-change hooks.  
Source: `JKSV.cpp`, `applet_mode_warning()` + `is_running()`.

**Frame pacing:** `jksv.update()` + `jksv.render()` in a tight while-loop gated
by `appletMainLoop()`. SDL presents via `SDL_RenderPresent`. Rate is vsync-locked
by SDL's default render flags.

**Q OS windowing compatibility verdict: YES**  
Fixed 720p SDL2. Standard capture path. The applet-type warning popup is benign —
it will appear if JKSV detects it is running as an applet rather than a full title,
but it does not abort execution.

---

### 4. Goldleaf

**Author:** XorTroll  
**Repo:** https://github.com/XorTroll/Goldleaf  
**Renderer library:** Plutonium (https://github.com/XorTroll/Plutonium)

**Framebuffer model:** SDL2 (`SDL_CreateRenderer`) via Plutonium's
`pu::ui::render::Renderer`. Hardware-accelerated flags (`RendererHardwareFlags`).  
Source: https://github.com/XorTroll/Plutonium/blob/master/Plutonium/source/pu/ui/render/render_Renderer.cpp

**Resolution / DPI assumptions:**  
Plutonium defaults to `ScreenWidth = 1920`, `ScreenHeight = 1080` unless
overridden. Goldleaf's `main.cpp` uses the default `RendererInitOptions` constructor
which passes `w = 1920, h = 1080`.  
Source: https://github.com/XorTroll/Plutonium/blob/master/Plutonium/include/pu/ui/render/render_Renderer.hpp

**Input model:** `padInitializeWithMask` inside Plutonium's renderer based on
`pad_id_mask`. Goldleaf adds `HidNpadIdType_Handheld` and `HidNpadIdType_No1`.

**Applet self-awareness:** No explicit applet hooks in Goldleaf or Plutonium.
Plutonium uses `SDL_RenderPresent` without dynamic mode switching.

**Frame pacing:** SDL vsync-locked via `sdl_render_flags`. Plutonium drives the
main loop.

**Q OS windowing compatibility verdict: YES**  
SDL renders to the nwindow surface at 1920×1080 by default. Capture works at
full resolution. No focus hooks that could interfere.

---

### 5. TinWoo / Tinleaf / TinNX

**Fork audited:** DDinghoya/TinWoo — https://github.com/DDinghoya/TinWoo  
**Note:** CVFireDragon/Tinwoo repo not found; DDinghoya is the widely-used fork.

**Framebuffer model:** SDL2 via Plutonium — identical to Goldleaf.  
Source: https://github.com/DDinghoya/TinWoo/blob/master/source/main.cpp  
(`pu::ui::render::Renderer::New(...)` with `RendererHardwareFlags`)

**Resolution / DPI assumptions:** Plutonium default 1920×1080 (same as Goldleaf).

**Input model:** Plutonium pad layer (`padInitializeWithMask`).

**Applet self-awareness:** None beyond `appletMainLoop`.

**Frame pacing:** SDL vsync-locked via Plutonium.

**Q OS windowing compatibility verdict: YES**  
Identical architecture to Goldleaf. Capture path is straightforward.

---

### 6. nx-hbmenu

**Author:** switchbrew  
**Repo:** https://github.com/switchbrew/nx-hbmenu

**Framebuffer model:** deko3d swapchain, but used in a hybrid mode:  
- GPU swapchain created with `dkSwapchainCreate` for presentation.
- A CPU-writable linear work-buffer (`DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuUncached`)
  holds the pixel data that software renders into.
- On each frame, a `dkCmdBufCopyBufferToImage` blit copies the linear buffer into
  the swapchain image.
This is effectively a software-rendered linear framebuffer uploaded through deko3d.  
Source: https://github.com/switchbrew/nx-hbmenu/blob/master/nx_main/nx_graphics.c

**Resolution / DPI assumptions:**  
`#define FB_WIDTH 1280` / `#define FB_HEIGHT 720` — hardcoded, no operation-mode
detection.  
Source: https://github.com/switchbrew/nx-hbmenu/blob/master/nx_main/main.c

**Input model:** `padInitializeAny(&g_pad)` + `padRepeaterInitialize` for long-press.
Touch via `touchInit()` (custom wrapper).

**Applet self-awareness:** `appletSetScreenShotPermission(AppletScreenShotPermission_Enable)`
explicitly enabled on startup. Only `appletMainLoop()` as loop guard. No mode-change
hooks.

**Frame pacing:** Tight loop — `graphicsPresent()` calls `dkQueuePresentImage` which
blocks on VBLANK. Effective 60 fps vsync-locked.

**Q OS windowing compatibility verdict: YES**  
Fixed 720p. The linear→swapchain upload path is fully GPU-submitted and the final
image lives in the vi surface — capture works identically to any other deko3d app.
Screenshot permission is explicitly enabled, which is a good sign.

---

### 7. RetroArch Switch port

**Author:** libretro  
**Repo:** https://github.com/libretro/RetroArch  
**Context driver:** `gfx/drivers_context/switch_ctx.c`

**Framebuffer model:** OpenGL via EGL (primary), optional Vulkan.  
`nwindowSetDimensions(ctx_nx->win, 1920, 1080)` called unconditionally on init —
sets the nwindow to full resolution before EGL surface creation.  
Source: https://github.com/libretro/RetroArch/blob/master/gfx/drivers_context/switch_ctx.c

**Resolution / DPI assumptions:**  
`switch_ctx_get_video_size` polls `appletGetOperationMode()` and returns 1280×720
(handheld) or 1920×1080 (docked). The driver is resolution-adaptive.

**Input model:** `hidInitializeTouchScreen()`, `hidInitializeMouse()`,
`hidInitializeKeyboard()` — uses **legacy hidInit** family, not the modern
`padInitializeDefault` API.  
Source: https://github.com/libretro/RetroArch/blob/master/input/drivers/switch_input.c

**Applet self-awareness:** YES — polls `appletGetFocusState()` in the platform
driver every frame. `platform_switch_has_focus` variable gates audio/rendering.  
Source: https://github.com/libretro/RetroArch/blob/master/frontend/drivers/platform_switch.c

**Frame pacing:** EGL vsync via `eglSwapBuffers`. RetroArch has a configurable
frame-delay/vsync system; by default it targets content-native refresh (typically
60 fps for most cores). The Switch platform does not add extra sleep.

**Q OS windowing compatibility verdict: CONDITIONAL**  
- EGL surface captures cleanly via vi:s — no fundamental blocker.
- `platform_switch_has_focus` polling: if the applet loses focus (expected during
  windowing), RetroArch will pause emulation and may blank the framebuffer.
  uMenu would need to keep the applet foregrounded (standard VM-in-window
  assumption), which sidesteps this.
- Legacy hid API: not a capture problem, but worth noting for future compatibility
  with newer firmware.
- `nwindowSetDimensions(1920, 1080)` on init: RetroArch always allocates at
  1920×1080 even in handheld mode. For VM-in-window this is fine — capture will
  deliver a 1920×1080 surface that uMenu downscales into the window.

---

### 8. NXThemesInstaller

**Author:** exelix11  
**Repo:** https://github.com/exelix11/SwitchThemeInjector  
**Switch binary:** `SwitchThemesNX/`

**Framebuffer model:** OpenGL 4.3 Core Profile via GLFW + glad + ImGui.  
`glfwCreateWindow(SCR_W, SCR_H, ...)`, then `glfwMakeContextCurrent`, then
ImGui's OpenGL3 backend.  
Source: https://github.com/exelix11/SwitchThemeInjector/blob/master/SwitchThemesNX/source/UI/UIManagement.cpp

**Resolution / DPI assumptions:**  
`SCR_W = 1280`, `SCR_H = 720` — compile-time constants in `UI.hpp`.  
Callback `windowFramebufferSizeCallback` scales `WRatio/HRatio` relative to
1280×720 baseline for ImGui framebuffer-scale.  
Source: https://github.com/exelix11/SwitchThemeInjector/blob/master/SwitchThemesNX/source/UI/UI.hpp

**Touch:** `hidGetTouchScreenStates` polled in `PlatformImguiBinds()` and
mapped to `io.MousePos`. Touch coordinates are divided by `GFX::WRatio` /
`GFX::HRatio` before use — correct for 720p but will produce wrong coordinates
if the framebuffer is ever resized (not a VM-in-window concern since capture
is fixed at the NRO's rendered resolution).

**Input model:** GLFW gamepad (`glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad)`) —
not the libnx `padInitialize` family. This means input is routed through GLFW's
HID abstraction layer, which internally uses libnx on Switch.

**Applet self-awareness:**  
`appletGetAppletType()` polled once at startup in `PlatformInit()` — sets
`UseLowMemory = true` if not running as a full Application.  
`glfwWindowShouldClose` gates the main loop (not `appletMainLoop`).

**Frame pacing:** `glfwSwapInterval(1)` — vsync-locked at 60 fps.

**Q OS windowing compatibility verdict: YES**  
OpenGL renders to nwindow. Capture path is standard. Fixed 720p.
The GLFW-based main loop (`glfwWindowShouldClose`) rather than `appletMainLoop`
is unusual but not a problem for capture.

---

### 9. AIO Switch Updater

**Author:** HamletDuFromage  
**Repo:** https://github.com/HamletDuFromage/aio-switch-updater

**Framebuffer model:** OpenGL / GLFW via Borealis framework (`brls::Application`).
Borealis uses NanoVG on top of OpenGL for vector UI.  
Source: https://github.com/HamletDuFromage/aio-switch-updater/blob/master/source/main.cpp

**Resolution / DPI assumptions:**  
Borealis (legacy branch) targets `WINDOW_WIDTH = 1280`, `WINDOW_HEIGHT = 720`.  
Source: Borealis `application.cpp` (`constexpr uint32_t WINDOW_WIDTH = 1280; WINDOW_HEIGHT = 720;`)  
Reference: https://github.com/natinusala/borealis/blob/legacy/library/lib/application.cpp

**Input model:** Borealis pad layer (wraps libnx HID).

**Applet self-awareness:** `brls::Application::mainLoop()` wraps `appletMainLoop`.
Borealis does not register explicit focus/mode hooks.

**Frame pacing:** Borealis `setMaximumFPS(60)` — sleep-based 60 fps cap.

**Q OS windowing compatibility verdict: YES**  
Identical architecture to sys-clk-manager (both use Borealis).
Fixed 720p, OpenGL, standard nwindow. Capture works cleanly.

---

### 10. sys-clk-manager

**Author:** retronx-team  
**Repo:** https://github.com/retronx-team/sys-clk (manager subdirectory)

**Framebuffer model:** OpenGL / GLFW via Borealis — identical to AIO Switch Updater.  
Source: https://github.com/retronx-team/sys-clk/blob/master/manager/src/main.cpp

**Resolution / DPI assumptions:** Borealis 1280×720 (same as AIO Switch Updater).

**Input model:** Borealis pad layer.

**Applet self-awareness:** `brls::Application::mainLoop()` only.

**Frame pacing:** Borealis 60 fps cap.

**Q OS windowing compatibility verdict: YES**  
Borealis + OpenGL. Fixed 720p. Capture is straightforward.

---

### 11. Ultrahand-Overlay / Ultrahand-Reload

**Author:** ppkantorski  
**Repo:** https://github.com/ppkantorski/Ultrahand-Overlay

**Framebuffer model:**  
NOT a standard NRO. Ultrahand is a **Tesla overlay** (`.ovl`) loaded by
nx-ovlloader. It renders via **libtesla** into a dedicated vi layer.
libtesla uses a `Framebuffer` at 1920×1080 with RGBA4444 pixel format in
a separate layer above the running game/applet.  
Source: `source/main.cpp` — `#define TESLA_INIT_IMPL`, `tsl::setNextOverlay(...)`,
`tsl::gfx::Renderer::get().setLayerPos(...)`.

**Resolution / DPI assumptions:** libtesla layer is 1920×1080 but rendered at
448 pixels wide (side panel) — it does not take the full screen.

**Input model:** Tesla input layer (hid gestures + button combo).

**Applet self-awareness:** YES — `.ovl` applet type is `AppletType_OverlayApplet`.
Entirely separate from normal NRO applet flow.

**Q OS windowing compatibility verdict: NOT APPLICABLE**  
Ultrahand is an overlay sysmodule, not a user-launched NRO. It runs
concurrently with other apps in its own applet slot. The VM-in-window model
(which targets user NROs in the applet slot) does not apply. No action needed.

---

### 12. EdiZon SE

**Author:** tomvita (fork of WerWolv's EdiZon)  
**Repo:** https://github.com/tomvita/EdiZon-SE

**Framebuffer model:** CPU-side linear framebuffer via libnx `framebufferCreate`:
```c
framebufferCreate(&Gui::g_fb_obj, nwindowGetDefault(), 1280, 720,
                  PIXEL_FORMAT_RGBA_8888, 2);
framebufferMakeLinear(&Gui::g_fb_obj);
```
Source: https://github.com/tomvita/EdiZon-SE/blob/master/source/main.cpp

**Resolution / DPI assumptions:**  
Hard-coded 1280×720 everywhere. Bounds checks in `gui.cpp`:
`if (x >= 1280 || y >= 720 || x < 0 || y < 0)` — pixels outside that range
are discarded.

**Input model:** **Legacy** `hidKeysDown(CONTROLLER_PLAYER_1)` /
`hidKeysHeld(CONTROLLER_HANDHELD)` — the pre-libnx2 API.  
This API was deprecated in libnx 4.x / firmware 11.x. It still works on modern
firmware but is not the preferred `padInitializeDefault` pattern.

**Applet self-awareness:** Only `appletMainLoop()` sentinel.
EdiZon SE also calls `appletCreateLibraryApplet` internally to launch the account
selector — this is a transient child applet, not an awareness hook.

**Frame pacing:** Tight loop, no explicit sleep or frame timer. Rate is gated by
`framebufferBegin`/`framebufferEnd` which blocks on VBLANK (60 fps).

**Q OS windowing compatibility verdict: CONDITIONAL — see Compatibility Hazards**  
The linear framebuffer writes pixels into the CPU-mapped surface that vi then
scans out. `vi:s CaptureScreenshot` will capture the scanout result correctly.
However, the legacy hid API and the LibraryApplet launch are noted hazards.

---

### 13. DBI

**Author:** duckbill (closed source)  
**Repo:** https://github.com/rashevskyv/dbi (docs/config only; no source code)

**Framebuffer model:** Unknown — source not public.  
Community-reported behaviour: DBI renders at 720p with a text-based / simple UI,
consistent with either SDL2 or a linear framebuffer.

**Resolution / DPI assumptions:** Unknown. Likely 1280×720 based on UI appearance.

**Input model:** Unknown.

**Applet self-awareness:** Unknown.

**Frame pacing:** Unknown.

**Q OS windowing compatibility verdict: UNKNOWN**  
Cannot be audited without source access. Based on community usage patterns and
the visual style of the UI (simple list-based, no complex 3D), it is likely
compatible, but this cannot be confirmed. Recommend empirical testing.

---

### 14. ftpd

**Author:** mtheall  
**Repo:** https://github.com/mtheall/ftpd

**Framebuffer model:** deko3d swapchain + Dear ImGui (deko3d backend).  
`dk::UniqueSwapchain s_swapchain` initialized at 1920×1080 max, rebuilt on
operation-mode change.  
Source: https://github.com/mtheall/ftpd/blob/master/source/switch/platform.cpp

**Resolution / DPI assumptions:**  
Initialized at 1920×1080 (`rebuildSwapchain(1920, 1080)` on startup).  
Applet hook triggers `rebuildSwapchain(s_width, s_height)` on mode change where
`s_width/s_height` is updated to 1280×720 (handheld) or 1920×1080 (docked).  
ImGui `s_focused` tracks focus state.  
Source: https://github.com/mtheall/ftpd/blob/master/source/switch/imgui_nx.cpp

**Touch:** `hidGetTouchScreenStates` polled, mapped to ImGui mouse in
`imgui_nx.cpp`. Coordinates used directly without scale correction — may produce
misaligned touch if physical res ≠ logical res, but this is a UX issue not a
capture issue.

**Input model:** `padInitializeDefault(&s_padState)`.

**Applet self-awareness:** YES — `appletHook` registered (`s_appletHookCookie`),
handles `AppletHookType_OnFocusState` and `AppletHookType_OnOperationMode`.
Focus state gates backlight control. Mode change triggers swapchain rebuild.

**Frame pacing:** deko3d `acquireImage` / `presentImage` — VBLANK-locked,
60 fps effective.

**Classic mode:** ftpd also ships a `--CLASSIC` build that uses `PrintConsole`
(text only, no GPU) — not relevant to windowing as it is a separate build target.

**Q OS windowing compatibility verdict: YES**  
deko3d swapchain, captures cleanly. The focus hook (`appletSetLcdBacklightOffEnabled`)
is benign — it only controls backlight, not rendering. The swapchain rebuild on
mode change is handled correctly and the NRO will stabilise at 720p when
uMenu signals handheld mode.

---

### 15. breeze (tomvita/breeze-beta)

**Author:** tomvita  
**Repo:** https://github.com/tomvita/breeze-beta

**Framebuffer model:** deko3d swapchain + NanoVG-dk — same stack as sphaira.  
`CApplication` framework (in `nanovg/source/framework/CApplication.cpp`).  
Source: https://github.com/tomvita/breeze-beta/blob/master/source/main.cpp  
Source: https://github.com/tomvita/breeze-beta/blob/master/nanovg/source/framework/CApplication.cpp

**Resolution / DPI assumptions:**  
`FramebufferWidth = 1280`, `FramebufferHeight = 720` — compile-time constants,
no operation-mode detection for dynamic resizing.  
Source: `source/main.cpp`, `static constexpr u32 FramebufferWidth = 1280;`

**Input model:** Legacy `hidGetTouchScreenStates` (raw HID call, not pad API).
Button input unclear from `CApplication.cpp` alone but the framework polls touch
directly.

**Applet self-awareness:** YES — but breeze declares itself as
`AppletType_LibraryApplet`:
```c
u32 __nx_applet_type = AppletType_LibraryApplet;
```
`CApplication` handles `appletGetFocusState` and `appletGetOperationMode` per frame
and calls virtual `onOperationMode` / `onFocus`.  
Source: `nanovg/source/framework/CApplication.cpp`

**Frame pacing:** `CApplication::onFrame` is called when `focused && !onFrame(...)`.
Frame timing via `armTicksToNs(armGetSystemTick() - tick_ref)`.

**Q OS windowing compatibility verdict: CONDITIONAL**  
breeze runs as `AppletType_LibraryApplet` — this is the same applet type that
uMenu would be launching it as. The applet is self-aware of focus and will gate
rendering on `focused == true`. As long as uMenu keeps the applet foregrounded,
rendering continues normally and capture works.  
Key concern: `LibraryApplet` mode may affect how the vi surface is allocated.
Verify in testing that `vi:s CaptureScreenshot` captures the LibraryApplet surface.

---

### 16. SimpleModDownloader

**Author:** ITotalJustice  
**Repo:** https://github.com/ITotalJustice/SimpleModDownloader  
*(Noted as deprecated; author recommends sphaira's mod manager instead.)*

**Framebuffer model:** SDL2 via Plutonium (same as Goldleaf / TinWoo).  
Source pattern confirmed by Plutonium dependency in project structure.

**Resolution / DPI assumptions:** Plutonium default 1920×1080.

**Input model:** Plutonium pad layer.

**Applet self-awareness:** No explicit hooks beyond `appletMainLoop`.

**Frame pacing:** SDL vsync via Plutonium.

**Q OS windowing compatibility verdict: YES**  
Same Plutonium/SDL2 architecture as Goldleaf. No blockers.

---

### 17. linkalho

**Author:** rdmrocha (original repo 404; impeeza/linkalho is the maintained fork)  
**Repo:** https://github.com/impeeza/linkalho

**Framebuffer model:** OpenGL / GLFW via Borealis (`brls::Application`).  
Source: https://github.com/impeeza/linkalho/blob/master/source/main.cpp

**Resolution / DPI assumptions:** Borealis 1280×720 fixed.

**Input model:** Borealis pad layer.

**Applet self-awareness:** `brls::Application::mainLoop()` only.

**Frame pacing:** Borealis 60 fps cap (sleep-based).

**Q OS windowing compatibility verdict: YES**  
Identical Borealis/OpenGL architecture to AIO Switch Updater and sys-clk-manager.

---

## Compatibility hazards

### HAZARD-1: EdiZon SE — legacy hid API + LibraryApplet sub-launch

**Severity: MEDIUM**  
**NRO:** EdiZon SE (tomvita/EdiZon-SE)

EdiZon SE uses the pre-libnx2 `hidKeysDown(CONTROLLER_PLAYER_1)` API. On recent
firmware (12+) this API is deprecated and its behaviour may differ from `padPoll`
in ways that affect multi-controller enumeration. This is not a capture problem
but a functional concern.

More critically: EdiZon SE calls `appletCreateLibraryApplet(AppletId_LibraryAppletPlayerSelect, LibAppletMode_AllForeground)`
during the account-select flow. This spawns a full-foreground child applet. When
running under uMenu's VM-in-window model, this child applet launch will attempt
to take over the foreground from uMenu's window manager. The child applet has no
awareness of being windowed and will try to acquire vi foreground directly.

**Recommendation:** Do not attempt to window an EdiZon SE session that triggers
account-select. The cheat-search main path (no account select) should be safe.

### HAZARD-2: RetroArch — focus-gated emulation pause

**Severity: LOW-MEDIUM**  
**NRO:** RetroArch (libretro/RetroArch)

`platform_switch_has_focus` is polled every frame. When the applet loses focus,
RetroArch pauses audio and may stop rendering. Under VM-in-window, uMenu must keep
the applet foregrounded or RetroArch will black-screen. This is the expected design
(uMenu holds foreground and captures from the applet slot), so this is a reminder,
not a blocker — as long as the applet slot remains active and focused.

Also: RetroArch uses `hidInitializeMouse` and `hidInitializeKeyboard` in addition
to standard gamepad. These do not affect capture but are worth noting for future
input-forwarding work in v3.1.

### HAZARD-3: breeze — AppletType_LibraryApplet self-declaration

**Severity: LOW**  
**NRO:** breeze (tomvita/breeze-beta)

breeze explicitly sets `__nx_applet_type = AppletType_LibraryApplet`. Most NROs
run as `AppletType_Application` or leave the type at the hbloader default. The
LibraryApplet type may affect how vi allocates surfaces and whether
`vi:s CaptureScreenshot` targets the correct layer. Empirical testing required.

### HAZARD-4: DBI — closed source

**Severity: UNKNOWN**  
**NRO:** DBI (rashevskyv/dbi)

No source available for inspection. Compatibility cannot be verified statically.
Treat as requiring hardware testing before inclusion in the v3.1 compatibility
matrix.

### HAZARD-5: EdiZon SE — linear framebuffer touch coordinate assumptions

**Severity: LOW**  
**NRO:** EdiZon SE

Linear framebuffer with hard-coded `x >= 1280` / `y >= 720` bounds checks means
touch input is permanently mapped to a 1280×720 physical pixel space. Under
VM-in-window, if the window is displayed at a different scale/offset, touch events
forwarded to the NRO would need to be re-mapped to the NRO's 1280×720 coordinate
space. This is a v3.1 input-forwarding concern, not a capture concern.

---

## Renderer family summary

Across all 17 entries:

| Renderer | NROs | Notes |
|---|---|---|
| deko3d swapchain | sphaira, nx-hbmenu, ftpd, breeze | Most modern pattern; all capture cleanly |
| SDL2 via Plutonium | Goldleaf, TinWoo, SimpleModDownloader | 1920×1080 default |
| OpenGL via EGL (direct) | NX-Shell | 1280×720 fixed |
| OpenGL via GLFW/Borealis | NXThemesInstaller, AIO Switch Updater, sys-clk-manager, linkalho | 1280×720 fixed |
| SDL2 (direct) | JKSV | 1280×720 fixed |
| libnx linear framebuffer | EdiZon SE | Legacy; capture works but touch mapping is a concern |
| OpenGL/EGL (RetroArch) | RetroArch | Adaptive resolution; focus-aware |
| libtesla overlay | Ultrahand-Overlay | Not applicable to VM-in-window |
| Closed source | DBI | Unknown |

**Pattern:** The majority of the catalog uses either deko3d swapchain (4) or
SDL2/OpenGL via a framework (Plutonium/Borealis/GLFW) (7). All of these render
into the standard vi surface via `nwindowGetDefault()` and are fully capturable
by `vi:s CaptureScreenshot`. Only EdiZon SE (linear framebuffer) and DBI
(unknown) require special attention. Ultrahand is not in-scope for VM-in-window.
