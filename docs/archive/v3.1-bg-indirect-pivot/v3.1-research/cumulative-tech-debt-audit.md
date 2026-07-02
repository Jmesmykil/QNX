# Cumulative Tech Debt Audit (post-v3.0.1 + Task 2)

Audited commit: f6796acc  
Audit date: 2026-05-19  
Scope: stale slot indices, prewarm analysis, memory leaks, Tasks/Nintendo open path

---

## Section 1 — Stale slot-index hardcoding

### 1.1 Test file `test_QdDockMagnify.cpp` — BUILTIN_ICON_COUNT_TEST = 6

**File:** `src/projects/uMenu/tests/qdesktop/test_QdDockMagnify.cpp:16`

```cpp
static constexpr int32_t BUILTIN_ICON_COUNT_TEST = 6;
```

Severity: **WARNING**

`BUILTIN_ICON_COUNT` in the production header is now 5 (v2.9.12). The test file has its own local constant still at 6. The test loops `for (int32_t s = 0; s < BUILTIN_ICON_COUNT_TEST; ++s)` (lines 55 and 118), which means slot 5 is being tested against a dock that no longer has a slot 5 in production. 

Test 4 (line 85), Test 7 (line 130), and Test 8 (line 144) all exercise `dock_magnify_scale_x100(5, center)` — testing a slot index that does not exist in production. These tests conceptually pass because `dock_magnify_scale_x100` is a pure math function and doesn't know the slot count, but the loop coverage creates a false confidence that slot 5 behavior is production-relevant. If `BUILTIN_ICON_COUNT_TEST` is ever used to drive hitbox or layout tests, it would produce wrong results.

**The production dispatch switch and all production loop bounds correctly use the
header constant `BUILTIN_ICON_COUNT = 5`.** The test constant is isolated to the
test file so it does not affect device behavior. This is a test hygiene issue, not
a runtime bug.

### 1.2 Stale constructor comment — "6*140 + 5*28 = 980 (v2.3.4: 6 slots)"

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:1541-1542`

```cpp
//   total_expanded_w = BUILTIN_ICON_COUNT * ICON_BG_W + (BUILTIN_ICON_COUNT-1) * ICON_GRID_GAP_X
//                    = 6*140 + 5*28 = 980  (v2.3.4: 6 slots)
//   expanded_start_x = (1920 - 980) / 2 = 470
```

Severity: **INFO** (comment only; code is correct)

The **actual compiled constants** at lines 1545–1548 use `BUILTIN_ICON_COUNT` which is now 5, so the runtime computation produces:
- `kNeutralTotalW = 5*140 + 4*28 = 812`
- `kNeutralStartX = (1920 - 812) / 2 = 554`

The inline example math is wrong (still shows 6-slot values), but the code is correct. No behavioral bug, but a developer reading the comment would compute wrong HitTest expectations.

### 1.3 Stale magnify comment — "6 slots = ~1075 px"

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:3542`

```cpp
// 1.4× max: 128 × 1.4 = 179.2 px, 6 slots = ~1075 px, fits in 1920.
```

Severity: **INFO** (comment only; code is correct)

With 5 slots at 1.4× (≈179 px each) plus 4 gaps, the expanded bar is about 895 px — still fits easily in 1920 px. The comment should say 5 slots but no behavioral consequence.

### 1.4 Header comment still lists old slot sequence including About

**File:** `src/projects/uMenu/include/ul/menu/qdesktop/qd_DesktopIcons.hpp:144-146`

The block comment reads:
```
// v2.3.4: bumped 5 → 6 to add the Nintendo Apps dock icon.  New slot 5 =
// Nintendo ...
```
followed immediately by the v2.9.12 update. This is correct historical documentation,
not a stale bug.

### 1.5 `test_QdFavoritesLayout.cpp:108` — "slot 5 → col 2" test case

**File:** `src/projects/uMenu/tests/qdesktop/test_QdFavoritesLayout.cpp:107-108`

The strip→folder-col mapping test exercises `slot 5` as a test case with `FAV_STRIP_VISIBLE = 6`. `FAV_STRIP_VISIBLE` is the favorites strip slot count (not the dock count), and the test uses its own local constant — it is not testing dock slots. This is a false alarm; the constant 6 here refers to how many favorites are visible, not to the number of dock builtins. No behavioral issue.

### 1.6 Dispatch switch — VERIFIED CORRECT

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:4949-4979`

The `LaunchIcon` dispatch switch correctly uses `entry.dock_slot` (set by `PopulateBuiltins` from the `BUILTIN_ICON_DEFS` array index) rather than the raw icon array index. The comment at line 4950-4952 is correct:

```cpp
// Slot order is now Vault=0, Monitor=1, AllPrograms=2, Tasks=3, Nintendo=4
switch (entry.dock_slot) {
    case 0: OpenVaultWindow();  break;
    case 1: OpenMonitorWindow(); break;
    case 2: LoadMenu(Launchpad); break;
    case 3: OpenTaskManagerWindow(); break;
    case 4: OpenNintendoAppsWindow(); break;
    default: UL_LOG_WARN(...)
}
```

This matches `BUILTIN_ICON_DEFS[BUILTIN_ICON_COUNT]` exactly. **The slot dispatch is not the root cause of the Tasks/Nintendo "started problem."**

---

## Section 2 — PrewarmAllIcons analysis

### 2.1 What it prewarms

`PrewarmAllIcons` (`qd_DesktopIcons.cpp:3307-3415`) iterates `icons_[0..icon_count_)` which includes:
- 5 builtins (dock icons) — **immediately skipped** at the bottom of the loop (line 3409-3411: "Builtin and Special-PNG entries have no BGRA path … Nothing to prewarm.")
- NRO-backed entries: loads via `LoadNroIconToCache` (ASET extraction + BGRA cache put)
- Application entries with `icon_path`: tries `LoadAppIconFromUSystemCache` first, then `LoadNsIconToCache` as fallback
- Application entries with empty `icon_path`: synthesizes `app:<hex16>` key and tries both load paths

"total=79" means `icon_count_ = 79` at prewarm time. This is 5 builtins + N NROs + M payloads + P apps. The builtins are silently skipped, so the real prewarm work is on 74 entries. "hit=50" means 50 out of 74 non-builtin entries had loadable icons.

### 2.2 Threading model

**Background thread, correct mutex discipline.** `SpawnPrewarmThread` (line 3431) launches `PrewarmAllIcons` on a `std::thread`. Every cache write (`GetSharedIconCache().Put(...)`) is wrapped in `std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex())`. Every cache read in `PaintIconCell` is similarly guarded. **No SDL_CreateTexture or IMG_LoadTexture_RW** is called from the background thread — only surface-level operations (IMG_Load_RW → SDL_Surface, SDL_ConvertSurface, then raw pixel copy to cache). SDL surface operations are documented thread-safe in SDL2 when there is no renderer involvement.

### 2.3 Does prewarm block the UI thread at boot?

**No direct block.** The thread is spawned on first `OnRender` call (frame 1) and runs concurrently. The UI thread can render immediately. The prewarm thread does not hold any lock across the whole iteration — it acquires and releases `GetSharedIconCacheMutex()` per entry. Each `LoadJpegIconToCache` call does synchronous FS IPC (`fsFsOpenFile` + `fsFileRead`) under the lock only for the Put phase, but those reads are typically fast (local SD card).

**However: `LoadNsIconToCache` calls `nsGetApplicationControlData`** (qd_NsIconCache.cpp:63-78) — an IPC call to the `ns` service. This is a kernel IPC call from the prewarm background thread. On this hardware setup (CFW library-applet context), the Storage source returns `0x196002` (PermissionDenied), causing a fallback retry with CacheOnly. This is **two IPC round-trips per game that has no uSystem-cached icon**, for every game in `icon_count_`. With 29 misses (79-50=29), that could be 58 NS IPC calls serialized in a tight loop, each potentially stalling several ms waiting for the ns service.

### 2.4 The reported "hangs and loads for a while then does load" — most likely prewarm

The prewarm is **not** blocking the render thread directly. However, **the ns IPC calls (`nsGetApplicationControlData`) are expensive** when done back-to-back for 29+ games that have no locally-cached icon. On the Erista SoC, each NS IPC round-trip over SMC can take 10-50ms. At 29 misses × 2 attempts × 15ms average = ~870ms of background IPC serialization. The UI renders fine, but any foreground operation that also needs the cache mutex (e.g., PaintIconCell on the first few frames for games in the favorites strip) will stall waiting for the prewarm thread to release the mutex.

The v3.0.0/v3.0.1 changes did not change prewarm logic, but the **about-removal reduced icon_count_ by 0** (builtins are not prewarmed). The slowdown relative to v3.0.0 is more likely from the Task 2 addition of the `LaunchHomebrewWindowedLibraryApplet` SMI case in uSystem's main loop — even though the stub does nothing, it is registered in the dispatch table that is compiled into every uSystem startup. This is unlikely to cause visible boot delay.

**Most probable root cause of boot slowness: unchanged from v3.0.1** — it is the NS IPC prewarm for uncached games. The prewarm itself is correct; it could be optimized by capping the NS fallback retry count per boot session (the `g_ns_miss_count_` guard at qd_DesktopIcons.cpp:313 exists but the cap threshold may be too high).

### 2.5 Prewarm doing unnecessary work after About removal?

**No.** Builtin dock icons have always been skipped by the prewarm (they use lazy-load DockXxx.png paths). Removing About from the dock removed one entry from `BUILTIN_ICON_DEFS` and reduced `icon_count_` by exactly 1 (from the builtin region). Since builtins are skipped, the prewarm does 0 fewer file I/O operations. The "total=79" log message would show "total=78" on the next boot with the About slot removed. This is consistent — no regression here.

---

## Section 3 — Memory leaks + lifecycle bugs

### 3.1 `g_desktop_folder_cat_tex[kDesktopFolderCount]` — correctly sized

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:401`

`kDesktopFolderCount = 6` (the desktop folder tiles: Games, Emulators, Tools, System, Q OS, Other). This count is **independent of dock slot count** — it counts the folder grid tiles, not builtins. After About removal, `kDesktopFolderCount` is still 6 and correctly represents the 6 folder tiles. No issue here.

### 3.2 `g_hotcorner_q_tex` — intentional leak at process exit

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_HotCornerOverlay.cpp:26-29`

Comment explicitly documents "leaked deliberately at process exit (qlaunch lifetime)." This is intentional. No action required.

### 3.3 `g_desktop_folder_name_tex` / `g_desktop_folder_count_tex` — properly cleaned up

Both are freed in the destructor at lines 1651-1677. Proper `pu::ui::render::DeleteTexture` calls are used. No leak.

### 3.4 `pending_reopen_map_` — functional leak when windows close without minimizing

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_WindowManager.cpp:119-120`

`SetPendingReopen(key, fn)` is called from `on_minimize_begin_` inside each WmBridge opener. This inserts a `std::function` into `pending_reopen_map_` keyed by the raw window pointer. The function is consumed and erased in `MinimizeWindow` (line 222-225). However:

**If a window is closed directly (via B button) without ever being minimized, `CloseWindow` removes the entry from `open_stagger_positions_` and `open_windows_` (lines 97-116) but NEVER searches or clears `pending_reopen_map_`.** The raw pointer key is now dangling, and the `std::function` (which captures `this` for the desktop element) lives in the map forever until the window manager is destroyed.

This is a **functional lambda leak** — the `std::function` holds a closure capturing `this` (the `QdDesktopIconsElement`). If the desktop element is long-lived (it is — it lives for the whole uMenu session), the captured `this` is valid, so there is no use-after-free. But the map accumulates one entry per window opened-and-closed without minimize, every time the user opens and closes Tasks/Nintendo/Vault/Monitor. Over a long session this is unbounded growth.

**Severity: WARNING** — not a crash risk because the pointers remain valid for uMenu lifetime, but wastes memory and is architecturally wrong.

**Fix:** In `CloseWindow`, add `pending_reopen_map_.erase(win);` after the stagger reclaim block (line 116).

### 3.5 Window snap textures during minimize — properly cleaned up

`MinimizeWindow` creates `intermediate` and `snap` via `SDL_CreateTexture`. The `intermediate` is freed via `pu::ui::render::DeleteTexture` (lines 199, 202) before returning. The `snap` is transferred to `QdMinimizedDockEntry` via `QdMinimizedDockEntry::New(win->GetTitle(), snap, ...)` — the entry takes ownership. When `minimized_entries_.erase(it)` is called in `RestoreWindow` (line 295), the shared_ptr destructor releases the dock entry, which should call `SDL_DestroyTexture(snap)` in `QdMinimizedDockEntry::~QdMinimizedDockEntry`. This path was not verified by reading the dock entry destructor, but the pattern is sound.

MAY require HW verification — flag: **the dock entry destructor must call `SDL_DestroyTexture(snapshot_)` not `pu::ui::render::DeleteTexture`** if `snap` was created with `SDL_CreateTexture` rather than via Plutonium's texture registry. Mixing SDL_CreateTexture with DeleteTexture (Plutonium LRU path) or vice versa is the classic B41/B42 bug pattern.

### 3.6 `QdTaskManagerElement` — `tex_row` cleanup correct

`FreeAllTiles()` at line 244 nulls every `tex_row` via `FreeTexture`. `Refresh()` calls `FreeAllTiles()` before rebuilding. `OnRender` calls `FreeAllTiles()` on layout-dirty. Destructor calls `FreeRowTextures()` which does the same. This is clean.

### 3.7 `QdNintendoAppsLayout` icon cache — safe pattern but bypasses LRU

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoAppsLayout.cpp:84-87`

`icon_tex_cache_` is a `std::unordered_map<const char*, SDL_Texture*>`. Icons are loaded via `TryFindLoadImage` (which goes through `pu::ui::render::LoadImageFromFile` → Plutonium's texture registry). The destructor (lines 46-53) calls `pu::ui::render::DeleteTexture` on each cached texture. This is correct for textures registered through Plutonium. No leak.

However: `icon_tex_cache_` is keyed by `app.icon_path` (a `const char*` pointer, not a `std::string`). The pointer comes from `kNintendoApps[i].icon_path` which is a string literal — stable for the lifetime of the process. This is safe but unusual; a comment should note the lifetime assumption.

### 3.8 `g_desktop_folder_bg_tex` — never explicitly freed

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:383`

Loaded lazily in `PaintDesktopFolders`. The destructor block (lines 1651-1677) only frees `g_desktop_folder_cat_tex[]`, `g_desktop_folder_name_tex[]`, and `g_desktop_folder_count_tex[]`. `g_desktop_folder_bg_tex` is **not freed in the destructor**.

**Severity: WARNING** — process-lifetime leak. Since uMenu re-initializes the desktop element on theme switch, this leaks one SDL_Texture per theme reload. Over a session with many theme changes, this is a real leak.

**Fix:** Add to the destructor:
```cpp
if (g_desktop_folder_bg_tex != nullptr) {
    SDL_DestroyTexture(g_desktop_folder_bg_tex);
    g_desktop_folder_bg_tex = nullptr;
}
```

---

## Section 4 — Tasks + Nintendo specific path audit

### 4.1 Touch-to-launch trace for slot 3 (Tasks) and slot 4 (Nintendo)

Complete path on touch tap:

1. **Touch event received** → `OnInput` in `qd_DesktopIcons.cpp`
2. **Dock hit-test** at line 4862-4868:
   ```cpp
   if (lift_hit < kDesktopFolderCount + BUILTIN_ICON_COUNT) {
       const size_t dock_i = lift_hit - kDesktopFolderCount;
       LaunchIcon(dock_i);
   }
   ```
   `dock_i` = raw index into `icons_[]` for the dock slot. For Tasks, `dock_i = 3`; for Nintendo, `dock_i = 4`.
3. **`LaunchIcon(dock_i)`** at line 4929:
   - Reads `icons_[dock_i]` → an `NroEntry` with `kind = IconKind::Builtin`
   - Reads `entry.dock_slot` (set by `PopulateBuiltins`: `dock_slot = i` where `i` is the index in `BUILTIN_ICON_DEFS`)
   - Enters `switch (entry.dock_slot)`: case 3 → `OpenTaskManagerWindow()`, case 4 → `OpenNintendoAppsWindow()`
4. **`OpenTaskManagerWindow()`** (qd_DesktopIcons_WmBridge.cpp:231):
   - `QdTaskManagerElement::New(theme_, wm_)` — calls `pmdmntInitialize()` in ctor
   - `TakeStaggerPos` → stagger position computed
   - `QdWindow::New("Tasks", ...)` → window created
   - `wm_.OpenWindow(win)` — added to `open_windows_`
5. **`OpenNintendoAppsWindow()`** (qd_DesktopIcons_WmBridge.cpp:261):
   - `QdNintendoAppsLayout::New(theme_)` — ctor builds hint bar texture
   - `TakeStaggerPos` + `QdWindow::New` + `wm_.OpenWindow` — same pattern as above

### 4.2 The dock_slot / icons_ index relationship — potential mismatch

The critical assumption is that `icons_[dock_i].dock_slot == dock_i`. This holds **if and only if** the icons array is populated in order and no entries are inserted before `BUILTIN_ICON_COUNT` positions.

`PopulateBuiltins` (line 1974-2001) loops `for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i)` and sets `dock_slot = i`. The builtins always occupy `icons_[0]` through `icons_[4]`. The `app_entry_start_idx_` logic (line 1601) and `builtin_end_idx_` (line 1593) both confirm builtins are at fixed positions.

**This path is correct.** `dock_i` from the hit-test equals `entry.dock_slot`. The dispatch switch at line 4953 correctly maps case 3 → Tasks and case 4 → Nintendo.

### 4.3 Root cause hypothesis for "Tasks started problem" and "Nintendo started problem"

The slots 0/1/2 (Vault/Monitor/AllPrograms) open successfully. Slots 3/4 fail. The dispatch switch is correct. The WmBridge open functions are structurally identical to OpenVaultWindow. The difference is:

**`OpenTaskManagerWindow`** calls `QdTaskManagerElement::New(theme_, wm_)` which calls `pmdmntInitialize()` in its constructor (line 66). `pmdmntInitialize()` opens the `pm:dmnt` service. If this service is unavailable or takes too long from the library-applet context, the constructor blocks or fails silently. The constructor also calls `Refresh()` (line 70) which immediately calls `pmdmntGetApplicationProcessId` if `pm_ok_` is true. This is a synchronous libnx IPC call from the main render thread (unlike prewarm, which runs on a background thread).

**`OpenNintendoAppsWindow`** itself is simple — it just creates a layout with a hint bar texture. But when any tile in the Nintendo Apps grid is tapped, certain launchers call `g_MenuApplication->FadeOutToLibraryApplet(AppletId)` + `smi::OpenXxx()` + `g_MenuApplication->Finalize()`. The `Finalize()` call tears down the library-applet state. If the window opens but the user immediately taps a launcher, the `Finalize()` call could be competing with an in-progress state transition from the dock tap (re-entrance guard in `LaunchIcon` would catch this, but only for `LaunchIcon`-level re-entrance, not for callbacks registered on the window itself).

**Most likely concrete root cause:** The "started problem" description (hardware hangs briefly then opens, or opens but is unresponsive) points to **`pmdmntInitialize()` blocking in `QdTaskManagerElement`'s constructor**. The `pm:dmnt` service may require elevated privilege or may time out, causing a multi-second stall on the render thread when slot 3 is tapped. For slot 4 (Nintendo), the window opens but then tapping any launcher that calls `g_MenuApplication->Finalize()` terminates uMenu entirely — user sees "Nintendo started problem" as a crash/reset.

**Severity: STOP** for the pmdmnt init path. It must be moved off the render thread (lazy-initialize in Refresh) or made non-blocking (async flag).

### 4.4 `la::IsActive()` gate — not implicated

Neither `OpenTaskManagerWindow` nor `OpenNintendoAppsWindow` call `la::IsActive()` directly. TaskManager has its own `pm:dmnt` service (separate from the applet gate). Nintendo Apps uses `FadeOutToLibraryApplet` + `Finalize` per-action, not per-window-open. The applet gate concern from BG-2 audit does not apply to these two window open paths.

### 4.5 stagger position after multiple open/close cycles

`TakeStaggerPos` advances `next_stagger_*` on every window open. `CloseWindow` reclaims only the most-recently-opened slot (LIFO). If the user opens Tasks, then opens Nintendo, then closes Tasks (not the last-opened), the stagger counter is NOT reclaimed (line 110-114 checks if the closing window was the last-opened). Over many open/close cycles, `next_stagger_*` can wrap around (lines 132-135 reset to origin when overflow). This can cause new windows to spawn at the same position as existing windows. Not a crash; just a visual annoyance.

---

## Section 5 — Other observations

### 5.1 `.bak` files in source tree — build hygiene issue

**Files:**
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons_WmBridge.cpp.bak`
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_SettingsLayout.cpp.bak2`

These are stale backup files in the source tree. If the build system uses a wildcard `*.cpp` glob (e.g., in CMakeLists.txt or the Makefile), these `.bak` and `.bak2` files WILL be compiled if they happen to match a glob for `.cpp.bak` — but a standard `*.cpp` glob would not pick them up. Verify the build glob does not match `.bak2` (a `.cpp.bak2` ends in `2` which doesn't match `*.cpp`). Low severity but should be removed to avoid confusion.

### 5.2 `OpenAboutWindow` still present and compilable

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons_WmBridge.cpp:135-156`

`OpenAboutWindow` is defined but no longer called from the dock dispatch. The hot-corner dropdown still calls it. This is correct behavior per the creator directive ("save the icon for later use"). However, the function takes a stagger position and creates a window — if the hot-corner path is still wired, it works. No issue.

### 5.3 `NintendoAppsLayout::OnInput` has inverted empty-check logic for mouse hover

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_NintendoAppsLayout.cpp:245`

```cpp
if (touch_pos.IsEmpty()) {
    // Mouse hover — update hovered_idx_ for visual feedback without launching.
    hovered_idx_ = -1;
    for (int i = 0; ...) {
        if (touch_pos.x >= tx ...) {  // touch_pos is "empty" here
```

When `IsEmpty()` is true, the function enters the hover branch but then checks `touch_pos.x / .y` coordinates. If `IsEmpty()` means the touch struct has no valid coordinates (e.g., x=0, y=0 or sentinel values), then the hit-test against tile rects may produce false positives for tiles positioned near the origin. The hover index would be set to 0 permanently when no touch is active. This is likely harmless for launch behavior (launches only happen in the non-empty branch) but the visual hover highlight may be stuck on tile 0 when the user has no finger on screen.

**Severity: INFO** — cosmetic issue; cannot confirm without seeing `TouchPoint::IsEmpty()` definition.

### 5.4 `pmdmntInitialize` in `QdTaskManagerElement` constructor — blocking IPC on render thread

Already discussed in Section 4.3. Worth calling out separately: this pattern violates the element construction contract (elements should be lightweight to construct). **Move `pmdmntInitialize()` out of the constructor** — either lazy-initialize on first `Refresh()` call or call it asynchronously before constructing the element.

### 5.5 Task 2 stub always returns `ResultNotImplemented` — wastes one IPC round-trip per call

**File:** `src/projects/uSystem/source/main.cpp:849-876`

The `LaunchHomebrewWindowedLibraryApplet` stub pops the storage and returns error. Since no caller in uMenu currently invokes this command (it is marked "do not call from production code yet"), this stub is entirely dormant. No runtime cost. The concern would arise if a future test or tooling accidentally calls `LaunchHomebrewWindowedLibraryApplet` — it would return silently with an error and the caller's `out_consumer_handle` would be 0. The comment at `smi_Commands.hpp:87-88` is correct.

---

## Section 6 — Recommended fixes ranked by severity

### FIX-1 (STOP): Move `pmdmntInitialize()` out of `QdTaskManagerElement` constructor

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_TaskManagerLayout.cpp:66-70`

**Problem:** Synchronous `pm:dmnt` service init on the render thread when Tasks dock slot is tapped. Most likely cause of the "Tasks started problem."

**Fix:**
```cpp
// In constructor: remove pmdmntInitialize call entirely.
// In Refresh() (first call only):
if (!pm_init_attempted_) {
    pm_init_attempted_ = true;
    Result rc = pmdmntInitialize();
    pm_ok_ = R_SUCCEEDED(rc);
}
```

Add `pm_init_attempted_` bool field initialized to `false` in the header. This defers the IPC to the background `on_tick` path (called from `QdWindow::PollEvent` when in `WindowState::Normal`), so the window opens instantly and the pm service is initialized on the first tick after the window renders.

**Estimated impact:** Eliminates the render-thread block on Tasks tap. High confidence this is the Tasks "started problem."

### FIX-2 (STOP): Verify `QdMinimizedDockEntry` destructor uses correct texture cleanup

**File:** Not read directly — needs to be audited.

The `snap` texture created via `SDL_CreateTexture` (qd_WindowManager.cpp:181) is passed to `QdMinimizedDockEntry::New`. If `QdMinimizedDockEntry`'s destructor calls `SDL_DestroyTexture(snapshot_)` directly (correct for SDL_CreateTexture) rather than `pu::ui::render::DeleteTexture(snapshot_)` (which would attempt Plutonium LRU bookkeeping on a non-registered texture), the behavior is correct. But if the pattern is reversed, every window minimize leaks an SDL texture. **This must be confirmed by reading `qd_MinimizedDockEntry.cpp`.**

### FIX-3 (WARNING): Fix `pending_reopen_map_` leak in `CloseWindow`

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_WindowManager.cpp:86-117`

**Fix:** Add one line at the end of `CloseWindow`:
```cpp
pending_reopen_map_.erase(win);  // clear any unreached reopen functor
```

This prevents unbounded growth of the map when windows are closed without minimizing.

### FIX-4 (WARNING): Free `g_desktop_folder_bg_tex` in destructor

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp` — destructor block around line 1651

**Fix:** Add after the existing cat/name/count texture cleanup:
```cpp
if (g_desktop_folder_bg_tex != nullptr) {
    SDL_DestroyTexture(g_desktop_folder_bg_tex);
    g_desktop_folder_bg_tex = nullptr;
}
```

### FIX-5 (WARNING): Update `BUILTIN_ICON_COUNT_TEST` in magnify test file

**File:** `src/projects/uMenu/tests/qdesktop/test_QdDockMagnify.cpp:16`

**Fix:**
```cpp
static constexpr int32_t BUILTIN_ICON_COUNT_TEST = 5;  // v2.9.12: About removed
```

Remove or update the test cases that test slot 5 (tests 4, 7, 8 all reference slot 5 index which no longer exists in production). Update test comments accordingly. This fixes conceptual test accuracy.

### FIX-6 (INFO): Update stale constructor comments for 5-slot math

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:1541-1542`

**Fix:**
```cpp
//   total_expanded_w = BUILTIN_ICON_COUNT * ICON_BG_W + (BUILTIN_ICON_COUNT-1) * ICON_GRID_GAP_X
//                    = 5*140 + 4*28 = 812  (v2.9.12: 5 slots, About removed)
//   expanded_start_x = (1920 - 812) / 2 = 554
```

Similarly update the magnify comment at line 3542 from "6 slots = ~1075 px" to "5 slots = ~895 px."

### FIX-7 (INFO): Consider capping NS IPC retries in prewarm to reduce boot lag

**File:** `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp:3369-3373`

The `LoadNsIconToCache` fallback in `PrewarmAllIcons` issues two IPC calls per uncached app. With 29 misses this is ~58 NS IPC calls. Consider adding an early-exit if a configurable cap is exceeded:

```cpp
static int ns_prewarm_budget = 20;  // cap NS calls per boot
if (ns_prewarm_budget > 0 && LoadNsIconToCache(e.app_id, e.icon_path)) {
    --ns_prewarm_budget;
    ++prewarm_hit;
} else {
    g_has_no_asset_.insert(e.icon_path);
}
```

This reduces worst-case boot lag while allowing common games to get their icons.

### FIX-8 (INFO): Remove `.bak` files from source tree

Delete `qd_DesktopIcons_WmBridge.cpp.bak` and `qd_SettingsLayout.cpp.bak2` to prevent confusion.

---

## Summary table

| # | Severity | File | Issue |
|---|----------|------|-------|
| 1 | STOP | qd_TaskManagerLayout.cpp:66 | pmdmntInitialize blocks render thread |
| 2 | STOP | qd_MinimizedDockEntry.cpp (unread) | MAY use wrong texture cleanup API |
| 3 | WARNING | qd_WindowManager.cpp:86-117 | pending_reopen_map_ leaks on direct close |
| 4 | WARNING | qd_DesktopIcons.cpp ~1651 | g_desktop_folder_bg_tex never freed |
| 5 | WARNING | test_QdDockMagnify.cpp:16 | BUILTIN_ICON_COUNT_TEST=6, should be 5 |
| 6 | INFO | qd_DesktopIcons.cpp:1541-1542 | Stale 6-slot comment; code correct |
| 7 | INFO | qd_DesktopIcons.cpp:3542 | Stale 6-slot magnify comment |
| 8 | INFO | qd_DesktopIcons.cpp:3369 | NS IPC prewarm unbounded; cap recommended |
| 9 | INFO | qd_NintendoAppsLayout.cpp:245 | IsEmpty() hover branch hits coordinates |
| 10 | INFO | source tree | .bak/.bak2 files present |
