#!/usr/bin/env bash
# restore.sh — Re-install HBMenu onto a Switch SD card.
#
# Downloads the latest nx-hbmenu release from GitHub, unpacks it, and places
# the files back at their canonical locations on the SD card.
#
# This is a one-way restore: it does not undo the Q OS vault integration, but
# it does put the HBMenu files back so HBMenu can run alongside (or instead of)
# the vault surface.
#
# Usage:
#   restore.sh <sd-card-path>
#
# Exit codes:
#   0  success
#   1  validation or network failure
#   2  install error

set -euo pipefail

###############################################################################
# Helpers
###############################################################################

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '  %s\n' "$*"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

###############################################################################
# Argument parsing
###############################################################################

[[ $# -eq 1 && -n "$1" ]] || die "Usage: $0 <sd-card-path>"

SD_PATH="$1"

###############################################################################
# Validation
###############################################################################

[[ -d "$SD_PATH" ]] || die "Path does not exist or is not a directory: $SD_PATH"

printf '\nValidating SD card at: %s\n' "$SD_PATH"

MISSING_DIRS=()
for dir in atmosphere bootloader switch; do
    if [[ ! -d "$SD_PATH/$dir" ]]; then
        MISSING_DIRS+=("$dir")
    fi
done

if [[ ${#MISSING_DIRS[@]} -gt 0 ]]; then
    die "Missing expected Switch directories: ${MISSING_DIRS[*]}
  Expected atmosphere/, bootloader/, and switch/ at the root of the SD card."
fi

printf '  atmosphere/ ... OK\n'
printf '  bootloader/ ... OK\n'
printf '  switch/     ... OK\n\n'

###############################################################################
# Dependency check
###############################################################################

require_cmd curl
require_cmd unzip

###############################################################################
# Resolve the latest release download URL
###############################################################################

RELEASES_URL="https://api.github.com/repos/switchbrew/nx-hbmenu/releases/latest"

printf 'Fetching latest release metadata from GitHub...\n'

RELEASE_JSON=$(curl -fsSL "$RELEASES_URL") \
    || die "Failed to fetch release metadata from $RELEASES_URL"

# Extract the browser_download_url for the .zip asset.
# The release asset is typically named "hbmenu.zip" or similar.
DOWNLOAD_URL=$(printf '%s' "$RELEASE_JSON" \
    | grep '"browser_download_url"' \
    | grep -i '\.zip"' \
    | head -1 \
    | sed 's/.*"browser_download_url": *"\([^"]*\)".*/\1/')

[[ -n "$DOWNLOAD_URL" ]] || die "Could not parse a .zip download URL from the release JSON.
  Check https://github.com/switchbrew/nx-hbmenu/releases/latest manually."

TAG=$(printf '%s' "$RELEASE_JSON" \
    | grep '"tag_name"' \
    | head -1 \
    | sed 's/.*"tag_name": *"\([^"]*\)".*/\1/')

printf '  Release tag:   %s\n' "$TAG"
printf '  Download URL:  %s\n\n' "$DOWNLOAD_URL"

###############################################################################
# Download and unpack
###############################################################################

WORK_DIR=$(mktemp -d)
# Ensure cleanup on any exit.
cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

ZIP_FILE="$WORK_DIR/hbmenu.zip"

printf 'Downloading %s...\n' "$DOWNLOAD_URL"
curl -fsSL -o "$ZIP_FILE" "$DOWNLOAD_URL" \
    || die "Download failed: $DOWNLOAD_URL"

printf 'Unpacking...\n'
unzip -q "$ZIP_FILE" -d "$WORK_DIR/unpacked" \
    || die "Failed to unpack $ZIP_FILE"

###############################################################################
# Locate key files in the unpacked archive
###############################################################################

# Canonical layout expected in the release zip:
#   hbmenu.nro                      (root NRO)
#   switch/hbmenu/                  (themes + language assets, optional)
#
# We search rather than assume a fixed layout to tolerate minor release changes.

UNPACKED_ROOT="$WORK_DIR/unpacked"

find_file_in_unpacked() {
    local name="$1"
    find "$UNPACKED_ROOT" -name "$name" -type f 2>/dev/null | head -1
}

UNPACKED_NRO=$(find_file_in_unpacked "hbmenu.nro")
UNPACKED_SWITCH_DIR=$(find "$UNPACKED_ROOT" -type d -name "hbmenu" 2>/dev/null | head -1)

###############################################################################
# Install
###############################################################################

printf '\nInstalling to %s...\n' "$SD_PATH"

ERRORS=0

if [[ -n "$UNPACKED_NRO" ]]; then
    cp "$UNPACKED_NRO" "$SD_PATH/hbmenu.nro" \
        && info "installed: $SD_PATH/hbmenu.nro" \
        || { printf 'WARN:  Failed to copy hbmenu.nro\n' >&2; ERRORS=$(( ERRORS + 1 )); }
else
    printf 'WARN:  hbmenu.nro not found in archive — skipping.\n' >&2
    ERRORS=$(( ERRORS + 1 ))
fi

if [[ -n "$UNPACKED_SWITCH_DIR" && -d "$UNPACKED_SWITCH_DIR" ]]; then
    DEST_SWITCH_DIR="$SD_PATH/switch/hbmenu"
    rm -rf "$DEST_SWITCH_DIR"
    cp -r "$UNPACKED_SWITCH_DIR" "$DEST_SWITCH_DIR" \
        && info "installed: $DEST_SWITCH_DIR/" \
        || { printf 'WARN:  Failed to copy switch/hbmenu/ assets\n' >&2; ERRORS=$(( ERRORS + 1 )); }
else
    info "switch/hbmenu/ not present in archive (normal for some releases) — skipping."
fi

printf '\n'

###############################################################################
# Final report
###############################################################################

if [[ "$ERRORS" -gt 0 ]]; then
    printf 'Restore completed with %d error(s). Check warnings above.\n' "$ERRORS" >&2
    exit 2
fi

printf 'Done. HBMenu %s installed to %s\n' "$TAG" "$SD_PATH"
printf '\nNote: The Q OS vault remains active. HBMenu will appear on next boot\n'
printf 'if you launch it via Album. Both surfaces can coexist.\n\n'

exit 0
