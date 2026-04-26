# Upstream Companion Apps — Strategy SSOT

> **Authoring SSOT:** this file
> **Drafted:** 2026-04-25T18:00:00Z
> **Authority:** Creator directive 2026-04-25T17:55:00Z:
> *"Udesigner could be rebuilt from the ground up because we will let them theme
>  Q os themselves later down the road. uScreen — if it can benefit us in some
>  way let's rebrand and integrate it."*
> **Cross-refs:** creator memory `feedback_all_apps_must_be_swift`, the Q OS
> "All apps on Mac are Swift SwiftUI" mandate.

---

## Inherited assets

The qos-ulaunch-fork tree carries two upstream uLaunch companion apps that don't
ship in the runtime path but exist on disk:

| App | Runtime | Status today | Creator decision |
|---|---|---|---|
| **uDesigner** | C++/Dear ImGui + GLFW → WebAssembly via emcc (Emscripten) — runs in browser | In `udesigner:` Makefile target (outputs to `docs/index.html` + `docs/udesigner.js`), NOT in `package:`, never built in current shipping path. Likely hosted on upstream's GitHub Pages. | Mark as legacy reference. Future Q OS-native rebuild planned: "let them theme Q OS themselves later down the road." |
| **uScreen** | Java/JavaFX (Linux-only classifier currently) | In `uscreen:` Makefile target, IS in `package:`, ships in zip but never invoked | Q OS-rebrand + integrate. USB-screen-capture path is genuinely useful. |

---

# uScreen — Investigation + Integration Plan

## What it actually does

uScreen is a Mac/Linux desktop app that displays the Switch's framebuffer in real
time over USB.

**Switch side** (uSystem `main.cpp:1556+` already implemented):
- Reads `ConfigEntryId::UsbScreenCaptureEnabled` (bit already exists)
- On enabled: `usbCommsInitialize()` + `capsscInitialize()`
- Allocates page-aligned USB packet buffer
- On firmware ≥ 9.0.0: uses `CaptureJpegScreenshot()` → JPEG mode
- Else: raw RGBA mode
- Spawns 2 threads — one reads screen, one writes to USB endpoint 0x01

**Desktop side** (Java/JavaFX `uScreen` app):
- libusb4java connection to Nintendo Switch VID 0x057E PID 0x3000
- Bulk endpoint 0x81 read for incoming frames
- JavaFX window renders JPEG / decoded RGBA frames
- ~1797 lines across 6 .java files; modest codebase

## Why it benefits Q OS

The screen-mirroring path solves four real problems for us:

1. **Headless development** — see Switch screen without an HDMI TV/dock setup.
2. **K+5 autonomous test rig — visual channel** — the test rig coordinator
   (see `AUTONOMOUS-TEST-RIG-DESIGN.md`) communicates with the Switch via SD-card
   sentinel files (PING/PONG). Adding a USB-mirror visual feed would let `visual-qa`
   agents assert on actual rendered frames without an HDMI capture card.
3. **Demo recording for marketing** — record the Switch screen on macOS without a
   capture device.
4. **Remote pair-debugging** — share live Switch view with creator over screen-share.

## Integration plan — TWO-PHASE

### Phase 1 (drop weight) — DO NOW

The Java uScreen will not run on creator's modern macOS without classifier patches
(pom.xml hardcodes `<classifier>linux</classifier>` for JavaFX 12). Shipping it
in our package as-is is just dead bytes in the zip.

**Action:** Remove `uscreen` from the `package:` target in `src/Makefile`. Source
stays on disk as legacy reference.

```diff
-package: arc usystem uloader umenu umanager uscreen default-theme-music
+package: arc usystem uloader umenu umanager default-theme-music
```

Add a `legacy-uscreen-archive:` target that zips the Java source to
`archive/legacy-companion-apps/uScreen-source-<sha>.zip` for posterity. Don't
delete from worktree — the source is the spec for Phase 2.

### Phase 2 (Q OS Mirror) — GATED ON Q OS DESKTOP READY

Build `QOS Mirror.app` — native macOS Swift companion app.

**Tech stack** (per "all apps must be Swift" mandate):
- Swift 5.9 + SwiftUI window shell
- AppKit/MetalView for frame blitting (SwiftUI Image is too slow for 60 fps)
- IOKit + IOUSBLib for USB connection (or SwiftIO wrapper)
- Reads same protocol as Java uScreen (VID 0x057E PID 0x3000, EP 0x81 bulk read)

**Wire protocol** (already defined by Switch-side uSystem; we don't break it):
```c
struct UsbPacketHeader {
    UsbMode mode;       // 1=RAW_RGBA, 2=JPEG
    union {
        struct { u32 width, height; } rgba;
        struct { u32 jpeg_size; } jpeg;
    };
};
// Followed by payload bytes.
```

**App surface**:
- Single window, 1280×720 default (Switch native res; resizable with aspect-lock).
- "Connect" button — discovers Switch via IOKit, opens USB endpoint.
- Status bar: connection state, FPS, frame size, mode (RGBA/JPEG).
- Toolbar: "Save Frame" (PNG export), "Record" (H.264 capture via AVFoundation).
- Menu: "Auto-connect on launch", "Always on top", "Stretch to fit".

**K+5 test rig integration** (bonus path):
- App exposes `qos-mirror://capture-frame` URL handler.
- Test rig coordinator can fire that URL and get a PNG written to a known path.
- visual-qa agent picks up the PNG and asserts (e.g., "icon visible at 32×32 in top-right").

**Security / scope**:
- Bonjour/network advertisement: NO. USB-only. Don't expose Switch frame buffer
  to the local network without explicit user consent.
- No automatic recording. User must press Record. Recordings save to user's
  Movies folder, not buried in app cache.

**Distribution**:
- Ships as a separate `.dmg` from the qos-ulaunch-fork release — the homebrew
  cycle must not block on a desktop app.
- Optional install. Q OS uMenu works fine without it.

**Effort estimate**: 6-10 sessions for a working v0.1 (USB connect + display
frames). Another 4-6 sessions for record/export/test-rig hooks.

---

# uDesigner — Future Rebuild Plan

## What it was

uDesigner was XorTroll's **browser-based** theme editor for upstream uLaunch (NOT
Java/JavaFX as initially mis-surveyed). Lets the user assemble `.ultheme` files
(zipped UI assets + metadata) for distribution, all from a web browser.

Currently:
- Source on disk at `src/projects/uDesigner/` (C++ + Dear ImGui + GLFW + OpenGL)
- Builds via Emscripten (`emcc`) to WebAssembly + JavaScript wrapper
- Outputs `docs/index.html` + `docs/udesigner.js` — designed to be hosted on
  upstream's GitHub Pages docs/ branch
- NOT in `package:` target → never built into our shipping zip
- Pulls in libimgui, libglfw, libGL via Emscripten headers

## Creator decision — FUTURE Q OS REBUILD

> *"Udesigner could be rebuilt from the ground up because we will let them theme
>  Q os themselves later down the road."*

This is a **post-1.0 Q OS feature**, not part of the current uMenu cycle. The plan:

1. **Now:** keep Java source on disk as **legacy reference + behavioral spec**.
   Future Q OS theme designer must produce `.qtheme` files that uMenu can read,
   but the editor UI/UX itself starts fresh.

2. **Future v1.x (post-public-release of qos-ulaunch-fork):** build
   **`QOS Theme Designer.app`** — native macOS Swift app.

   Tech stack (per "all apps must be Swift"):
   - Swift 5.9 + SwiftUI
   - PhotoKit / Core Graphics for icon editing
   - Codable + JSON for `.qtheme` manifest
   - ZIP via `Compression.framework` for `.qtheme` packaging

   App surface (high level — full design when its turn comes):
   - File browser left pane: shows asset categories (EntryIcon, OverIcon, TopMenu, etc.)
   - Edit panel center: per-asset PNG preview + replace from Files
   - Live preview right panel: shows mock uMenu desktop with current theme applied
   - Export → `.qtheme` file (zip with manifest.json + ui/ tree mirroring romfs/default/ui/)
   - Auto-validates that all referenced assets exist + dimensions match expected per
     `QOS-REBRAND-ASSET-INVENTORY.md` (which becomes the theme spec).

3. **Wire format**: `.qtheme` is just a renamed `.zip` containing:
   ```
   manifest.json    — {name, author, version, qos_min_version}
   ui/              — full directory tree mirroring romfs/default/ui/
     Background.png
     Main/EntryIcon/Settings.png
     Main/OverIcon/Border.png
     ...
   sound/           — optional; bgm/sfx replacements
   ```
   uMenu's existing theme loader (`TryGetActiveThemeResource` in ui_Common.cpp)
   already reads from `sdmc:/ulaunch/cache/active/` after extraction. The new
   format is .qtheme but the cache layout is identical to today's.

## Action right now

Add these markers without touching code:

1. **`docs/ROADMAP.md`** — add "Q OS Theme Designer" to post-1.0 section.
2. **Top-of-source comment** in `src/projects/uDesigner/Makefile` and the Java
   `Main.java` — mark as "LEGACY UPSTREAM REFERENCE — Q OS native rebuild planned
   post-1.0 in Swift. Do not maintain. Do not modify. Do not include in package."
3. **NO source deletion** — keep as behavioral spec for the future rebuild.

---

## Summary of actions ordered

| Order | Action | Effort | Status |
|---|---|---|---|
| 1 | Remove `uscreen` from `package:` Makefile target | trivial | ready to do now |
| 2 | Add `legacy-uscreen-archive:` Makefile target | trivial | ready to do now |
| 3 | Add LEGACY header comment to uDesigner Makefile + Main.java | trivial | ready to do now |
| 4 | Update `ROADMAP.md` with post-1.0 "QOS Mirror.app" + "QOS Theme Designer.app" | trivial | ready to do now |
| 5 | Build QOS Mirror.app v0.1 in Swift | 6-10 sessions | future, not blocking uMenu cycle |
| 6 | Build QOS Theme Designer.app v0.1 in Swift | 8-12 sessions | post-1.0, not in cycle |

Items 1-4 are pure-deletion/marker work, no risk to current uMenu functionality.
Want me to do 1-4 now?

---

## Cross-references

- `src/Makefile` — `package:` target (modify)
- `src/projects/uScreen/` — Java source (preserve as Phase 2 reference)
- `src/projects/uDesigner/` — Java source (preserve as future-rebuild reference)
- `docs/ROADMAP.md` — add post-1.0 native Swift companions
- `feedback_all_apps_must_be_swift` (creator memory) — Mac apps are Swift SwiftUI
- `feedback_clean_room_claude_desktop_target` (creator memory) — Mac UX north star
- `AUTONOMOUS-TEST-RIG-DESIGN.md` — K+5 visual channel justification for QOS Mirror
