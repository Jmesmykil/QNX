#!/usr/bin/env bash
# build-nsp.sh — Q OS Manager NRO → NSP wrapper
#
# Title ID : 0x0500000051AFE003
# Output   : /Users/nsa/Astral/QOS/staging/next-sd-push/switch/uManager.nsp
#
# Strategy
# --------
# devkitPro's switch_rules already knows how to produce an NSP from a Makefile
# *when* a CONFIG_JSON (.npdm config file) is present alongside the Makefile.
# The minimal path is therefore:
#   1. Generate uManager.json (NPDM config) with the reserved TID.
#   2. Re-run make — switch_rules emits uManager.nso + uManager.npdm, then links
#      them into uManager.nsp via build/switch_nsp.py (shipped with libnx ≥ 4.6).
#   3. Copy the finished NSP to the staging lane.
#
# If your devkitPro is older than libnx 4.6 (which includes build/switch_nsp.py)
# the fallback path uses hacbrewpack, installed separately:
#   dkp-pacman -Sy hacbrewpack     (on a real devkitPro Linux / WSL environment)
#   brew install hacbrewpack       (community tap on macOS — not official)
#
# Usage
#   cd projects/uManager
#   bash scripts/build-nsp.sh [--hacbrewpack-fallback]

set -euo pipefail

# ── Configuration ──────────────────────────────────────────────────────────────
TITLE_ID="0x0500000051AFE003"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGING_OUT="/Users/nsa/Astral/QOS/staging/next-sd-push/switch/uManager.nsp"
APP_TITLE="Q OS Manager"
APP_AUTHOR="Jamesmykil"
APP_VERSION="0.6.1"

USE_HACBREWPACK=0
if [[ "${1:-}" == "--hacbrewpack-fallback" ]]; then
    USE_HACBREWPACK=1
fi

# ── Sanity checks ─────────────────────────────────────────────────────────────
if [[ -z "${DEVKITPRO:-}" ]]; then
    echo "[build-nsp] ERROR: DEVKITPRO is not set. Export it before running this script." >&2
    echo "  export DEVKITPRO=/opt/devkitpro" >&2
    exit 1
fi

if [[ ! -f "${DEVKITPRO}/libnx/switch_rules" ]]; then
    echo "[build-nsp] ERROR: ${DEVKITPRO}/libnx/switch_rules not found." >&2
    exit 1
fi

# ── Step 1: emit uManager.json (NPDM config) ─────────────────────────────────
# switch_rules looks for <TARGET>.json or config.json next to the Makefile.
# When found it switches the output from .nro to .nsp automatically.
NPDM_JSON="${PROJECT_DIR}/uManager.json"

cat > "${NPDM_JSON}" <<JSON
{
    "name": "${APP_TITLE}",
    "title_id": "${TITLE_ID}",
    "main_thread_stack_size": "0x00100000",
    "main_thread_priority": 47,
    "default_cpu_id": 0,
    "process_category": "regular",
    "is_retail": true,
    "pool_partition": "application",
    "kernel_capabilities": [
        { "type": "kernel_flags",     "value": { "highest_thread_priority": 63, "lowest_thread_priority": 0,
                                                  "highest_cpu_id": 3,          "lowest_cpu_id": 0 } },
        { "type": "syscalls",         "value": { "svcSleepThread": true, "svcGetInfo": true,
                                                  "svcOutputDebugString": true } },
        { "type": "handle_table_size","value": 0x200 },
        { "type": "debug_flags",      "value": { "allow_debug": false, "force_debug": false } }
    ]
}
JSON

echo "[build-nsp] Wrote ${NPDM_JSON}"
echo "[build-nsp] Title ID : ${TITLE_ID}"

# ── Step 2: build ──────────────────────────────────────────────────────────────
cd "${PROJECT_DIR}"

if [[ "${USE_HACBREWPACK}" -eq 0 ]]; then
    # Native path — switch_rules + npdmtool + elf2nso + build_pfs0
    # UL_DEFS must be passed on the make command line so the inner quoting on
    # UL_VERSION survives the shell→make handoff intact.
    UL_DEFS_INLINE="-DUL_MAJOR=0 -DUL_MINOR=6 -DUL_MICRO=1 -DUL_VERSION=\\\"0.6.1\\\""
    echo "[build-nsp] Building via devkitPro make (native NSP path)..."
    make clean
    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" \
        "UL_DEFS=${UL_DEFS_INLINE}"

    NSP_CANDIDATE="${PROJECT_DIR}/uManager.nsp"
    if [[ ! -f "${NSP_CANDIDATE}" ]]; then
        echo "[build-nsp] ERROR: make completed but uManager.nsp not found." >&2
        echo "  This usually means your libnx is older than 4.6 and does not ship" >&2
        echo "  build/switch_nsp.py.  Re-run with --hacbrewpack-fallback." >&2
        exit 1
    fi
else
    # Fallback path — hacbrewpack
    if ! command -v hacbrewpack &>/dev/null; then
        echo "[build-nsp] ERROR: hacbrewpack not found in PATH." >&2
        echo "  On devkitPro Linux/WSL: dkp-pacman -Sy hacbrewpack" >&2
        echo "  On macOS (community):   brew install hacbrewpack" >&2
        exit 1
    fi

    # Build NRO first (without the .json so make uses the NRO path)
    NPDM_JSON_BAK="${NPDM_JSON}.bak"
    mv "${NPDM_JSON}" "${NPDM_JSON_BAK}"
    echo "[build-nsp] Building NRO for hacbrewpack wrapping..."
    make clean
    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    mv "${NPDM_JSON_BAK}" "${NPDM_JSON}"

    NRO_PATH="${PROJECT_DIR}/uManager.nro"
    if [[ ! -f "${NRO_PATH}" ]]; then
        echo "[build-nsp] ERROR: uManager.nro not found after make." >&2
        exit 1
    fi

    # Wrap with hacbrewpack
    # hacbrewpack expects: --nro-filepath, --title-id, --title-name, etc.
    WORK_DIR="$(mktemp -d)"
    EXEFS_DIR="${WORK_DIR}/exefs"
    mkdir -p "${EXEFS_DIR}"

    # hacbrewpack needs a raw NRO in exefs/main
    cp "${NRO_PATH}" "${EXEFS_DIR}/main"

    echo "[build-nsp] Wrapping with hacbrewpack (TID=${TITLE_ID})..."
    hacbrewpack \
        --titleid "${TITLE_ID}" \
        --exefsdir "${EXEFS_DIR}" \
        --romfsdir "${PROJECT_DIR}/romfs" \
        --iconpath "${PROJECT_DIR}/uManager.jpg" \
        --titlename "${APP_TITLE}" \
        --authorfrom "${APP_AUTHOR}" \
        --version  "${APP_VERSION}" \
        --noromfs false \
        --outdir   "${WORK_DIR}/out"

    NSP_CANDIDATE="${WORK_DIR}/out/${TITLE_ID}.nsp"
    if [[ ! -f "${NSP_CANDIDATE}" ]]; then
        # hacbrewpack versions differ on exact output filename format
        NSP_CANDIDATE="$(find "${WORK_DIR}/out" -name "*.nsp" | head -1)"
    fi

    if [[ -z "${NSP_CANDIDATE}" || ! -f "${NSP_CANDIDATE}" ]]; then
        echo "[build-nsp] ERROR: hacbrewpack did not produce an NSP." >&2
        rm -rf "${WORK_DIR}"
        exit 1
    fi

    rm -rf "${WORK_DIR}"
fi

# ── Step 3: copy to staging ────────────────────────────────────────────────────
STAGING_DIR="$(dirname "${STAGING_OUT}")"
mkdir -p "${STAGING_DIR}"
cp "${NSP_CANDIDATE}" "${STAGING_OUT}"
echo "[build-nsp] SUCCESS"
echo "  Output : ${STAGING_OUT}"
echo "  Size   : $(wc -c < "${STAGING_OUT}") bytes"
echo ""
echo "Install on-device:"
echo "  UMS: copy to /switch/ on SD card"
echo "  Or:  tinfoil → file browser → ${STAGING_OUT}"
