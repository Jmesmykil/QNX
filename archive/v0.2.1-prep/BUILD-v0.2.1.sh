#!/usr/bin/env bash
# BUILD-v0.2.1.sh — Q OS uLaunch fork v0.2.1 build script
# Builds umenu + umanager + core sysmodule → complete exefs.nsp
# Requires: devkitPro devkitA64, switch-sdl2 (2.28.5-3), switch-sdl2_mixer,
#           switch-sdl2_image, switch-sdl2_ttf, switch-sdl2_gfx
#
# Copyright (c) XorTroll (upstream uLaunch, GPLv2).
# Q OS build scripts: Q OS project, GPLv2.
#
# DO NOT run with sudo. DO NOT touch /Volumes/SWITCH SD.
# Creator handles SD deployment separately via DEPLOY-v0.2.1.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_DIR="${SCRIPT_DIR}/upstream"
OUT_DIR="${UPSTREAM_DIR}/SdOut"
EXEFS_OUT="${UPSTREAM_DIR}/projects/uSystem/out/nintendo_nx_arm64_armv8a/release/uSystem.nsp"

# ── ANSI colours ──────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

log_info()  { echo -e "${GREEN}[BUILD]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN ]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ── Step (a): SDL2 pre-check (non-sudo, read-only) ────────────────────────────
log_info "Step (a): checking for switch-sdl2..."

if ! dkp-pacman -Qs switch-sdl2 > /dev/null 2>&1; then
    log_error "switch-sdl2 is NOT installed."
    log_error ""
    log_error "Run the following command in an interactive terminal (requires sudo):"
    log_error ""
    log_error "  sudo dkp-pacman -S switch-sdl2 switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx"
    log_error ""
    log_error "SDL2 VERSION NOTE (CRITICAL — read before installing):"
    log_error "  The build requires switch-sdl2 revision 2.28.5-3 (audout-based)."
    log_error "  Newer revisions (2.28.5-4+) use audren and cause the audio sysmodule"
    log_error "  to crash when suspending games on real hardware."
    log_error ""
    log_error "  If dkp-pacman offers a newer version, install from the pinned tarball:"
    log_error "  https://github.com/devkitPro/pacman-packages/tree/0ae8790f6e092cf8df937d143e70a785f7e27997/switch/SDL2"
    log_error "  then: sudo dkp-pacman -U <path-to-switch-sdl2-2.28.5-3-aarch64.pkg.tar.xz>"
    log_error ""
    log_error "After installing SDL2, re-run this script."
    exit 1
fi

# Warn if installed version is not the pinned revision
SDL2_VER="$(dkp-pacman -Qi switch-sdl2 2>/dev/null | awk '/^Version/{print $3}' || echo unknown)"
log_info "switch-sdl2 detected: version=${SDL2_VER}"
if [[ "${SDL2_VER}" != "2.28.5-3" ]]; then
    log_warn "SDL2 version is '${SDL2_VER}', expected '2.28.5-3'."
    log_warn "Continuing — but test on real hardware for audio-on-suspend regression."
    log_warn "See INTEGRATION-SPEC.md Risk 2 for details."
fi

# Also verify the SDL2 mixer/image/ttf/gfx portlib packages
for pkg in switch-sdl2_mixer switch-sdl2_image switch-sdl2_ttf switch-sdl2_gfx; do
    if ! dkp-pacman -Qs "${pkg}" > /dev/null 2>&1; then
        log_error "Missing package: ${pkg}"
        log_error "Run: sudo dkp-pacman -S ${pkg}"
        exit 1
    fi
    log_info "${pkg}: installed"
done

# ── Environment ───────────────────────────────────────────────────────────────
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="${DEVKITPRO}/devkitA64/bin:${DEVKITPRO}/tools/bin:${PATH}"

log_info "DEVKITPRO=${DEVKITPRO}"
log_info "Compiler: $(aarch64-none-elf-gcc --version | head -1)"

cd "${UPSTREAM_DIR}"

# ── QOS-PATCH-004: version-macro injection ────────────────────────────────────
# Root Makefile defines UL_DEFS and VERSION but only exports them to child
# makes it spawns directly.  When BUILD-v0.2.1.sh calls `make -C projects/X`
# as a fresh process, those variables are NOT inherited, causing:
#   ul_Include.hpp: 'UL_MAJOR' was not declared in this scope
# Fix: mirror the root Makefile's version assignment here and export UL_DEFS +
# VERSION so every per-project make invocation below picks them up.
#
# Version string: 1.2.3 matches upstream root Makefile (VERSION_MAJOR/MINOR/MICRO).
# Our fork is labelled 0.2.1 externally; the internal ABI version stays 1.2.3
# to remain wire-compatible with the upstream uLaunch ecosystem on fw 20.0.0.
# QOS-PATCH-004: build-version-injection
UL_MAJOR=1
UL_MINOR=2
UL_MICRO=3
export VERSION="${UL_MAJOR}.${UL_MINOR}.${UL_MICRO}"
export UL_DEFS="-DUL_MAJOR=${UL_MAJOR} -DUL_MINOR=${UL_MINOR} -DUL_MICRO=${UL_MICRO} -DUL_VERSION='\"${VERSION}\"'"
log_info "Version: ${VERSION}  UL_DEFS: ${UL_DEFS}"

# ── Step (b): build uMenu ─────────────────────────────────────────────────────
log_info "Step (b): building uMenu (Plutonium + SDL2 dependent)..."
make -C projects/uMenu
log_info "uMenu: PASS"

# ── Step (c): build uManager ─────────────────────────────────────────────────
log_info "Step (c): building uManager..."
make -C projects/uManager
log_info "uManager: PASS"

# ── Step (d): build uSystem (core sysmodule) ──────────────────────────────────
# Note: arc codegen + libs (Atmosphere-libs, libnx-ext, libnx-ipcext, Plutonium,
# uCommon) were already built during uMenu. uSystem picks up cached objects.
log_info "Step (d): building uSystem (core sysmodule)..."
make -C projects/uSystem
log_info "uSystem: PASS"

# ── Step (e): assemble exefs.nsp and full SdOut/ ─────────────────────────────
log_info "Step (e): assembling SdOut/ directory tree..."
mkdir -p "${OUT_DIR}/atmosphere/contents/0100000000001000"
cp "${EXEFS_OUT}" "${OUT_DIR}/atmosphere/contents/0100000000001000/exefs.nsp"

mkdir -p "${OUT_DIR}/ulaunch/bin/uMenu"
cp projects/uMenu/uMenu.nso "${OUT_DIR}/ulaunch/bin/uMenu/main"
cp projects/uMenu/uMenu.npdm "${OUT_DIR}/ulaunch/bin/uMenu/main.npdm"
cp assets/Logo.png projects/uMenu/romfs/Logo.png
rm -rf projects/uMenu/romfs/default
cp -r default-theme/ projects/uMenu/romfs/default/
build_romfs projects/uMenu/romfs "${OUT_DIR}/ulaunch/bin/uMenu/romfs.bin"
log_info "uMenu romfs.bin: PASS"

mkdir -p "${OUT_DIR}/switch"
cp projects/uManager/uManager.nro "${OUT_DIR}/switch/uManager.nro"
log_info "uManager.nro: PASS"

mkdir -p "${OUT_DIR}/ulaunch/bin/uLoader/applet"
mkdir -p "${OUT_DIR}/ulaunch/bin/uLoader/application"
cp projects/uLoader/uLoader.nso "${OUT_DIR}/ulaunch/bin/uLoader/applet/main"
cp projects/uLoader/uLoader_applet.npdm "${OUT_DIR}/ulaunch/bin/uLoader/applet/main.npdm"
cp projects/uLoader/uLoader.nso "${OUT_DIR}/ulaunch/bin/uLoader/application/main"
cp projects/uLoader/uLoader_application.npdm "${OUT_DIR}/ulaunch/bin/uLoader/application/main.npdm"
log_info "uLoader: PASS"

EXEFS_NSP="${OUT_DIR}/atmosphere/contents/0100000000001000/exefs.nsp"
EXEFS_SHA="$(shasum -a 256 "${EXEFS_NSP}" | awk '{print $1}')"
EXEFS_SIZE="$(stat -f%z "${EXEFS_NSP}")"

log_info ""
log_info "BUILD COMPLETE — v0.2.1"
log_info "  exefs.nsp : ${EXEFS_NSP}"
log_info "  SHA256    : ${EXEFS_SHA}"
log_info "  Size      : ${EXEFS_SIZE} bytes"
log_info ""
log_info "Run DEPLOY-v0.2.1.sh to stage artifacts to archive/ and atmosphere-clone/."
