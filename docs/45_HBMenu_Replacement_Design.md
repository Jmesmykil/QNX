# 45 — HBMenu Replacement Design (Phase 1 — Vault)

**Author:** Design captured 2026-04-24
**Phase:** Phase 1 of `44_Three_Phase_Roadmap.md` — applet-mode + vault
**Trigger:** Creator directive: "roll in the default HBmenu code into the vault system and restructure it to make more sense like our existing one like a real computers"
**Goal:** Eliminate the standalone HBMenu surface from the Q OS user experience; reclaim its storage footprint; absorb its functionality into a real-computer-style file browser ("the vault").

---

## 1. What HBMenu does today

HBMenu is the canonical homebrew launcher on Switch CFW. There are two distinct components frequently conflated:

1. **`hbloader`** — the NRO that gets injected into the Photo Capture / Album library-applet via Atmosphère's `loader` mitm. This is the **applet runtime**: it sets up `nx_main`, configures heap, initialises `appletInitialize`, and exposes the homebrew-ABI environment that NROs link against. Without `hbloader`, an `.nro` file simply won't run.

2. **`hbmenu`** — a user-facing NRO that runs ON TOP of `hbloader`, presents a list of `.nro` files in `sdmc:/switch/`, and launches the selected one (it asks `hbloader` to chain into the chosen NRO and exits itself).

These two are usually shipped together as a bundle. `hbloader` is structural — without it no NROs work. `hbmenu` is just one possible UI for picking which NRO to chain. Q OS already replaces the UI surface with the desktop, so `hbmenu` is redundant.

**What we keep:** `hbloader` (must keep — it's how the Switch executes any NRO at all under CFW).

**What we replace:** `hbmenu` (the NRO listing/launching UI). Q OS desktop already does this for the dock; the vault file browser will do it for the full SD-card NRO inventory.

---

## 2. Storage footprint

| Asset | Approx size | Action |
|---|---|---|
| `sdmc:/hbmenu.nro` | 5–8 MB | DELETE in Q OS install |
| `sdmc:/switch/hbmenu/*` (theme, language) | 1–3 MB | DELETE in Q OS install |
| `sdmc:/atmosphere/contents/<hbmenu-tid>/exefs/main.nso` (rare custom) | 0–6 MB | varies — DELETE if present |
| `sdmc:/atmosphere/contents/010000000000100D/exefs/main.npdm` (Album applet override) | small | KEEP — this is `hbloader` injection |
| `sdmc:/atmosphere/contents/010000000000100D/exefs/main.nso` (the `hbloader` NRO) | 1–3 MB | KEEP — required for any NRO to run |

**Net savings: ~6–17 MB.** Modest but free, and the disappearance of HBMenu as a competing surface is the more important win.

---

## 3. The Q OS vault — design

The vault is the Q OS file browser. It serves three purposes:

1. **NRO inventory + launch.** All `.nro` files on the SD card become visible, browsable, launchable. Replaces `hbmenu`'s function entirely.
2. **General file manager.** Real-computer feel — navigate folders, see file metadata, open known formats (logs, configs, JSON, plain text, images).
3. **System config surface.** `/qos-shell/`, `/atmosphere/config/`, `/switch/` are exposed as canonical "system folders" with friendly labels.

### 3.1 Layout — Finder-style

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ◀ ▶  ⌂  Path: /switch/                          [Search...]    [⊞ ⊟]    │  ← top: nav + search
├────────────────────┬─────────────────────────────────────────────────────┤
│ FAVORITES          │  ┌────┐ ┌────┐ ┌────┐ ┌────┐                       │
│   Desktop          │  │ ▣  │ │ ▣  │ │ ▣  │ │ ▣  │                       │
│   Switch (NROs)    │  └────┘ └────┘ └────┘ └────┘                       │
│   Logs             │  homebrew_browser.nro                              │
│   Atmosphère       │  retroarch.nro                                     │
│ DEVICES            │  goldleaf.nro                                      │
│   SD card          │  ...                                               │
│   ums              │                                                    │
│ TAGS               │                                                    │
│   Recent           │                                                    │
│   Pinned           │                                                    │
└────────────────────┴─────────────────────────────────────────────────────┘
```

Two-pane: sidebar (favourites / devices / tags) + main pane (current folder, grid or list view via the `⊞ ⊟` toggle).

### 3.2 Backend — NRO discovery

Same scan logic that `qd_DesktopIcons.cpp::ScanNros()` already implements, but parameterised by directory:

```cpp
void QdVaultLayout::ScanDirectory(const char *path) {
    DIR *d = opendir(path);
    if (!d) { return; }
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (IsDirectory(e)) {
            entries_.push_back(MakeFolderEntry(e));
        } else if (EndsWithIgnoreCase(e->d_name, ".nro")) {
            entries_.push_back(MakeNroEntry(path, e));
        } else if (IsKnownFileType(e->d_name)) {
            entries_.push_back(MakeFileEntry(path, e));
        }
    }
    closedir(d);
}
```

NRO entries get the same NACP-icon decode pass as the desktop (read the embedded NACP block at the end of the NRO file, extract icon JPEG, decode via SDL2_image — same path as `qd_UserCard::DecodeAvatar` for Switch user avatars, since they're the same JPEG format).

### 3.3 Launch — NRO

When the user taps an NRO entry, the vault calls into the existing applet-launch path:

```cpp
ul::menu::smi::LaunchNro(entry.full_path, /*argv*/{});
```

Same code path that the dock currently uses. No new launch primitive needed — we are just exposing more NROs through the desktop UI.

### 3.4 Open — non-NRO files

For known viewable types (`.log`, `.json`, `.toml`, `.txt`, `.png`, `.jpg`), the vault opens them in a built-in viewer applet:

| Extension | Viewer |
|---|---|
| `.log`, `.txt`, `.toml`, `.json` | `QdTextViewer` — scrollable monospace pane (programmatic, no new applet) |
| `.png`, `.jpg`, `.bmp` | `QdImageViewer` — full-screen image viewer (programmatic) |
| `.nro` | LAUNCH (see 3.3) |
| Anything else | "No viewer for this file type" toast — selectable for hex/copy operations |

These viewers are inline elements on the desktop, NOT separate NROs. They open in a panel over the vault and dismiss on B-button — feels native, sub-100ms open/close.

---

## 4. Phase 1 dev windows — separate from vault

The creator's directive separates dev tools from the vault:

> "we will be able to start NX link in a window and usb serial etc"

These are **applet-mode windows**, not vault entries:

| Tool | Mechanism | Status |
|---|---|---|
| NXLink session | Existing `qd_DevTools.cpp::TryEnableNxlink` already toggles state. Wrap in a window UI that shows current host + port + flush button. | wraps existing |
| USB serial | Existing `qd_DevTools.cpp::TryEnableUsbSerial` | wraps existing |
| Log flush | Existing `qd_DevTools.cpp::FlushAllChannels` | wraps existing |
| Telemetry HUD | New — overlay rendered by the desktop, not a window | scheduled v0.22 |
| Stress mode | New — ramps system load, captures crash logs | scheduled v0.23 |

The dev-tool windows live in their own dock zone (left edge of dock, separate from the user-app icons), launchable from a single "Dev Tools" tray icon that expands when clicked.

---

## 5. Implementation plan

**Stage 1 — Vault skeleton**

- New layout `QdVaultLayout` registered as `MenuType::Vault` in the layout enum.
- Entry point: a "Files" icon on the dock, plus a "/" key on USB-keyboard once that input is wired.
- Reuses `QdDesktopIconsElement::ScanNros` (refactored to take a path parameter) and the NACP icon-decode pass.

**Stage 2 — Sidebar + nav**

- Sidebar element with hard-coded canonical roots (`Desktop`, `/switch/`, `/qos-shell/logs/`, `/atmosphere/`, `/`, etc.).
- Path bar at top with back/forward/up history stack.

**Stage 3 — Inline viewers**

- `QdTextViewer` element: scrollable monospace text, line-number gutter, no syntax highlighting (deferred). Backed by `mmap`-style chunk read so 50 MB log files don't OOM.
- `QdImageViewer` element: SDL2_image decode + fit-to-screen + zoom on Y/X.

**Stage 4 — HBMenu removal**

- Q OS installer (or a one-time migration script in `tools/`) deletes `sdmc:/hbmenu.nro` and `sdmc:/switch/hbmenu/`.
- Documented in `STATE.toml` migration notes.
- Add a desktop banner toast on first run that says "HBMenu has been integrated into the Q OS Vault. Reclaimed ~12 MB."

**Stage 5 — Dev-tool windows**

- Three small layouts (`QdNxlinkWindow`, `QdUsbSerialWindow`, `QdLogFlushWindow`) that wrap the existing `qd_DevTools.cpp` API and add live status display (host:port for nxlink, byte counters for USB, last-flush timestamp).

---

## 6. What this design does NOT do

- It does not replace `hbloader`. NROs still execute via Atmosphère's library-applet hijack — same as today.
- It does not require Phase 2 work. Everything in this document fits the 448 MB applet ceiling.
- It does not modify signed firmware.
- It does not introduce new IPC services or kernel-side patches.
- It does not enable concurrent NRO execution. NROs still run one at a time, killed on Home press, cold-relaunch on next click. Background concurrency is a Phase 2/3 concern.

---

## 7. Effort estimate

| Stage | LOC estimate | Risk |
|---|---|---|
| 1 — Vault skeleton | ~500 | low (refactor of existing scan code) |
| 2 — Sidebar + nav | ~400 | low (UI plumbing) |
| 3 — Inline viewers | ~700 (text + image combined) | medium (memory bounds for big files) |
| 4 — HBMenu removal | ~50 (migration script) | low (just file deletes) |
| 5 — Dev-tool windows | ~600 (three small layouts) | medium (state-display polish) |
| **Total** | **~2250** | overall low — almost all paths already exist somewhere |

---

## 8. Cross-references

- `docs/44_Three_Phase_Roadmap.md` — strategic context (Phase 1 vs 2 vs 3)
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp` — existing NRO scan + launch
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp` — existing dev-tool toggles
- `src/projects/uMenu/source/ul/menu/qdesktop/qd_UserCard.cpp` — existing NACP-style JPEG decode
- SwitchBrew wiki: [`Homebrew_Loader_(hbl)`](https://switchbrew.org/wiki/Homebrew_Loader_(hbl)), [`Album_Library_Applet`](https://switchbrew.org/wiki/Album_(applet))

