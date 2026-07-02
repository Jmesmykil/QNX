# L-Cycle Window Manager Design SSOT

> **Authored:** 2026-04-25T18:30:00Z
> **Author:** Design agent, sourced from codebase archaeology + Switch/libnx constraint analysis.
> **Status:** Design only. Implementation gated on K+1 through K+4 landing first.
> **Cross-refs:** `ui_MenuApplication.hpp` (MenuType, LoadMenu), `ui_IMenuLayout.hpp`,
> `qd_DesktopIcons.cpp` (LaunchIcon, HitTest), `docs/45_HBMenu_Replacement_Design.md`,
> `docs/K+1-FOLDERS-CATEGORIES-DESIGN.md` through `K+3-K+4-EDIT-MODE-RECENTS-DESIGN.md`

---

## 1. Why This Exists

Every Q OS menu transition today calls `g_MenuApplication->LoadMenu(MenuType::X)`.
LoadMenu tears down the current layout entirely and constructs the new one from
scratch. Vault opens? The desktop layout is gone. Monitor opens? The desktop layout
is gone. The user returns and the desktop reconstructs cold from disk.

Three concrete problems this causes:

1. Round-trip latency. Every Vault or Monitor open is a full SDL texture reload.
   On the Tegra X1's eMMC, that is 200-600ms of dead black screen.

2. No multitasking surface. The user cannot have Vault open while glancing at
   the desktop. There is no concept of "two things visible at once."

3. No visual continuity. Apps feel like pages in a book, not surfaces on a
   desktop. The real-computer model the creator is targeting requires windows.

The L-cycle introduces three subsystems that together fix this. They ship in
order: L+1 (window manager) first, L+2 (homebrew windowed launcher) second,
L+3 (task manager) third. L+2 depends on L+1. L+3 depends on L+1.

---

## 2. L+1 Window Manager Architecture

### 2.1 The problem with LoadMenu today

`LoadMenu` in `ui_MenuApplication.hpp` calls `pu::ui::Application::LoadLayout()`,
which Plutonium implements as: fade out, swap the active layout pointer, call
`OnLayoutAdded()` on the new layout, fade in. There is exactly one active layout
at any time. Plutonium's render loop calls `this->layout->OnRender(renderer)` once
per frame on a single layout.

To have multiple windows, we need multiple elements rendering simultaneously on
top of a persistent base layer. Plutonium already supports this via `pu::ui::Layout::Add(element)`.
The solution is not to replace Plutonium's layout model. It is to keep a single
persistent layout (the desktop) and render all windows as elements stacked on top
of it.

### 2.2 Class hierarchy

```
pu::ui::Element (base, Plutonium)
  └── QdWindowElement
        ├── QdWindowTitleBar      (drag region, title text, close button)
        └── QdWindowContentArea   (holds one QdWindowClient)

QdWindowClient (abstract base)
  ├── QdWindowClient_Vault        (wraps QdVaultLayout content)
  ├── QdWindowClient_Monitor      (wraps QdMonitorLayout content)
  ├── QdWindowClient_About        (wraps QdAboutLayout content)
  ├── QdWindowClient_Launchpad    (wraps QdLaunchpadElement content)
  └── QdWindowClient_TaskManager  (L+3 — see section 4)

QdWindowManager (owns N QdWindowElement, manages z-order and focus)
  Registered as a single element on MainMenuLayout
  MainMenuLayout::OnRender calls QdWindowManager::OnRender last (drawn on top)
  MainMenuLayout::OnMenuInput routes to QdWindowManager::OnInput first
```

The key decision: `QdWindowManager` is a Plutonium element, not a layout.
It lives on `MainMenuLayout` permanently. It renders on top of the wallpaper
and icon grid. When no windows are open it is a no-op (zero render cost).
When windows are open it composites them in z-order.

The existing `MenuType::Vault`, `MenuType::Monitor`, `MenuType::About`,
`MenuType::Launchpad` dispatch paths in `LaunchIcon` change from calling
`g_MenuApplication->LoadMenu(...)` to calling
`g_QdWindowManager->OpenWindow(QdWindowKind::Vault)` (or Monitor, About, etc.).
The old `QdVaultLayout` still exists as the source of truth for Vault business
logic. `QdWindowClient_Vault` delegates rendering and input to it, but wraps it
in the window chrome.

### 2.3 QdWindowManager state

```cpp
enum class QdWindowKind : u8 {
    Vault        = 0,
    Monitor      = 1,
    About        = 2,
    Launchpad    = 3,
    TaskManager  = 4,  // L+3
    // Future Q OS apps extend here
};

struct QdWindowState {
    QdWindowKind  kind;
    s32           x, y;           // top-left in 1920x1080 screen space
    s32           w, h;           // current size
    bool          minimized;      // not rendered, but in z-order list
    bool          focused;        // receives input
    u64           open_time_ns;   // for z-order tiebreak (newer = higher)
};

class QdWindowManager : public pu::ui::Element {
public:
    // Max concurrent windows. 6 is enough for all current Q OS apps.
    // Each window costs: window chrome textures (2-4 SDL_Texture) +
    // the client's own state. More than 6 risks the 448MB applet heap.
    static constexpr size_t MaxWindows = 6;

    void OpenWindow(QdWindowKind kind);
    void CloseWindow(QdWindowKind kind);
    void FocusWindow(QdWindowKind kind);
    void CycleWindowFocus(bool forward);  // shoulder button binding

    // pu::ui::Element overrides
    s32  GetX() override { return 0; }
    s32  GetY() override { return 0; }
    s32  GetWidth() override  { return 1920; }
    s32  GetHeight() override { return 1080; }
    void OnRender(pu::ui::render::Renderer::Ref &r, const s32 x, const s32 y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // Persistence
    void SaveWindowState();    // writes to sdmc:/ulaunch/window-state.bin
    void RestoreWindowState(); // reads on construct; fails silently if absent

private:
    std::array<QdWindowElement::Ptr, MaxWindows> windows_;
    size_t window_count_ = 0;
    size_t focused_idx_  = 0;  // index into windows_; MAX if none focused
};
```

`QdWindowManager` is constructed once in `MainMenuLayout`'s constructor (under
`#ifdef QDESKTOP_MODE`) and added to the layout via `this->Add(qd_wm_)`. It is
never destroyed until process exit.

### 2.4 Window chrome

Each `QdWindowElement` renders:

- A rounded-rectangle frame using `FillRoundRect` (already implemented in
  `qd_DesktopIcons.cpp`; extract to `qd_Drawing.hpp` shared utility).
- A title bar region (top 36px of window). Contains: drag handle zone (left
  80%), app name (centered in drag zone), close button (X glyph, right 20%).
- A content area (window height minus 36px). The `QdWindowClient` renders into
  this rect using a scissor clip.
- A focus ring (1px highlight border) when this window is the focused window.
- A resize handle: bottom-right 24x24px corner zone. Dragging it changes w/h.

Window default sizes (in 1920x1080 screen space):

| App | Default w | Default h | Default x | Default y |
|---|---|---|---|---|
| Vault | 860 | 680 | 120 | 140 |
| Monitor | 700 | 520 | 340 | 200 |
| About | 640 | 480 | 400 | 240 |
| Launchpad | 1400 | 820 | 80 | 80 |
| TaskManager | 640 | 480 | 400 | 240 |

Windows stack. The focused window is on top. Clicking a window that is not
on top brings it to front (updates z-order by moving it to the back of the
rendering list, because rendering order = back to front).

Minimum window size: 360x280 (title bar + enough content to be useful).
Maximum window size: 1800x960 (leaves the dock visible and a margin on all edges).

### 2.5 Input routing

`MainMenuLayout::OnMenuInput` changes to check `QdWindowManager` first:

```cpp
void MainMenuLayout::OnMenuInput(u64 down, u64 up, u64 held, pu::ui::TouchPoint tp) {
    // Window manager intercepts input before the desktop icons.
    if (qd_wm_ && qd_wm_->HasOpenWindows()) {
        qd_wm_->OnInput(down, up, held, tp);
        // Only fall through to desktop icons if WM did NOT consume the input.
        if (qd_wm_->ConsumedLastInput()) return;
    }
    // Existing desktop icon input handling below.
    qdesktop_icons_->OnInput(down, up, held, tp);
}
```

Key bindings when at least one window is open:

| Input | Action |
|---|---|
| Stick (analog) | Move cursor (existing cursor element continues to work) |
| R shoulder | Cycle window focus forward |
| L shoulder | Cycle window focus backward |
| Plus | Open window menu (list of open windows + "Close all" option) |
| B in focused window | Close that window |
| A on title bar + stick | Drag window |
| A on resize handle + stick | Resize window |
| Touch on any window | Focus that window; route touch to its client |
| Touch outside all windows | Route to desktop icons |

The existing Home-button double-press dev menu path (`qdesktop_last_home_press_ns`
in `MainMenuLayout`) is unaffected. Home button behavior does not change.

### 2.6 Window state persistence

File: `sdmc:/ulaunch/window-state.bin`

Schema (binary, packed):

```
[u8 magic: 0x57]   // 'W'
[u8 version: 1]
[u8 count]         // number of window state records
for each record:
  [u8  kind]       // QdWindowKind
  [s16 x]          // last x
  [s16 y]          // last y
  [u16 w]          // last w
  [u16 h]          // last h
  [u8  flags]      // bit0=was_open_at_exit, bit1=was_minimized
```

`SaveWindowState()` is called:
- On B-to-close for any window.
- When `QdWindowManager` is destroyed (process exit).

`RestoreWindowState()` is called in `QdWindowManager`'s constructor. Records
with `flags & 0x01` (was open at exit) are restored to their saved position
and size but NOT automatically reopened. The user's window positions are
remembered across reboots; windows do not reopen automatically (that would be
surprising on console UX).

### 2.7 What does NOT change

The upstream `MenuType::Settings`, `MenuType::Lockscreen`, `MenuType::Startup`,
`MenuType::Themes` paths continue to use `LoadMenu`. They are full-screen system
applets, not desktop windows. The window manager handles only the Q OS native apps.

The `LoadMenu` path is not removed. It is narrowed to system-mode transitions only.

---

## 3. L+2 Homebrew Window Launcher

### 3.1 What windowed NRO execution would require

For an NRO to render inside a Q OS window, Q OS would need to:

1. Intercept the NRO's framebuffer output before it reaches the display.
2. Composite that framebuffer as a texture inside the window rectangle.
3. Forward input to the NRO scaled to its coordinate space.

On Switch, the framebuffer pipeline is:

```
NRO process → libnx nvnFlip/SDL_RenderPresent → nvnss (display server in Horizon)
  → the Layer that nvnss owns for this applet context → the hardware compositor
  → HDMI/panel output
```

Q OS uMenu runs as a Library Applet in Atmosphère's qlaunch override. NROs run
in a second Library Applet context that uLoader sets up via
`appletInitializeLibraryAppletCreator`. Each applet context has its own Layer
and its own memory-mapped framebuffer. There is no mechanism in Atmosphère or
libnx for one applet to capture another applet's framebuffer.

The hypothetical paths and why each fails:

**Path A: Hook nvnFlip via Atmosphère sysmodule mitm.** Atmosphère's `sm` mitm
pattern lets a sysmodule intercept service calls. But `nvnss` is not a named
service in the standard IPC model; it is a kernel-level display session. There
is no Atmosphère hook point for inter-applet framebuffer capture. Forking
Atmosphère's `ns:am2` service to add such a hook is a multi-month project and
would require signing a custom NPDM, which Atmosphère-on-stock-FW only allows
in unsafe mode.

**Path B: `appletHookSurfaceUpdate` callback.** This libnx function registers a
callback for Applet events, but the `appletSurfaceUpdate` event notifies that the
foreground applet's surface ownership changed, not that a new frame is ready to
capture. It provides no buffer pointer. No framebuffer capture is possible via this
path.

**Path C: Run the NRO in-process.** Load the NRO into the uMenu process address
space and call its `nro_main()` directly. This fails because NRO init code calls
`appletInitialize()` which asserts it has not already been called. uMenu has
already called it. Double-init crashes.

**Path D: Shared memory ring buffer between uLoader and uMenu.** uLoader sends
each frame via a named shared memory block; uMenu reads and composites it.
Feasible in theory. Prohibitive in practice: 1920x1080 RGBA = 7.9 MB per frame.
At 30fps that is 237 MB/s through shared memory on Tegra X1, which has a shared
memory bandwidth of roughly 25.6 GB/s peak on paper but shared across CPU, GPU,
and display. In practice, uMenu is running its own render loop simultaneously.
The memory pressure alone would cause jitter. Additionally, the shared memory
API in libnx (`shmemCreate`) requires a kernel handle exchange via IPC. Getting
that handle from uLoader to uMenu requires a custom IPC service, which requires
registering with `sm`, which requires a named service known at build time. None
of this infrastructure exists today.

**Path E: libnx `viCreateLayer` for the NRO's surface.** In theory, uMenu could
create a second VI layer, hand its handle to the NRO via the argv string, and the
NRO could render into that layer while uMenu composites it. VI layer creation for
a non-foreground context requires `vi:s` (system-level VI service) access. Library
applets do not have `vi:s` access; only the system applet (qlaunch itself, which
uMenu replaces) does. uMenu-as-qlaunch-replacement does have elevated IPC access,
but even with that, mapping another applet's VI layer handle into the current
process requires kernel calls that Atmosphère does not expose via any HLE layer
short of a kernel patch.

### 3.2 Conclusion: native windowed NRO compositing is not feasible

On current Atmosphère + stock Horizon firmware, NRO framebuffer capture for
windowed display is not implementable without forking Atmosphère's display
stack and writing a custom kernel patch. That work is Phase 2/3 scope (Hekate
bare-metal lane), not Phase 1.5.

This is not a policy decision. It is a hardware/OS constraint.

### 3.3 Fallback design: windowed shell mode

The user's experience goal is: launch a homebrew tool without losing the desktop.
The L+2 fallback achieves this without framebuffer capture.

**Model:** the desktop is a persistent environment. NROs still do a full-screen
process swap via the existing `smi::LaunchHomebrewLibraryApplet` path. But Q OS
saves its visual state before the swap and restores it on return. From the user's
perspective: the desktop "goes away" while the NRO runs (full-screen, as today),
and when the NRO exits, the desktop "comes back" with all windows where they were.

The state that survives the NRO round-trip:

1. `window-state.bin` is written before launching the NRO.
2. Window positions, open/closed state, and focused window are all in that file.
3. On return from the NRO, `QdWindowManager::RestoreWindowState()` re-reads it
   and restores positions. Windows that were open are reopened to their last state.

This is the same mechanism as Section 2.6 persistence. No new file format needed.

**User-visible improvement over today:**

Today: tap NRO icon, desktop disappears, NRO runs, NRO exits, desktop reconstructs
cold, all prior state (which windows were open, cursor position) is lost.

After L+2: tap NRO icon, desktop disappears, NRO runs, NRO exits, desktop
reappears with all windows restored to their prior positions.

**What L+2 adds in code terms:**

In `QdDesktopIconsElement::LaunchIcon`, for `IconKind::Nro`, before calling
`smi::LaunchHomebrewLibraryApplet`:

```cpp
// L+2: persist window state so RestoreWindowState() finds it on return.
if (g_QdWindowManager) {
    g_QdWindowManager->SetPreLaunchSnapshot(true);
    g_QdWindowManager->SaveWindowState();
}
```

On return (when uMenu regains the foreground via the existing
`appletHookSurfaceUpdate` + `smi_Commands` resume path), call:

```cpp
if (g_QdWindowManager) {
    g_QdWindowManager->RestoreWindowState();
    g_QdWindowManager->SetPreLaunchSnapshot(false);
}
```

The `SetPreLaunchSnapshot` flag tells `RestoreWindowState()` to reopen windows
that were open at snapshot time (normal `RestoreWindowState` restores positions
only, does not reopen). This flag is the only behavioral difference between the
two call sites.

**A new "Launch" window:** when the user taps an NRO icon, a small transient
window appears over the desktop for the ~400ms before the display swap:

```
┌─────────────────────────┐
│  goldleaf               │
│  Launching...           │
│  [X] Cancel             │
└─────────────────────────┘
```

This window closes automatically when the applet swap begins. It gives the user
a moment to see which NRO they tapped and cancel if it was a misclick. The cancel
path calls `appletUnhook` and aborts the launch. If the NRO is already launched
(the swap already started), Cancel is a no-op.

This window is a `QdWindowClient_NroLaunch` that the WM opens for 1 second max
before the display swap takes over.

### 3.4 What L+2 explicitly does NOT do

- It does not capture NRO framebuffer.
- It does not run NROs concurrently with the desktop.
- It does not reduce the full-screen swap latency. That is uLoader's domain.
- It does not require any Atmosphère modification.

---

## 4. L+3 Task Manager

### 4.1 Overview

A floating window showing all active processes and Q OS background tasks.
Refreshes at 1 Hz. Provides focus and terminate controls.

### 4.2 Data sources

Three categories of information, each from a different libnx API:

**A. Running applet processes**

`appletGetCurrentFocusState()` tells you whether uMenu is in foreground, background,
or suspended. `appletGetOperationMode()` gives handheld/docked. Neither gives a
process list.

For a process list: `svcGetProcessList()` (requires `DebugOrAbove` permission, which
uMenu as qlaunch-replacement does have via its NPDM). Returns up to 300 PIDs.
Filter by comparing each PID's program ID (via `svcGetProcessId` and then matching
against known title IDs) to skip Horizon kernel/system threads.

**B. Memory usage per process**

`svcGetInfo(InfoType_TotalMemoryUsage, proc_handle, 0, &bytes)` returns the
total heap+code+stack byte count for a process handle. Call this for each PID
that passes the filter above. The call costs roughly 1 microsecond on Tegra X1.
At 1 Hz refresh and up to 20 visible processes, that is 20 microseconds total
per refresh. Well within budget.

**C. CPU usage (sampled)**

Horizon does not expose a native CPU percentage counter per process. To get CPU
usage, take two samples of a process's cycle counter and divide by wall time:

```cpp
struct CpuSample {
    u64 pid;
    u64 cycles_at_sample;  // from svcGetDebugFutureThreadInfo or svcGetThreadContext
    u64 wall_ns_at_sample; // from armTicksToNs(armGetSystemTick())
};
```

The problem: `svcGetDebugFutureThreadInfo` requires opening the process as a debug
handle via `svcDebugActiveProcess`. Opening a debug handle suspends the target
process. Suspending an active game to read its CPU counter is not acceptable.

Feasible alternative: use `svcGetInfo(InfoType_CpuTimeInfo, ...)`. This returns
cumulative user-mode CPU ticks for a process handle (not suspended). Take two
readings 1 second apart; divide by 1 second in ticks; multiply by 100.

This requires a process handle, not just a PID. Get the handle by opening the
process with `svcOpenProcess(pid, &handle)`. This requires `DebugOrAbove` but
does NOT suspend the target. Close the handle after reading.

If `svcOpenProcess` returns a "not permitted" result for system processes, that
is a signal to mark those processes as "system, CPU info unavailable" in the UI.

**D. Q OS background tasks**

The telemetry ring buffer already tracks Q OS internal tasks. Read the live ring
buffer state from the existing `QdTelemetry` channel to show active log categories
(INPUT, ANIM, FINDER, VAULT) as pseudo-tasks in the task manager.

### 4.3 UI layout

Window size: 640x480 (resizable per L+1 WM).

```
┌─ Task Manager ─────────────────────────────────────────────── [X] ─┐
│ Refresh: 1 Hz                              [Sort: CPU ▾]           │
├──────────────────────────────────────────────────────────────────────┤
│  NAME                    PID     MEM        CPU    ACTION           │
│  uMenu (this)            0x8     42.1 MB    12%    (cannot kill)    │
│  goldleaf.nro            0x9F    18.4 MB     3%    [Focus] [Kill]   │
│  Atmosphere-dmnt         0x7A     1.2 MB    <1%    (system)         │
│  Atmosphere-loader       0x7B     0.9 MB    <1%    (system)         │
│  Q OS Telemetry Ring     --      640 KB     <1%    (internal)       │
│  Q OS Harness            0xAB     8.2 MB    22%    [Focus] [Kill]   │
│                                                                      │
│  Total: 6 processes   71.4 MB used                                  │
│  [Kill All Non-System]                                               │
└──────────────────────────────────────────────────────────────────────┘
```

### 4.4 Rows and columns

| Column | Source | Width |
|---|---|---|
| NAME | Program ID matched against known title IDs; fallback to hex PID | 200px |
| PID | Decimal | 60px |
| MEM | `svcGetInfo(InfoType_TotalMemoryUsage)` formatted as MB | 80px |
| CPU | Sampled as described in 4.2 C | 60px |
| ACTION | Conditional buttons (see below) | 200px |

Action column logic:

- If PID is uMenu itself: "(cannot kill)" label, no button.
- If PID is a known Atmosphère system module (dmnt, loader, sm, pm, etc.):
  "(system)" label, no button.
- If PID is a known Horizon service (nvnss, vi, audio, etc.): "(system)" label.
- Anything else: `[Focus]` and `[Kill]` buttons.

`[Focus]` calls `appletRequestForeground()` with the target AppletId if it is
an applet. For sysmodules, Focus is disabled (sysmodules have no window).

`[Kill]` calls `svcTerminateProcess(handle)`. Before terminating, a confirmation
dialog: "Kill [name]? This will close the application." On confirm, terminate.
On cancel, no action. After termination, the refresh at the next 1 Hz tick will
show the process as gone.

`[Kill All Non-System]` kills every process in the ACTION=enabled set. Confirmation
dialog: "Kill all non-system processes? ([count] processes will be closed.)"

### 4.5 Refresh strategy

A 1 Hz timer using the existing `frame_tick_` mechanism in the render loop:

```cpp
// In QdWindowClient_TaskManager::OnRender:
if (frame_tick_ % 60 == 0) {  // 60 fps assumed; 60 frames = 1 second
    RefreshProcessList();
}
```

`RefreshProcessList()` calls `svcGetProcessList` (one syscall), then for each PID
in the result that is not already in the cached list, calls `svcGetInfo` for memory
and `svcOpenProcess` + `svcGetInfo(CpuTimeInfo)` + `svcCloseHandle` for CPU.

For processes already in the cached list, only update memory and CPU (skip
`svcGetProcessList` per-process overhead by keying on stable PID).

The first refresh at window-open time is immediate (does not wait for a 60-frame
boundary). Subsequent refreshes are at 1 Hz.

### 4.6 Anti-stub note on CPU sampling

If `svcGetInfo(InfoType_CpuTimeInfo, ...)` is not available on firmware 20.0.0
(the version in the live test environment), then CPU column shows `N/A` for all
processes and a footnote in the window: "CPU info requires FW 12.0.0+." The
absence of CPU data does not reduce the utility of the memory and process-list
view. The `N/A` path is a real code path, not a stub.

---

## 5. Atmosphère and libnx Constraints

This section records what Switch hardware actually allows. These are not design
preferences; they are firmware-enforced limits.

### 5.1 Library Applet heap ceiling

uMenu runs as a Library Applet. Library Applets have a fixed maximum heap:
448 MB on Switch OG (Erista). The GPU pool fix (SP2) reduced the GPU allocation
from 8 MB to 3.5 MB. The remaining usable heap for window chrome textures,
window client state, and process list data is approximately 40-60 MB depending
on how many SDL textures are loaded.

Each `QdWindowElement` allocates:
- Window chrome background: one SDL_Texture (approx 1-2 MB at window size)
- Title bar text: two cached textures (small, < 100 KB each)
- The client's own allocations (Vault: icon textures ~15 MB; Monitor: minimal)

With MaxWindows=6 and all windows open: worst case ~80 MB additional GPU texture
memory if all six windows are Vault-class. This exceeds the available headroom.

Mitigation: evict GPU textures for minimized windows. A minimized window's client
calls `ReleaseGpuTextures()` to free SDL textures. On `Restore`, they are
reloaded (same as cold-open). The window position and state persist. Only textures
are freed. This keeps peak GPU allocation bounded by the number of non-minimized
windows, not total window count.

### 5.2 No inter-applet framebuffer capture

Covered in Section 3.1. Hard limit. Not a firmware version issue.

### 5.3 No concurrent NRO execution in applet context

`appletInitialize()` is single-instance. Only one NRO can be hosted by uLoader
at a time. This is structural to how Horizon's applet service model works.
Running two NROs simultaneously would require two independent Library Applet
slots, which requires `ns:am` system-level applet management. Library Applets
do not have access to `ns:am`.

### 5.4 `svcTerminateProcess` permission scope

uMenu-as-qlaunch has `DebugOrAbove` in its NPDM. On the live test environment
(FW 20.0.0), the uMenu NPDM is the upstream XorTroll v1.2.0 NPDM (the Q OS fork
NPDM was rejected by FW 20.0.0 per ROADMAP.md). Until the NPDM issue is resolved,
`svcTerminateProcess` may fail for processes outside uMenu's applet group.
The Task Manager handles this: if `svcTerminateProcess` returns a non-success
result, show "Kill failed: permission denied" toast instead of crashing.

### 5.5 Input model is polling, not event-driven

libnx `padUpdate()` and `touchScreenRead()` poll once per frame. There is no
asynchronous input event. The window manager's drag-and-resize implementation
samples touch position every frame while the A button is held. This is the same
model `qd_DesktopIcons.cpp` already uses for cursor movement and click detection.
No new input model is needed.

### 5.6 SDL_RenderCopyEx and rotation

Window drag and resize do not rotate the SDL texture. `SDL_RenderCopyEx` is used
in K+3 edit mode for icon wiggle (already in the codebase). For window chrome,
only `SDL_RenderCopy` and `SDL_RenderDrawRect` are needed. No rotation.

---

## 6. Implementation Order

```
K+1 (folders)
K+2 (settings + filter)
K+3 (edit mode)      ─────────────────────────────────────────────────────┐
K+4 (recents)        ─────────────────────────────────────────────────────┤
                                                                          ▼
L+1.0: QdWindowManager skeleton + QdWindowElement chrome + persistence
  │  (window open/close/drag/resize/z-order working; no app content inside yet)
  │
L+1.1: QdWindowClient_Vault wired up
  │  (Vault opens in a window instead of LoadMenu; all Vault functionality works)
  │
L+1.2: QdWindowClient_Monitor, QdWindowClient_About, QdWindowClient_Launchpad
  │  (All four existing Q OS apps windowed; LoadMenu removed for these four)
  │
L+2.0: Pre-launch snapshot save + restore-on-return for NRO launches
  │  (QdWindowClient_NroLaunch transient window; window positions survive NRO round-trip)
  │
L+3.0: QdWindowClient_TaskManager
       (Process list, memory, CPU sampling, kill/focus controls)
```

Each step is a separate version in the version chain. The version numbers slot
after the K-cycle entries (K+4 is the last K entry):

| Version | Feature |
|---|---|
| L+1.0 | WM skeleton: open/close/drag/resize/z-order/persistence |
| L+1.1 | Vault windowed |
| L+1.2 | Monitor + About + Launchpad windowed |
| L+2.0 | NRO round-trip window state survival + launch window |
| L+3.0 | Task Manager window |

There is no L+1.3, L+2.1, or other sub-versions planned. If a step needs a
bugfix after landing, it gets its own bugfix version per the existing ROADMAP.md
"one change per version" rule.

---

## 7. Anti-stub Gates

Per global rule R42: every L-cycle version is DONE only when all of these pass.

### L+1.0 gates

1. Open a window. Drag it. Resize it. Close it. The window is gone. No crash.
2. Open two windows. Click the back window. It comes to front. The prior front
   window is now behind it. Z-order confirmed by visual inspection.
3. Reboot uMenu. Previously placed window opens at its saved position from
   `window-state.bin`. File round-trip verified.
4. Open MaxWindows (6) windows. Verify no crash and no GPU texture OOM. Verify
   minimizing a window frees its GPU textures (log line in telemetry).
5. B-button closes the focused window. Plus opens the window list menu.
   Shoulder buttons cycle focus. All verified on hardware.

### L+1.1 gates

6. Open Vault via dock icon. Vault opens inside a window (not full-screen).
   Browse files. Launch an NRO from inside the Vault window. Vault window
   persists when Q OS regains foreground (L+2.0 rounds this out further).
7. Close Vault window. Desktop icon for Vault is back to its normal state.
   Tapping it reopens Vault in the window again.

### L+1.2 gates

8. Monitor, About, Launchpad each open in windows. All three work with their
   full existing functionality inside the window chrome.
9. All four apps (Vault, Monitor, About, Launchpad) open simultaneously. No
   heap exhaustion. All four responsive to input.
10. `LoadMenu(MenuType::Vault/Monitor/About/Launchpad)` is removed from
    `LaunchIcon`. If any code path still calls it for these four types, that
    is a test failure.

### L+2.0 gates

11. Open Vault and Monitor windows. Launch a NRO from the dock. Both windows
    come back to their prior positions after the NRO exits.
12. `SetPreLaunchSnapshot(true)` is set before the launch. `RestoreWindowState()`
    reopens the windows that were open. Confirmed by log lines in telemetry.
13. The NroLaunch transient window appears for max 1 second before the display
    swap. Tapping Cancel before the swap aborts the launch. Confirmed on hardware.

### L+3.0 gates

14. Task Manager window opens. Process list renders with PID, memory, CPU.
    At least uMenu itself appears in the list.
15. Kill a non-system process (the test harness NRO if running, or a known safe
    target). Process disappears from the list on the next 1 Hz refresh.
16. Attempt to kill a known system process. Confirm the button is absent and
    "(system)" label is shown. No kill dialog appears.
17. 1 Hz refresh rate confirmed: telemetry log shows `TaskManager: refresh`
    entries at 60-frame intervals.

---

## 8. Open Questions for Creator

These are decisions the design cannot make without explicit creator input.

1. **App window sizes.** The defaults in Section 2.4 are estimates based on 1920x1080
   screen space. Are these proportions acceptable? Launchpad at 1400x820 leaves very
   little desktop visible. Should Launchpad be maximized by default instead?

2. **Window title bar height.** 36px is the minimum for a close button and drag zone.
   The Switch physical screen at 720p (scaled to 1080p) makes 36px narrow. Should
   this be 48px?

3. **Full-screen escape hatch.** Should windows have a "maximize to full screen"
   button that brings back the old LoadMenu behavior for apps that work better
   full-screen (like Launchpad)? Or is windowed-only the permanent model?

4. **NRO icon click behavior after L+2.** Today: click NRO icon, full-screen swap
   immediately. After L+2: click NRO icon, transient launch window appears for
   1 second, then swap. Is the 1-second preview window wanted, or should the old
   immediate-swap behavior be preserved for NROs (only windows get the WM, NROs
   stay full-screen immediate)?

5. **Task Manager: kill confirmation dialog.** The design requires a confirmation
   dialog before every kill. On Switch, dialogs require touch or button navigation.
   Is the extra step acceptable, or should Kill be immediate with an Undo window
   (5-second toast with undo button, after which the kill is irreversible)?

6. **CPU accounting on FW 20.0.0.** If `svcGetInfo(InfoType_CpuTimeInfo)` returns
   an access violation on FW 20.0.0 with the upstream XorTroll NPDM (because
   DebugOrAbove is absent), should the CPU column be hidden entirely or show N/A?
   This depends on what the live hardware test reveals. The NPDM situation (fork
   NPDM rejected by FW 20.0.0, per ROADMAP.md) means we are running with reduced
   privilege until the NPDM issue is resolved.

7. **MaxWindows = 6.** Is 6 concurrent windows enough, or should the ceiling be
   higher? Each window past 4 starts to crowd a 1920x1080 screen. If more than 4
   are typically needed, the design should add a window tray/taskbar rather than
   stacking more floating windows.

8. **Window persistence for reopened windows after NRO.** The L+2 design reopens
   windows that were open before the NRO launched. If the NRO crashes and Q OS
   regains foreground via the crash recovery path (not the clean exit path), should
   the same restore behavior happen, or should windows come back closed?

---

## 9. Cross-References

- `ui_MenuApplication.hpp` — MenuType enum, LoadMenu (paths 4 of these migrate to WM)
- `ui_IMenuLayout.hpp` — IMenuLayout base class (L+1 WM does NOT inherit this)
- `qd_DesktopIcons.cpp` — LaunchIcon (L+2 modifies the NRO launch path here)
- `src/projects/uLoader/source/main.cpp` — uLoader chainload entry (unchanged by L-cycle)
- `docs/45_HBMenu_Replacement_Design.md` — Vault design; L+1.1 wraps this in a window
- `docs/K+1-FOLDERS-CATEGORIES-DESIGN.md` through `K+3-K+4-EDIT-MODE-RECENTS-DESIGN.md`
  (L-cycle assumes K+1..K+4 landed)
- `ROADMAP.md` — version chain; L versions slot after K+4 in the chain
- `STATE.toml [qos_ulaunch_fork]` — SSOT for current version; update when L versions land
- SwitchBrew wiki: `Process_Manager_services` (svcGetProcessList, svcOpenProcess,
  svcTerminateProcess permissions), `Display_services` (VI layer model),
  `Applet_Manager_services` (Library Applet heap limits)
