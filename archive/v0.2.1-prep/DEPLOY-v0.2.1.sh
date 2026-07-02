#!/usr/bin/env bash
# DEPLOY-v0.2.1.sh — Q OS uLaunch fork v0.2.1 deployment script
# Stages exefs.nsp from the completed build into:
#   1. tools/qos-ulaunch-fork/archive/v0.2.1/exefs.nsp
#   2. tools/qos-atmosphere-clone/archive/v0.1.0/atmosphere/contents/0100000000001000/exefs.nsp
#
# Copyright (c) XorTroll (upstream uLaunch, GPLv2).
# Q OS deploy scripts: Q OS project, GPLv2.
#
# RULES:
#   - Does NOT touch /Volumes/SWITCH SD (creator handles final SD deploy).
#   - Does NOT run sudo or dkp-pacman.
#   - Must be run AFTER BUILD-v0.2.1.sh exits 0.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"  # → QOS/tools/

SRC_EXEFS="${SCRIPT_DIR}/upstream/SdOut/atmosphere/contents/0100000000001000/exefs.nsp"

ARCHIVE_V021="${SCRIPT_DIR%/v0.2.1-prep}/v0.2.1"
ATMO_CLONE_DEST="${TOOLS_DIR}/qos-atmosphere-clone/archive/v0.1.0/atmosphere/contents/0100000000001000/exefs.nsp"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log_info()  { echo -e "${GREEN}[DEPLOY]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN  ]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR ]${NC} $*" >&2; }

# Verify build output exists
if [[ ! -f "${SRC_EXEFS}" ]]; then
    log_error "exefs.nsp not found at expected build output path:"
    log_error "  ${SRC_EXEFS}"
    log_error ""
    log_error "Run BUILD-v0.2.1.sh first (in the v0.2.1-prep/ directory)."
    exit 1
fi

SHA="$(shasum -a 256 "${SRC_EXEFS}" | awk '{print $1}')"
SIZE="$(stat -f%z "${SRC_EXEFS}")"
log_info "Source exefs.nsp verified: SHA256=${SHA} size=${SIZE}B"

# ── Destination 1: archive/v0.2.1/ ───────────────────────────────────────────
mkdir -p "${ARCHIVE_V021}"
cp "${SRC_EXEFS}" "${ARCHIVE_V021}/exefs.nsp"
log_info "Staged → ${ARCHIVE_V021}/exefs.nsp"

# ── Destination 2: qos-atmosphere-clone/archive/v0.1.0/ ──────────────────────
ATMO_DEST_DIR="$(dirname "${ATMO_CLONE_DEST}")"
if [[ ! -d "${ATMO_DEST_DIR}" ]]; then
    log_warn "Atmosphere-clone destination directory does not exist:"
    log_warn "  ${ATMO_DEST_DIR}"
    log_warn "Creating it now (verify qos-atmosphere-clone is correctly placed)."
    mkdir -p "${ATMO_DEST_DIR}"
fi
cp "${SRC_EXEFS}" "${ATMO_CLONE_DEST}"
log_info "Staged → ${ATMO_CLONE_DEST}"
log_info "(qos-atmosphere-clone v0.1.0 now carries uMenu v0.2.1)"

# ── Verification ─────────────────────────────────────────────────────────────
for dest in "${ARCHIVE_V021}/exefs.nsp" "${ATMO_CLONE_DEST}"; do
    dest_sha="$(shasum -a 256 "${dest}" | awk '{print $1}')"
    if [[ "${dest_sha}" != "${SHA}" ]]; then
        log_error "SHA256 mismatch after copy! ${dest}"
        log_error "  expected: ${SHA}"
        log_error "  got:      ${dest_sha}"
        exit 1
    fi
done
log_info "SHA256 verified at both destinations."

log_info ""
log_info "DEPLOY COMPLETE — v0.2.1"
log_info ""
log_info "SD deploy path (creator runs manually when ready):"
log_info "  cp \"${ARCHIVE_V021}/exefs.nsp\" \"/Volumes/SWITCH SD/atmosphere/contents/0100000000001000/exefs.nsp\""
log_info "  diskutil eject \"/Volumes/SWITCH SD\""
log_info ""
log_info "Do NOT eject SD until all build iterations for this session are staged."
