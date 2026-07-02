#!/usr/bin/env bash
# STAGE-TO-SD.sh — Q OS uManager v0.2.3 SD deployment
#
# Stages the uManager.nsp (built by BUILD-v0.2.3.sh) onto the Switch SD card
# so Atmosphère can load the updated sysmodule on next boot.
#
# Deployment layout on SD (Atmosphère ExeFS override path):
#   /Volumes/SWITCH/atmosphere/contents/<TITLE_ID>/exefs/main
#   /Volumes/SWITCH/atmosphere/contents/<TITLE_ID>/exefs/main.npdm
#
# The .nsp produced by the uManager Makefile is an NSP containing a single
# Program NCA.  nut/hacPack/hactool are needed to extract the ExeFS from it.
# If the build instead produced an .nro (headless dev mode), we copy it into:
#   /Volumes/SWITCH/switch/uManager.nro
# for manual launch via the homebrew menu.
#
# Prerequisites:
#   - SD card mounted at /Volumes/SWITCH  (or override $SD_MOUNT)
#   - hactool in PATH (for NSP → ExeFS extraction)
#   - Keyfiles at ~/.switch/prod.keys     (or override $KEYFILE)
#   - uManager.nsp (or .nro) already built by BUILD-v0.2.3.sh
#
# DO NOT run with sudo.
# DO NOT eject the SD card while this script is running.
#
# Copyright (c) Q OS project, GPLv2.

set -euo pipefail

# ── Configurable paths ────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
UMAN_DIR="${REPO_ROOT}/src/projects/uManager"

SD_MOUNT="${SD_MOUNT:-/Volumes/SWITCH}"
KEYFILE="${KEYFILE:-${HOME}/.switch/prod.keys}"
VERSION="${VERSION:-0.2.3}"

# uManager Atmosphère title ID (must match the one in the NSP).
TITLE_ID="${TITLE_ID:-0100000000001008}"

ATMOS_CONTENT="${SD_MOUNT}/atmosphere/contents/${TITLE_ID}"
EXEFS_DIR="${ATMOS_CONTENT}/exefs"
NRO_DEST="${SD_MOUNT}/switch/uManager.nro"

NSP="${UMAN_DIR}/uManager.nsp"
NRO="${UMAN_DIR}/uManager.nro"

# ── Sanity checks ─────────────────────────────────────────────────────────────
echo "============================================================"
echo " Q OS uManager v${VERSION} — SD staging"
echo " SD mount   : ${SD_MOUNT}"
echo " Title ID   : ${TITLE_ID}"
echo "============================================================"

if [[ ! -d "${SD_MOUNT}" ]]; then
    echo "[ERROR] SD card not mounted at ${SD_MOUNT}." >&2
    echo "        Insert the Switch SD card and mount it, or set SD_MOUNT." >&2
    exit 1
fi

if [[ ! -f "${NSP}" && ! -f "${NRO}" ]]; then
    echo "[ERROR] Neither uManager.nsp nor uManager.nro found in ${UMAN_DIR}." >&2
    echo "        Run BUILD-v0.2.3.sh first." >&2
    exit 1
fi

# ── NSP path (sysmodule / Atmosphère ExeFS override) ─────────────────────────
if [[ -f "${NSP}" ]]; then
    echo "[mode] NSP artifact found — extracting ExeFS via hactool."

    if ! command -v hactool &>/dev/null; then
        echo "[ERROR] hactool not found in PATH.  Install hactool and retry." >&2
        echo "        Alternatively, place the .nro at switch/uManager.nro for" >&2
        echo "        homebrew-menu loading." >&2
        exit 1
    fi

    if [[ ! -f "${KEYFILE}" ]]; then
        echo "[ERROR] Keyfile not found at ${KEYFILE}." >&2
        echo "        Set KEYFILE=/path/to/prod.keys and retry." >&2
        exit 1
    fi

    TMPDIR_EXTRACT="$(mktemp -d)"
    trap 'rm -rf "${TMPDIR_EXTRACT}"' EXIT

    echo "[extract] hactool → ${TMPDIR_EXTRACT}/exefs ..."
    hactool --keyset="${KEYFILE}" \
            --exefsdir="${TMPDIR_EXTRACT}/exefs" \
            "${NSP}"

    if [[ ! -f "${TMPDIR_EXTRACT}/exefs/main" ]]; then
        echo "[ERROR] ExeFS extraction failed — 'main' not present." >&2
        exit 1
    fi

    echo "[stage] Creating Atmosphère content dir: ${EXEFS_DIR}"
    mkdir -p "${EXEFS_DIR}"

    echo "[stage] Copying ExeFS files..."
    cp -v "${TMPDIR_EXTRACT}/exefs/main"      "${EXEFS_DIR}/main"
    [[ -f "${TMPDIR_EXTRACT}/exefs/main.npdm" ]] && \
        cp -v "${TMPDIR_EXTRACT}/exefs/main.npdm" "${EXEFS_DIR}/main.npdm"

    SHA_MAIN=$(shasum -a 256 "${EXEFS_DIR}/main" | cut -d' ' -f1)
    SZ_MAIN=$(wc -c < "${EXEFS_DIR}/main" | tr -d ' ')
    echo "[output] main  size=${SZ_MAIN}  sha256=${SHA_MAIN}"

# ── NRO fallback path (homebrew menu / hbloader) ─────────────────────────────
elif [[ -f "${NRO}" ]]; then
    echo "[mode] NRO artifact found — staging to switch/ for homebrew menu."
    echo "[warn] NRO mode: uManager will NOT run as a sysmodule.  App scanning"
    echo "       requires the NSP (sysmodule) build for production use."

    mkdir -p "$(dirname "${NRO_DEST}")"
    cp -v "${NRO}" "${NRO_DEST}"

    SHA_NRO=$(shasum -a 256 "${NRO_DEST}" | cut -d' ' -f1)
    SZ_NRO=$(wc -c < "${NRO_DEST}" | tr -d ' ')
    echo "[output] uManager.nro  size=${SZ_NRO}  sha256=${SHA_NRO}"
fi

# ── Sync + report ─────────────────────────────────────────────────────────────
echo ""
echo "[sync] Flushing SD card write cache..."
sync

echo ""
echo "------------------------------------------------------------"
echo "[done] v${VERSION} staged to SD."
echo ""
echo "  Next steps:"
echo "    1. Safely eject: diskutil eject ${SD_MOUNT}"
echo "    2. Insert SD into Switch."
echo "    3. Boot into Atmosphère (via Hekate payload or direct CFW boot)."
echo "    4. uManager sysmodule will run at boot and write:"
echo "         sdmc:/switch/qos-apps/records.bin"
echo "         sdmc:/switch/qos-apps/records.json  (debug)"
echo "         sdmc:/switch/qos-apps/icons/<hex16>.jpg  (per title)"
echo "    5. Launch the Q OS NRO from hbloader to read the scan output."
echo "------------------------------------------------------------"
