# Phase Z Right-Click — Mass Bug Check Test Plan

Cheat-sheet for HW verification of everything shipped between v3.1.0 (committed
as `f01d2fed`) and the v3.1.1-staging working tree.

**Build identifiers on SD:**
- `uMenu/main` md5 `2123b9464569faab38fcd1574934aa11`
- `uSystem exefs.nsp` md5 `ef3ab36615710bc884c1f9f20fa7cbed`

If anything below fails or surprises you, capture: which surface, what action,
what you expected, what actually happened, and (if visible) any on-screen error
text. Then we triage by surface.

---

## A. Tesla overlay disable (uSystem)

Pre-condition: cold boot uMenu from hekate.

| # | Check | Pass if |
|---|---|---|
| A1 | All four screen edges | No red border or cyan border. Clean screen, no chrome around the perimeter. |
| A2 | Top 50px of screen | No persistent navy bar with white text. Only the Plutonium battery/time widgets visible. |
| A3 | uSystem log (optional) | Boot log contains `"Tesla overlay GATED OFF (UL_ENABLE_TESLA_OVERLAY=0)"`. |
| A4 | Stress reboot | Reboot 3× — overlay never appears. |

If A1-A4 all pass, the Tesla-overlay-during-launch flash should also be gone.

---

## B. Launch / suspend / terminate (regression check)

| # | Check | Pass if |
|---|---|---|
| B1 | Launch a Switch retail Application from the dock | Clean fade-to-black, then game starts fullscreen. No chrome flash. |
| B2 | Launch a homebrew NRO from the dock | Clean fade-to-black, then NRO starts. No chrome flash. |
| B3 | Press HOME on a running Switch Application | Returns to uMenu. Suspended-app dock tile appears in the dock band. |
| B4 | Tap the suspended-app dock tile | Game resumes (FadeOut + Finalize). |
| B5 | ZL on suspended-app dock tile (cursor must be OVER tile) | Menu pops up: `Resume`, `Terminate`, `Cancel`. |
| B6 | Pick `Terminate` from B5 | Notification "Closed running app." appears. Dock tile disappears immediately. Task Manager (long-press HOME → Task Manager) no longer shows the row. |
| B7 | Pick `Resume` from B5 | Same as B4. |
| B8 | Pick `Cancel` from B5 | Menu closes silently, no action. |

---

## C. Empty-desktop ZL (Z2.1)

Cursor must be over truly empty desktop area — NOT over a folder tile, dock
tile, favorites strip, window, hot-corner widget (top-left/right 96×72), or
top-bar (y<48).

| # | Check | Pass if |
|---|---|---|
| C1 | ZL on empty desktop area | Menu opens at cursor: `New Custom Folder`, `Change Wallpaper ▸`, `Refresh`, `Cancel`. |
| C2 | Hover `Change Wallpaper ▸` then press A (or tap) | Submenu opens to the right with 10 theme names: Glass, Neon, Minimal, Retro, Cards, Pastel, Dark, Gradient, Blueprint, Pixel. Each has a `▸` chevron indicator on its parent row. |
| C3 | Pick a theme from the submenu | Palette, wallpaper, AND folder-icon pack all switch together. Notification "Wallpaper: <name>" appears. |
| C4 | Reboot after C3 | Theme persists. `sdmc:/ulaunch/qos-folder-theme.toml` reflects the choice. |
| C5 | Press B while in the submenu | Submenu closes, parent menu still visible. Hover stays on the parent row. |
| C6 | Press B again | Whole menu closes, no action. |
| C7 | Press Left while in submenu | Same as C5 — closes submenu only. |
| C8 | Pick `New Custom Folder` | Swkbd opens. Type a folder name. Confirm. Folder is created in classifier. Notification "Custom folder created." |
| C9 | Pick `Refresh` | All folder counts re-classify. Notification "Desktop refreshed." |
| C10 | Press ZL at top-bar y (e.g. y=20, x=400) | Top-bar menu opens INSTEAD of empty-desktop menu (Z2.9). |
| C11 | Press ZL at hot-corner (e.g. x=20, y=20) | Nothing happens — dropdown owner has priority. |

---

## D. Desktop folder tile ZL (Z2.2-minimal)

| # | Check | Pass if |
|---|---|---|
| D1 | ZL on Games tile (any of the 6 desktop folder tiles) | Menu opens: `Open`, `Re-classify Apps in Games`, `Choose Folder Theme ▸`, `Cancel`. |
| D2 | Pick `Open` | Opens the folder as a window (same as tapping the folder tile normally). |
| D3 | Pick `Re-classify Apps in Games` | Notification "Re-classified apps in Games." Classifier resets, MarkDesktopFolderLayoutDirty fires, counts update. |
| D4 | Pick `Choose Folder Theme ▸` + a theme | Same effect as the Wallpaper submenu (unified theme pack). |
| D5 | D-pad nav between submenu items | Up/Down works. Hover highlight tracks. |
| D6 | Touch tap a submenu item | Confirms that item. |

---

## E. Dock-icon ZL (Z2.3a — Move to Folder)

| # | Check | Pass if |
|---|---|---|
| E1 | ZL on a built-in dock slot (Vault / Monitor / All Programs / Tasks / Nintendo) | Menu: `Open`, `Cancel`. **No** "Move to Folder" — built-ins are not movable. |
| E2 | ZL on an Application dock icon | Menu: `Open` (or `Open (close current first)` if another app is suspended), `Close currently running game` (if applicable), `Move to Folder ▸`, `Cancel`. |
| E3 | ZL on an NRO dock icon | Same as E2 with `Move to Folder ▸` present. |
| E4 | Hover `Move to Folder ▸` and press A | Submenu opens: `NX Games`, `3rd Party`, `Emulators`, `Tools`, `System`, `Payloads`, `Q OS`, `Homebrew`. |
| E5 | Pick a folder from the submenu | Notification "Moved 'AppName' to <FolderName>". `QdFolderClassifier::SetUserOverride` writes to disk. |
| E6 | Reboot after E5 | Override persists. Re-launching the app and viewing the folder grid shows the moved app in the new bucket. |
| E7 | Same app, ZL again, pick a different folder | Re-assigns. Bucket counts update. |

**Known UX:** The dock itself isn't filtered by classifier bucket; the icon
stays in the same dock slot. The override affects the Launchpad's bucket
counts and the desktop-folder-grid's bucket assignments only.

---

## F. Minimized-window dock tile ZL (Z2.4)

Pre-condition: open a uMenu window (e.g. Settings via top-left dropdown), then
tap the `−` button on the window's titlebar to minimize it. A snapshot tile
appears in the dock band on the right side.

| # | Check | Pass if |
|---|---|---|
| F1 | ZL on the minimized tile (cursor must be OVER it) | Menu: `Restore`, `Close Window`, `Cancel`. |
| F2 | Pick `Restore` | Window restores (same as tapping the tile). Tile disappears. |
| F3 | Pick `Close Window` | Tile drops without restoring. No window pop-up. Snapshot texture freed. |
| F4 | Pick `Cancel` | Menu closes silently. |
| F5 | Tap the tile (no ZL) | Window restores (existing behavior preserved). |
| F6 | Stress: minimize 3 windows, close 2 via ZL, restore the third via tap | Each tile dispatches correctly. No double-restore. No memory growth. |

---

## G. Favorites-strip ZL (Z2.5)

Pre-condition: have at least one favorite. Add via Y-press on a focused dock icon.

| # | Check | Pass if |
|---|---|---|
| G1 | ZL on a favorite tile in the strip (between folder grid and dock band) | Menu: `Launch`, `Remove from Favorites`, `Cancel`. |
| G2 | Pick `Launch` | App launches (same as tap). |
| G3 | Pick `Remove from Favorites` | Strip updates immediately. Notification "Removed '<name>' from Favorites." `qos-favorites.toml` updated. |
| G4 | Reboot after G3 | Removal persists. |
| G5 | Pick `Cancel` | Menu closes silently. |

---

## H. QdWindow titlebar ZL (Z2.7)

Pre-condition: any uMenu window open (Settings, About, Files/Vault, Monitor, Tasks).

| # | Check | Pass if |
|---|---|---|
| H1 | ZL anywhere over a window (cursor inside its rect) | Menu: `Close`, `Minimize`, `Maximize` (or `Restore` if already maxed), `Snap Left`, `Snap Right`, `Move to Center`, `Cancel`. |
| H2 | Pick `Snap Left` | Window snaps to left half of content area (excludes top-bar + dock). |
| H3 | Pick `Snap Right` | Window snaps to right half. |
| H4 | Pick `Move to Center` | Window's top-left moves so that the window is centered. snapped_ / maximized_ flags clear (window is back to free-float). |
| H5 | Pick `Maximize` after a Snap | Maximize takes over from snap state. Pick `Restore` again to revert to pre-max geometry. |
| H6 | Pick `Close` | Window closes. WM removes it. |
| H7 | Pick `Minimize` | Window minimizes to dock tile (deferred to OnRender). |

---

## I. Top-bar ZL (Z2.9)

| # | Check | Pass if |
|---|---|---|
| I1 | ZL at top of screen (y<48, x in middle range — not hot-corner) | Menu: `Lock Screen`, `About Q OS`, `Cancel`. |
| I2 | Pick `Lock Screen` | uMenu loads the QLockscreen layout. |
| I3 | Unlock and pick `About Q OS` | About window opens (same as the hot-corner dropdown's About row). |
| I4 | ZL at hot-corner area (x<96, y<72) | Nothing happens (excluded). |
| I5 | ZL at top-bar but BELOW y=48 | EmptyDesktop menu opens (only the top 48px is top-bar). |

---

## J. Submenu primitive (Z2.0a) — covers all submenu use cases

| # | Check | Pass if |
|---|---|---|
| J1 | Open any menu with a `▸` chevron row (e.g. Empty Desktop's Change Wallpaper) | Chevron visible on the right side of the row. |
| J2 | Hover the row | Row highlights. Chevron remains visible. |
| J3 | Press A on the row | Submenu opens to the right of the parent. Hover starts on submenu item 0. |
| J4 | If submenu would extend past right edge of screen | Submenu auto-flips to LEFT of parent. (Trigger by opening a menu near the right edge.) |
| J5 | Press B in submenu | Closes submenu only; parent stays open. |
| J6 | Press B again | Whole menu closes. |
| J7 | Press Left in submenu | Same as J5. |
| J8 | Touch outside both panels | Cancels everything. |
| J9 | Touch on parent panel while submenu open | Submenu closes, parent stays. |
| J10 | Touch a submenu row | Confirms that item. |
| J11 | Submenu with 10 items (theme picker) | All 10 fit, scrolls / displays cleanly (kMaxSubItems=16). |

---

## K. Cross-surface mutual exclusion

| # | Check | Pass if |
|---|---|---|
| K1 | Open dock-icon ZL menu, press ZL again | Existing menu cancels (B/ZL == cancel). No second menu opens. |
| K2 | Open desktop-layer ctx menu, then try to interact with a minimized tile | Dock tile ignores input until menu closes. |
| K3 | Suspended-app menu open, then ZL on empty desktop | First press cancels suspended menu. Second press opens empty-desktop menu. |
| K4 | Have 5+ minimized tiles + 1 suspended app | Each context-menu instance is independent. No cross-contamination. |

---

## L. State persistence (across reboot)

| Action | File | Persistence verified |
|---|---|---|
| Add/remove favorite | `sdmc:/ulaunch/qos-favorites.toml` | ✓ Existing |
| Move app to folder | (`QdFolderClassifier` internal persist) | ✓ Existing |
| Change wallpaper / theme | `sdmc:/ulaunch/qos-folder-theme.toml` | ✓ Existing |
| Custom folder created | `sdmc:/ulaunch/qos-custom-folders.toml` | ✓ Existing |
| Hide entry (Z2.3b, plumbed but no UI yet) | `sdmc:/ulaunch/qos-visibility.toml` | Future Z2.3b |

---

## M. Known limitations / non-bugs

These are NOT bugs — they're documented design decisions for Z2:

- **Move to Folder doesn't move the dock icon.** The dock is fixed; the override affects Launchpad/desktop-folder-grid buckets only. Dock reorder is Z2.6 (future).
- **Built-in dock icons (Vault/Monitor/etc.) have only `Open` + `Cancel` in their ZL menu.** They're not movable until Z2.6 dock-reorder ships.
- **No `Hide` option yet.** `QdVisibility` is plumbed but not wired into renderer/hit-test. Coming in Z2.3b.
- **`Disable Folder` and `Rename Folder` not in the desktop-folder ctx menu.** Need `qos-folder-settings.toml` schema extension + swkbd. Coming in Z2.2-extra.
- **Vault still uses its own bespoke popup for the file-manager context menu** (not migrated to `QdContextMenu` yet). Z2.10 is the planned migration but it's a behavior-preserving refactor of a heavily-used feature, deferred until the rest of Z2 stabilizes.
- **Hot-corner widgets (top-left Q logo, top-right cluster) have no ZL menu.** The tap-to-open dropdown is the primary interaction; ZL is intentionally no-op there. Customize support is Z2.8 (future).

---

## N. If something crashes

If uMenu hangs / crashes / black-screens during any of these tests:

1. Reboot to hekate.
2. Note which test was running (e.g. "G3 — Remove from Favorites").
3. Note any error text or stuck-screen description.
4. Don't repeat the test — capture the state first.

The fb8f8b18 baseline (pre-Z2 with only the suspended-app fix) is at
`SdOut/...` in the build tree if rollback is needed. The v3.0.2 baseline
(pre-Z) is the committed state at HEAD~1.
