#!/usr/bin/env python3
"""
generate-qos-ultheme-bundles.py — emit the 10 Q OS in-binary themes as real
uLaunch .ultheme zip bundles.

This is the *single source of truth* for the 10 Q OS themes. The C++ palette
factories in qd_Theme.hpp still exist as in-memory defaults and as the
fallback when no .ultheme is active, but the user-pickable themes ARE these
bundles — they go through uLaunch's existing SetActiveTheme + RestartMenu
apply flow instead of the parallel qos-folder-theme.toml system.

Run from anywhere:
    python3 scripts/generate-qos-ultheme-bundles.py

Output: 10 files under projects/uMenu/romfs/themes/, e.g.
    q-os-1-q-os.ultheme
    q-os-2-neon.ultheme
    ... etc.

Each bundle contains:
    theme/Manifest.json    (name, format_version=3, release, description, author)
    theme/Icon.png         (1x1 PNG of the theme's accent color)
    ui/QdPalette.json      (17 QdTheme color tokens + wallpaper_pack hint)

The "wallpaper_pack" field is a Q OS extension to QdPalette.json read by
qd_Theme.cpp::LoadThemeFromCache; it tells the wallpaper renderer which
procedural algorithm to use ("Glass" / "Neon" / "Minimal" / "Retro" / "Cards"
/ "Pastel" / "Dark" / "Gradient" / "Blueprint" / "Pixel"). Themes authored
by third parties can omit this field and Q OS falls back to pack 0 (Glass).
"""

import json
import os
import struct
import sys
import zipfile
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(REPO_ROOT, "src", "projects", "uMenu", "romfs", "themes")
RELEASE = "2.7.0"

# Each theme = name + 17 QdTheme color tokens. Tokens match qd_Theme.hpp
# factories EXACTLY (Glass / Neon / Minimal / Retro / Cards / Pastel / Dark
# / Gradient / Blueprint / Pixel). Minimal/Cards/Dark use the v2.6.0
# distinctness-fix palettes (warm / amber / ember instead of cyan family).

THEMES = [
    {
        "pack": 0,
        "name": "Q OS",
        "wallpaper_pack": "Glass",
        "description": "Canonical Q OS — Dark Liquid Glass. Cyan accent on deep navy with Cold Plasma Cascade wallpaper.",
        "palette": {
            "desktop_bg":         "#0A0A14",
            "surface_glass":      "#12122A",
            "topbar_bg":          "#0C0C20",
            "dock_bg":            "#10102A",
            "accent":             "#7DD3FC",
            "text_primary":       "#E0E0F0",
            "text_secondary":     "#8888AA",
            "focus_ring":         "#7CC5FF",
            "button_close":       "#F87171",
            "button_minimize":    "#FBBF24",
            "button_maximize":    "#4ADE80",
            "cursor_fill":        "#F5F5FF",
            "cursor_outline":     "#050510",
            "cursor_right_click": "#E54B4B",
            "titlebar_inactive":  "#181830",
            "button_restore":     "#4ADE80",
            "grid_line":          "#181832",
        },
    },
    {
        "pack": 1,
        "name": "Neon",
        "wallpaper_pack": "Neon",
        "description": "Black base + hot magenta + electric cyan + lime. 4 horizontal neon bands wallpaper.",
        "palette": {
            "desktop_bg":         "#050010",
            "surface_glass":      "#140522",
            "topbar_bg":          "#0A0018",
            "dock_bg":            "#100520",
            "accent":             "#FF2AD0",
            "text_primary":       "#F0FFF0",
            "text_secondary":     "#A870C8",
            "focus_ring":         "#2AFFE5",
            "button_close":       "#FF326E",
            "button_minimize":    "#F5E02A",
            "button_maximize":    "#6AFF50",
            "cursor_fill":        "#FFF0FF",
            "cursor_outline":     "#100018",
            "cursor_right_click": "#FF2AD0",
            "titlebar_inactive":  "#220A32",
            "button_restore":     "#6AFF50",
            "grid_line":          "#2A103A",
        },
    },
    {
        "pack": 2,
        "name": "Minimal",
        "wallpaper_pack": "Minimal",
        "description": "Warm stone gray + dusty amber accent (v2.6.0 distinctness rework — no longer cyan).",
        "palette": {
            "desktop_bg":         "#1A1816",
            "surface_glass":      "#262422",
            "topbar_bg":          "#1E1C1A",
            "dock_bg":            "#22201E",
            "accent":             "#D4C8B4",
            "text_primary":       "#F2EEE6",
            "text_secondary":     "#90887C",
            "focus_ring":         "#C8A86A",
            "button_close":       "#E06060",
            "button_minimize":    "#E0C040",
            "button_maximize":    "#40C870",
            "cursor_fill":        "#FAF6EE",
            "cursor_outline":     "#1A1816",
            "cursor_right_click": "#E06060",
            "titlebar_inactive":  "#2E2A26",
            "button_restore":     "#40C870",
            "grid_line":          "#2C2824",
        },
    },
    {
        "pack": 3,
        "name": "Retro",
        "wallpaper_pack": "Retro",
        "description": "Deep navy + amber + CRT green. Amber band + scanline wallpaper.",
        "palette": {
            "desktop_bg":         "#0A1420",
            "surface_glass":      "#141C2A",
            "topbar_bg":          "#101826",
            "dock_bg":            "#141C2C",
            "accent":             "#FFA83A",
            "text_primary":       "#6AFF82",
            "text_secondary":     "#4AB060",
            "focus_ring":         "#FFC860",
            "button_close":       "#E85A4C",
            "button_minimize":    "#FFA83A",
            "button_maximize":    "#6AFF82",
            "cursor_fill":        "#6AFF82",
            "cursor_outline":     "#0A1420",
            "cursor_right_click": "#E85A4C",
            "titlebar_inactive":  "#1A2436",
            "button_restore":     "#6AFF82",
            "grid_line":          "#222E42",
        },
    },
    {
        "pack": 4,
        "name": "Cards",
        "wallpaper_pack": "Cards",
        "description": "Warm amber on blue slate (v2.6.0 distinctness rework — no longer cyan). Layered cards wallpaper.",
        "palette": {
            "desktop_bg":         "#1C2030",
            "surface_glass":      "#262C40",
            "topbar_bg":          "#202436",
            "dock_bg":            "#24283A",
            "accent":             "#FF9A3C",
            "text_primary":       "#F0ECE4",
            "text_secondary":     "#B0A896",
            "focus_ring":         "#FF7AD0",
            "button_close":       "#FF7A82",
            "button_minimize":    "#FFCC4A",
            "button_maximize":    "#60E89C",
            "cursor_fill":        "#FFF5E6",
            "cursor_outline":     "#10141E",
            "cursor_right_click": "#FF7AD0",
            "titlebar_inactive":  "#2A3044",
            "button_restore":     "#60E89C",
            "grid_line":          "#2E3448",
        },
    },
    {
        "pack": 5,
        "name": "Pastel",
        "wallpaper_pack": "Pastel",
        "description": "Soft slate + powder pink + mint + lavender. Four pastel circles wallpaper.",
        "palette": {
            "desktop_bg":         "#1E1E28",
            "surface_glass":      "#2A2A38",
            "topbar_bg":          "#24222E",
            "dock_bg":            "#262432",
            "accent":             "#FBC6E4",
            "text_primary":       "#F4ECF4",
            "text_secondary":     "#B8A8C2",
            "focus_ring":         "#C9BBF0",
            "button_close":       "#F5A8AE",
            "button_minimize":    "#FAE3A4",
            "button_maximize":    "#B0E8C0",
            "cursor_fill":        "#FDF2F5",
            "cursor_outline":     "#1E1828",
            "cursor_right_click": "#F5A8AE",
            "titlebar_inactive":  "#322C3C",
            "button_restore":     "#B0E8C0",
            "grid_line":          "#363040",
        },
    },
    {
        "pack": 6,
        "name": "Dark",
        "wallpaper_pack": "Dark",
        "description": "Embers in a void — pure black + ember orange accent (v2.6.0 distinctness rework — no longer cyan).",
        "palette": {
            "desktop_bg":         "#000004",
            "surface_glass":      "#120A06",
            "topbar_bg":          "#080402",
            "dock_bg":            "#0C0604",
            "accent":             "#FF6040",
            "text_primary":       "#ECE6E0",
            "text_secondary":     "#807066",
            "focus_ring":         "#FF8050",
            "button_close":       "#E84040",
            "button_minimize":    "#E8A030",
            "button_maximize":    "#60C070",
            "cursor_fill":        "#FFE6D8",
            "cursor_outline":     "#000000",
            "cursor_right_click": "#E84040",
            "titlebar_inactive":  "#1A0E08",
            "button_restore":     "#60C070",
            "grid_line":          "#1A0E08",
        },
    },
    {
        "pack": 7,
        "name": "Gradient",
        "wallpaper_pack": "Gradient",
        "description": "Deep indigo + violet + cyan accent. 3-stop vertical gradient wallpaper.",
        "palette": {
            "desktop_bg":         "#100522",
            "surface_glass":      "#1A1232",
            "topbar_bg":          "#140828",
            "dock_bg":            "#180C30",
            "accent":             "#A070FF",
            "text_primary":       "#ECE8FA",
            "text_secondary":     "#9A88C0",
            "focus_ring":         "#7AE0FF",
            "button_close":       "#F86A9A",
            "button_minimize":    "#FFC470",
            "button_maximize":    "#70E0B8",
            "cursor_fill":        "#F5EFFF",
            "cursor_outline":     "#0A0518",
            "cursor_right_click": "#F86A9A",
            "titlebar_inactive":  "#221838",
            "button_restore":     "#70E0B8",
            "grid_line":          "#261C3C",
        },
    },
    {
        "pack": 8,
        "name": "Blueprint",
        "wallpaper_pack": "Blueprint",
        "description": "Deep blueprint blue + cyan accents + white technical text. Drafting-grid wallpaper.",
        "palette": {
            "desktop_bg":         "#051832",
            "surface_glass":      "#0A2240",
            "topbar_bg":          "#061C36",
            "dock_bg":            "#081F3A",
            "accent":             "#7AE0FF",
            "text_primary":       "#E0F0FF",
            "text_secondary":     "#70A8D0",
            "focus_ring":         "#A8E8FF",
            "button_close":       "#E87A7A",
            "button_minimize":    "#F0C86A",
            "button_maximize":    "#6AE8B0",
            "cursor_fill":        "#F0F8FF",
            "cursor_outline":     "#051224",
            "cursor_right_click": "#E87A7A",
            "titlebar_inactive":  "#0C2646",
            "button_restore":     "#6AE8B0",
            "grid_line":          "#123252",
        },
    },
    {
        "pack": 9,
        "name": "Pixel",
        "wallpaper_pack": "Pixel",
        "description": "NES limited palette + bold primaries on dark navy. 32×32 colorful checkerboard wallpaper.",
        "palette": {
            "desktop_bg":         "#000018",
            "surface_glass":      "#101030",
            "topbar_bg":          "#080820",
            "dock_bg":            "#0C0C28",
            "accent":             "#FFCC00",
            "text_primary":       "#F8F8F8",
            "text_secondary":     "#8080C0",
            "focus_ring":         "#00E800",
            "button_close":       "#E80000",
            "button_minimize":    "#FFCC00",
            "button_maximize":    "#00E800",
            "cursor_fill":        "#FFFFFF",
            "cursor_outline":     "#000000",
            "cursor_right_click": "#E80000",
            "titlebar_inactive":  "#181838",
            "button_restore":     "#00E800",
            "grid_line":          "#202040",
        },
    },
]


def parse_hex(h: str) -> tuple[int, int, int]:
    """Parse #RRGGBB → (r, g, b)."""
    h = h.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def make_1x1_png(accent_hex: str) -> bytes:
    """Build a minimal 1×1 RGB PNG of the accent color. ~67 bytes."""
    r, g, b = parse_hex(accent_hex)
    sig = b"\x89PNG\r\n\x1a\n"

    def chunk(tag: bytes, data: bytes) -> bytes:
        length = struct.pack(">I", len(data))
        crc = struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        return length + tag + data + crc

    # IHDR: width=1, height=1, bit depth=8, color type=2 (RGB), compression=0, filter=0, interlace=0
    ihdr_data = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    ihdr = chunk(b"IHDR", ihdr_data)
    # IDAT: zlib-compressed filtered scanlines. 1 row of 3 RGB bytes preceded
    # by filter byte 0 (none). Use zlib.compress with level 0 (stored block).
    raw = bytes([0, r, g, b])
    idat = chunk(b"IDAT", zlib.compress(raw, 0))
    iend = chunk(b"IEND", b"")
    return sig + ihdr + idat + iend


def slugify(name: str) -> str:
    """Convert 'Q OS' → 'q-os', 'Neon' → 'neon', etc."""
    out = []
    for c in name.lower():
        if c.isalnum():
            out.append(c)
        elif c in " _":
            out.append("-")
    s = "".join(out)
    # collapse double dashes
    while "--" in s:
        s = s.replace("--", "-")
    return s.strip("-")


def make_ultheme(theme: dict, out_dir: str) -> str:
    """Build one .ultheme zip. Returns the output path."""
    pack = theme["pack"]
    name = theme["name"]
    wallpaper_pack = theme["wallpaper_pack"]
    description = theme["description"]
    palette = theme["palette"]

    manifest = {
        "format_version": 3,
        "name": f"Q OS — {name}",
        "release": RELEASE,
        "description": description,
        "author": "Q OS",
    }
    # QdPalette.json with wallpaper_pack as a Q OS extension to the schema.
    # qd_Theme.cpp::LoadThemeFromCache reads this field and sets
    # g_active_wallpaper_pack so the procedural wallpaper matches the theme.
    qd_palette = dict(palette)
    qd_palette["wallpaper_pack"] = wallpaper_pack

    icon_png = make_1x1_png(palette["accent"])
    manifest_json = json.dumps(manifest, indent=2).encode("utf-8")
    palette_json = json.dumps(qd_palette, indent=2).encode("utf-8")

    # Filename: q-os-{pack}-{slug}.ultheme so they sort by pack index in the
    # FindThemes scan (alphabetical by filename → menu order matches pack order).
    slug = slugify(name)
    fname = f"q-os-{pack}-{slug}.ultheme"
    out_path = os.path.join(out_dir, fname)

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.writestr("theme/Manifest.json", manifest_json)
        z.writestr("theme/Icon.png", icon_png)
        z.writestr("ui/QdPalette.json", palette_json)

    return out_path


def main() -> int:
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Remove stale q-os-*.ultheme so the output is exactly the THEMES list.
    for f in os.listdir(OUTPUT_DIR):
        if f.startswith("q-os-") and f.endswith(".ultheme"):
            os.unlink(os.path.join(OUTPUT_DIR, f))

    print(f"Writing 10 Q OS .ultheme bundles → {OUTPUT_DIR}")
    for theme in THEMES:
        out = make_ultheme(theme, OUTPUT_DIR)
        size = os.path.getsize(out)
        print(f"  pack {theme['pack']}: {os.path.basename(out)} ({size} B)")

    print(f"\nDone. {len(THEMES)} bundles, ~{sum(os.path.getsize(os.path.join(OUTPUT_DIR, f)) for f in os.listdir(OUTPUT_DIR) if f.startswith('q-os-') and f.endswith('.ultheme'))} B total.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
