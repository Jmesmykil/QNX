#!/usr/bin/env bash
# Q OS uMenu v0.2.1 — Full SD staging
# ====================================
# Run this AFTER BUILD-v0.2.1.sh + DEPLOY-v0.2.1.sh have produced SdOut/
# and the Switch SD is mounted via UMS at /Volumes/SWITCH SD/.
#
# Why this script exists: DEPLOY-v0.2.1.sh only archives exefs.nsp into
# Rung-2 + Atmo-clone archive/. It does NOT push the full uLaunch file
# tree to SD. That caused the 2128-0100 pgl crash on first hw test —
# pgl tried to load ulaunch/bin/uMenu/main which didn't exist on SD.
#
# This script stages EVERYTHING uLaunch needs:
#   1. atmosphere/contents/0100000000001000/exefs.nsp  (qlaunch replacement sysmodule)
#   2. switch/uManager.nro                              (management NRO)
#   3. ulaunch/bin/uMenu/*                              (menu UI + romfs)
#   4. ulaunch/bin/uLoader/applet/*                     (hbloader replacement — applet)
#   5. ulaunch/bin/uLoader/application/*                (hbloader replacement — application)
#
# After successful stage, ejects the SD.
#
# Rollback (if hw test fails):
#   rm -rf "/Volumes/SWITCH SD/atmosphere/contents/0100000000001000"
#   rm -rf "/Volumes/SWITCH SD/ulaunch"
#   (Atmosphère NAND-stable; NAND untouched throughout.)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDOUT="${SCRIPT_DIR}/upstream/SdOut"
SD="/Volumes/SWITCH SD"

c_ok='\033[0;32m'; c_err='\033[0;31m'; c_info='\033[0;36m'; c_end='\033[0m'
log_info()  { echo -e "${c_info}[STAGE]${c_end} $*"; }
log_ok()    { echo -e "${c_ok}[STAGE]${c_end} $*"; }
log_error() { echo -e "${c_err}[ERROR]${c_end} $*" >&2; }

# Preflight
[[ -d "${SDOUT}" ]] || { log_error "SdOut missing. Run BUILD-v0.2.1.sh first."; exit 1; }
[[ -f "${SDOUT}/atmosphere/contents/0100000000001000/exefs.nsp" ]] || { log_error "exefs.nsp missing."; exit 1; }
[[ -f "${SDOUT}/switch/uManager.nro" ]] || { log_error "uManager.nro missing."; exit 1; }
[[ -d "${SDOUT}/ulaunch/bin/uMenu" ]] || { log_error "ulaunch/bin/uMenu missing."; exit 1; }

if [[ ! -d "${SD}" ]]; then
    log_error "SD not mounted at '${SD}'. UMS-mount the Switch SD first (Hekate → Tools → USB Tools → SD Card)."
    exit 1
fi

log_info "SdOut: ${SDOUT}"
log_info "SD   : ${SD}"

# 1. Backup existing LayeredFS override if present
TS="$(date +%s)"
BACKUP="${SD}/switch/qos-backups/pre-qos-umenu-v0.2.1-${TS}"
if [[ -d "${SD}/atmosphere/contents/0100000000001000" ]] && [[ -n "$(ls -A "${SD}/atmosphere/contents/0100000000001000" 2>/dev/null || true)" ]]; then
    mkdir -p "${BACKUP}"
    cp -R "${SD}/atmosphere/contents/0100000000001000/." "${BACKUP}/"
    log_ok "Backed up existing LayeredFS override → ${BACKUP}"
fi
if [[ -d "${SD}/ulaunch" ]]; then
    mkdir -p "${BACKUP}/ulaunch"
    cp -R "${SD}/ulaunch/." "${BACKUP}/ulaunch/"
    log_ok "Backed up existing /ulaunch → ${BACKUP}/ulaunch/"
fi

# 2. Stage exefs.nsp (sysmodule)
mkdir -p "${SD}/atmosphere/contents/0100000000001000"
cp "${SDOUT}/atmosphere/contents/0100000000001000/exefs.nsp" \
   "${SD}/atmosphere/contents/0100000000001000/exefs.nsp"
log_ok "Staged sysmodule: atmosphere/contents/0100000000001000/exefs.nsp"

# 3. Stage /switch/uManager.nro
cp "${SDOUT}/switch/uManager.nro" "${SD}/switch/uManager.nro"
log_ok "Staged: switch/uManager.nro"

# 4. Stage /ulaunch tree (full — bin/uMenu + bin/uLoader/{applet,application})
rm -rf "${SD}/ulaunch/bin"  # clean slate to avoid stale binaries
mkdir -p "${SD}/ulaunch/bin"
cp -R "${SDOUT}/ulaunch/bin/." "${SD}/ulaunch/bin/"
log_ok "Staged: ulaunch/bin/{uMenu,uLoader} (main + main.npdm + romfs.bin)"

# 5. Cleanup macOS resource forks
log_info "Cleaning ._ resource forks…"
find "${SD}/atmosphere/contents/0100000000001000" "${SD}/ulaunch" "${SD}/switch/uManager.nro" \
    -name "._*" -delete 2>/dev/null || true

# 6. SHA manifest for telemetry
log_info "SHA manifest:"
shasum -a 256 "${SD}/atmosphere/contents/0100000000001000/exefs.nsp" | sed 's|.*/||' | awk '{print "  "$0}'
shasum -a 256 "${SD}/switch/uManager.nro" | sed 's|.*/||' | awk '{print "  "$0}'
find "${SD}/ulaunch/bin" -type f \( -name main -o -name main.npdm -o -name romfs.bin \) \
    -exec shasum -a 256 {} \; | awk '{print "  "substr($2, length("'"${SD}"'")+2)" "$1}'

# 7. Sync + eject
sync
log_info "Ejecting SD…"
diskutil eject "${SD}"
log_ok "Done. Insert SD in Switch, reboot normally. Atmosphère LayeredFS loads Q OS uMenu at qlaunch."
log_info "Rollback if needed:"
log_info "  rm -rf '${SD}/atmosphere/contents/0100000000001000'"
log_info "  rm -rf '${SD}/ulaunch'"
log_info "  (or restore from ${BACKUP})"
