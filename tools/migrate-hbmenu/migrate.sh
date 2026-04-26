#!/usr/bin/env bash
# migrate.sh — Remove standalone HBMenu surface from a Q OS Switch SD card.
#
# Q OS replaces HBMenu's NRO-launcher UI with the vault file browser.
# This script removes only the HBMenu user-surface assets; it never touches
# hbloader (TID 010000000000100D), which is required to execute any NRO.
#
# Usage:
#   migrate.sh <sd-card-path>          # dry-run: show what WILL be removed
#   migrate.sh <sd-card-path> --yes    # execute removal
#
# Exit codes:
#   0  success (dry-run preview, or removal completed)
#   1  validation failure
#   2  partial removal error (one or more delete operations failed)

set -euo pipefail

###############################################################################
# Helpers
###############################################################################

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'WARN:  %s\n' "$*" >&2
}

info() {
    printf '  %s\n' "$*"
}

# Print a human-readable size for a path (file or directory).
human_size() {
    local path="$1"
    if command -v du >/dev/null 2>&1; then
        # -s = summarise, -k = kilobytes; pipe to awk to convert to MB or KB
        local kbytes
        kbytes=$(du -sk "$path" 2>/dev/null | awk '{print $1}')
        if [[ -z "$kbytes" || "$kbytes" -eq 0 ]]; then
            printf '0 B'
        elif [[ "$kbytes" -ge 1024 ]]; then
            awk -v k="$kbytes" 'BEGIN { printf "%.1f MB", k/1024 }'
        else
            printf '%s KB' "$kbytes"
        fi
    else
        printf '(unknown size)'
    fi
}

# Return raw byte count for a path; 0 if not present.
bytes_for() {
    local path="$1"
    if [[ ! -e "$path" ]]; then
        printf '0'
        return
    fi
    if command -v du >/dev/null 2>&1; then
        # macOS du -s gives 512-byte blocks by default; use -k for kilobytes
        # then multiply.  On Linux du -sb gives bytes directly.
        if du -sb "$path" >/dev/null 2>&1; then
            du -sb "$path" 2>/dev/null | awk '{print $1}'
        else
            # macOS fallback: -k gives KiB blocks
            du -sk "$path" 2>/dev/null | awk '{print $1 * 1024}'
        fi
    else
        printf '0'
    fi
}

###############################################################################
# Argument parsing
###############################################################################

SD_PATH=""
EXECUTE=0

for arg in "$@"; do
    case "$arg" in
        --yes) EXECUTE=1 ;;
        --*)   die "Unknown flag: $arg" ;;
        *)
            if [[ -z "$SD_PATH" ]]; then
                SD_PATH="$arg"
            else
                die "Unexpected argument: $arg (SD path already set to '$SD_PATH')"
            fi
            ;;
    esac
done

[[ -n "$SD_PATH" ]] || die "Usage: $0 <sd-card-path> [--yes]"

###############################################################################
# Validation — confirm this looks like a Switch SD card
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
  Expected atmosphere/, bootloader/, and switch/ at the root of the SD card.
  Check that '$SD_PATH' is the correct mount point."
fi

printf '  atmosphere/ ... OK\n'
printf '  bootloader/ ... OK\n'
printf '  switch/     ... OK\n\n'

###############################################################################
# Build the removal list
###############################################################################

# Target 1: /hbmenu.nro at SD root
HBMENU_NRO="$SD_PATH/hbmenu.nro"

# Target 2: /switch/hbmenu/ directory (themes + language assets)
HBMENU_SWITCH_DIR="$SD_PATH/switch/hbmenu"

# Target 3: rare custom-hbmenu Album-applet override.
# Some setups place a patched hbmenu as main.nso under a custom title ID
# (NOT the canonical hbloader TID 010000000000100D).
# We scan atmosphere/contents/ for any TID that is NOT 010000000000100D
# and has an exefs/main.nso that we can attribute to hbmenu.
# Because there is no reliable way to identify an arbitrary NSO as hbmenu,
# we target only the well-known community TID used for custom hbmenu builds.
# The canonical hbmenu override TID documented by SwitchBrew / nx-hbmenu
# community releases is 010000000000100B (the Flog / Hbmenu fake title).
HBMENU_CUSTOM_TID="010000000000100B"
HBMENU_CUSTOM_NSO="$SD_PATH/atmosphere/contents/$HBMENU_CUSTOM_TID/exefs/main.nso"

# Guard: never touch hbloader's TID.
HBLOADER_TID="010000000000100D"
# Sanity-check: if the user's SD has hbloader in its canonical path, confirm
# we are NOT targeting it.
HBLOADER_GUARD="$SD_PATH/atmosphere/contents/$HBLOADER_TID/exefs/main.nso"

printf '%-72s\n' "$(printf '%0.s-' {1..72})"
printf 'REMOVAL CANDIDATES\n'
printf '%-72s\n' "$(printf '%0.s-' {1..72})"

TOTAL_BYTES=0
FOUND_ANY=0

if [[ -f "$HBMENU_NRO" ]]; then
    SZ=$(human_size "$HBMENU_NRO")
    RAW=$(bytes_for "$HBMENU_NRO")
    printf '  [REMOVE]  %s  (%s)\n' "$HBMENU_NRO" "$SZ"
    TOTAL_BYTES=$(( TOTAL_BYTES + RAW ))
    FOUND_ANY=1
else
    printf '  [skip]    %s  (not present)\n' "$HBMENU_NRO"
fi

if [[ -d "$HBMENU_SWITCH_DIR" ]]; then
    SZ=$(human_size "$HBMENU_SWITCH_DIR")
    RAW=$(bytes_for "$HBMENU_SWITCH_DIR")
    printf '  [REMOVE]  %s/  (%s)\n' "$HBMENU_SWITCH_DIR" "$SZ"
    TOTAL_BYTES=$(( TOTAL_BYTES + RAW ))
    FOUND_ANY=1
else
    printf '  [skip]    %s/  (not present)\n' "$HBMENU_SWITCH_DIR"
fi

if [[ -f "$HBMENU_CUSTOM_NSO" ]]; then
    SZ=$(human_size "$HBMENU_CUSTOM_NSO")
    RAW=$(bytes_for "$HBMENU_CUSTOM_NSO")
    printf '  [REMOVE]  %s  (%s)\n' "$HBMENU_CUSTOM_NSO" "$SZ"
    TOTAL_BYTES=$(( TOTAL_BYTES + RAW ))
    FOUND_ANY=1
else
    printf '  [skip]    %s  (not present)\n' "$HBMENU_CUSTOM_NSO"
fi

printf '\n'

# Print the "will NOT touch" guard line so operators can verify
printf '%-72s\n' "$(printf '%0.s-' {1..72})"
printf 'PROTECTED (NOT TOUCHED)\n'
printf '%-72s\n' "$(printf '%0.s-' {1..72})"
if [[ -f "$HBLOADER_GUARD" ]]; then
    printf '  [KEEP]    %s  (hbloader — required)\n' "$HBLOADER_GUARD"
else
    printf '  [n/a]     %s  (hbloader not present at canonical path — that is fine)\n' "$HBLOADER_GUARD"
fi
printf '\n'

if [[ "$TOTAL_BYTES" -gt 0 ]]; then
    TOTAL_MB=$(awk -v b="$TOTAL_BYTES" 'BEGIN { printf "%.1f", b/1048576 }')
    printf 'Total to reclaim: %s MB (%d bytes)\n\n' "$TOTAL_MB" "$TOTAL_BYTES"
else
    printf 'Total to reclaim: 0 bytes\n\n'
fi

###############################################################################
# Dry-run gate
###############################################################################

if [[ "$EXECUTE" -eq 0 ]]; then
    printf 'Dry run complete. No files were changed.\n'
    printf 'Run with --yes to execute the removal.\n\n'
    exit 0
fi

###############################################################################
# Execute removal
###############################################################################

if [[ "$FOUND_ANY" -eq 0 ]]; then
    printf 'Nothing to remove. SD card is already clean.\n'
    exit 0
fi

printf '%-72s\n' "$(printf '%0.s-' {1..72})"
printf 'REMOVING\n'
printf '%-72s\n' "$(printf '%0.s-' {1..72})"

ERRORS=0
REMOVED=()

remove_file() {
    local path="$1"
    if rm -f "$path"; then
        REMOVED+=("$path")
        printf '  deleted   %s\n' "$path"
    else
        warn "Failed to delete: $path"
        ERRORS=$(( ERRORS + 1 ))
    fi
}

remove_dir() {
    local path="$1"
    if rm -rf "$path"; then
        REMOVED+=("$path/")
        printf '  deleted   %s/\n' "$path"
    else
        warn "Failed to delete directory: $path"
        ERRORS=$(( ERRORS + 1 ))
    fi
}

[[ -f "$HBMENU_NRO" ]]         && remove_file "$HBMENU_NRO"
[[ -d "$HBMENU_SWITCH_DIR" ]]  && remove_dir  "$HBMENU_SWITCH_DIR"
[[ -f "$HBMENU_CUSTOM_NSO" ]]  && remove_file "$HBMENU_CUSTOM_NSO"

printf '\n'

###############################################################################
# Write migration log
###############################################################################

LOG_DIR="$SD_PATH/qos-shell/migrations"
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
LOG_FILE="$LOG_DIR/hbmenu-$TIMESTAMP.log"

mkdir -p "$LOG_DIR" || warn "Could not create log directory: $LOG_DIR"

if [[ -d "$LOG_DIR" ]]; then
    {
        printf 'Q OS HBMenu Migration Log\n'
        printf 'Timestamp:    %s\n' "$(date '+%Y-%m-%dT%H:%M:%S')"
        printf 'SD card path: %s\n' "$SD_PATH"
        printf 'Script:       %s\n' "$0"
        printf '\n'
        printf 'Files removed:\n'
        if [[ ${#REMOVED[@]} -gt 0 ]]; then
            for item in "${REMOVED[@]}"; do
                printf '  %s\n' "$item"
            done
        else
            printf '  (none)\n'
        fi
        printf '\n'
        if [[ "$TOTAL_BYTES" -gt 0 ]]; then
            TOTAL_MB=$(awk -v b="$TOTAL_BYTES" 'BEGIN { printf "%.1f", b/1048576 }')
            printf 'Bytes reclaimed: %d (~%s MB)\n' "$TOTAL_BYTES" "$TOTAL_MB"
        else
            printf 'Bytes reclaimed: 0\n'
        fi
        printf '\n'
        printf 'Protected (not touched):\n'
        printf '  %s  (hbloader TID %s)\n' "$HBLOADER_GUARD" "$HBLOADER_TID"
        printf '\n'
        if [[ "$ERRORS" -gt 0 ]]; then
            printf 'Errors: %d (see stderr output above)\n' "$ERRORS"
        else
            printf 'Errors: none\n'
        fi
    } > "$LOG_FILE" 2>&1
    printf 'Migration log: %s\n\n' "$LOG_FILE"
else
    warn "Skipping log write — could not create $LOG_DIR"
fi

###############################################################################
# Final report
###############################################################################

if [[ "$ERRORS" -gt 0 ]]; then
    printf 'Completed with %d error(s). Check warnings above.\n' "$ERRORS" >&2
    exit 2
fi

if [[ "$TOTAL_BYTES" -gt 0 ]]; then
    TOTAL_MB=$(awk -v b="$TOTAL_BYTES" 'BEGIN { printf "%.1f", b/1048576 }')
    printf 'Done. Reclaimed %s MB (%d bytes).\n' "$TOTAL_MB" "$TOTAL_BYTES"
else
    printf 'Done. No files were present to remove.\n'
fi

exit 0
