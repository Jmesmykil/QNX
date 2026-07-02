#!/usr/bin/env python3
# generate-qos-theme-icons.py — per-theme stylized glyph generator (v3).
#
# CREATOR DIRECTIVES (2026-05-18 — final scope):
#   1. NO emblem frames.  Just glyphs on transparent backgrounds.  The runtime
#      already paints a themed squircle behind each icon (qd_DesktopIcons.cpp
#      round_bg_tex_) — these PNGs are glyph-only.
#   2. Glyphs themselves differ per THEME, not just per role.  A vault in Glass
#      is smooth-filled cyan; a vault in Pixel is 16×16 chunky pixel-art; a
#      vault in Neon is outlined magenta with glow; a vault in Minimal is a
#      thin line drawing.  Each theme has a SIGNATURE RENDER STYLE.
#   3. Per-category glyphs for the desktop folder tiles — Games = controller,
#      Emulators = arcade joystick, Tools = wrench, System = gear, QOS = Q,
#      Other = question mark.
#   4. Hot-corner top-left Q glyph also gets per-theme treatment.
#
# Per-theme RENDER STYLES:
#   Glass     — FILL_SMOOTH        solid antialiased fill in accent color
#   Neon      — OUTLINE_GLOW       outlined-only with magenta glow
#   Minimal   — LINE_THIN          thin dark line, no fill
#   Retro     — PIXEL_LARGE        chunky 24×24 pixel-art (8 px effective pixels)
#   Cards     — FILL_SMOOTH        clean filled silhouette
#   Pastel    — FILL_SOFT          soft filled, slight blur for plushness
#   Dark      — FILL_EDGE          filled body + ember-orange edge highlight
#   Gradient  — FILL_GRADIENT      vertical gradient fill (surface_glass → accent)
#   Blueprint — LINE_TECH          thin white technical-drawing strokes
#   Pixel     — PIXEL_FINE         strict 16×16 pixel art (12 px effective pixels)
#
# Glyph roles (17 total per theme = 170 PNGs):
#   Launchpad (10):
#     DockVault DockMonitor DockAbout DockAllPrograms DockControl DockTasks
#     Folder DefaultApplication DefaultHomebrew Empty
#   Desktop folder categories (6):
#     FolderGames FolderEmulators FolderTools FolderSystem FolderQOS FolderOther
#   Hot corner (1):
#     HotCornerQ
#
# Output:
#   Glass theme  -> romfs/default/ui/Main/EntryIcon/<Role>.png
#   Themes 1-9   -> q-os-N-<name>.ultheme zip @ ui/Main/EntryIcon/<Role>.png
#                   Picked up automatically by TryGetActiveThemeResource at
#                   runtime (ui_Common.cpp:83-95).  Zero new C++ needed for
#                   the entry-icon path — only the per-category folder lookup
#                   in PaintDesktopFolders and the per-theme HotCornerQ lookup
#                   in qd_HotCornerOverlay need code changes (separate diffs).

import json
import math
import os
import sys
import zipfile
from io import BytesIO

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageOps
except ImportError:
    print("FATAL: Pillow not installed. Run: pip3 install Pillow", file=sys.stderr)
    sys.exit(1)

# ── Paths ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
REPO_SRC    = os.path.dirname(SCRIPT_DIR)
THEMES_DIR  = os.path.join(REPO_SRC, "src", "projects", "uMenu", "romfs", "themes")
# IMPORTANT: the `umenu` Makefile target wipes projects/uMenu/romfs/default and
# repopulates it from src/default-theme/ at build time:
#     @rm -rf projects/uMenu/romfs/default
#     @cp -r default-theme/ projects/uMenu/romfs/default/
# So Glass (default) theme PNGs MUST land in src/default-theme/ui/Main/EntryIcon
# to survive into the packed romfs.bin.  Writing into projects/uMenu/romfs/default
# directly is destroyed by the build.  Discovered on the v2.9.5 HW-test pass —
# folder tiles still rendered as plain Folder.png because the romfs/default
# wipe-and-recopy nuked the freshly generated per-category PNGs before
# build_romfs ran.
DEFAULT_DIR = os.path.join(REPO_SRC, "src", "default-theme", "ui", "Main", "EntryIcon")

W = 192
H = 192

# ── Themes ────────────────────────────────────────────────────────────────────

THEMES = [
    ("q-os-0-q-os",     "Glass"),
    ("q-os-1-neon",     "Neon"),
    ("q-os-2-minimal",  "Minimal"),
    ("q-os-3-retro",    "Retro"),
    ("q-os-4-cards",    "Cards"),
    ("q-os-5-pastel",   "Pastel"),
    ("q-os-6-dark",     "Dark"),
    ("q-os-7-gradient", "Gradient"),
    ("q-os-8-blueprint","Blueprint"),
    ("q-os-9-pixel",    "Pixel"),
]

# ── Roles ─────────────────────────────────────────────────────────────────────

LAUNCHPAD_ROLES = [
    "DockVault",
    "DockMonitor",
    "DockAbout",
    "DockAllPrograms",
    "DockControl",
    "DockTasks",
    "Folder",
    "DefaultApplication",
    "DefaultHomebrew",
    "Empty",
]

FOLDER_CATEGORY_ROLES = [
    "FolderGames",
    "FolderEmulators",
    "FolderTools",
    "FolderSystem",
    "FolderQOS",
    "FolderOther",
]

HOT_CORNER_ROLES = [
    "HotCornerQ",
]

ALL_ROLES = LAUNCHPAD_ROLES + FOLDER_CATEGORY_ROLES + HOT_CORNER_ROLES

# ── Color helpers ─────────────────────────────────────────────────────────────

def hex_to_rgb(h):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))

def rgba(rgb, a):
    return (rgb[0], rgb[1], rgb[2], a)

def mix(c1, c2, t):
    return tuple(int(c1[i] * (1 - t) + c2[i] * t) for i in range(3))

def lighten(c, t):
    return mix(c, (255,255,255), t)

def darken(c, t):
    return mix(c, (0,0,0), t)

def load_palette(theme_filename_part):
    ultheme = os.path.join(THEMES_DIR, theme_filename_part + ".ultheme")
    with zipfile.ZipFile(ultheme, "r") as z:
        with z.open("ui/QdPalette.json") as f:
            return json.load(f)

# ═════════════════════════════════════════════════════════════════════════════
# GLYPH SHAPES — each glyph is a function that takes (image, cx, cy, size,
# stroke, fill).  Stroke/fill control fill_mode: FILL=solid filled, OUTLINE=
# stroke-only.  Glyphs draw to a fresh RGBA image masked region.
#
# These functions are CANONICAL shape definitions.  Per-theme rendering wraps
# these with style-specific post-processing (pixelation, glow, gradient fill).
# ═════════════════════════════════════════════════════════════════════════════

# Mode parameter:
#   "FILL"     — solid filled shape (use fill_color, ignore stroke)
#   "OUTLINE"  — outlined only (use fill_color as stroke, ignore fill)
# Each glyph function obeys this.

# ── Launchpad glyphs (10) ─────────────────────────────────────────────────────

def glyph_vault(d, cx, cy, size, color, mode, sw):
    body_w = int(size * 0.85)
    body_h = int(size * 0.55)
    body_x = cx - body_w // 2
    body_y = cy - body_h // 2 + size // 12
    if mode == "FILL":
        d.rounded_rectangle((body_x, body_y, body_x + body_w, body_y + body_h),
                            radius=size // 16, fill=color)
    else:
        d.rounded_rectangle((body_x, body_y, body_x + body_w, body_y + body_h),
                            radius=size // 16, outline=color, width=sw)
    shackle_w = int(body_w * 0.65)
    shackle_x = cx - shackle_w // 2
    shackle_top = body_y - size // 4
    d.arc((shackle_x, shackle_top, shackle_x + shackle_w, shackle_top + size // 2),
          start=180, end=360,
          fill=color, width=max(sw, size // 16) if mode == "OUTLINE" else size // 12)

def glyph_monitor(d, cx, cy, size, color, mode, sw):
    sw_h = int(size * 0.6)
    sw_x = cx - size // 2
    sw_y = cy - sw_h // 2 - size // 16
    if mode == "FILL":
        d.rounded_rectangle((sw_x, sw_y, sw_x + size, sw_y + sw_h),
                            radius=size // 24, fill=color)
        # Cutout to make it look like a screen edge.
        inner = int(size * 0.06)
        # Don't actually cutout — keep solid for simpler render.
    else:
        d.rounded_rectangle((sw_x, sw_y, sw_x + size, sw_y + sw_h),
                            radius=size // 24, outline=color, width=sw)
    # Stand.
    stand_w = int(size * 0.40)
    stand_x = cx - stand_w // 2
    stand_y = sw_y + sw_h + 2
    if mode == "FILL":
        d.rectangle((stand_x, stand_y, stand_x + stand_w, stand_y + size // 16), fill=color)
        # Base.
        base_w = int(size * 0.65)
        d.rounded_rectangle((cx - base_w // 2, stand_y + size // 16,
                             cx + base_w // 2, stand_y + size // 16 + size // 24),
                            radius=size // 60, fill=color)
    else:
        d.rectangle((stand_x, stand_y, stand_x + stand_w, stand_y + size // 16),
                    outline=color, width=sw)
        base_w = int(size * 0.65)
        d.rounded_rectangle((cx - base_w // 2, stand_y + size // 16,
                             cx + base_w // 2, stand_y + size // 16 + size // 24),
                            radius=size // 60, outline=color, width=sw)

def glyph_about(d, cx, cy, size, color, mode, sw):
    r = size // 2
    d.ellipse((cx-r, cy-r, cx+r, cy+r),
              fill=color if mode == "FILL" else None,
              outline=color, width=sw)
    # Dot
    dot_r = int(size * 0.08)
    dot_color = (0,0,0,255) if mode == "FILL" else color
    if mode == "FILL":
        # Punch the 'i' as the bg surface color (transparent).
        d.ellipse((cx-dot_r, cy-int(size*0.28)-dot_r,
                   cx+dot_r, cy-int(size*0.28)+dot_r), fill=(0,0,0,0))
        bar_w = int(size * 0.10)
        bar_h = int(size * 0.40)
        d.rounded_rectangle((cx-bar_w//2, cy-int(size*0.08),
                              cx+bar_w//2, cy-int(size*0.08)+bar_h),
                            radius=bar_w//2, fill=(0,0,0,0))
    else:
        d.ellipse((cx-dot_r, cy-int(size*0.28)-dot_r,
                   cx+dot_r, cy-int(size*0.28)+dot_r), fill=color)
        bar_w = int(size * 0.10)
        bar_h = int(size * 0.40)
        d.rounded_rectangle((cx-bar_w//2, cy-int(size*0.08),
                              cx+bar_w//2, cy-int(size*0.08)+bar_h),
                            radius=bar_w//2, fill=color)

def glyph_all_programs(d, cx, cy, size, color, mode, sw):
    cell = size // 6
    gap = size // 18
    total = 3 * cell + 2 * gap
    x0 = cx - total // 2
    y0 = cy - total // 2
    for r_ in range(3):
        for c_ in range(3):
            cx0 = x0 + c_ * (cell + gap)
            cy0 = y0 + r_ * (cell + gap)
            if mode == "FILL":
                d.rounded_rectangle((cx0, cy0, cx0 + cell, cy0 + cell),
                                    radius=cell//4, fill=color)
            else:
                d.rounded_rectangle((cx0, cy0, cx0 + cell, cy0 + cell),
                                    radius=cell//4, outline=color, width=max(2, sw))

def glyph_control(d, cx, cy, size, color, mode, sw):
    bar_w = size
    bar_t = max(4, size // 18)
    gap = bar_t + size // 12
    start_y = cy - gap
    for i in range(3):
        by = start_y + i * gap
        rect = (cx - bar_w // 2, by - bar_t // 2, cx + bar_w // 2, by + bar_t // 2)
        if mode == "FILL":
            d.rounded_rectangle(rect, radius=bar_t // 2, fill=color)
        else:
            d.rounded_rectangle(rect, radius=bar_t // 2, outline=color, width=sw)
        knob_r = bar_t + 3
        knob_offset = (((i * 2) - 1) * (bar_w // 4))
        kx = cx + knob_offset
        if mode == "FILL":
            d.ellipse((kx - knob_r, by - knob_r, kx + knob_r, by + knob_r), fill=color)
        else:
            d.ellipse((kx - knob_r, by - knob_r, kx + knob_r, by + knob_r),
                      outline=color, width=sw)

def glyph_tasks(d, cx, cy, size, color, mode, sw):
    row_h = size // 8
    gap = size // 16
    rows = 4
    total_h = rows * row_h + (rows - 1) * gap
    x0 = cx - size // 2
    y0 = cy - total_h // 2
    for i in range(rows):
        ry = y0 + i * (row_h + gap)
        # Checkbox.
        d.rounded_rectangle((x0, ry, x0 + row_h, ry + row_h),
                            radius=2, fill=None, outline=color, width=max(2, sw))
        if i < rows - 1:
            # Checkmark.
            d.line((x0 + 3, ry + row_h // 2,
                    x0 + row_h // 2, ry + row_h - 3),
                   fill=color, width=max(2, sw))
            d.line((x0 + row_h // 2, ry + row_h - 3,
                    x0 + row_h + 1, ry + 2),
                   fill=color, width=max(2, sw))
        # Task line bar.
        bar_x = x0 + row_h + 6
        bar_rect = (bar_x, ry + 2, x0 + size, ry + row_h - 2)
        if mode == "FILL":
            d.rounded_rectangle(bar_rect, radius=1, fill=color)
        else:
            d.rounded_rectangle(bar_rect, radius=1, outline=color, width=max(2, sw))

def glyph_folder(d, cx, cy, size, color, mode, sw):
    w = size
    h = int(size * 0.78)
    x0 = cx - w // 2
    y0 = cy - h // 2 + 2
    # Tab.
    tab_w = int(w * 0.42)
    tab_h = int(h * 0.18)
    if mode == "FILL":
        d.rounded_rectangle((x0, y0, x0 + tab_w, y0 + tab_h),
                            radius=tab_h // 3, fill=color)
        d.rounded_rectangle((x0, y0 + tab_h - 2, x0 + w, y0 + h),
                            radius=tab_h // 3, fill=color)
    else:
        d.rounded_rectangle((x0, y0, x0 + tab_w, y0 + tab_h),
                            radius=tab_h // 3, outline=color, width=sw)
        d.rounded_rectangle((x0, y0 + tab_h - 2, x0 + w, y0 + h),
                            radius=tab_h // 3, outline=color, width=sw)

def glyph_default_app(d, cx, cy, size, color, mode, sw):
    s = size
    d.rounded_rectangle((cx - s//2, cy - s//2, cx + s//2, cy + s//2),
                        radius=size // 12, fill=color if mode == "FILL" else None,
                        outline=color, width=sw)
    inner_s = int(s * 0.55)
    if mode == "FILL":
        # Punch inner square as transparent.
        d.rounded_rectangle((cx - inner_s//2, cy - inner_s//2,
                              cx + inner_s//2, cy + inner_s//2),
                            radius=size // 18, fill=(0,0,0,0))
    else:
        d.rounded_rectangle((cx - inner_s//2, cy - inner_s//2,
                              cx + inner_s//2, cy + inner_s//2),
                            radius=size // 18, outline=color, width=sw)

def glyph_default_homebrew(d, cx, cy, size, color, mode, sw):
    s = size
    pts = [(cx, cy - s//2), (cx + s//2, cy), (cx, cy + s//2), (cx - s//2, cy)]
    if mode == "FILL":
        d.polygon(pts, fill=color)
        # Punch inner diamond.
        inner_pts = [(cx, cy - s//4), (cx + s//4, cy), (cx, cy + s//4), (cx - s//4, cy)]
        d.polygon(inner_pts, fill=(0,0,0,0))
    else:
        d.polygon(pts, outline=color)
        # Pillow polygon outline is 1px — stroke manually.
        for i in range(len(pts)):
            d.line([pts[i], pts[(i+1) % len(pts)]], fill=color, width=sw)

def glyph_empty(d, cx, cy, size, color, mode, sw):
    r = size // 2
    # Dashed circle.
    n_dashes = 16
    for i in range(0, n_dashes, 2):
        start = i * 360 / n_dashes
        end = (i + 1) * 360 / n_dashes
        d.arc((cx-r, cy-r, cx+r, cy+r), start=start, end=end,
              fill=color, width=max(2, sw))
    plus_w = int(size * 0.35)
    plus_t = max(3, size // 36)
    d.rounded_rectangle((cx - plus_w//2, cy - plus_t//2,
                          cx + plus_w//2, cy + plus_t//2),
                        radius=plus_t//2, fill=color)
    d.rounded_rectangle((cx - plus_t//2, cy - plus_w//2,
                          cx + plus_t//2, cy + plus_w//2),
                        radius=plus_t//2, fill=color)

# ── Desktop folder category glyphs (6) ────────────────────────────────────────

def glyph_games_controller(d, cx, cy, size, color, mode, sw):
    """Stylized game controller — central body + two grip handles."""
    body_w = int(size * 0.95)
    body_h = int(size * 0.55)
    body_x = cx - body_w // 2
    body_y = cy - body_h // 2 + size // 24
    # Main body — pill shape (rounded rect with full-radius ends).
    if mode == "FILL":
        d.rounded_rectangle((body_x, body_y, body_x + body_w, body_y + body_h),
                            radius=body_h // 2, fill=color)
    else:
        d.rounded_rectangle((body_x, body_y, body_x + body_w, body_y + body_h),
                            radius=body_h // 2, outline=color, width=sw)
    # D-pad on left (plus sign).
    dpad_cx = body_x + body_w // 4
    dpad_cy = cy + size // 32
    dpad_arm = size // 12
    dpad_t = size // 30
    cutout_color = (0,0,0,0) if mode == "FILL" else color
    # Horizontal arm.
    d.rectangle((dpad_cx - dpad_arm, dpad_cy - dpad_t // 2,
                 dpad_cx + dpad_arm, dpad_cy + dpad_t // 2),
                fill=cutout_color)
    # Vertical arm.
    d.rectangle((dpad_cx - dpad_t // 2, dpad_cy - dpad_arm,
                 dpad_cx + dpad_t // 2, dpad_cy + dpad_arm),
                fill=cutout_color)
    # ABXY-style 4-button cluster on right (4 small circles).
    btn_cx = body_x + (body_w * 3) // 4
    btn_cy = cy + size // 32
    btn_r = size // 22
    btn_gap = size // 13
    btn_color = (0,0,0,0) if mode == "FILL" else color
    for (dx, dy) in [(0,-btn_gap),(btn_gap,0),(0,btn_gap),(-btn_gap,0)]:
        d.ellipse((btn_cx + dx - btn_r, btn_cy + dy - btn_r,
                   btn_cx + dx + btn_r, btn_cy + dy + btn_r), fill=btn_color)

def glyph_emulators_joystick(d, cx, cy, size, color, mode, sw):
    """Arcade-style joystick — vertical stick with ball top, square base."""
    # Square base.
    base_w = int(size * 0.70)
    base_h = int(size * 0.18)
    base_x = cx - base_w // 2
    base_y = cy + size // 6
    if mode == "FILL":
        d.rounded_rectangle((base_x, base_y, base_x + base_w, base_y + base_h),
                            radius=size // 24, fill=color)
    else:
        d.rounded_rectangle((base_x, base_y, base_x + base_w, base_y + base_h),
                            radius=size // 24, outline=color, width=sw)
    # Shaft (vertical rectangle).
    shaft_w = size // 12
    shaft_h = int(size * 0.50)
    if mode == "FILL":
        d.rectangle((cx - shaft_w // 2, base_y - shaft_h,
                     cx + shaft_w // 2, base_y), fill=color)
    else:
        d.rectangle((cx - shaft_w // 2, base_y - shaft_h,
                     cx + shaft_w // 2, base_y), outline=color, width=sw)
    # Ball top.
    ball_r = size // 8
    ball_y = base_y - shaft_h - ball_r + size // 32
    if mode == "FILL":
        d.ellipse((cx - ball_r, ball_y - ball_r, cx + ball_r, ball_y + ball_r), fill=color)
    else:
        d.ellipse((cx - ball_r, ball_y - ball_r, cx + ball_r, ball_y + ball_r),
                  outline=color, width=sw)
    # 4 buttons on the base (right side).
    btn_r = size // 28
    btn_y = base_y + base_h // 2
    for i in range(3):
        bx = base_x + base_w - (i + 1) * (btn_r * 3 + 4)
        if mode == "FILL":
            d.ellipse((bx - btn_r, btn_y - btn_r, bx + btn_r, btn_y + btn_r),
                      fill=(0,0,0,0))
        else:
            d.ellipse((bx - btn_r, btn_y - btn_r, bx + btn_r, btn_y + btn_r),
                      outline=color, width=max(1, sw // 2))

def glyph_tools_wrench(d, cx, cy, size, color, mode, sw):
    """Wrench — angled rectangle handle with open hex head."""
    # Handle (long rectangle, rotated 45 degrees conceptually).  Simplify: use
    # two rectangles forming an L-like shape for stylized wrench feel.
    # Diagonal handle from top-right to bottom-left.
    handle_w = size // 10
    handle_l = int(size * 0.70)
    # Draw as rotated rect via polygon.
    angle = math.radians(-45)
    cosA, sinA = math.cos(angle), math.sin(angle)
    # Handle endpoints.
    end1 = (cx + (handle_l/2) * cosA, cy + (handle_l/2) * sinA)
    end2 = (cx - (handle_l/2) * cosA, cy - (handle_l/2) * sinA)
    # Perpendicular offset for width.
    perpA = angle + math.pi / 2
    pw = handle_w / 2
    pts = [
        (end1[0] + pw * math.cos(perpA), end1[1] + pw * math.sin(perpA)),
        (end1[0] - pw * math.cos(perpA), end1[1] - pw * math.sin(perpA)),
        (end2[0] - pw * math.cos(perpA), end2[1] - pw * math.sin(perpA)),
        (end2[0] + pw * math.cos(perpA), end2[1] + pw * math.sin(perpA)),
    ]
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i+1) % len(pts)]], fill=color, width=sw)
    # Hex head at top-right end.
    head_r = size // 6
    hx, hy = end1
    hex_pts = []
    for i in range(6):
        a = math.radians(60 * i)
        hex_pts.append((hx + head_r * math.cos(a), hy + head_r * math.sin(a)))
    if mode == "FILL":
        d.polygon(hex_pts, fill=color)
        # Punch hole.
        inner_pts = []
        for i in range(6):
            a = math.radians(60 * i)
            inner_pts.append((hx + (head_r * 0.55) * math.cos(a),
                               hy + (head_r * 0.55) * math.sin(a)))
        d.polygon(inner_pts, fill=(0,0,0,0))
    else:
        for i in range(6):
            d.line([hex_pts[i], hex_pts[(i+1) % 6]], fill=color, width=sw)

def glyph_system_gear(d, cx, cy, size, color, mode, sw):
    """Gear/cog wheel — circle with 8 rectangular teeth + centre hole."""
    outer_r = size // 2 - 4
    inner_r = int(outer_r * 0.78)
    tooth_w = size // 14
    tooth_extend = size // 12
    n_teeth = 8
    if mode == "FILL":
        # Draw teeth as rotated rectangles.
        for i in range(n_teeth):
            a = math.radians(360 / n_teeth * i)
            tx = cx + (inner_r + tooth_extend / 2) * math.cos(a)
            ty = cy + (inner_r + tooth_extend / 2) * math.sin(a)
            # Build small rotated rect.
            perpA = a + math.pi / 2
            pw = tooth_w / 2
            pl = tooth_extend / 2 + 4
            pts = [
                (tx + pl * math.cos(a) + pw * math.cos(perpA),
                 ty + pl * math.sin(a) + pw * math.sin(perpA)),
                (tx + pl * math.cos(a) - pw * math.cos(perpA),
                 ty + pl * math.sin(a) - pw * math.sin(perpA)),
                (tx - pl * math.cos(a) - pw * math.cos(perpA),
                 ty - pl * math.sin(a) - pw * math.sin(perpA)),
                (tx - pl * math.cos(a) + pw * math.cos(perpA),
                 ty - pl * math.sin(a) + pw * math.sin(perpA)),
            ]
            d.polygon(pts, fill=color)
        # Outer ring (filled circle).
        d.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r), fill=color)
        # Centre hole.
        hole_r = inner_r // 3
        d.ellipse((cx - hole_r, cy - hole_r, cx + hole_r, cy + hole_r), fill=(0,0,0,0))
    else:
        # Outlined version — ring with teeth notches.
        for i in range(n_teeth):
            a = math.radians(360 / n_teeth * i)
            tx = cx + (inner_r + tooth_extend / 2) * math.cos(a)
            ty = cy + (inner_r + tooth_extend / 2) * math.sin(a)
            perpA = a + math.pi / 2
            pw = tooth_w / 2
            pl = tooth_extend / 2 + 4
            pts = [
                (tx + pl * math.cos(a) + pw * math.cos(perpA),
                 ty + pl * math.sin(a) + pw * math.sin(perpA)),
                (tx + pl * math.cos(a) - pw * math.cos(perpA),
                 ty + pl * math.sin(a) - pw * math.sin(perpA)),
                (tx - pl * math.cos(a) - pw * math.cos(perpA),
                 ty - pl * math.sin(a) - pw * math.sin(perpA)),
                (tx - pl * math.cos(a) + pw * math.cos(perpA),
                 ty - pl * math.sin(a) + pw * math.sin(perpA)),
            ]
            for j in range(4):
                d.line([pts[j], pts[(j+1) % 4]], fill=color, width=sw)
        d.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r),
                  outline=color, width=sw)
        hole_r = inner_r // 3
        d.ellipse((cx - hole_r, cy - hole_r, cx + hole_r, cy + hole_r),
                  outline=color, width=sw)

def glyph_qos_q(d, cx, cy, size, color, mode, sw):
    """Stylized 'Q' — circle outline + diagonal tail bottom-right."""
    r = size // 2 - 4
    if mode == "FILL":
        # Big circle outline, thick stroke.
        thickness = size // 9
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)
        d.ellipse((cx - r + thickness, cy - r + thickness,
                   cx + r - thickness, cy + r - thickness), fill=(0,0,0,0))
    else:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=sw)
    # Tail — diagonal line bottom-right.
    tail_start_x = cx + int(r * 0.45)
    tail_start_y = cy + int(r * 0.45)
    tail_end_x = cx + int(r * 0.95)
    tail_end_y = cy + int(r * 0.95)
    tail_t = max(sw, size // 10) if mode == "OUTLINE" else size // 9
    d.line((tail_start_x, tail_start_y, tail_end_x, tail_end_y),
           fill=color, width=tail_t)

def glyph_other_question(d, cx, cy, size, color, mode, sw):
    """Question mark — top hook + lower dot."""
    # Use the question mark via shape primitives.
    r = size // 4
    # Top arc — half-circle opening to the bottom-left.
    arc_top = cy - size // 3
    arc_size = size // 2
    if mode == "FILL":
        # Filled top hook.
        d.pieslice((cx - arc_size // 2, arc_top - arc_size // 2,
                    cx + arc_size // 2, arc_top + arc_size // 2),
                   start=140, end=400, fill=color)
        # Punch inside.
        d.pieslice((cx - arc_size // 2 + r // 2, arc_top - arc_size // 2 + r // 2,
                    cx + arc_size // 2 - r // 2, arc_top + arc_size // 2 - r // 2),
                   start=140, end=400, fill=(0,0,0,0))
        # Vertical bar from arc bottom to centre.
        bar_w = size // 9
        d.rectangle((cx - bar_w // 2, arc_top + arc_size // 4,
                     cx + bar_w // 2, cy + size // 6), fill=color)
        # Dot at bottom.
        dot_r = size // 11
        dot_y = cy + size // 3
        d.ellipse((cx - dot_r, dot_y - dot_r, cx + dot_r, dot_y + dot_r), fill=color)
    else:
        # Outlined version.
        d.arc((cx - arc_size // 2, arc_top - arc_size // 2,
               cx + arc_size // 2, arc_top + arc_size // 2),
              start=140, end=400, fill=color, width=sw)
        bar_w = size // 9
        d.rectangle((cx - bar_w // 2, arc_top + arc_size // 4,
                     cx + bar_w // 2, cy + size // 6), outline=color, width=sw)
        dot_r = size // 11
        dot_y = cy + size // 3
        d.ellipse((cx - dot_r, dot_y - dot_r, cx + dot_r, dot_y + dot_r),
                  outline=color, width=sw)

# ── Per-theme hot-corner emblems (9 + Glass) ──────────────────────────────────
#
# CREATOR DIRECTIVE 2026-05-18 evening:
#   "Q should only be for liquid glass Q OS Theme default. The rest of the
#    themes will have their own custom glyph not Q related."
#
# Glass (Q OS Liquid Glass) keeps the Q because it IS the Q OS brand mark.
# Each non-default theme gets its own distinctive identity emblem.

def glyph_lightning_bolt(d, cx, cy, size, color, mode, sw):
    """Lightning bolt — Neon theme emblem."""
    s = size
    # Classic Z-bolt: top point, kick left, kick right, bottom point.
    pts = [
        (cx + s // 8,  cy - s // 2),     # top apex
        (cx - s // 4,  cy - s // 16),    # left inflection
        (cx + s // 16, cy + s // 12),    # spine middle right
        (cx - s // 8,  cy + s // 2),     # bottom apex
        (cx + s // 4,  cy + s // 16),    # right inflection back
        (cx - s // 16, cy - s // 12),    # spine middle left
    ]
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)

def glyph_minimal_dots(d, cx, cy, size, color, mode, sw):
    """Three vertical dots — Minimal theme emblem (intentionally restrained)."""
    r = max(4, size // 11)
    gap = int(size * 0.32)
    for dy in (-gap, 0, gap):
        if mode == "FILL":
            d.ellipse((cx - r, cy + dy - r, cx + r, cy + dy + r), fill=color)
        else:
            d.ellipse((cx - r, cy + dy - r, cx + r, cy + dy + r),
                      outline=color, width=sw)

def glyph_retro_wedge(d, cx, cy, size, color, mode, sw):
    """Pac-Man-style mouth-open wedge — Retro theme emblem."""
    r = size // 2
    if mode == "FILL":
        d.pieslice((cx - r, cy - r, cx + r, cy + r),
                   start=30, end=330, fill=color)
    else:
        # Stroked: arc + two mouth lines.
        d.arc((cx - r, cy - r, cx + r, cy + r),
              start=30, end=330, fill=color, width=sw)
        a1 = math.radians(30)
        a2 = math.radians(330)
        x1 = cx + int(r * math.cos(a1))
        y1 = cy + int(r * math.sin(a1))
        x2 = cx + int(r * math.cos(a2))
        y2 = cy + int(r * math.sin(a2))
        d.line([(cx, cy), (x1, y1)], fill=color, width=sw)
        d.line([(cx, cy), (x2, y2)], fill=color, width=sw)

def glyph_card_spade(d, cx, cy, size, color, mode, sw):
    """Playing card spade — Cards theme emblem."""
    s = size
    # Body: pointed top + two rounded shoulders + tucked waist.
    body_pts = [
        (cx,             cy - s // 2),       # top apex
        (cx + s // 3,    cy + s // 18),      # right shoulder
        (cx + s // 7,    cy + s // 4),       # right curl
        (cx,             cy + s // 7),       # waist crook
        (cx - s // 7,    cy + s // 4),       # left curl
        (cx - s // 3,    cy + s // 18),      # left shoulder
    ]
    if mode == "FILL":
        d.polygon(body_pts, fill=color)
    else:
        for i in range(len(body_pts)):
            d.line([body_pts[i], body_pts[(i + 1) % len(body_pts)]],
                   fill=color, width=sw)
    # Stem: trapezoid base.
    stem_pts = [
        (cx - s // 8, cy + s // 7),
        (cx + s // 8, cy + s // 7),
        (cx + s // 5, cy + s // 2),
        (cx - s // 5, cy + s // 2),
    ]
    if mode == "FILL":
        d.polygon(stem_pts, fill=color)
    else:
        for i in range(len(stem_pts)):
            d.line([stem_pts[i], stem_pts[(i + 1) % len(stem_pts)]],
                   fill=color, width=sw)

def glyph_heart(d, cx, cy, size, color, mode, sw):
    """Heart — Pastel theme emblem."""
    s = size
    # Two upper lobes + downward point.
    r = s // 4
    lobe_y = cy - s // 8
    lobe_lx = cx - r // 2 - r // 8
    lobe_rx = cx + r // 2 + r // 8
    if mode == "FILL":
        d.ellipse((lobe_lx - r, lobe_y - r, lobe_lx + r, lobe_y + r), fill=color)
        d.ellipse((lobe_rx - r, lobe_y - r, lobe_rx + r, lobe_y + r), fill=color)
        # Lower triangle joining the lobes to the bottom point.
        pts = [
            (lobe_lx - r + 2, lobe_y),
            (lobe_rx + r - 2, lobe_y),
            (cx, cy + s // 2),
        ]
        d.polygon(pts, fill=color)
    else:
        d.ellipse((lobe_lx - r, lobe_y - r, lobe_lx + r, lobe_y + r),
                  outline=color, width=sw)
        d.ellipse((lobe_rx - r, lobe_y - r, lobe_rx + r, lobe_y + r),
                  outline=color, width=sw)
        # Outline triangle — left side + right side.
        d.line([(lobe_lx - r + 2, lobe_y), (cx, cy + s // 2)],
               fill=color, width=sw)
        d.line([(lobe_rx + r - 2, lobe_y), (cx, cy + s // 2)],
               fill=color, width=sw)

def glyph_flame(d, cx, cy, size, color, mode, sw):
    """Teardrop flame — Dark theme emblem (ember-orange theme accent)."""
    s = size
    # Parametric tapered teardrop: wide at bottom, pointed top.
    n_pts = 24
    pts = []
    for i in range(n_pts):
        t = i / n_pts
        a = 2 * math.pi * t
        # Top of the shape (t near 0.75 in trig convention) tapers narrow;
        # bottom (t near 0.25) is full width.  This produces a teardrop.
        # cos(a)=-1 at a=π (left), 1 at a=0 (right); sin(a)=-1 at a=3π/2 (top).
        # Width modulated by sin(a): narrow at top (sin=-1), wide at bottom (sin=+1).
        taper = 0.4 + 0.6 * ((math.sin(a) + 1) / 2)
        r_x = (s / 2.6) * taper
        r_y = s / 2
        pts.append((cx + r_x * math.cos(a), cy + r_y * math.sin(a)))
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)

def glyph_prism(d, cx, cy, size, color, mode, sw):
    """Triangle prism with refracted beam — Gradient theme emblem."""
    s = size
    pts = [
        (cx,           cy - s // 2 + 4),
        (cx + s // 2,  cy + s // 3),
        (cx - s // 2,  cy + s // 3),
    ]
    if mode == "FILL":
        d.polygon(pts, fill=color)
        # Punch inner triangle so it reads as a prism outline rather than a
        # solid blob (helps the gradient style show through).
        inner = [
            (cx,                 cy - s // 4),
            (cx + s // 4,        cy + s // 6),
            (cx - s // 4,        cy + s // 6),
        ]
        d.polygon(inner, fill=(0, 0, 0, 0))
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)
    # Refracted beam exiting on the right (small horizontal line).
    beam_w = max(2, size // 26)
    d.line([(cx + s // 3, cy + s // 12), (cx + s // 2 + s // 12, cy)],
           fill=color, width=beam_w)

def glyph_compass(d, cx, cy, size, color, mode, sw):
    """Drafting compass — Blueprint theme emblem."""
    s = size
    knob_r = max(4, s // 14)
    knob_y = cy - s // 3
    # Top hinge knob.
    if mode == "FILL":
        d.ellipse((cx - knob_r, knob_y - knob_r,
                   cx + knob_r, knob_y + knob_r), fill=color)
    else:
        d.ellipse((cx - knob_r, knob_y - knob_r,
                   cx + knob_r, knob_y + knob_r), outline=color, width=sw)
    # Two splayed legs (V shape) — thicker in FILL, sw-wide in OUTLINE.
    leg_t = max(4, s // 28) if mode == "FILL" else sw
    d.line([(cx, knob_y), (cx - s // 3, cy + s // 3)],
           fill=color, width=leg_t)
    d.line([(cx, knob_y), (cx + s // 3, cy + s // 3)],
           fill=color, width=leg_t)
    # Curved arc at the bottom showing the angle the compass would draw.
    arc_r = s // 3
    d.arc((cx - arc_r, cy, cx + arc_r, cy + arc_r * 2),
          start=15, end=165, fill=color, width=max(2, sw - 1))

def glyph_8bit_star(d, cx, cy, size, color, mode, sw):
    """Five-point star — Pixel theme emblem (pixelates cleanly via PIXEL_FINE)."""
    s = size
    r_outer = s // 2
    r_inner = int(s * 0.21)
    n_points = 5
    pts = []
    for i in range(n_points * 2):
        r = r_outer if (i % 2 == 0) else r_inner
        a = math.radians(-90 + i * (360 / (n_points * 2)))
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)

# ═════════════════════════════════════════════════════════════════════════════
# PER-THEME ICON VOCABULARIES (v2.9.8 — final icon pass)
#
# Creator directive: "I want ALL different ones like icon packs all themed
# better.  Be more in depth with it so we can finish it."
#
# Each (role, theme) pair gets its OWN distinct glyph shape, not just a
# recolored version of a shared shape.  Themes pull from different visual
# vocabularies that fit their aesthetic:
#   Glass     → modern sleek (current default)
#   Neon      → arcade / cyberpunk
#   Minimal   → bare-essentials line art
#   Retro     → 80s/early-90s console era
#   Cards     → playing-card-suit motif
#   Pastel    → soft rounded "cute" forms
#   Dark      → heavy gothic / runic
#   Gradient  → flowing curves
#   Blueprint → technical schematic drawings
#   Pixel     → 8-bit / pixel-art chunky shapes
# ═════════════════════════════════════════════════════════════════════════════

# ── FolderGames variants (10) ─────────────────────────────────────────────────

def glyph_games_ergo(d, cx, cy, size, color, mode, sw):
    """Modern ergonomic gamepad — Glass / Cards."""
    glyph_games_controller(d, cx, cy, size, color, mode, sw)  # reuse current

def glyph_games_arcade(d, cx, cy, size, color, mode, sw):
    """Arcade joystick ball-top + 4 buttons — Neon."""
    glyph_emulators_joystick(d, cx, cy, size, color, mode, sw)

def glyph_games_play_tri(d, cx, cy, size, color, mode, sw):
    """Big play triangle (▶) — Minimal."""
    s = size
    pts = [(cx - s // 3, cy - s // 2),
           (cx - s // 3, cy + s // 2),
           (cx + s // 2, cy)]
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)

def glyph_games_nes_pad(d, cx, cy, size, color, mode, sw):
    """NES-style rectangular pad — Retro / Pixel."""
    s = size
    body_w = int(s * 0.95)
    body_h = int(s * 0.50)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
    else:
        d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)
    cutout = (0, 0, 0, 0) if mode == "FILL" else color
    # D-pad cross (left third).
    dx = bx + body_w // 5
    dy = cy
    arm = s // 14
    t = s // 30
    d.rectangle((dx - arm, dy - t, dx + arm, dy + t), fill=cutout if mode == "FILL" else None,
                outline=cutout if mode == "OUTLINE" else None, width=sw if mode == "OUTLINE" else 0)
    d.rectangle((dx - t, dy - arm, dx + t, dy + arm), fill=cutout if mode == "FILL" else None,
                outline=cutout if mode == "OUTLINE" else None, width=sw if mode == "OUTLINE" else 0)
    # Two AB buttons (right third) — round circles.
    bx2 = bx + (body_w * 4) // 5
    br = s // 22
    for dxoff in (-s // 14, s // 14):
        d.ellipse((bx2 + dxoff - br, dy - br, bx2 + dxoff + br, dy + br),
                  fill=cutout if mode == "FILL" else None,
                  outline=cutout if mode == "OUTLINE" else None,
                  width=sw if mode == "OUTLINE" else 0)
    # Center: two small rectangles (Start / Select).
    sb_w = s // 10
    sb_h = s // 30
    for dxoff in (-s // 14, s // 14):
        d.rectangle((cx + dxoff - sb_w // 2, by + body_h - s // 8 - sb_h // 2,
                     cx + dxoff + sb_w // 2, by + body_h - s // 8 + sb_h // 2),
                    fill=cutout if mode == "FILL" else None,
                    outline=cutout if mode == "OUTLINE" else None,
                    width=sw if mode == "OUTLINE" else 0)

def glyph_games_club(d, cx, cy, size, color, mode, sw):
    """Club suit (♣) — Cards."""
    s = size
    r = s // 4
    # Three lobes: top, lower-left, lower-right.
    top_y = cy - s // 5
    ll_x, ll_y = cx - int(r * 0.85), cy + s // 12
    lr_x, lr_y = cx + int(r * 0.85), cy + s // 12
    if mode == "FILL":
        d.ellipse((cx - r, top_y - r, cx + r, top_y + r), fill=color)
        d.ellipse((ll_x - r, ll_y - r, ll_x + r, ll_y + r), fill=color)
        d.ellipse((lr_x - r, lr_y - r, lr_x + r, lr_y + r), fill=color)
        # Stem.
        stem_pts = [(cx - s // 12, cy + s // 16),
                    (cx + s // 12, cy + s // 16),
                    (cx + s // 5, cy + s // 2),
                    (cx - s // 5, cy + s // 2)]
        d.polygon(stem_pts, fill=color)
    else:
        d.ellipse((cx - r, top_y - r, cx + r, top_y + r), outline=color, width=sw)
        d.ellipse((ll_x - r, ll_y - r, ll_x + r, ll_y + r), outline=color, width=sw)
        d.ellipse((lr_x - r, lr_y - r, lr_x + r, lr_y + r), outline=color, width=sw)
        stem_pts = [(cx - s // 12, cy + s // 16),
                    (cx + s // 12, cy + s // 16),
                    (cx + s // 5, cy + s // 2),
                    (cx - s // 5, cy + s // 2)]
        for i in range(len(stem_pts)):
            d.line([stem_pts[i], stem_pts[(i + 1) % len(stem_pts)]],
                   fill=color, width=sw)

def glyph_games_blob(d, cx, cy, size, color, mode, sw):
    """Cute blobby gamepad — Pastel."""
    s = size
    # Wider, more rounded pill with cute bumpers.
    body_w = int(s * 1.00)
    body_h = int(s * 0.62)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=body_h // 2, fill=color)
    else:
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=body_h // 2, outline=color, width=sw)
    cutout = (0, 0, 0, 0) if mode == "FILL" else color
    # Two big eye-like circles (instead of buttons / d-pad — playful).
    eye_r = s // 13
    for dxoff in (-s // 4, s // 4):
        d.ellipse((cx + dxoff - eye_r, cy - eye_r,
                   cx + dxoff + eye_r, cy + eye_r),
                  fill=cutout if mode == "FILL" else None,
                  outline=cutout if mode == "OUTLINE" else None,
                  width=sw if mode == "OUTLINE" else 0)

def glyph_games_brutalist(d, cx, cy, size, color, mode, sw):
    """Heavy slab gamepad with sharp corners — Dark."""
    s = size
    body_w = int(s * 0.92)
    body_h = int(s * 0.52)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
    else:
        d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)
    cutout = (0, 0, 0, 0) if mode == "FILL" else color
    # Two grip handles flaring down.
    grip_h = s // 8
    grip_w = s // 8
    for sign in (-1, 1):
        gx = cx + sign * (body_w // 2 - grip_w // 2)
        d.rectangle((gx - grip_w // 2, by + body_h,
                     gx + grip_w // 2, by + body_h + grip_h),
                    fill=color if mode == "FILL" else None,
                    outline=color if mode == "OUTLINE" else None,
                    width=sw if mode == "OUTLINE" else 0)
    # Trigger slots (squares).
    sl_w = s // 8
    sl_h = s // 20
    d.rectangle((cx - s // 3 - sl_w // 2, cy - sl_h // 2,
                 cx - s // 3 + sl_w // 2, cy + sl_h // 2),
                fill=cutout if mode == "FILL" else None,
                outline=cutout if mode == "OUTLINE" else None,
                width=sw if mode == "OUTLINE" else 0)
    d.rectangle((cx + s // 3 - sl_w // 2, cy - sl_h // 2,
                 cx + s // 3 + sl_w // 2, cy + sl_h // 2),
                fill=cutout if mode == "FILL" else None,
                outline=cutout if mode == "OUTLINE" else None,
                width=sw if mode == "OUTLINE" else 0)

def glyph_games_curved(d, cx, cy, size, color, mode, sw):
    """Swooping curved gamepad silhouette — Gradient."""
    s = size
    # Asymmetric "wing" silhouette via polygon.
    pts = [
        (cx - s // 2, cy),
        (cx - s // 3, cy - s // 3),
        (cx + s // 3, cy - s // 3),
        (cx + s // 2, cy),
        (cx + s // 3, cy + s // 3),
        (cx - s // 3, cy + s // 3),
    ]
    if mode == "FILL":
        d.polygon(pts, fill=color)
        # Cut a sweeping curve out of the middle (use an ellipse mask).
        d.ellipse((cx - s // 3, cy - s // 8, cx + s // 3, cy + s // 8),
                  fill=(0, 0, 0, 0))
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)
        d.ellipse((cx - s // 3, cy - s // 8, cx + s // 3, cy + s // 8),
                  outline=color, width=sw)

def glyph_games_schematic(d, cx, cy, size, color, mode, sw):
    """Schematic gamepad with dimension lines — Blueprint."""
    s = size
    body_w = int(s * 0.90)
    body_h = int(s * 0.46)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    # Body — thin outline only (blueprint = drawing, not solid).
    d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)
    # Dimension tick marks on bottom.
    tick_y = by + body_h + s // 12
    d.line([(bx, tick_y), (bx + body_w, tick_y)], fill=color, width=max(1, sw - 1))
    for tx in (bx, bx + body_w):
        d.line([(tx, tick_y - s // 28), (tx, tick_y + s // 28)],
               fill=color, width=max(1, sw - 1))
    # Crosshair targets on each "button" location.
    target_r = s // 16
    for tx in (cx - s // 4, cx + s // 4):
        d.ellipse((tx - target_r, cy - target_r, tx + target_r, cy + target_r),
                  outline=color, width=max(1, sw - 1))
        d.line([(tx - target_r * 3 // 2, cy), (tx + target_r * 3 // 2, cy)],
               fill=color, width=max(1, sw - 1))
        d.line([(tx, cy - target_r * 3 // 2), (tx, cy + target_r * 3 // 2)],
               fill=color, width=max(1, sw - 1))

# ── FolderEmulators variants ──────────────────────────────────────────────────

def glyph_emu_floppy(d, cx, cy, size, color, mode, sw):
    """3.5" floppy disk — Glass."""
    s = size
    body_w = int(s * 0.85)
    body_h = int(s * 0.85)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
        # Top-right corner notch (clipped corner).
        notch = s // 10
        d.polygon([(bx + body_w - notch, by),
                   (bx + body_w, by),
                   (bx + body_w, by + notch)], fill=(0, 0, 0, 0))
        # Metal slider on top.
        sl_w = int(body_w * 0.55)
        sl_h = int(body_h * 0.30)
        d.rectangle((cx - sl_w // 2, by + s // 16,
                     cx + sl_w // 2, by + s // 16 + sl_h),
                    fill=(0, 0, 0, 0))
        # Label area on bottom (negative space).
        lbl_w = int(body_w * 0.70)
        lbl_h = int(body_h * 0.30)
        d.rectangle((cx - lbl_w // 2, by + body_h - s // 8 - lbl_h,
                     cx + lbl_w // 2, by + body_h - s // 8),
                    fill=(0, 0, 0, 0))
    else:
        d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)
        sl_w = int(body_w * 0.55)
        sl_h = int(body_h * 0.30)
        d.rectangle((cx - sl_w // 2, by + s // 16,
                     cx + sl_w // 2, by + s // 16 + sl_h),
                    outline=color, width=sw)
        lbl_w = int(body_w * 0.70)
        lbl_h = int(body_h * 0.30)
        d.rectangle((cx - lbl_w // 2, by + body_h - s // 8 - lbl_h,
                     cx + lbl_w // 2, by + body_h - s // 8),
                    outline=color, width=sw)

def glyph_emu_crt(d, cx, cy, size, color, mode, sw):
    """CRT television silhouette — Neon."""
    s = size
    body_w = int(s * 0.95)
    body_h = int(s * 0.75)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 10, fill=color)
        # Screen cutout.
        sc_w = int(body_w * 0.78)
        sc_h = int(body_h * 0.66)
        d.rounded_rectangle((cx - sc_w // 2, by + s // 12,
                             cx + sc_w // 2, by + s // 12 + sc_h),
                            radius=s // 18, fill=(0, 0, 0, 0))
    else:
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 10, outline=color, width=sw)
        sc_w = int(body_w * 0.78)
        sc_h = int(body_h * 0.66)
        d.rounded_rectangle((cx - sc_w // 2, by + s // 12,
                             cx + sc_w // 2, by + s // 12 + sc_h),
                            radius=s // 18, outline=color, width=sw)
    # Two feet at the bottom.
    foot_w = s // 8
    foot_h = s // 18
    for sign in (-1, 1):
        fx = cx + sign * (body_w // 4)
        d.rectangle((fx - foot_w // 2, by + body_h,
                     fx + foot_w // 2, by + body_h + foot_h),
                    fill=color if mode == "FILL" else None,
                    outline=color if mode == "OUTLINE" else None,
                    width=sw if mode == "OUTLINE" else 0)

def glyph_emu_rect(d, cx, cy, size, color, mode, sw):
    """Plain rectangle — Minimal."""
    s = size
    body_w = int(s * 0.80)
    body_h = int(s * 0.50)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
    else:
        d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)

def glyph_emu_cartridge(d, cx, cy, size, color, mode, sw):
    """Game cartridge (NES-shape) — Retro / Pixel."""
    s = size
    body_w = int(s * 0.78)
    body_h = int(s * 0.92)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
        # Label area (large negative-space rect near top).
        lbl_w = int(body_w * 0.75)
        lbl_h = int(body_h * 0.45)
        d.rectangle((cx - lbl_w // 2, by + s // 14,
                     cx + lbl_w // 2, by + s // 14 + lbl_h),
                    fill=(0, 0, 0, 0))
        # Connector slot at bottom (thin strip).
        slot_w = int(body_w * 0.85)
        slot_h = s // 18
        d.rectangle((cx - slot_w // 2, by + body_h - s // 8,
                     cx + slot_w // 2, by + body_h - s // 8 + slot_h),
                    fill=(0, 0, 0, 0))
    else:
        d.rectangle((bx, by, bx + body_w, by + body_h), outline=color, width=sw)
        lbl_w = int(body_w * 0.75)
        lbl_h = int(body_h * 0.45)
        d.rectangle((cx - lbl_w // 2, by + s // 14,
                     cx + lbl_w // 2, by + s // 14 + lbl_h),
                    outline=color, width=sw)
        slot_w = int(body_w * 0.85)
        slot_h = s // 18
        d.rectangle((cx - slot_w // 2, by + body_h - s // 8,
                     cx + slot_w // 2, by + body_h - s // 8 + slot_h),
                    outline=color, width=sw)

def glyph_emu_tarot(d, cx, cy, size, color, mode, sw):
    """Tarot-card silhouette with inner crescent moon — Cards."""
    s = size
    body_w = int(s * 0.65)
    body_h = int(s * 0.95)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 16, fill=color)
        # Crescent moon (negative space).
        moon_r = s // 5
        d.ellipse((cx - moon_r, cy - moon_r, cx + moon_r, cy + moon_r),
                  fill=(0, 0, 0, 0))
        # "Bite" out of the moon to make crescent (offset overlay using color).
        d.ellipse((cx - moon_r // 3, cy - moon_r, cx - moon_r // 3 + moon_r * 2, cy + moon_r),
                  fill=color)
    else:
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 16, outline=color, width=sw)
        moon_r = s // 5
        d.ellipse((cx - moon_r, cy - moon_r, cx + moon_r, cy + moon_r),
                  outline=color, width=sw)

def glyph_emu_heart_cassette(d, cx, cy, size, color, mode, sw):
    """Heart-shaped cassette / soft media — Pastel."""
    s = size
    # Outer rounded rectangle (cassette shape with rounded corners).
    body_w = int(s * 0.90)
    body_h = int(s * 0.62)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    if mode == "FILL":
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 8, fill=color)
        # Two reel circles (negative space).
        reel_r = s // 9
        gap = s // 4
        for dxoff in (-gap, gap):
            d.ellipse((cx + dxoff - reel_r, cy - reel_r,
                       cx + dxoff + reel_r, cy + reel_r), fill=(0, 0, 0, 0))
    else:
        d.rounded_rectangle((bx, by, bx + body_w, by + body_h),
                            radius=s // 8, outline=color, width=sw)
        reel_r = s // 9
        gap = s // 4
        for dxoff in (-gap, gap):
            d.ellipse((cx + dxoff - reel_r, cy - reel_r,
                       cx + dxoff + reel_r, cy + reel_r),
                      outline=color, width=sw)

def glyph_emu_skull_disc(d, cx, cy, size, color, mode, sw):
    """Disc/CD with subtle skull motif — Dark."""
    s = size
    r_outer = s // 2 - 4
    r_inner = s // 8
    if mode == "FILL":
        d.ellipse((cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer),
                  fill=color)
        d.ellipse((cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner),
                  fill=(0, 0, 0, 0))
        # Two "eyes" as off-centre punched circles inside the disc.
        eye_r = s // 16
        for dxoff in (-s // 6, s // 6):
            d.ellipse((cx + dxoff - eye_r, cy - s // 8 - eye_r,
                       cx + dxoff + eye_r, cy - s // 8 + eye_r),
                      fill=(0, 0, 0, 0))
    else:
        d.ellipse((cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer),
                  outline=color, width=sw)
        d.ellipse((cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner),
                  outline=color, width=sw)
        eye_r = s // 16
        for dxoff in (-s // 6, s // 6):
            d.ellipse((cx + dxoff - eye_r, cy - s // 8 - eye_r,
                       cx + dxoff + eye_r, cy - s // 8 + eye_r),
                      outline=color, width=sw)

def glyph_emu_vinyl(d, cx, cy, size, color, mode, sw):
    """Vinyl record — Gradient."""
    s = size
    r_outer = s // 2 - 4
    r_label = s // 5
    r_hole = s // 28
    if mode == "FILL":
        d.ellipse((cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer),
                  fill=color)
        d.ellipse((cx - r_label, cy - r_label, cx + r_label, cy + r_label),
                  fill=(0, 0, 0, 0))
        d.ellipse((cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole),
                  fill=color)
    else:
        d.ellipse((cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer),
                  outline=color, width=sw)
        d.ellipse((cx - r_label, cy - r_label, cx + r_label, cy + r_label),
                  outline=color, width=sw)
        # Concentric groove lines.
        for r in (int(r_outer * 0.85), int(r_outer * 0.65)):
            d.ellipse((cx - r, cy - r, cx + r, cy + r),
                      outline=color, width=max(1, sw - 1))

def glyph_emu_circuit(d, cx, cy, size, color, mode, sw):
    """Circuit board / chip outline — Blueprint."""
    s = size
    body_w = int(s * 0.78)
    body_h = int(s * 0.78)
    bx = cx - body_w // 2
    by = cy - body_h // 2
    d.rectangle((bx, by, bx + body_w, by + body_h),
                outline=color, width=sw if mode == "OUTLINE" else max(2, sw))
    if mode == "FILL":
        d.rectangle((bx, by, bx + body_w, by + body_h), fill=color)
        # Inner negative-space rect for the "chip" centre.
        ip = s // 16
        d.rectangle((bx + ip, by + ip, bx + body_w - ip, by + body_h - ip),
                    fill=(0, 0, 0, 0))
    # Pin marks on all 4 sides (negative cuts in FILL mode, lines in OUTLINE).
    pin_n = 3
    pin_len = s // 14
    pin_t = s // 28
    for i in range(1, pin_n + 1):
        offs = (body_w // (pin_n + 1)) * i
        # Top + bottom pins.
        for sign, edge_y in ((-1, by), (1, by + body_h)):
            d.rectangle((bx + offs - pin_t // 2, edge_y - (pin_len if sign < 0 else 0),
                         bx + offs + pin_t // 2, edge_y + (pin_len if sign > 0 else 0)),
                        fill=color if mode == "FILL" else None,
                        outline=color if mode == "OUTLINE" else None,
                        width=sw if mode == "OUTLINE" else 0)
        # Left + right pins.
        for sign, edge_x in ((-1, bx), (1, bx + body_w)):
            offy = (body_h // (pin_n + 1)) * i
            d.rectangle((edge_x - (pin_len if sign < 0 else 0),
                         by + offy - pin_t // 2,
                         edge_x + (pin_len if sign > 0 else 0),
                         by + offy + pin_t // 2),
                        fill=color if mode == "FILL" else None,
                        outline=color if mode == "OUTLINE" else None,
                        width=sw if mode == "OUTLINE" else 0)

# ── FolderTools variants ──────────────────────────────────────────────────────

def glyph_tools_wrench_smooth(d, cx, cy, size, color, mode, sw):
    """Smooth modern wrench — Glass."""
    glyph_tools_wrench(d, cx, cy, size, color, mode, sw)

def glyph_tools_bolt(d, cx, cy, size, color, mode, sw):
    """Lightning bolt + wrench hybrid — Neon."""
    glyph_lightning_bolt(d, cx, cy, size, color, mode, sw)

def glyph_tools_hammer_line(d, cx, cy, size, color, mode, sw):
    """Simple hammer silhouette — Minimal."""
    s = size
    # Head: small rectangle at top.
    head_w = int(s * 0.50)
    head_h = int(s * 0.20)
    hx = cx - head_w // 2
    hy = cy - s // 3
    # Handle: thin vertical bar below.
    handle_w = s // 12
    handle_h = int(s * 0.65)
    if mode == "FILL":
        d.rectangle((hx, hy, hx + head_w, hy + head_h), fill=color)
        d.rectangle((cx - handle_w // 2, hy + head_h,
                     cx + handle_w // 2, hy + head_h + handle_h), fill=color)
    else:
        d.rectangle((hx, hy, hx + head_w, hy + head_h),
                    outline=color, width=sw)
        d.rectangle((cx - handle_w // 2, hy + head_h,
                     cx + handle_w // 2, hy + head_h + handle_h),
                    outline=color, width=sw)

def glyph_tools_pickaxe(d, cx, cy, size, color, mode, sw):
    """Pickaxe (Minecraft-style) — Retro."""
    s = size
    # Head: horizontal pickaxe (two pointed tips).
    head_w = int(s * 0.85)
    head_h = int(s * 0.20)
    hy = cy - s // 4
    # Polygon head: rectangle + triangular tips.
    head_pts = [
        (cx - head_w // 2, hy + head_h // 2),
        (cx - head_w // 2 - s // 12, hy + head_h // 2 + s // 18),
        (cx - head_w // 2 + s // 14, hy + head_h),
        (cx + head_w // 2 - s // 14, hy + head_h),
        (cx + head_w // 2 + s // 12, hy + head_h // 2 + s // 18),
        (cx + head_w // 2, hy + head_h // 2),
        (cx + head_w // 2 - s // 14, hy),
        (cx - head_w // 2 + s // 14, hy),
    ]
    if mode == "FILL":
        d.polygon(head_pts, fill=color)
    else:
        for i in range(len(head_pts)):
            d.line([head_pts[i], head_pts[(i + 1) % len(head_pts)]],
                   fill=color, width=sw)
    # Handle.
    handle_w = s // 14
    if mode == "FILL":
        d.rectangle((cx - handle_w // 2, hy + head_h,
                     cx + handle_w // 2, cy + s // 2), fill=color)
    else:
        d.rectangle((cx - handle_w // 2, hy + head_h,
                     cx + handle_w // 2, cy + s // 2), outline=color, width=sw)

def glyph_tools_dice(d, cx, cy, size, color, mode, sw):
    """Die with pip pattern — Cards."""
    s = size
    body = int(s * 0.78)
    bx = cx - body // 2
    by = cy - body // 2
    if mode == "FILL":
        d.rounded_rectangle((bx, by, bx + body, by + body),
                            radius=s // 12, fill=color)
        # 5-pip pattern (corners + center).
        pip_r = s // 16
        pip_offsets = [(-s // 4, -s // 4), (s // 4, -s // 4), (0, 0),
                       (-s // 4, s // 4), (s // 4, s // 4)]
        for (dxoff, dyoff) in pip_offsets:
            d.ellipse((cx + dxoff - pip_r, cy + dyoff - pip_r,
                       cx + dxoff + pip_r, cy + dyoff + pip_r),
                      fill=(0, 0, 0, 0))
    else:
        d.rounded_rectangle((bx, by, bx + body, by + body),
                            radius=s // 12, outline=color, width=sw)
        pip_r = s // 16
        pip_offsets = [(-s // 4, -s // 4), (s // 4, -s // 4), (0, 0),
                       (-s // 4, s // 4), (s // 4, s // 4)]
        for (dxoff, dyoff) in pip_offsets:
            d.ellipse((cx + dxoff - pip_r, cy + dyoff - pip_r,
                       cx + dxoff + pip_r, cy + dyoff + pip_r),
                      outline=color, width=sw)

def glyph_tools_paintbrush(d, cx, cy, size, color, mode, sw):
    """Paint brush (chubby) — Pastel."""
    s = size
    # Ferrule (metal band) — small filled rect.
    fer_w = int(s * 0.42)
    fer_h = int(s * 0.10)
    fy = cy + s // 12
    # Bristle bulb (wider, rounded).
    bristle_w = int(s * 0.55)
    bristle_h = int(s * 0.40)
    if mode == "FILL":
        d.rounded_rectangle((cx - bristle_w // 2, fy + fer_h,
                             cx + bristle_w // 2, fy + fer_h + bristle_h),
                            radius=s // 10, fill=color)
        d.rectangle((cx - fer_w // 2, fy, cx + fer_w // 2, fy + fer_h),
                    fill=color)
    else:
        d.rounded_rectangle((cx - bristle_w // 2, fy + fer_h,
                             cx + bristle_w // 2, fy + fer_h + bristle_h),
                            radius=s // 10, outline=color, width=sw)
        d.rectangle((cx - fer_w // 2, fy, cx + fer_w // 2, fy + fer_h),
                    outline=color, width=sw)
    # Handle (long rectangle pointing up).
    handle_w = s // 9
    if mode == "FILL":
        d.rectangle((cx - handle_w // 2, cy - s // 2,
                     cx + handle_w // 2, fy), fill=color)
    else:
        d.rectangle((cx - handle_w // 2, cy - s // 2,
                     cx + handle_w // 2, fy), outline=color, width=sw)

def glyph_tools_axe(d, cx, cy, size, color, mode, sw):
    """Battle-axe / war-hammer — Dark."""
    s = size
    # Head: large axe-blade shape (asymmetric).
    head_pts = [
        (cx - s // 3, cy - s // 4),
        (cx + s // 5, cy - s // 3),
        (cx + s // 3, cy - s // 6),
        (cx + s // 3, cy + s // 6),
        (cx + s // 5, cy + s // 4),
        (cx - s // 3, cy + s // 8),
    ]
    if mode == "FILL":
        d.polygon(head_pts, fill=color)
    else:
        for i in range(len(head_pts)):
            d.line([head_pts[i], head_pts[(i + 1) % len(head_pts)]],
                   fill=color, width=sw)
    # Handle (diagonal).
    handle_t = s // 12
    pts = [
        (cx - s // 2 + s // 8, cy + s // 2 - s // 8),
        (cx - s // 4, cy + s // 8),
        (cx - s // 4 + handle_t, cy + s // 8 + handle_t),
        (cx - s // 2 + s // 8 + handle_t, cy + s // 2 - s // 8 + handle_t),
    ]
    if mode == "FILL":
        d.polygon(pts, fill=color)
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)

def glyph_tools_brushstroke(d, cx, cy, size, color, mode, sw):
    """Flowing brush stroke (S-curve) — Gradient."""
    s = size
    # Three bezier-like control points sketched as connected thick lines.
    # Approximation: 4-segment polyline forming an S-shape.
    pts = [
        (cx - s // 2, cy + s // 3),
        (cx - s // 6, cy - s // 4),
        (cx + s // 6, cy + s // 4),
        (cx + s // 2, cy - s // 3),
    ]
    stroke_t = max(6, s // 12) if mode == "FILL" else sw
    for i in range(len(pts) - 1):
        d.line([pts[i], pts[i + 1]], fill=color, width=stroke_t)
    # End dots for an "ink drip" feel.
    end_r = s // 18
    for end in (pts[0], pts[-1]):
        d.ellipse((end[0] - end_r, end[1] - end_r,
                   end[0] + end_r, end[1] + end_r),
                  fill=color if mode == "FILL" else None,
                  outline=color if mode == "OUTLINE" else None,
                  width=sw if mode == "OUTLINE" else 0)

def glyph_tools_schematic_wrench(d, cx, cy, size, color, mode, sw):
    """Wrench with dimension callouts — Blueprint."""
    s = size
    # Reuse the wrench but render OUTLINE-only regardless of mode.
    glyph_tools_wrench(d, cx, cy, size, color, "OUTLINE", max(2, sw))
    # Dimension brackets above the head.
    bx_x = cx + s // 8
    bx_y = cy - s // 3
    d.line([(bx_x - s // 8, bx_y - s // 10), (bx_x + s // 8, bx_y - s // 10)],
           fill=color, width=max(1, sw - 1))
    d.line([(bx_x - s // 8, bx_y - s // 10 - s // 28),
            (bx_x - s // 8, bx_y - s // 10 + s // 28)],
           fill=color, width=max(1, sw - 1))
    d.line([(bx_x + s // 8, bx_y - s // 10 - s // 28),
            (bx_x + s // 8, bx_y - s // 10 + s // 28)],
           fill=color, width=max(1, sw - 1))

def glyph_tools_sword(d, cx, cy, size, color, mode, sw):
    """Pixel-style sword — Pixel."""
    s = size
    # Blade (long vertical).
    blade_w = s // 7
    blade_top = cy - s // 2 + s // 14
    blade_bot = cy + s // 6
    if mode == "FILL":
        d.rectangle((cx - blade_w // 2, blade_top,
                     cx + blade_w // 2, blade_bot), fill=color)
    else:
        d.rectangle((cx - blade_w // 2, blade_top,
                     cx + blade_w // 2, blade_bot),
                    outline=color, width=sw)
    # Point: triangle at top.
    point_pts = [(cx - blade_w // 2, blade_top),
                 (cx + blade_w // 2, blade_top),
                 (cx, blade_top - s // 10)]
    if mode == "FILL":
        d.polygon(point_pts, fill=color)
    else:
        for i in range(len(point_pts)):
            d.line([point_pts[i], point_pts[(i + 1) % len(point_pts)]],
                   fill=color, width=sw)
    # Crossguard (horizontal bar at the bottom of the blade).
    cg_w = int(s * 0.55)
    cg_h = s // 14
    if mode == "FILL":
        d.rectangle((cx - cg_w // 2, blade_bot,
                     cx + cg_w // 2, blade_bot + cg_h), fill=color)
    else:
        d.rectangle((cx - cg_w // 2, blade_bot,
                     cx + cg_w // 2, blade_bot + cg_h),
                    outline=color, width=sw)
    # Grip (handle).
    grip_w = s // 10
    grip_h = int(s * 0.18)
    if mode == "FILL":
        d.rectangle((cx - grip_w // 2, blade_bot + cg_h,
                     cx + grip_w // 2, blade_bot + cg_h + grip_h),
                    fill=color)
    else:
        d.rectangle((cx - grip_w // 2, blade_bot + cg_h,
                     cx + grip_w // 2, blade_bot + cg_h + grip_h),
                    outline=color, width=sw)
    # Pommel (rounded knob at bottom).
    pom_r = s // 14
    if mode == "FILL":
        d.ellipse((cx - pom_r, blade_bot + cg_h + grip_h - pom_r // 2,
                   cx + pom_r, blade_bot + cg_h + grip_h + pom_r + pom_r // 2),
                  fill=color)
    else:
        d.ellipse((cx - pom_r, blade_bot + cg_h + grip_h - pom_r // 2,
                   cx + pom_r, blade_bot + cg_h + grip_h + pom_r + pom_r // 2),
                  outline=color, width=sw)

# ── FolderSystem variants ─────────────────────────────────────────────────────

def glyph_system_gear_smooth(d, cx, cy, size, color, mode, sw):
    """8-tooth gear — Glass / Cards / Retro / Pixel (reuse current)."""
    glyph_system_gear(d, cx, cy, size, color, mode, sw)

def glyph_system_ring(d, cx, cy, size, color, mode, sw):
    """Holographic ring (concentric circles) — Neon."""
    s = size
    rings = [(s // 2 - 4, max(4, s // 14)),
             (int(s * 0.35), max(3, s // 18)),
             (int(s * 0.22), max(2, s // 24))]
    for (r, t) in rings:
        if mode == "FILL":
            d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=t)
        else:
            d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=t)
    # Centre dot.
    dot_r = s // 14
    if mode == "FILL":
        d.ellipse((cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r), fill=color)
    else:
        d.ellipse((cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r),
                  outline=color, width=sw)

def glyph_system_circ_dot(d, cx, cy, size, color, mode, sw):
    """Circle + centre dot — Minimal."""
    r = s_outer = size // 2 - 4
    dot_r = max(4, size // 10)
    if mode == "FILL":
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=max(3, size // 30))
        d.ellipse((cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r), fill=color)
    else:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=sw)
        d.ellipse((cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r),
                  outline=color, width=sw)

def glyph_system_flower(d, cx, cy, size, color, mode, sw):
    """Flower-shaped 6-petal — Pastel."""
    s = size
    petal_r = s // 5
    n_petals = 6
    for i in range(n_petals):
        a = math.radians(60 * i - 90)
        px = cx + (s // 4) * math.cos(a)
        py = cy + (s // 4) * math.sin(a)
        if mode == "FILL":
            d.ellipse((px - petal_r, py - petal_r, px + petal_r, py + petal_r),
                      fill=color)
        else:
            d.ellipse((px - petal_r, py - petal_r, px + petal_r, py + petal_r),
                      outline=color, width=sw)
    # Centre.
    cen_r = s // 9
    cen_color = (0, 0, 0, 0) if mode == "FILL" else color
    d.ellipse((cx - cen_r, cy - cen_r, cx + cen_r, cy + cen_r),
              fill=cen_color if mode == "FILL" else None,
              outline=cen_color if mode == "OUTLINE" else None,
              width=sw if mode == "OUTLINE" else 0)

def glyph_system_cog_chains(d, cx, cy, size, color, mode, sw):
    """Heavy cog with chain teeth — Dark."""
    s = size
    outer_r = s // 2 - 4
    inner_r = int(outer_r * 0.55)
    tooth_h = s // 8
    n_teeth = 10
    if mode == "FILL":
        # Square-block teeth around a ring.
        for i in range(n_teeth):
            a = math.radians(360 / n_teeth * i)
            tx = cx + (outer_r - tooth_h // 2) * math.cos(a)
            ty = cy + (outer_r - tooth_h // 2) * math.sin(a)
            d.rectangle((tx - tooth_h // 2, ty - tooth_h // 2,
                         tx + tooth_h // 2, ty + tooth_h // 2), fill=color)
        d.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r),
                  fill=color)
        # Centre hole (large square — gives it a brutalist look).
        hole_s = s // 6
        d.rectangle((cx - hole_s, cy - hole_s, cx + hole_s, cy + hole_s),
                    fill=(0, 0, 0, 0))
    else:
        for i in range(n_teeth):
            a = math.radians(360 / n_teeth * i)
            tx = cx + (outer_r - tooth_h // 2) * math.cos(a)
            ty = cy + (outer_r - tooth_h // 2) * math.sin(a)
            d.rectangle((tx - tooth_h // 2, ty - tooth_h // 2,
                         tx + tooth_h // 2, ty + tooth_h // 2),
                        outline=color, width=sw)
        d.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r),
                  outline=color, width=sw)
        hole_s = s // 6
        d.rectangle((cx - hole_s, cy - hole_s, cx + hole_s, cy + hole_s),
                    outline=color, width=sw)

def glyph_system_orbital(d, cx, cy, size, color, mode, sw):
    """Orbital rings around a nucleus — Gradient."""
    s = size
    r_core = s // 8
    if mode == "FILL":
        d.ellipse((cx - r_core, cy - r_core, cx + r_core, cy + r_core), fill=color)
    else:
        d.ellipse((cx - r_core, cy - r_core, cx + r_core, cy + r_core),
                  outline=color, width=sw)
    # Three tilted orbit ellipses.
    orbit_w = s // 2 - 6
    orbit_h = s // 5
    for angle_deg in (0, 60, 120):
        a = math.radians(angle_deg)
        # Build rotated-ellipse approximation by drawing many segments.
        pts = []
        N = 36
        cosA, sinA = math.cos(a), math.sin(a)
        for k in range(N):
            theta = 2 * math.pi * k / N
            ox = orbit_w * math.cos(theta)
            oy = orbit_h * math.sin(theta)
            px = cx + ox * cosA - oy * sinA
            py = cy + ox * sinA + oy * cosA
            pts.append((px, py))
        for j in range(N):
            d.line([pts[j], pts[(j + 1) % N]], fill=color, width=max(2, sw - 1))

def glyph_system_exploded_gear(d, cx, cy, size, color, mode, sw):
    """Exploded gear assembly diagram — Blueprint."""
    s = size
    # Two stacked gears slightly offset, OUTLINE only.
    gear_r = s // 3
    # Gear 1 (top-left).
    glyph_system_gear(d, cx - s // 6, cy - s // 8, int(s * 0.65), color,
                      "OUTLINE", max(2, sw - 1))
    # Gear 2 (bottom-right).
    glyph_system_gear(d, cx + s // 6, cy + s // 8, int(s * 0.55), color,
                      "OUTLINE", max(2, sw - 1))
    # Dashed "assembly axis" line connecting them.
    n_dashes = 8
    for i in range(0, n_dashes, 2):
        t0 = i / n_dashes
        t1 = (i + 1) / n_dashes
        p0 = (cx - s // 6 + (s // 3) * t0, cy - s // 8 + (s // 4) * t0)
        p1 = (cx - s // 6 + (s // 3) * t1, cy - s // 8 + (s // 4) * t1)
        d.line([p0, p1], fill=color, width=max(1, sw - 1))

# ── FolderQOS variants ────────────────────────────────────────────────────────

def glyph_qos_q_smooth(d, cx, cy, size, color, mode, sw):
    """Q with tail (Glass / Cards / Pastel / Dark / Gradient / Blueprint)."""
    glyph_qos_q(d, cx, cy, size, color, mode, sw)

def glyph_qos_q_neon(d, cx, cy, size, color, mode, sw):
    """Q rendered as outlined circle with sparks — Neon."""
    s = size
    r = s // 2 - 4
    # Just an outlined circle — let OUTLINE_GLOW post-processing add the neon.
    d.ellipse((cx - r, cy - r, cx + r, cy + r),
              outline=color, width=max(sw, s // 14))
    # Tail.
    tail_t = max(sw, s // 12)
    d.line((cx + int(r * 0.5), cy + int(r * 0.5),
            cx + int(r * 0.95), cy + int(r * 0.95)),
           fill=color, width=tail_t)

def glyph_qos_q_line(d, cx, cy, size, color, mode, sw):
    """Just a thin Q letter — Minimal."""
    s = size
    r = s // 2 - 4
    d.ellipse((cx - r, cy - r, cx + r, cy + r),
              outline=color, width=max(3, s // 32))
    d.line((cx + int(r * 0.45), cy + int(r * 0.45),
            cx + int(r * 0.95), cy + int(r * 0.95)),
           fill=color, width=max(3, s // 26))

def glyph_qos_q_block(d, cx, cy, size, color, mode, sw):
    """Blocky chunky Q — Retro / Pixel."""
    s = size
    # Outer square with thick stroke.
    outer = int(s * 0.85)
    bx = cx - outer // 2
    by = cy - outer // 2
    thickness = s // 9
    if mode == "FILL":
        # Outer rect filled.
        d.rectangle((bx, by, bx + outer, by + outer), fill=color)
        # Inner cutout.
        d.rectangle((bx + thickness, by + thickness,
                     bx + outer - thickness, by + outer - thickness),
                    fill=(0, 0, 0, 0))
    else:
        d.rectangle((bx, by, bx + outer, by + outer), outline=color, width=thickness)
    # Tail (diagonal blocky line bottom-right).
    tail_t = max(thickness, sw)
    d.line((cx + outer // 4, cy + outer // 4,
            cx + outer // 2 + tail_t // 2, cy + outer // 2 + tail_t // 2),
           fill=color, width=tail_t)

# ── FolderOther variants ──────────────────────────────────────────────────────

def glyph_other_question_smooth(d, cx, cy, size, color, mode, sw):
    """Standard question mark — Glass / Cards / Pastel."""
    glyph_other_question(d, cx, cy, size, color, mode, sw)

def glyph_other_warning(d, cx, cy, size, color, mode, sw):
    """Warning triangle with exclamation — Neon / Dark."""
    s = size
    # Triangle pointing up.
    pts = [(cx, cy - s // 2),
           (cx + s // 2, cy + s // 3),
           (cx - s // 2, cy + s // 3)]
    if mode == "FILL":
        d.polygon(pts, fill=color)
        # Punch ! glyph (vertical bar + dot).
        bar_w = s // 12
        bar_h = int(s * 0.30)
        d.rectangle((cx - bar_w // 2, cy - s // 12 - bar_h // 2,
                     cx + bar_w // 2, cy - s // 12 + bar_h // 2),
                    fill=(0, 0, 0, 0))
        dot_r = s // 16
        d.ellipse((cx - dot_r, cy + s // 5 - dot_r,
                   cx + dot_r, cy + s // 5 + dot_r), fill=(0, 0, 0, 0))
    else:
        for i in range(len(pts)):
            d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=sw)
        bar_w = s // 12
        bar_h = int(s * 0.30)
        d.rectangle((cx - bar_w // 2, cy - s // 12 - bar_h // 2,
                     cx + bar_w // 2, cy - s // 12 + bar_h // 2),
                    outline=color, width=sw)
        dot_r = s // 16
        d.ellipse((cx - dot_r, cy + s // 5 - dot_r,
                   cx + dot_r, cy + s // 5 + dot_r),
                  outline=color, width=sw)

def glyph_other_dot(d, cx, cy, size, color, mode, sw):
    """Single dot — Minimal."""
    r = size // 8
    if mode == "FILL":
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)
    else:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=sw)

def glyph_other_qblock(d, cx, cy, size, color, mode, sw):
    """? block (Mario style) — Retro / Pixel."""
    s = size
    body = int(s * 0.85)
    bx = cx - body // 2
    by = cy - body // 2
    if mode == "FILL":
        d.rectangle((bx, by, bx + body, by + body), fill=color)
        # ? inside (negative space — simplified ?).
        d.rectangle((cx - s // 18, cy - s // 8,
                     cx + s // 18, cy + s // 8), fill=(0, 0, 0, 0))
        d.ellipse((cx - s // 8, cy - s // 4,
                   cx + s // 8, cy - s // 4 + s // 4), fill=(0, 0, 0, 0))
        d.rectangle((cx - s // 18, cy + s // 6,
                     cx + s // 18, cy + s // 6 + s // 18), fill=(0, 0, 0, 0))
    else:
        d.rectangle((bx, by, bx + body, by + body), outline=color, width=sw)
        glyph_other_question(d, cx, cy, int(size * 0.60), color, "OUTLINE", sw)

def glyph_other_spiral(d, cx, cy, size, color, mode, sw):
    """Spiral — Gradient."""
    s = size
    # Approximate Archimedean spiral via line segments.
    N = 60
    prev = (cx, cy)
    stroke = max(3, s // 22)
    for i in range(1, N):
        t = i / N
        a = t * 4 * math.pi  # 2 full turns
        r = (s // 2 - 6) * t
        x = cx + r * math.cos(a)
        y = cy + r * math.sin(a)
        d.line([prev, (x, y)], fill=color, width=stroke)
        prev = (x, y)

def glyph_other_drafted_q(d, cx, cy, size, color, mode, sw):
    """? with technical-drawing crosshairs — Blueprint."""
    s = size
    # ? in outline.
    glyph_other_question(d, cx, cy, int(s * 0.78), color, "OUTLINE", max(2, sw))
    # Crosshair circle around it.
    r = int(s * 0.46)
    d.ellipse((cx - r, cy - r, cx + r, cy + r),
              outline=color, width=max(1, sw - 1))
    # Diagonal dashed lines through centre.
    n_dashes = 8
    diag_len = r * 1.4
    for direction in ((1, 1), (1, -1)):
        for i in range(0, n_dashes, 2):
            t0 = -0.5 + i / n_dashes
            t1 = -0.5 + (i + 1) / n_dashes
            p0 = (cx + t0 * diag_len * direction[0],
                  cy + t0 * diag_len * direction[1])
            p1 = (cx + t1 * diag_len * direction[0],
                  cy + t1 * diag_len * direction[1])
            d.line([p0, p1], fill=color, width=max(1, sw - 1))

# ═════════════════════════════════════════════════════════════════════════════
# PER-THEME GLYPH DISPATCH TABLE
#
# Lookup key: (role, theme_name) → glyph function.
# Falls back to GLYPHS[role] when no theme-specific entry exists.
# ═════════════════════════════════════════════════════════════════════════════

PER_THEME_GLYPHS = {
    # FolderGames — gamepads in distinctly different vocabularies.
    ("FolderGames", "Glass"):     glyph_games_ergo,
    ("FolderGames", "Neon"):      glyph_games_arcade,
    ("FolderGames", "Minimal"):   glyph_games_play_tri,
    ("FolderGames", "Retro"):     glyph_games_nes_pad,
    ("FolderGames", "Cards"):     glyph_games_club,
    ("FolderGames", "Pastel"):    glyph_games_blob,
    ("FolderGames", "Dark"):      glyph_games_brutalist,
    ("FolderGames", "Gradient"):  glyph_games_curved,
    ("FolderGames", "Blueprint"): glyph_games_schematic,
    ("FolderGames", "Pixel"):     glyph_games_nes_pad,

    # FolderEmulators — media / hardware shapes.
    ("FolderEmulators", "Glass"):     glyph_emu_floppy,
    ("FolderEmulators", "Neon"):      glyph_emu_crt,
    ("FolderEmulators", "Minimal"):   glyph_emu_rect,
    ("FolderEmulators", "Retro"):     glyph_emu_cartridge,
    ("FolderEmulators", "Cards"):     glyph_emu_tarot,
    ("FolderEmulators", "Pastel"):    glyph_emu_heart_cassette,
    ("FolderEmulators", "Dark"):      glyph_emu_skull_disc,
    ("FolderEmulators", "Gradient"):  glyph_emu_vinyl,
    ("FolderEmulators", "Blueprint"): glyph_emu_circuit,
    ("FolderEmulators", "Pixel"):     glyph_emu_cartridge,

    # FolderTools — different tool archetypes per theme.
    ("FolderTools", "Glass"):     glyph_tools_wrench_smooth,
    ("FolderTools", "Neon"):      glyph_tools_bolt,
    ("FolderTools", "Minimal"):   glyph_tools_hammer_line,
    ("FolderTools", "Retro"):     glyph_tools_pickaxe,
    ("FolderTools", "Cards"):     glyph_tools_dice,
    ("FolderTools", "Pastel"):    glyph_tools_paintbrush,
    ("FolderTools", "Dark"):      glyph_tools_axe,
    ("FolderTools", "Gradient"):  glyph_tools_brushstroke,
    ("FolderTools", "Blueprint"): glyph_tools_schematic_wrench,
    ("FolderTools", "Pixel"):     glyph_tools_sword,

    # FolderSystem — control mechanisms per theme.
    ("FolderSystem", "Glass"):     glyph_system_gear_smooth,
    ("FolderSystem", "Neon"):      glyph_system_ring,
    ("FolderSystem", "Minimal"):   glyph_system_circ_dot,
    ("FolderSystem", "Retro"):     glyph_system_gear_smooth,
    ("FolderSystem", "Cards"):     glyph_system_gear_smooth,
    ("FolderSystem", "Pastel"):    glyph_system_flower,
    ("FolderSystem", "Dark"):      glyph_system_cog_chains,
    ("FolderSystem", "Gradient"):  glyph_system_orbital,
    ("FolderSystem", "Blueprint"): glyph_system_exploded_gear,
    ("FolderSystem", "Pixel"):     glyph_system_gear_smooth,

    # FolderQOS — Q OS brand mark in different idioms.  Q OS branding stays
    # (creator: "Q OS branding stays themed, not removed" for Q OS-related
    # icons), but each theme styles the Q differently.
    ("FolderQOS", "Glass"):     glyph_qos_q_smooth,
    ("FolderQOS", "Neon"):      glyph_qos_q_neon,
    ("FolderQOS", "Minimal"):   glyph_qos_q_line,
    ("FolderQOS", "Retro"):     glyph_qos_q_block,
    ("FolderQOS", "Cards"):     glyph_qos_q_smooth,
    ("FolderQOS", "Pastel"):    glyph_qos_q_smooth,
    ("FolderQOS", "Dark"):      glyph_qos_q_block,
    ("FolderQOS", "Gradient"):  glyph_qos_q_smooth,
    ("FolderQOS", "Blueprint"): glyph_qos_q_line,
    ("FolderQOS", "Pixel"):     glyph_qos_q_block,

    # FolderOther — "unknown" / "miscellaneous" symbols per theme.
    ("FolderOther", "Glass"):     glyph_other_question_smooth,
    ("FolderOther", "Neon"):      glyph_other_warning,
    ("FolderOther", "Minimal"):   glyph_other_dot,
    ("FolderOther", "Retro"):     glyph_other_qblock,
    ("FolderOther", "Cards"):     glyph_other_question_smooth,
    ("FolderOther", "Pastel"):    glyph_other_question_smooth,
    ("FolderOther", "Dark"):      glyph_other_warning,
    ("FolderOther", "Gradient"):  glyph_other_spiral,
    ("FolderOther", "Blueprint"): glyph_other_drafted_q,
    ("FolderOther", "Pixel"):     glyph_other_qblock,
}

# Per-theme dispatch for the hot-corner emblem.  Filename stays HotCornerQ.png
# (filesystem path is internal); content varies per theme.
EMBLEM_GLYPHS = {
    "Glass":     glyph_qos_q,           # Q OS Liquid Glass DEFAULT — keep the Q
    "Neon":      glyph_lightning_bolt,
    "Minimal":   glyph_minimal_dots,
    "Retro":     glyph_retro_wedge,
    "Cards":     glyph_card_spade,
    "Pastel":    glyph_heart,
    "Dark":      glyph_flame,
    "Gradient":  glyph_prism,
    "Blueprint": glyph_compass,
    "Pixel":     glyph_8bit_star,
}

def glyph_hot_corner_q(d, cx, cy, size, color, mode, sw):
    """Default delegate — only used as a fallback if EMBLEM_GLYPHS is missing
    an entry.  render_themed_glyph dispatches per-theme via EMBLEM_GLYPHS."""
    glyph_qos_q(d, cx, cy, size, color, mode, sw)

# ── Glyph registry ────────────────────────────────────────────────────────────

GLYPHS = {
    "DockVault":         glyph_vault,
    "DockMonitor":       glyph_monitor,
    "DockAbout":         glyph_about,
    "DockAllPrograms":   glyph_all_programs,
    "DockControl":       glyph_control,
    "DockTasks":         glyph_tasks,
    "Folder":            glyph_folder,
    "DefaultApplication":glyph_default_app,
    "DefaultHomebrew":   glyph_default_homebrew,
    "Empty":             glyph_empty,
    # Desktop folder categories.  FolderQOS keeps the Q glyph because it is the
    # Q OS folder (Q OS branding stays themed, not removed).
    "FolderGames":       glyph_games_controller,
    "FolderEmulators":   glyph_emulators_joystick,
    "FolderTools":       glyph_tools_wrench,
    "FolderSystem":      glyph_system_gear,
    "FolderQOS":         glyph_qos_q,
    "FolderOther":       glyph_other_question,
    # Hot corner — registry entry is a fallback; render_themed_glyph routes
    # through EMBLEM_GLYPHS[theme_name] for the actual per-theme emblem.
    "HotCornerQ":        glyph_hot_corner_q,
}

# ═════════════════════════════════════════════════════════════════════════════
# RENDER STYLES — per theme, applies post-processing to the canonical glyph.
# ═════════════════════════════════════════════════════════════════════════════

THEME_STYLES = {
    "Glass":     "FILL_SMOOTH",
    "Neon":      "OUTLINE_GLOW",
    "Minimal":   "LINE_THIN",
    "Retro":     "PIXEL_LARGE",
    "Cards":     "FILL_SMOOTH",
    "Pastel":    "FILL_SOFT",
    "Dark":      "FILL_EDGE",
    "Gradient":  "FILL_GRADIENT",
    "Blueprint": "LINE_TECH",
    "Pixel":     "PIXEL_FINE",
}

def glyph_color(theme_name, palette):
    """Color the glyph should render in for the given theme.

    Runtime constraint: PaintIconCell paints the tile background as 0x3A dark
    gray (qd_DesktopIcons.cpp), so the glyph MUST be bright enough to read on
    that background.  Every palette's `accent` is designed to contrast with
    the theme's dark surface, so accent is always a safe high-contrast choice.

    Blueprint is the exception — its accent is light cyan, but the technical-
    drawing style wants pure white strokes to read as "blueprint paper".
    """
    accent = hex_to_rgb(palette["accent"])
    if theme_name == "Blueprint":
        return (255, 255, 255)
    return accent

def render_themed_glyph(role, theme_name, palette):
    """Top-level: return an RGBA 192×192 image of the role's glyph rendered
       in the theme's signature style.  Transparent everywhere except the
       glyph itself.

       For role=HotCornerQ the glyph dispatches per-theme through
       EMBLEM_GLYPHS — Glass is the Q, every other theme gets its own
       distinctive identity emblem (NOT a Q), per the 2026-05-18 creator
       directive.  Filename stays HotCornerQ.png; content varies."""
    style = THEME_STYLES[theme_name]
    if role == "HotCornerQ":
        glyph_fn = EMBLEM_GLYPHS.get(theme_name, glyph_qos_q)
    else:
        # v2.9.8 — per-theme dispatch for the 6 desktop folder categories.
        # Each (role, theme) entry uses a DIFFERENT base shape (NES pad for
        # Retro Games vs ergo controller for Glass Games), creating real
        # icon-pack differentiation beyond the post-process style.
        glyph_fn = PER_THEME_GLYPHS.get((role, theme_name)) or GLYPHS[role]
    color = glyph_color(theme_name, palette)
    GLYPH_SIZE = int(W * 0.62)  # ~119 px nominal glyph bounding box at 192² canvas

    if style == "FILL_SMOOTH":
        img = Image.new("RGBA", (W, H), (0,0,0,0))
        d = ImageDraw.Draw(img)
        glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(color, 240), "FILL", sw=0)
        return img

    if style == "FILL_SOFT":
        # Render at 2x for clean downsample → fluffy edges.
        big = Image.new("RGBA", (W*2, H*2), (0,0,0,0))
        d = ImageDraw.Draw(big)
        glyph_fn(d, W, H, GLYPH_SIZE * 2, rgba(color, 240), "FILL", sw=0)
        img = big.resize((W, H), Image.LANCZOS)
        # Soft blur for plushness.
        img = img.filter(ImageFilter.GaussianBlur(radius=1.0))
        return img

    if style == "FILL_EDGE":
        # Filled glyph in darkened color + accent edge highlight.
        # NOTE: glyphs use fill=(0,0,0,0) for cutouts (e.g. the "i" in DockAbout
        # or the inner hole in DefaultApplication), so we MUST draw on an RGBA
        # canvas — drawing onto an L-mode mask blows up.  Extract the alpha
        # channel after rendering to get a true binary mask of the visible glyph.
        bg_glyph_color = darken(color, 0.6)
        img = Image.new("RGBA", (W, H), (0,0,0,0))
        d = ImageDraw.Draw(img)
        glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(bg_glyph_color, 240), "FILL", sw=0)
        # Use the alpha channel of the rendered glyph as the FIND_EDGES source.
        mask = img.split()[3]
        edges = mask.filter(ImageFilter.FIND_EDGES)
        # Dilate slightly.
        edges = edges.filter(ImageFilter.MaxFilter(3))
        edge_layer = Image.new("RGBA", (W, H), color + (0,))
        edge_layer.putalpha(edges)
        img.alpha_composite(edge_layer)
        return img

    if style == "FILL_GRADIENT":
        # Render glyph onto RGBA, then use its alpha channel as the gradient mask.
        # (Drawing onto an L-mode mask breaks because of (0,0,0,0) cutouts.)
        glyph_img = Image.new("RGBA", (W, H), (0,0,0,0))
        gd0 = ImageDraw.Draw(glyph_img)
        glyph_fn(gd0, W // 2, H // 2, GLYPH_SIZE, rgba(color, 255), "FILL", sw=0)
        mask = glyph_img.split()[3]
        # Gradient column.
        bg = hex_to_rgb(palette["surface_glass"])
        accent = hex_to_rgb(palette["accent"])
        # Top = surface_glass; bottom = accent.
        grad_img = Image.new("RGBA", (W, H), (0,0,0,0))
        gd = ImageDraw.Draw(grad_img)
        for row in range(H):
            t = row / H
            c = mix(bg, accent, t)
            gd.line((0, row, W, row), fill=c + (255,))
        # Apply mask.
        out = Image.new("RGBA", (W, H), (0,0,0,0))
        out.paste(grad_img, (0,0), mask)
        return out

    if style == "OUTLINE_GLOW":
        # Render OUTLINE-only at 4 px stroke, then 3-layer gaussian glow.
        # 1. Outline image (sharp).
        outline_img = Image.new("RGBA", (W, H), (0,0,0,0))
        d = ImageDraw.Draw(outline_img)
        glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(color, 255), "OUTLINE", sw=5)
        # 2. Layered glow: progressively blurred copies.
        glow = Image.new("RGBA", (W, H), (0,0,0,0))
        for blur_r, alpha_mul in [(8, 0.20), (4, 0.35), (2, 0.55)]:
            layer = outline_img.filter(ImageFilter.GaussianBlur(radius=blur_r))
            # Reduce alpha for outer layers.
            a = layer.split()[3]
            a = a.point(lambda p: int(p * alpha_mul))
            layer.putalpha(a)
            glow.alpha_composite(layer)
        # 3. Composite glow + sharp outline.
        final = Image.new("RGBA", (W, H), (0,0,0,0))
        final.alpha_composite(glow)
        final.alpha_composite(outline_img)
        return final

    if style == "LINE_THIN":
        # 3px outline only.
        img = Image.new("RGBA", (W, H), (0,0,0,0))
        d = ImageDraw.Draw(img)
        glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(color, 255), "OUTLINE", sw=3)
        return img

    if style == "LINE_TECH":
        # 2px white outline (blueprint technical drawing).
        img = Image.new("RGBA", (W, H), (0,0,0,0))
        d = ImageDraw.Draw(img)
        glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(color, 230), "OUTLINE", sw=2)
        return img

    if style == "PIXEL_LARGE":
        # Render at high res, downscale to 24×24, upscale 192×192 NEAREST.
        big = Image.new("RGBA", (W*4, H*4), (0,0,0,0))
        d = ImageDraw.Draw(big)
        glyph_fn(d, W*2, H*2, GLYPH_SIZE * 4, rgba(color, 255), "FILL", sw=0)
        small = big.resize((24, 24), Image.LANCZOS)
        # Quantize alpha to binary (full or none) for crisp pixel art.
        a = small.split()[3].point(lambda v: 255 if v > 96 else 0)
        small.putalpha(a)
        return small.resize((W, H), Image.NEAREST)

    if style == "PIXEL_FINE":
        # Render at high res, downscale to 16×16, upscale NEAREST.
        big = Image.new("RGBA", (W*4, H*4), (0,0,0,0))
        d = ImageDraw.Draw(big)
        glyph_fn(d, W*2, H*2, GLYPH_SIZE * 4, rgba(color, 255), "FILL", sw=0)
        small = big.resize((16, 16), Image.LANCZOS)
        a = small.split()[3].point(lambda v: 255 if v > 96 else 0)
        small.putalpha(a)
        return small.resize((W, H), Image.NEAREST)

    # Fallback — simple FILL_SMOOTH.
    img = Image.new("RGBA", (W, H), (0,0,0,0))
    d = ImageDraw.Draw(img)
    glyph_fn(d, W // 2, H // 2, GLYPH_SIZE, rgba(color, 240), "FILL", sw=0)
    return img

# ═════════════════════════════════════════════════════════════════════════════
# Main
# ═════════════════════════════════════════════════════════════════════════════

def main():
    print(f"Q OS theme glyph generator (v3 — no frames, per-theme styles)")
    print(f"10 themes × {len(ALL_ROLES)} roles = {10 * len(ALL_ROLES)} PNGs at {W}×{H}")
    print()

    os.makedirs(DEFAULT_DIR, exist_ok=True)

    for theme_part, theme_name in THEMES:
        palette = load_palette(theme_part)
        rendered = {}
        for role in ALL_ROLES:
            png = render_themed_glyph(role, theme_name, palette)
            buf = BytesIO()
            png.save(buf, "PNG", optimize=True)
            rendered[role] = buf.getvalue()

        total_bytes = sum(len(v) for v in rendered.values())

        if theme_name == "Glass":
            for role, png_bytes in rendered.items():
                target = os.path.join(DEFAULT_DIR, role + ".png")
                with open(target, "wb") as f:
                    f.write(png_bytes)
            print(f"  default/   ({theme_name:9s})  {len(rendered)} PNGs  {total_bytes:6d} B total")
        else:
            ultheme = os.path.join(THEMES_DIR, theme_part + ".ultheme")
            # Asset-preservation: keep EVERY existing entry that we aren't about to
            # replace.  Only the EntryIcon PNGs for roles in ALL_ROLES get
            # overwritten — any future sound/UI assets in the bundle stay intact.
            replace_paths = {f"ui/Main/EntryIcon/{r}.png" for r in ALL_ROLES}
            existing = {}
            with zipfile.ZipFile(ultheme, "r") as z:
                for name in z.namelist():
                    if name in replace_paths:
                        continue  # we're replacing this with the fresh render
                    existing[name] = z.read(name)
            for role, png_bytes in rendered.items():
                existing[f"ui/Main/EntryIcon/{role}.png"] = png_bytes
            with zipfile.ZipFile(ultheme, "w", compression=zipfile.ZIP_DEFLATED) as z:
                for name, data in existing.items():
                    z.writestr(name, data)
            size = os.path.getsize(ultheme)
            print(f"  {theme_part:18s}.ultheme  ({theme_name:9s})  {len(rendered)} PNGs  {size:7d} B")

    print()
    print("Done. Next: rebuild romfs.bin (make umenu)")

if __name__ == "__main__":
    main()
