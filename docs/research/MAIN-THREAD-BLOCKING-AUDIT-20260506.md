# Static Analysis Report: Main-Thread Blocking Audit

## Target
- **Path:** `/Users/nsa/QOS/tools/qos-ulaunch-fork/src/projects/uMenu/`
- **Type:** Repository (C++ Nintendo Switch homebrew, Plutonium PU applet)
- **Authorization:** creator-owned
- **Analysis Date:** 2026-05-06

---

## Confirmed Blocking Calls on Main Thread

### 1. Per-frame TTF rendering in `StartupMenuLayout::OnMenuUpdate`

**File:** `src/projects/uMenu/source/ul/menu/ui/ui_StartupMenuLayout.cpp:347–374`

`OnMenuUpdate` is called by the Plutonium frame loop on the **main thread** before each render.
Inside the `#ifdef QDESKTOP_MODE` block, every frame unconditionally calls
`pu::ui::render::RenderText(...)` twice:

- Line 347: `RenderText(..., "Q OS", white)` — Large font
- Line 364: `RenderText(..., hint_str, hint_clr, 1880u)` — Small font, max-width clamp

`RenderText` checks an LRU cache first. On a **cache hit** (after first frame) this is a
hashtable lookup — cheap. On a **cache miss** (first frame, or if the cache is evicted) it
calls `font->RenderText()` which calls `TTF_RenderUTF8_Blended` + `SDL_CreateTextureFromSurface`
— this is a GPU texture upload and is slow (measured at 2–10 ms per call on Switch hardware in
prior uMenu profiling).

The strings are **compile-time constants** so the cache will hit after the first frame.
**This is not the frame-drop source in isolation.** However it does burn two hashtable lookups
per frame unnecessarily, and more critically: see item 2 below.

### 2. Per-frame `SetText` in `RefreshDevToolLabels` — texture recreation on state change

**File:** `src/projects/uMenu/source/ul/menu/ui/ui_StartupMenuLayout.cpp:445–461`

Also called from `OnMenuUpdate` every frame. `qd_lbl_nxlink->SetText(...)` calls
`TextBlock::SetText` (Plutonium `elm_TextBlock.cpp:38–43`) which **always** calls:

```
render::DeleteTexture(this->text_tex);
this->text_tex = render::RenderText(...);
```

`DeleteTexture` invalidates the cached pointer. `RenderText` then re-renders the TTF glyph.
This fires a **full TTF re-render every frame the text does not change**, because `SetText`
does not guard on `this->text == text`. On Switch hardware this is ~2–4 ms per label per frame
when the network server state is stable (which is the common case on the login screen).

With two labels (`qd_lbl_nxlink`, `qd_lbl_usbserial`) this costs **4–8 ms per frame** on the
main thread — enough to drop from 60 fps to roughly 40 fps or stutter depending on the rest of
frame budget.

**This is the most likely cause of the reported login screen reactivity loss.**

---

## Worker-Thread Sleeps That Look Correct

| File | Line(s) | Sleep | Reasoning |
|------|---------|-------|-----------|
| `qd_NxlinkServer.cpp` | 167, 186, 217, 336 | 2 s, 1 s, 1 s, 1 s | All inside `QdNxlinkServer::ThreadEntry`, spawned via `threadCreate(&g_NxlinkServerThread, ...)` — confirmed worker thread |
| `qd_RemoteShellServer.cpp` | 116, 130, 158, 208 | 2 s, 1 s, 1 s, 1 s | All inside `QdRemoteShellServer::ThreadEntry`, spawned via `threadCreate(&g_ShellServerThread, ...)` — confirmed worker thread |
| `sf_PrivateService.cpp` | 75 | 10 ms | Inside `MenuMessageReceiverThread`, spawned via `threadCreate(&g_ReceiverThread, ...)` line 101 |
| `bt_Manager.cpp` | 125 | 1 ms | Inside `BluetoothThread`, spawned as a dedicated thread |
| `am_LibnxLibappletWrap.cpp` | 52 | 10 ms | Inside `__wrap_libappletStart` — this IS the main thread but only while a library applet is running (blocking by design; not a frame-loop context) |

The EHOSTUNREACH backoffs in `qd_NxlinkServer.cpp:336` and `qd_RemoteShellServer.cpp:208`
are **on their respective worker threads**. Thread spawn is intact. These changes are clean.

---

## Hot-Path Hotspots (Per-Frame Work That Has Grown)

1. **`SetText` called unconditionally each frame** on `qd_lbl_nxlink` and `qd_lbl_usbserial`
   in `OnMenuUpdate`. Prior to any Q OS addition, these labels may not have existed or may
   not have been refreshed per-frame. The cost is TTF + SDL texture upload on state change,
   hashtable miss + LRU eviction on stable state.

2. **`SettingsLayout::BuildRows` + `MakeText` in `OnInput`** — lines 921–931, 978–988,
   1074–1084, 1127–1140. These run only on user input events (toggle presses in Folders tab),
   not every frame. The new row 9 adds one `strncpy` to `BuildRows` at line 548. The
   `MAX_DETAIL_ROWS` bump from 8 to 9 enlarges the `detail_tex_` fixed array by 14 entries
   (`7 tabs * 2 * 1 = 14 SDL_Texture* pointers`). This is a compile-time array, not a
   heap allocation — no reallocation occurs per frame. Not a regression.

3. **`SettingsLayout::Refresh()` + `MakeText`** — called via `QdWindow::on_tick` every
   `kTickRefreshHz = 60` frames (once per second). The new row 9 adds one `MakeText` call
   inside `build_tab_textures` for the System tab. This is 1 extra TTF render per second.
   Immaterial.

---

## Specific Fixes

### Fix A — Guard `SetText` against same-value writes (primary fix)

**File:** `src/projects/uMenu/source/ul/menu/ui/ui_StartupMenuLayout.cpp`

```cpp
// BEFORE (line 449):
this->qd_lbl_nxlink->SetText(
    qdesktop::dev::IsNxlinkActive() ? "Nxlink: ON" : "Nxlink: OFF");

// AFTER:
{
    const std::string nxlink_str =
        qdesktop::dev::IsNxlinkActive() ? "Nxlink: ON" : "Nxlink: OFF";
    if(this->qd_lbl_nxlink->GetText() != nxlink_str) {
        this->qd_lbl_nxlink->SetText(nxlink_str);
    }
}
```

Apply the same guard to `qd_lbl_usbserial`. `TextBlock::GetText()` is a free string read —
no SDL work. This reduces the per-frame cost to a string comparison on stable state (near zero).

### Fix B — Cache the "Q OS" and hints textures (secondary, lower priority)

**File:** `src/projects/uMenu/source/ul/menu/ui/ui_StartupMenuLayout.cpp`

Move `brand_tex` and `hints_tex` from local stack variables inside `OnMenuUpdate` to
`SDL_Texture*` member fields initialized once in the constructor (or lazily on first paint).
This eliminates two `RenderText` hashtable lookups per frame. Given the LRU cache is already
warm, the impact is small (saves two `std::unordered_map::find` calls per frame), but it
removes the allocation pattern entirely and prevents any future LRU eviction from stalling
the login frame.

---

## Confidence

- **Server thread placement (NxlinkServer, RemoteShellServer):** HIGH — thread spawn code is
  unambiguous; `ThreadEntry` is the only function passed to `threadCreate`.
- **`SetText` per-frame TTF cost:** HIGH — `elm_TextBlock::SetText` unconditionally destroys
  and recreates the texture; no guard in Plutonium source.
- **`SettingsLayout` row 9 impact:** HIGH (none) — compile-time array, tick-gated Refresh.
- **`OnMenuUpdate` brand/hints RenderText being cache-hot:** MEDIUM — depends on LRU eviction
  policy under memory pressure; cache miss is still possible but first-frame only in practice.
