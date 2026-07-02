# uMenu romfs Asset Patch Notes

## v0.2.2 — Q OS Visual Branding (2026-04-18)

### Summary

Intermediate visual-branding patch. No Plutonium C++ code was modified.
All changes are asset-only (UI.json color literals + PNG replacements).

### Attribution

Upstream: uLaunch by XorTroll — https://github.com/XorTroll/uLaunch
License: GPLv2. Q OS fork assets also released under GPLv2.

### Changed files

#### ui/UI.json — color token remapping

| Token              | Stock (uLaunch)  | v0.2.2 (Q OS)    | Notes                        |
|--------------------|------------------|------------------|------------------------------|
| text_color         | #e1e1e1ff        | #e0e0f0ff        | off-white primary text       |
| toast_base_color   | #282828ff        | #1a0b3dff        | deep-space dark               |
| toast_text_color   | #e1e1e1ff        | #e0e0f0ff        | matches text_color            |
| menu_focus_color   | #0094ffff        | #7dd3fcff        | Liquid Glass cyan accent      |
| menu_bg_color      | #046dd0ff        | #5b2a86ff        | cosmic purple primary         |
| dialog_title_color | #e1e1e1ff        | #e0e0f0ff        | off-white                     |
| dialog_cnt_color   | #e9e9e9ff        | #e0e0f0ff        | off-white                     |
| dialog_opt_color   | #ebebebff        | #9d8dc7ff        | muted purple secondary text   |
| dialog_color       | #0094ffff        | #7dd3fcff        | cyan accent                   |
| dialog_over_color  | #046dd0ff        | #5b2a86ff        | cosmic purple                 |

#### PNG replacements (exact same dimensions — uLaunch render positions unchanged)

| File                                       | Size      | Change                                     |
|--------------------------------------------|-----------|--------------------------------------------|
| ui/Background.png                          | 1920x1080 | Deep-space radial gradient + star field    |
| ui/Main/TopMenuBackground/Default.png      | 1797x136  | Purple glass bar with cyan bottom edge     |
| ui/Main/TopMenuBackground/Application.png  | 1797x136  | Purple glass bar variant                   |
| ui/Main/TopMenuBackground/Folder.png       | 1797x136  | Purple glass bar variant                   |
| ui/Main/TopMenuBackground/Homebrew.png     | 1797x136  | Purple glass bar variant                   |
| ui/Main/EntryMenuBackground.png            | 1920x837  | Deep-space gradient panel, cyan top line   |
| ui/Main/InputBarBackground.png             | 1797x60   | Translucent purple glass bar               |
| ui/Main/OverIcon/Selected.png              | 416x416   | Cyan selection ring with purple fill       |
| ui/Main/OverIcon/Border.png                | 444x444   | Cyan rounded-rect border                   |
| ui/Main/OverIcon/Cursor.png                | 444x444   | Cyan glow ring cursor                      |
| ui/Settings/InputBarBackground.png         | 1797x60   | Translucent purple glass bar               |
| ui/Settings/SettingEditableIcon.png        | 100x100   | Green badge (#4ADE80) with pencil mark     |
| ui/Settings/SettingNonEditableIcon.png     | 100x100   | Amber badge (#FBBF24) with lock icon       |
| Logo.png (romfs root)                      | 256x256   | "Q OS / universal os" wordmark             |

#### NOT changed (functional status icons — preserved stock)

- ui/Main/TopIcon/Battery/*.png
- ui/Main/TopIcon/Connection/*.png
- All EntryIcon/*.png (app icon placeholders — not branded)
- All QuickIcon/*.png
- All OverIcon/Corrupted, Gamecard, HomebrewTakeoverApplication, NeedsUpdate, NotLaunchable, Suspended

#### lang/en.json — visible-only strings (keys preserved)

| Key                         | Before                                | After                                  |
|-----------------------------|---------------------------------------|----------------------------------------|
| ulaunch_about               | "uLaunch is an extended..."           | "Q OS Menu is an extended..."          |
| set_info_text               | "Console & uLaunch settings"          | "Console & Q OS settings"              |
| special_entry_text_settings | "Console & uLaunch settings"          | "Console & Q OS settings"              |
| special_entry_text_themes   | "uLaunch themes"                      | "Q OS themes"                          |
| theme_reset_conf            | "...return to uLaunch's default..."   | "...return to Q OS default theme?"     |
| theme_changed               | "uLaunch's theme was reset."          | "Q OS theme was reset."                |
| set_menu_ulaunch            | "uLaunch"                             | "Q OS"                                 |

### Stock-asset backup location

`archive/v0.2.2-prep/stock-assets-backup/`

### Not changed

- uDaemon, uMenu, uManager, uSystem, uLoader process/binary names
- All source code (.cpp, .hpp, Makefile)
- All other lang files (de, es, fr, it, ko, pt-BR)
- theme/Manifest.json

### Restore procedure

To revert to stock uLaunch appearance:
```
cp archive/v0.2.2-prep/stock-assets-backup/default-theme/ui/Background.png \
   archive/v0.2.1-prep/upstream/default-theme/ui/
# ... repeat for each file listed in stock-assets-backup/
```

### Next step

Proceed to v0.3.0 UX-port-design (`archive/v0.3.0-planning/UX-PORT-DESIGN.md`) for
Plutonium C++ class changes enabling full Q OS navigation paradigm.
