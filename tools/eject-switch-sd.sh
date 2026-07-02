#!/usr/bin/env bash
# eject-switch-sd.sh — clean-eject the Switch SD when UMS refuses to unmount.
#
# macOS Spotlight + fseventsd love to grab handles on freshly-mounted
# Windows_FAT_32 volumes ("unload attempt prevented" message in UMS console).
# This script:
#   1. Confirms the SD is actually a Switch SD (atmosphere/ + switch/ at root)
#      so we never force-unmount an unrelated volume by accident.
#   2. Re-drops the Spotlight/fseventsd opt-out sentinels (idempotent — they
#      persist on the FAT partition; only re-touched if missing).
#   3. Syncs.
#   4. Force-unmounts.
#
# Usage:
#   tools/eject-switch-sd.sh            # default mount path /Volumes/SWITCH SD
#   tools/eject-switch-sd.sh "/Volumes/MY SD"
#
# Exit codes:
#   0  success (already ejected, or just ejected)
#   1  path doesn't look like a Switch SD
#   2  unmount failed after retry

set -uo pipefail

SD="${1:-/Volumes/SWITCH SD}"

if [[ ! -d "$SD" ]]; then
    echo "  ✓ $SD is not mounted — nothing to do"
    exit 0
fi

# Safety check — refuse to eject anything that doesn't look like a Switch SD.
if [[ ! -d "$SD/atmosphere" || ! -d "$SD/switch" ]]; then
    echo "  ✗ '$SD' does not look like a Switch SD (no atmosphere/ + switch/ at root)" >&2
    echo "    Refusing to force-unmount to avoid hitting an unrelated volume." >&2
    exit 1
fi

# Refresh the Spotlight/fseventsd opt-out sentinels if anything's missing.
# These persist on the FAT partition — the Switch ignores them, macOS reads
# them every mount.
for sentinel in ".metadata_never_index" ".metadata_never_index_unless_rootfs"; do
    if [[ ! -f "$SD/$sentinel" ]]; then
        touch "$SD/$sentinel" 2>/dev/null && echo "  + dropped sentinel: $sentinel"
    fi
done
mkdir -p "$SD/.fseventsd" 2>/dev/null
[[ -f "$SD/.fseventsd/no_log" ]] || { touch "$SD/.fseventsd/no_log" 2>/dev/null && echo "  + dropped sentinel: .fseventsd/no_log"; }

# Make sure Spotlight is OFF (idempotent; no-op if already off).
mdutil -i off "$SD" >/dev/null 2>&1 && echo "  - Spotlight indexing: OFF"

sync

# Try a polite unmount first.
if diskutil unmount "$SD" >/dev/null 2>&1; then
    echo "  ✓ unmounted cleanly"
    exit 0
fi

# Fall back to force.
sleep 1
if diskutil unmount force "$SD" >/dev/null 2>&1; then
    echo "  ✓ force-unmounted"
    exit 0
fi

# One more retry after a longer wait — Spotlight sometimes releases late.
sleep 3
sync
if diskutil unmount force "$SD" >/dev/null 2>&1; then
    echo "  ✓ force-unmounted (retry)"
    exit 0
fi

echo "  ✗ unmount failed even after retry" >&2
echo "    Open processes (lsof):" >&2
lsof "$SD" 2>&1 | head -10 >&2
exit 2
