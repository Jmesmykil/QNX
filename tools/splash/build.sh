#!/usr/bin/env bash
# tools/splash/build.sh — Q OS branded splash assets
#
# Generates the three splash assets specified in docs/43_Splash_Replacement_Research.md:
#   1. qos_atmos_splash_1280x720.png        — Atmosphère splash source
#   2. qos_hekate_bootlogo_720x1280.bmp     — Hekate bootloader logo (portrait)
#   3. (Nintendo logo IPS — see Phase 2; out of scope here)
#
# Then `tools/splash/install.sh` deploys them to the SD card.
#
# Requires: ImageMagick (`brew install imagemagick`).
# Assets are placeholder Q OS branding generated programmatically; replace with
# higher-quality artwork before public release.

set -euo pipefail

cd "$(dirname "$0")"

# Brand palette (kept in sync with docs/45_HBMenu_Replacement_Design.md and
# qd_Theme.hpp comments).
readonly BG_TOP='#0a0817'
readonly BG_BOTTOM='#1a0a2a'
readonly BRAND_CYAN='#00E5FF'
readonly BRAND_LAVENDER='#A78BFA'
readonly TEXT_DIM='#5A5670'

# Font selection — Arial Bold from macOS Supplemental fonts. Not bundled with
# Homebrew ImageMagick; the absolute path avoids the "unable to read font"
# regression we hit on first attempt.
readonly FONT_BOLD='/System/Library/Fonts/Supplemental/Arial Bold.ttf'
readonly FONT_REG='/System/Library/Fonts/Supplemental/Arial.ttf'

readonly OUT_PNG='qos_atmos_splash_1280x720.png'
readonly OUT_BMP='qos_hekate_bootlogo_720x1280.bmp'

echo "==> Generating Atmosphère splash (1280×720 PNG)..."
magick -size 1280x720 \
    "gradient:${BG_TOP}-${BG_BOTTOM}" \
    -font "${FONT_BOLD}" -pointsize 200 -fill "${BRAND_CYAN}" \
    -gravity center -annotate +0-30 'Q OS' \
    -font "${FONT_REG}" -pointsize 36 -fill "${BRAND_LAVENDER}" \
    -gravity center -annotate +0+90 'operating system' \
    -font "${FONT_REG}" -pointsize 16 -fill "${TEXT_DIM}" \
    -gravity southeast -annotate +24+24 'phase 1 — applet preview' \
    "${OUT_PNG}"

echo "==> Generating Hekate bootlogo (720×1280 BMP, rotated -90°)..."
magick "${OUT_PNG}" -rotate -90 \
    -define bmp:format=bmp4 -depth 8 \
    "BMP3:${OUT_BMP}"

echo "==> Output:"
ls -la "${OUT_PNG}" "${OUT_BMP}"
file "${OUT_BMP}"

echo ""
echo "Next step:"
echo "  • Atmosphère: run insert_splash_screen.py against package3 with ${OUT_PNG}."
echo "    See docs/43_Splash_Replacement_Research.md §1 for the exact procedure."
echo "  • Hekate: copy ${OUT_BMP} to SD:/bootloader/bootlogo.bmp."
echo "  • Nintendo logo: out of scope for this script (Phase 2 — switch-logo-patcher IPS)."
