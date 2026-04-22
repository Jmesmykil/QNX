#!/usr/bin/env bash
# BUILD-v0.2.3.sh — Q OS uLaunch fork v0.2.3 (uManager AppScanner)
#
# What changed in v0.2.3 (C++ / uManager only):
#   - NEW  src/projects/uManager/include/ul/man/man_AppScanner.hpp
#           ScanAndWriteAppList() declaration, QAPP wire-format constants,
#           ScanResult struct, SD path constants.
#   - NEW  src/projects/uManager/source/ul/man/man_AppScanner.cpp
#           nsListApplicationRecord pagination loop, NACP extraction,
#           inline JPEG copy, records.bin (binary) + records.json (debug)
#           atomic write, per-title icon .jpg writes.
#   - MOD  src/projects/uManager/source/main.cpp
#           Calls ScanAndWriteAppList() after nsInitialize() with telemetry.
#
# Output artifact: uManager.nsp (sysmodule ExeFS; NOT an NRO).
# The .nsp is what Atmosphère loads from atmosphere/contents/<titleID>/.
#
# Prerequisites (host must have):
#   - devkitPro / devkitA64 in $DEVKITPRO (typically /opt/devkitpro)
#   - libnx, libnx-ext, Plutonium, uCommon, all built for Switch target
#   - VERSION env var set to "0.2.3" or auto-derived below
#
# Copyright (c) XorTroll (upstream uLaunch, GPLv2).
# Q OS build scripts: Q OS project, GPLv2.
#
# DO NOT run with sudo. DO NOT touch /Volumes/SWITCH SD from this script.
# SD deployment is handled by STAGE-TO-SD.sh (run separately after review).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
UMAN_DIR="${REPO_ROOT}/src/projects/uManager"

VERSION="${VERSION:-0.2.3}"
export VERSION

echo "============================================================"
echo " Q OS uManager v${VERSION} — AppScanner build"
echo " Repo root  : ${REPO_ROOT}"
echo " Build dir  : ${UMAN_DIR}"
echo "============================================================"

# Sanity checks.
if [[ -z "${DEVKITPRO:-}" ]]; then
    echo "[ERROR] \$DEVKITPRO is not set. Export it before running." >&2
    exit 1
fi
if [[ ! -d "${DEVKITPRO}/libnx" ]]; then
    echo "[ERROR] libnx not found under \$DEVKITPRO (${DEVKITPRO}/libnx)" >&2
    exit 1
fi
if [[ ! -f "${UMAN_DIR}/Makefile" ]]; then
    echo "[ERROR] uManager Makefile not found at: ${UMAN_DIR}/Makefile" >&2
    exit 1
fi

# Verify new source files are present.
HPP="${UMAN_DIR}/include/ul/man/man_AppScanner.hpp"
CPP="${UMAN_DIR}/source/ul/man/man_AppScanner.cpp"
for f in "${HPP}" "${CPP}"; do
    if [[ ! -f "${f}" ]]; then
        echo "[ERROR] Required v0.2.3 source file missing: ${f}" >&2
        exit 1
    fi
done
echo "[check] man_AppScanner.hpp  — present"
echo "[check] man_AppScanner.cpp  — present"

# Clean previous build artefacts (keeps the build hermetic).
echo ""
echo "[build] Cleaning previous artefacts..."
make -C "${UMAN_DIR}" clean || true

# Build.
echo ""
echo "[build] Running make VERSION=${VERSION} ..."
make -C "${UMAN_DIR}" VERSION="${VERSION}"

# Report output.
NSP="${UMAN_DIR}/uManager.nsp"
NRO="${UMAN_DIR}/uManager.nro"

echo ""
echo "------------------------------------------------------------"
if [[ -f "${NSP}" ]]; then
    SHA=$(shasum -a 256 "${NSP}" | cut -d' ' -f1)
    SZ=$(wc -c < "${NSP}" | tr -d ' ')
    echo "[output] uManager.nsp  size=${SZ} bytes  sha256=${SHA}"
elif [[ -f "${NRO}" ]]; then
    # Fallback: if CONFIG_JSON is not present, make produces an NRO.
    SHA=$(shasum -a 256 "${NRO}" | cut -d' ' -f1)
    SZ=$(wc -c < "${NRO}" | tr -d ' ')
    echo "[output] uManager.nro  size=${SZ} bytes  sha256=${SHA}"
else
    echo "[ERROR] Expected output (uManager.nsp or uManager.nro) not found." >&2
    exit 1
fi
echo "------------------------------------------------------------"
echo ""
echo "[done] v${VERSION} build complete."
echo "       Run STAGE-TO-SD.sh to deploy to the Switch SD card."
