# Rung-2 uMenu UX-Port Design (v0.3.0 → v0.7.0)

> Post-SDL2 implementation roadmap. Planning-only doc; implementation agents dispatch when creator runs `sudo dkp-pacman -S switch-sdl2 switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx`.

## Overview

The Rung-1 GUI lane (`mock-nro-desktop-gui`) has delivered 8 validated UX features. Rung-2 (`qos-ulaunch-fork`) must port them from Rust/SDL2 into the Plutonium UI framework (C++ OOP) used by upstream uLaunch. This plan structures v0.3.0–v0.7.0 so build agents fire with clear, isolated deliverables.

**Key advantage of Rung-2 vs Rung-1:** uMenu runs AS qlaunch (TID `0100000000001000`), not inside an Album-applet sandbox via hbloader. This means:
- `ns:am2` is reachable → **real installed-games list works**
- Full applet memory pool (~240 MB, not 50 MB)
- Direct access to `set:sys`, `set:cal`, `time:s`, `nifm:a` without container restrictions
- Can chainload to any application directly

## 8 UX Features Port Table

| # | Feature | Rung-1 Rust Source | Plutonium Target | Risk |
|---|---------|--------------------|------------------|------|
| 1 | Dock magnification (1.4x/1.2x/1.05x hover) | `src/launchpad.rs` + `src/wm.rs` hover-zone | NEW `ul/menu/ui/ui_DockElement.hpp` | Low |
| 2 | Icon grid bounds + wrap | `src/desktop_icons.rs::clamp_icon_position()` | Modify `EntryMenu::OnInput()` | Low |
| 3 | Click/A-launch → chainload | `src/main.rs` HitTarget::DesktopIcon + `wm.rs::launch_desktop_icon` | `MainMenuLayout::HandleDesktopIconClick()` + `smi::sf::LaunchApplication()` | Low |
| 4 | NACP JPEG icons (ASET decode) | `src/desktop_icons.rs` + v1.1.4 zune-jpeg | Extend `ul/menu/ui/ui_Common.cpp::LoadApplicationIcon()` (stb_image already linked) | Moderate |
| 5 | Finder D-pad focus surfaces | `src/wm.rs::FocusSurface` enum | NEW state machine in `MainMenuLayout::RouteDpadInput()` | Low-Moderate |
| 6 | Full-width OSK (1280×320) | `src/osk.rs` | NEW `ul/menu/ui/ui_OnScreenKeyboard.hpp` | Moderate |
| 7 | Draggable keyboard window | `src/osk.rs` + `src/multitouch.rs` drag state | NEW drag + velocity smoothing; mouse input wiring in Application | **HIGH** (uLaunch is gamepad/touch only; SDL2 mouse events need wiring) |
| 8 | Per-surface right-click ctxmenu | `src/ctxmenu.rs::MenuSource` enum | NEW `ul/menu/ui/ui_ContextMenu.hpp` | Low-Moderate |

Plus: **Unrestricted installed-games list** — `ns:am2::ListApplicationRecord` works here (not container-restricted).

## Rung-2 Plutonium Architecture Primer

Plutonium framework namespaces (`pu::ui::*`) — OOP-based:
- **`pu::ui::Application`** — render loop + event dispatch (inherits SDL2 App)
- **`pu::ui::Layout`** — stateful container (uLaunch uses `IMenuLayout` subclasses)
- **`pu::ui::elm::Element`** — base class for interactive components
- **`pu::ui::elm::Menu`** — list-based selector with callbacks + transitions
- **`pu::ui::SigmoidIncrementer<T>`** — frame-based easing curve (reusable for our magnify interp)

Upstream uLaunch classes:
- `MainMenuLayout` — home screen, hosts `entry_menu`
- `EntryMenu` — custom `pu::ui::elm::Element` for icon grid
- `MenuApplication` — pu::ui::Application subclass, owns SDL event loop

## Per-Feature Port Design

### Feature 1 — Dock Magnification

**Target:** `src/projects/uMenu/include/ul/menu/ui/ui_DockElement.hpp` (NEW)

```cpp
class DockElement : public pu::ui::elm::Element {
private:
    struct DockSlot {
        pu::sdl2::TextureHandle::Ref icon;
        u32 base_size = 64;
        float scale = 1.0f;
    };
    std::vector<DockSlot> slots;

public:
    void UpdateHoverZone(s32 mouse_x, s32 mouse_y) {
        for (auto &slot : slots) {
            bool in_zone = IsMouseOver(slot, mouse_x, mouse_y);
            slot.scale = in_zone
                ? LerpToward(slot.scale, 1.4f, 4)
                : LerpToward(slot.scale, 1.0f, 4);
        }
    }
    void OnRender(const s32 x, const s32 y) override {
        for (const auto &slot : slots) {
            u32 size = slot.base_size * slot.scale;
            slot.icon->Draw(x, y, size, size);
        }
    }
};
```

**Plutonium API gaps:** `SigmoidIncrementer` already exists (used in cursor transitions) — reuse. Hover detection requires wiring from parent Layout's mouse/touch handler.

### Feature 2 — Icon Grid Bounds + Wrap

**Target:** modify `EntryMenu::OnInput()` / `EntryMenu::ClampGridPosition()`

```cpp
void EntryMenu::ClampGridPosition() {
    const u32 total = this->entries.size();
    const u32 max_col = (total + entry_height_count - 1) / entry_height_count;
    const u32 cur_col = this->cur_entry_idx / entry_height_count;
    if (cur_col >= max_col && max_col > 0) {
        this->cur_entry_idx = this->cur_entry_idx % entry_height_count;
        this->base_entry_idx_x = 0;
    }
}
```

**Plutonium API gaps:** none.

### Feature 3 — Click/A Launch Chainload

**Target:** `MainMenuLayout::HandleDesktopIconClick(const Entry &)`

```cpp
void MainMenuLayout::HandleDesktopIconClick(const Entry &selected) {
    if (selected.Is<EntryType::Homebrew>()) {
        pu::audio::PlaySfx(launch_hb_sfx);
        auto input = CreateLaunchTargetInput(selected.hb_info.nro_target);
        smi::sf::LaunchApplication(input);
    } else if (selected.Is<EntryType::Application>()) {
        pu::audio::PlaySfx(launch_app_sfx);
        smi::sf::LaunchApplication(selected.app_info.title_id);
    }
}
```

**Plutonium API gaps:** none. `smi::sf::LaunchApplication()` exists in uLaunch's SystemManagerInterface.

### Feature 4 — NACP JPEG Icons

**Target:** extend `src/projects/uMenu/source/ul/menu/ui/ui_Common.cpp::LoadApplicationIcon()`

```cpp
pu::sdl2::TextureHandle::Ref LoadApplicationIconFromNACP(const std::string &nacp_path) {
    // Try embedded JPEG first (256×256 baseline DCT)
    if (FsFileExists(nacp_path)) {
        auto jpeg_data = fs::ReadFile(nacp_path);
        int w = 0, h = 0, channels = 3;
        u8 *pixels = stbi_load_from_memory(
            jpeg_data.data(), jpeg_data.size(), &w, &h, &channels, 3);
        if (pixels) {
            auto tex = pu::sdl2::TextureFromRGBPixels(w, h, pixels);
            stbi_image_free(pixels);
            return tex;
        }
    }
    return LoadApplicationIcon(nacp_path);  // PNG fallback
}
```

**Plutonium API gaps:** verify `pu::sdl2::TextureFromRGBPixels()` signature — if not present, use `SDL_CreateTextureFromSurface()` directly.
**Risk note:** JPEG decode time can cause frame hitches on icon-heavy screens. Async load (thread pool) recommended; show placeholder until ready.

### Feature 5 — Finder D-Pad Focus Surfaces

**Target:** add to `MainMenuLayout`

```cpp
enum class FocusSurface { Dock, DesktopGrid, Window, OnScreenKeyboard, Overlay };

class MainMenuLayout : public IMenuLayout {
private:
    FocusSurface current_surface = FocusSurface::Dock;

    void RouteDpadInput(const u64 keys_down) {
        switch (current_surface) {
        case FocusSurface::Dock:
            if (keys_down & HidNpadButton_Left)  selected_dock_index--;
            if (keys_down & HidNpadButton_Right) selected_dock_index++;
            if (keys_down & HidNpadButton_Down)  current_surface = FocusSurface::DesktopGrid;
            break;
        case FocusSurface::DesktopGrid:
            this->entry_menu->OnInput(keys_down);
            if (keys_down & HidNpadButton_Up && at_top_row())
                current_surface = FocusSurface::Dock;
            break;
        case FocusSurface::OnScreenKeyboard:
            this->osk->OnInput(keys_down);
            if (keys_down & HidNpadButton_B) current_surface = prev_surface;
            break;
        /* Window, Overlay */
        }
    }
};
```

**Plutonium API gaps:** none. Render a subtle focus ring (1-2 px outline) using `pu::ui::elm::Rectangle` on the active surface so user can see where D-pad is.

### Feature 6 — Full-Width On-Screen Keyboard

**Target:** `src/projects/uMenu/include/ul/menu/ui/ui_OnScreenKeyboard.hpp` (NEW)

```cpp
class OnScreenKeyboard : public pu::ui::elm::Element {
private:
    struct KeyButton {
        std::string label;
        pu::ui::Color bg_color;
        s32 x, y, w, h;
    };
    std::vector<std::vector<KeyButton>> rows;  // QWERTY + numerics + symbols
    bool is_visible = false;
    s32 origin_x = 0, origin_y = 400;  // default: bottom-docked, 1280 wide × 320 tall

public:
    void OnRender(const s32, const s32) override {
        if (!is_visible) return;
        pu::sdl2::DrawRectFill({20,20,20,240}, origin_x, origin_y, 1280, 320);
        for (const auto &row : rows)
            for (const auto &key : row) {
                pu::sdl2::DrawRectFill(key.bg_color, origin_x+key.x, origin_y+key.y, key.w, key.h);
                pu::ui::elm::TextBlock::Draw(key.label, origin_x+key.x+5, origin_y+key.y+5);
            }
    }
};
```

**Plutonium API gaps:** `pu::sdl2::DrawRectFill` exists. TextBlock::Draw outside a Layout hierarchy — verify or use inline `SDL_RenderCopy`.

### Feature 7 — Draggable Keyboard Window

**Target:** `OnScreenKeyboard::OnDrag()` + Application-level SDL2 mouse/touch event hook

```cpp
struct DragState {
    s32 drag_start_x = -1, drag_start_y = -1;
    s32 current_x = 0, current_y = 400;
    float velocity_x = 0, velocity_y = 0;
};

void OnScreenKeyboard::OnDrag(s32 mouse_x, s32 mouse_y, bool is_titlebar_hit) {
    if (!is_titlebar_hit) return;
    if (drag.drag_start_x < 0) {
        drag.drag_start_x = mouse_x;
        drag.drag_start_y = mouse_y;
        return;
    }
    s32 dx = mouse_x - drag.drag_start_x;
    s32 dy = mouse_y - drag.drag_start_y;
    drag.velocity_x = drag.velocity_x * 0.8f + dx * 0.2f;
    drag.velocity_y = drag.velocity_y * 0.8f + dy * 0.2f;
    drag.current_x += (s32)drag.velocity_x;
    drag.current_y += (s32)drag.velocity_y;
    // Clamp: 50% minimum visible
    drag.current_x = std::clamp(drag.current_x, -640, 1280);
    drag.current_y = std::clamp(drag.current_y, -160, 720);
}
```

**Plutonium API gaps:** **MAJOR** — uLaunch is gamepad/touch only. No mouse event dispatch. Two options:
- **Option A:** Touch-drag only (no mouse support). Single-finger drag on OSK titlebar. Plutonium has touch event dispatch — this works without Application changes.
- **Option B:** Patch `pu::ui::Application::SetupInput()` to pump SDL2 `SDL_MOUSEMOTION` / `SDL_MOUSEBUTTONDOWN` events. Bigger scope, enables cursor everywhere.

**Recommendation:** Option A for v0.5.0 (touch only). Option B for a later milestone.

### Feature 8 — Per-Surface Right-Click Context Menus

**Target:** `src/projects/uMenu/include/ul/menu/ui/ui_ContextMenu.hpp` (NEW)

```cpp
class ContextMenu : public pu::ui::elm::Element {
public:
    enum class ItemType {
        Launch, OpenInNewWindow, Rename, Delete,
        ShowInVault, Properties, CopyPath, Cancel
    };
    enum class ContextKind {
        DesktopIcon, DockIcon, EmptyDesktop, WindowTitleBar,
        WindowContent, VaultItem, Keyboard
    };

    void Populate(ContextKind kind) {
        items.clear();
        switch (kind) {
        case ContextKind::DesktopIcon:
            items = { {"Open", Launch}, {"Open in New Window", OpenInNewWindow},
                      {"Rename", Rename}, {"Delete", Delete},
                      {"Show in Vault", ShowInVault}, {"Properties", Properties} };
            break;
        case ContextKind::DockIcon:
            items = { {"Open", Launch}, {"Keep in Dock", ...},
                      {"Show in Vault", ShowInVault}, {"Remove from Dock", ...} };
            break;
        /* ...5 more kinds */
        }
    }

    void OnInput(const u64 keys_down) override {
        if (keys_down & HidNpadButton_Up)   selected_idx = (selected_idx + items.size()-1) % items.size();
        if (keys_down & HidNpadButton_Down) selected_idx = (selected_idx + 1) % items.size();
        if (keys_down & HidNpadButton_A)    DispatchAction(items[selected_idx].second);
        if (keys_down & HidNpadButton_B)    Close();
    }
};
```

**Plutonium API gaps:** none — menus are first-class. Integration requires tracking active surface at right-click time.

## v0.3.0 Minimum Deliverable (post-SDL2)

- Upstream uLaunch compiled + boots
- Dark Liquid Glass color theme applied (JSON via cfg system)
- **Installed games unrestricted list** (no container sandbox)
- Features 1, 2, 3, 5 (dock magnify, grid clamp, icon launch, D-pad focus) **MUST ship**
- Feature 4 partial (PNG path works; JPEG as v0.3.1 patch)
- Features 6, 7, 8 deferred to v0.4.0+

**Success criterion:** boots on Switch OG Erista, home screen renders dock + games list, Feature-1-through-5 emit `EVENT UX_…` lines to `sdmc:/switch/qos-ulaunch-vX.Y.Z.log`, zero crashes over a 10-minute session.

## v0.4.0–v0.7.0 Milestone Plan

| Version | Adds | Notes |
|---------|------|-------|
| **v0.4.0** | Full-width OSK (Feature 6) + text input for Settings app | Gatekeeps any feature needing keyboard entry |
| **v0.5.0** | Draggable OSK (Feature 7) via touch-drag (Option A) | Skips full mouse support — add in later cycle |
| **v0.5.1** | JPEG NACP icons complete (Feature 4 full) | Async decode + thumbnail cache |
| **v0.6.0** | Per-surface ctxmenu (Feature 8) + real action handlers | Rename/Delete wire to `fsp-srv`; Properties reads NACP |
| **v0.7.0** | Wallpaper integration + theme polish | Port procedural render from `src/wallpaper.rs` (Cold Plasma Cascade) |
| **v1.0.0** | Sysmodule install replaces stock qlaunch | Ship complete uMenu |

## Plutonium API Gap Register

| Gap | Severity | Workaround |
|-----|----------|-----------|
| Mouse events not wired into Application loop | **HIGH** | Touch-drag only (Option A) for v0.5.0. Defer full mouse. |
| `pu::sdl2::TextureFromRGBPixels()` may not exist | **MODERATE** | Wrap `SDL_CreateTextureFromSurface()` directly |
| TextBlock rendering outside Layout | **MODERATE** | Inline `SDL_RenderCopy` with manual coords, or Element parent |
| Async JPEG decode infrastructure | **MODERATE** | Use `std::thread` + a bounded queue; icons are 256×256 baseline DCT so fast |
| Telemetry logging infra (match Rung-1 `EVENT …` format) | **LOW** | Add a thin `UL_LOG_EVENT(key, value)` macro that writes to sdmc log |

## Test Procedure Per-Feature

Each feature must emit an `EVENT UX_<kind>` line on exercise; logs go to `sdmc:/switch/qos-ulaunch-v<X.Y.Z>.log`:

1. **Dock magnify** — hover over dock icons, verify 1.4x zoom, logs `EVENT UX_DOCK scale=1.4 slot=2`
2. **Grid wrap** — navigate right at edge, verify wrap + `EVENT UX_GRID_WRAP from=11 to=0`
3. **Icon launch** — select game, A-press, verify `EVENT UX_LAUNCH tid=0x0100...`
4. **NACP icons** — boot with icon-heavy game list, verify `EVENT UX_ICON_DECODE format=jpeg w=256 h=256 ms=<N>`
5. **D-pad focus** — tab through surfaces, verify `EVENT UX_FOCUS_SWITCH from=Dock to=DesktopGrid`
6. **OSK** — open settings with text field, verify `EVENT UX_OSK_OPEN w=1280 h=320`
7. **OSK drag** — touch-drag titlebar, verify `EVENT UX_OSK_DRAG vx=<f> vy=<f>`
8. **ctxmenu** — long-press dock icon, verify `EVENT UX_CTX_MENU kind=DockIcon items=4`

## Risk Summary

- **Mouse input (Feature 7)** — HIGH risk, mitigated by touch-drag-only Option A
- **JPEG decode stalls (Feature 4)** — MODERATE, mitigated by async load + placeholder
- **ctxmenu event bubbling (Feature 8)** — MODERATE, mitigated by strict `OnInput() → true (consumed)` discipline
- **D-pad state machine bugs (Feature 5)** — LOW-MODERATE, add unit tests for all transitions

## Success Criteria (v0.3.0 gate)

- [ ] Compiles clean on `aarch64-none-elf` via devkitPro
- [ ] Boots on Switch OG, home screen renders dock + installed-games list
- [ ] Features 1, 2, 3, 5 each emit ≥1 `EVENT UX_…` line per exercise
- [ ] 0 fatal reports in `/atmosphere/fatal_reports/` during 10-min test session
- [ ] Rollback verified: delete `atmosphere/contents/0100000000001000/exefs.nsp` → stock qlaunch resumes on next boot

---

**Owner:** Rung-2 Lane
**Dispatch trigger:** `sudo dkp-pacman -S switch-sdl2 switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx`
**Ready when:** this doc green + upstream uLaunch compiling cleanly in `archive/v0.2.1-prep/`
**Depends on:** Rung-1 GUI v1.1.4 (JPEG decoder) + v1.1.5 (chainload pattern) for reference impls
