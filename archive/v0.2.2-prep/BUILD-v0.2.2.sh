#!/usr/bin/env bash
# BUILD-v0.2.2.sh — Q OS uLaunch fork v0.2.2 build script
# v0.2.2 is an assets-only patch on top of v0.2.1.
# All Q OS visual assets are already in-place in default-theme/ and
# projects/uMenu/romfs/default/ — this script is a thin version-label
# wrapper around BUILD-v0.2.1.sh.
#
# Changes from v0.2.1:
#   - Q OS visual branding: UI.json colour tokens + 13 PNG replacements
#   - en.json: 7 user-visible "uLaunch" → "Q OS" string substitutions
#   - Logo.png: "Q OS / universal os" wordmark (256x256)
#   - romfs/PATCH-NOTES.md: added GPLv2 attribution + asset-swap log
#   - NO Plutonium C++ code changes (those are in v0.3.0)
#
# Copyright (c) XorTroll (upstream uLaunch, GPLv2).
# Q OS build scripts: Q OS project, GPLv2.
#
# DO NOT run with sudo. DO NOT touch /Volumes/SWITCH SD.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V021_BUILD="${SCRIPT_DIR}/../v0.2.1-prep/BUILD-v0.2.1.sh"

if [[ ! -f "${V021_BUILD}" ]]; then
    echo "[ERROR] v0.2.1 build script not found at: ${V021_BUILD}" >&2
    exit 1
fi

echo "[BUILD] uMenu v0.2.2 — Q OS visual branding (assets-only)"
echo "[BUILD] Delegating to BUILD-v0.2.1.sh (same toolchain, same romfs source)"
echo "[BUILD] Note: assets already patched in-place in default-theme/ and romfs/default/"
echo ""

bash "${V021_BUILD}"

echo ""
echo "[BUILD] v0.2.2 label: done."
echo "[BUILD] romfs.bin and exefs.nsp are the v0.2.2 artifacts — SHA values"
echo "[BUILD] printed above by v0.2.1 build script are authoritative."
echo ""
echo "[BUILD] Next: run STAGE-TO-SD.sh (from v0.2.1-prep/) to push to switch SD."
echo "[BUILD] Creator handles SD deployment. Do NOT touch /Volumes/SWITCH SD here."
