#!/usr/bin/env bash
# build-nxtheme.sh -- Build .nxtheme files on macOS ARM64
# Pure Python 3, zero C extensions, zero pip installs.
#
# Usage:
#   ./build-nxtheme.sh <input_dir> <output.nxtheme>       # build
#   ./build-nxtheme.sh --extract <input.nxtheme> <dir>    # extract
#   ./build-nxtheme.sh --roundtrip <input.nxtheme>        # verify
#   ./build-nxtheme.sh --test-roundtrip                   # auto-test SD card
#   ./build-nxtheme.sh --check-deps                       # verify Python path

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Python 3 resolution order: env override -> Homebrew ARM64 -> system
PYTHON3="${PYTHON3:-}"
if [[ -z "$PYTHON3" || ! -x "$PYTHON3" ]]; then
    HB_PY="/opt/homebrew/Frameworks/Python.framework/Versions/3.12/bin/python3.12"
    if [[ -x "$HB_PY" ]]; then
        PYTHON3="$HB_PY"
    else
        PYTHON3="$(command -v python3 2>/dev/null || true)"
    fi
fi

if [[ -z "$PYTHON3" || ! -x "$PYTHON3" ]]; then
    echo "ERROR: Python 3 not found." >&2
    echo "  Install: brew install python@3.12" >&2
    exit 1
fi

if [[ "${1:-}" == "--check-deps" ]]; then
    echo "Python: $PYTHON3"
    "$PYTHON3" -c "import sys; print(f'Version: {sys.version}')"
    "$PYTHON3" -c "import struct; print('struct: OK')"
    echo "All dependencies satisfied (stdlib only)."
    exit 0
fi

# Auto round-trip test against first .nxtheme on mounted Switch SD
if [[ "${1:-}" == "--test-roundtrip" ]]; then
    THEME_DIR="/Volumes/SWITCH SD/themes/ThemezerNX"
    if [[ ! -d "$THEME_DIR" ]]; then
        echo "ERROR: Switch SD not mounted at /Volumes/SWITCH SD" >&2
        exit 1
    fi
    SAMPLE="$(find "$THEME_DIR" -name '*.nxtheme' | head -1 || true)"
    if [[ -z "$SAMPLE" ]]; then
        echo "ERROR: No .nxtheme files found in $THEME_DIR" >&2
        exit 1
    fi
    echo "Auto-test: $SAMPLE"
    exec "$PYTHON3" "$SCRIPT_DIR/nxtheme_build.py" --roundtrip "$SAMPLE"
fi

exec "$PYTHON3" "$SCRIPT_DIR/nxtheme_build.py" "$@"
