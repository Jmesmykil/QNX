# Phase Z — OS-Grade Right-Click Context System

**Status:** Design locked 2026-05-19. Z1 audit complete. Z2 implementation pending.
**Owner:** uMenu qdesktop.
**Predecessor work:** L-cycle window manager, K+1 folders/categories, suspended-app dock tile (`fb8f8b18`).

## Vision

Q OS uMenu becomes feel-like-a-computer: every clickable surface offers a right-click
(ZL) context menu with the right verbs for that surface. Per-icon hide/show, custom
folders, dock reorder, rename, theme switch, snap windows — all from the same
unified primitive (`QdContextMenu`) with sub-menu support.

> Quote: *"The context ZL menu needs to work like a computer and allow the user
> to eventually turn on and off individual icons, create custom folder/icons etc.
> This will be our most powerful tool. We want this JUST like a computer
> operating system the best we can without sacrificing too much performance and
> make it beautiful like it is."*

## Locked design decisions (2026-05-19)

| # | Decision | Choice |
|---|---|---|
| D1 | Hide persistence | **New `sdmc:/ulaunch/qos-visibility.toml`** — dedicated TOML keyed by stable_id; same atomic tmp+rename pattern as `qos-favorites.toml`. Easy reset; no schema migration in `EntryControlData`. |
| D2 | Dock reorder model | **All dock slots reorderable.** Built-ins (Vault/Monitor/AllPrograms/Tasks/Nintendo) become moveable and hideable; user can pin/unpin Application/NRO entries. Persisted in new `sdmc:/ulaunch/qos-dock-layout.toml`. |
| D3 | Folder rename | **Renameable with persistence.** Custom names in `qos-folder-settings.toml`; internal stable indices (0..5) preserve dispatch. Reset-to-default exposed in context menu. |
| D4 | Submenu UX | **Build submenu support in `QdContextMenu` first (Z2.0).** Hierarchical UX is the right final shape; accept ~2-day delay before Z2.3 ships. |
| D5 | Dispatcher pattern | **Hybrid — keep current.** `QdDesktopIconsElement::context_menu_` stays the single desktop-layer instance; content-window menus (Task Manager, Vault post-migration) remain content-owned. No new central dispatcher class. |

## Surface inventory (18 surfaces — 4 wired, 1 legacy, 13 to build)

| # | Surface | Source file:line | ZL today? | Phase |
|---|---|---|---|---|
| 1 | Dock icon (built-in) | `qd_DesktopIcons.cpp:1494,2509,4421` | ✓ | Z2.3, Z2.6 |
| 2 | Dock icon (App/NRO) | same dispatch path | ✓ | Z2.3 |
| 3 | Desktop folder tile | `qd_DesktopIcons.cpp:365-378,2501,4492` | ✓ (partial) | Z2.2 |
| 4 | Empty desktop | `qd_DesktopIcons.cpp:4516` (no-op) | ✗ | **Z2.1** |
| 5 | Wallpaper | `qd_Wallpaper.hpp:117` (pass-through) | ✗ | bundled with Z2.1 |
| 6 | Favorites strip item | `qd_DesktopIcons.cpp:783` | ✗ | Z2.5 |
| 7 | Minimized-window tile | `qd_MinimizedDockEntry.hpp` | ✗ | Z2.4 |
| 8 | Suspended-app tile | `qd_SuspendedAppDockEntry.hpp:75` | ✓ shipped | — |
| 9 | QdWindow titlebar | `qd_Window.hpp:96`, `qd_DesktopIcons.cpp:4386` | ✓ | Z2.7 (enhance) |
| 10 | Top status bar | `qd_GlobalChrome.hpp:55` | ✗ | Z2.9 |
| 11 | Hot-corner Q logo (L) | `qd_HotCornerOverlay.hpp:49` | ✗ | Z2.8 |
| 12 | Hot-corner (R) | `qd_HotCornerRightOverlay.hpp:55` | ✗ | Z2.8 |
| 13 | Left dropdown rows | `qd_HotCornerDropdown.hpp:6-12` | ✗ | skip |
| 14 | Right dropdown rows | `qd_HotCornerRightDropdown.hpp:60-65` | ✗ | skip |
| 15 | Settings rows | `qd_SettingsLayout.hpp:62-71` | ✗ | skip (Z3+) |
| 16 | About info rows | `qd_AboutLayout.hpp:44` | ✗ | skip (Z3+) |
| 17 | Task Manager rows | `qd_TaskManagerLayout.hpp:191-205` | ✓ | — |
| 18 | Vault file row | `qd_VaultLayout.hpp:194-232` | ✓ (legacy) | Z2.10 (migrate) |

## Existing backend APIs (reuse — do not duplicate)

| Concept | API | Persistence |
|---|---|---|
| Per-entry custom name/icon | `Entry.control.custom_*` + `Entry::Save()` | uMenu RootPath/.../entry |
| Favorites | `qd_DesktopIcons.cpp:597-783` (`ToggleFavorite`) | `qos-favorites.toml` |
| Folder classification | `QdFolderClassifier::SetUserOverride(stable_id, idx)` | `qd_FolderClassifier.hpp:86-87` |
| Auto-folder toggle | `SetAutoFolderEnabled(idx, bool)` | `qos-folder-settings.toml` |
| Custom user folders | `GetCustomFolders()` + `CreateFolderFlow()` | `qos-custom-folders.toml` |
| Theme/wallpaper pack | `SetActiveThemePack(idx)` | `qos-folder-theme.toml` |
| Window snap | `QdWindow::ApplySnap(SnapTarget, ...)` | in-memory only |

## New backend APIs to introduce

### B1 — Visibility (Z2.0/Z2.1)

```cpp
// include/ul/menu/qdesktop/qd_Visibility.hpp
namespace ul::menu::qdesktop {
    class QdVisibility {
    public:
        static QdVisibility& Get();   // process singleton
        bool IsHidden(const std::string& stable_id) const;
        void SetHidden(const std::string& stable_id, bool hidden);
        // Atomic tmp+rename to sdmc:/ulaunch/qos-visibility.toml
        // Same pattern as ToggleFavorite/PersistFavorites.
    };
}
```

### B2 — Dock layout (Z2.6)

```cpp
// include/ul/menu/qdesktop/qd_DockLayout.hpp
namespace ul::menu::qdesktop {
    enum class DockSlotKind : u8 { BuiltinVault, BuiltinMonitor, BuiltinAllPrograms,
                                    BuiltinTasks, BuiltinNintendo,
                                    Application, Nro };
    struct DockSlot {
        DockSlotKind kind;
        std::string  stable_id;   // for Application/Nro; empty for built-ins
        bool         visible = true;
    };
    class QdDockLayout {
    public:
        static QdDockLayout& Get();
        const std::vector<DockSlot>& Slots() const;
        void MoveLeft(size_t idx);   // bounds-checked
        void MoveRight(size_t idx);
        void Pin(DockSlotKind kind, const std::string& stable_id);
        void Unpin(size_t idx);
        void SetVisible(size_t idx, bool visible);
        void ResetToDefault();
    };
}
// Persists to sdmc:/ulaunch/qos-dock-layout.toml
// Default order on first run = current hardcoded built-ins order.
```

### B3 — Folder rename (Z2.2)

```cpp
// Extension to qd_AutoFolders (no new file)
// Add to qos-folder-settings.toml:
//   [folder.0]   ; Games
//   name = "My Games"
//   enabled = true
namespace ul::menu::qdesktop {
    std::string GetFolderDisplayName(FolderIdx idx);  // user override → default
    void        SetFolderDisplayName(FolderIdx idx, const std::string& name);
    void        ResetFolderName(FolderIdx idx);
}
```

## Dispatcher refactor (Z2.0)

Extend `CtxSurface` enum from 4 → 9 values:

```cpp
// include/ul/menu/qdesktop/qd_DesktopIcons.hpp (around line 838)
enum class CtxSurface : u8 {
    None,
    Window,
    Dock,
    DesktopFolder,
    EmptyDesktop,      // NEW (Z2.1)
    FavoriteItem,      // NEW (Z2.5)
    MinimizedTile,     // NEW (Z2.4)
    HotCornerWidget,   // NEW (Z2.8)
    TopBar,            // NEW (Z2.9)
};
```

Replace the 4 per-surface `ctx_opt_*` index members with a single dispatch table:

```cpp
// Per-surface option mapping — selector index → action enum
struct CtxOptionMap {
    CtxSurface surface;
    int        selected_index;
    int        action_code;       // surface-private enum
};
static constexpr size_t kMaxCtxOpts = 16;
int  ctx_opts_[kMaxCtxOpts] = {-1};   // action codes by visible-index
int  ctx_opt_count_ = 0;
```

Dispatch site (`qd_DesktopIcons.cpp:3984-4058`) becomes:
```cpp
switch (ctx_surface_) {
    case CtxSurface::Window:         DispatchWindowCtx(...); break;
    case CtxSurface::Dock:           DispatchDockCtx(...); break;
    case CtxSurface::DesktopFolder:  DispatchFolderCtx(...); break;
    case CtxSurface::EmptyDesktop:   DispatchEmptyDesktopCtx(...); break;
    case CtxSurface::FavoriteItem:   DispatchFavoriteCtx(...); break;
    case CtxSurface::MinimizedTile:  DispatchMinimizedCtx(...); break;
    case CtxSurface::HotCornerWidget:DispatchHotCornerCtx(...); break;
    case CtxSurface::TopBar:         DispatchTopBarCtx(...); break;
    default: break;
}
```

## Submenu support (Z2.0)

`QdContextMenu` extension — exactly one nested level:

```cpp
// include/ul/menu/qdesktop/qd_ContextMenu.hpp
struct MenuItem {
    std::string                 label;
    std::vector<std::string>    submenu_items;   // empty = leaf item
    bool                        disabled = false;
};

void Open(SDL_Renderer* r, const std::vector<MenuItem>& items, s32 ax, s32 ay);
// Convenience overload to keep existing call sites working:
void Open(SDL_Renderer* r, const std::vector<std::string>& items, s32 ax, s32 ay);

struct Selection {
    int parent_index = -1;   // index in top-level items
    int sub_index    = -1;   // index in submenu (-1 if leaf was top-level)
};
Selection GetSelection() const;
// Existing GetSelectedIndex() returns parent_index when sub_index == -1,
// preserving back-compat for current call sites.
```

Visual: submenu chevron `▸` after label; opens to the right of the parent menu;
ESC/B/cancel-anywhere closes back to parent (one level); second ESC closes everything.

## Phase Z2 implementation order

Progress legend: ✅ shipped + HW-verified · 🚢 shipped + awaiting HW verify · ⏳ pending · 🟡 partial

```
Z2.0a ✅ QdContextMenu submenu primitive (chevron, auto-flip, ESC-bubble)
Z2.0b ✅ QdVisibility singleton + qos-visibility.toml atomic I/O
        (plumbed for Z2.3b; no surface consumes yet)
Z2.0c   SKIPPED — folded enum + dispatch wiring into each surface's own
        sub-phase to avoid dead code in the tree

Z2.1  ✅ Empty desktop ZL — [New Custom Folder, Change Wallpaper ▸ (10),
                              Refresh, Cancel]

Z2.2  🟡 Desktop folder tile — Z2.2-minimal: Open + Choose Folder Theme ▸
        DEFERRED to Z2.2-extra (after HW gate):
          • Disable Folder (needs DesktopFolderId-level enable flag in
            qos-folder-settings.toml — different from auto-folder bucket enables)
          • Rename Folder (needs swkbd UX + name persistence)

Z2.3  🟡 Dock icon — Z2.3a: Move to Folder ▸ (8 classifier folders)
        DEFERRED to Z2.3b (after HW gate):
          • Hide flag wired into renderer + hit-test (uses QdVisibility,
            plumbed in Z2.0b but no surface consumes yet)

Z2.4  ✅ Minimized-window tile ZL — [Restore, Close Window, Cancel]
        + new QdWindowManager::CloseMinimizedEntry (no-restore drop)

Z2.5  ✅ Favorites strip ZL — [Launch, Remove from Favorites, Cancel]

[HW gate at this point — first verification pass]
[Bug fix: Terminate-suspended-app now clears g_GlobalSettings locally
 so dock tile + Task Manager row drop immediately]

[Tesla overlay gated OFF (uSystem #define UL_ENABLE_TESLA_OVERLAY 0)
 — fixes the persistent-chrome-during-applet-transition flash that
 was making launches look like the app was loading inside a window]

Z2.6  ⏳ Dock reorder model — QdDockLayout + qos-dock-layout.toml.
        Biggest remaining persistence change.

Z2.7  ✅ Window titlebar — added Snap Left, Snap Right, Move to Center
        to the existing Close/Minimize/Maximize menu. New QdWindow::MoveTo.

Z2.8  ⏳ Hot-corner widget ZL — Customize Hot Corner ▸ + qos-hotcorner-layout.toml
        Allows reordering dropdown rows.  Wider scope than initially
        scoped — needs row-swap UX design.

Z2.9  ✅ Top-bar ZL — [Lock Screen, About Q OS, Cancel]
        New CtxSurface::TopBar.  Hot-corner exclusion shared with
        empty-desktop path.

Z2.10 ⏳ Vault popup migration — replace bespoke 10-option enum with
        QdContextMenu submenu shape. Behavior-preserving refactor of
        a heavily-used feature, deferred until rest of Z2 stabilizes
        on HW.
```

**Status as of 2026-05-19:** v3.1.0 committed (`f01d2fed`) with Z2.0a/Z2.0b/Z2.1/Z2.2-min/Z2.4/Z2.5 + terminate-clear fix. v3.1.1-staging working tree adds Z2.3a/Z2.7/Z2.9 + Tesla overlay gate.

**Remaining:** Z2.2-extra (Disable + Rename), Z2.3b (Hide rendered), Z2.6 (Dock reorder), Z2.8 (Hot-corner customize), Z2.10 (Vault migration). Plus any HW-bug-check follow-ups.

## Phase Z3+ (deferred)

- Rename/Change Icon UI for dock entries (needs swkbd + file picker)
- Per-entry hide flag UI sweep (Z2.0 plumbed backend; surfaces still need hide options)
- Settings/About row context menus (low value)
- Per-window persistent state (always-on-top, sticky workspace)
- Notifications surface
- Clipboard primitive for "Copy Value"

## Risk register

| Risk | Mitigation |
|---|---|
| Submenu primitive corner cases (ESC bubbling, hover transitions) | Z2.0 includes unit-test for cancel paths; ship Z2.1 first as smoke test before Z2.2/Z2.3 use submenus heavily |
| Dock reorder breaks existing tap dispatch | Z2.6 is gated behind Z2.1-Z2.5 HW verify; dock rendering and dispatch share single source-of-truth (`QdDockLayout::Slots()`) |
| `qos-visibility.toml` stale entries (entry deleted after hide) | Stale entries are harmless (lookup returns false); add `Prune()` call on uMenu startup that drops IDs no longer in the entry registry |
| Folder rename breaks classifier dispatch | Internal `FolderIdx` (0..5) is the stable key; rename is display-only. Re-classify still works |
| Submenu render z-order vs. context menu | Z-order rule: parent menu z=N, submenu z=N+1, help overlay z=N+2 (preserve existing contract) |

## Reference points (code reuse pattern)

The just-shipped suspended-app context menu (`fb8f8b18`) is the cleanest exemplar
of the unified pattern:

- Tile owns its surface state (`QdSuspendedAppDockEntry`)
- Window manager owns the `QdContextMenu` instance (`suspended_ctx_menu_`)
- Caller drains menu first on each input pass, then dispatches via target id
- Render hook called by `QdDesktopIconsElement::OnRender` after `RenderAll`

Same pattern translates to:
- Minimized tile + `QdWindowManager::minimized_ctx_menu_` (Z2.4)
- Favorite item + `QdDesktopIconsElement::context_menu_` (Z2.5)
- Empty desktop + `QdDesktopIconsElement::context_menu_` (Z2.1)

## Acceptance criteria

| Phase | Criterion |
|---|---|
| Z2.0 | Submenu primitive renders, navigates D-pad + touch, closes parent on ESC twice; `QdVisibility::Get()` survives reboot |
| Z2.1 | "New Custom Folder" creates a folder; "Change Wallpaper ▸" cycles through 10 packs persistently |
| Z2.2 | "Disable Folder" hides folder until re-enabled; rename persists across reboot |
| Z2.3 | "Move to Folder ▸ Games" repositions the entry; persists across reclassification |
| Z2.4 | "Restore" + "Close Window" on minimized tile work without flicker |
| Z2.5 | "Remove from Favorites" updates strip immediately and persists |
| Z2.6 | Dock survives reorder + reboot; hidden built-ins stay hidden; reset-to-default restores |
| Z2.7 | Snap Left/Right/Center apply to focused window |
| Z2.8 | Hot-corner row reorder persists |
| Z2.9 | Lock Screen from top-bar opens lock screen |
| Z2.10 | Vault context menu identical in behavior to today, rendered via `QdContextMenu` |

## Versioning

- Phase Z2.0-Z2.5 → uMenu v3.2.0 (incremental)
- Phase Z2.6+ → uMenu v3.3.0 (introduces dock-layout TOML schema)
- Phase Z2.10 → uMenu v3.4.0 (vault migration)

---

*Approved by creator decision matrix 2026-05-19. Z2.0 implementation pending creator "proceed" signal.*
