# Top-level Makefile — Q OS uMenu v0.7-deko3d-skeleton
# Provides a `umenu` target that:
#   1. Builds libs (uCommon, libnx-ext)
#   2. Compiles shaders (GLSL → .dksh) and the uMenu project
#   3. Packs romfs.bin from projects/uMenu/romfs/
#   4. Stages output to SdOut/qos-shell/bin/uMenu/{main,main.npdm,romfs.bin}
#
# Usage:
#   export DEVKITPRO=/opt/devkitpro
#   export DEVKITA64=/opt/devkitpro/devkitA64
#   export PATH=$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH
#   make umenu

DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64

OUT_DIR    := SdOut
FORK_ROOT  := $(abspath $(CURDIR)/../..)
UPSTREAM_LIBS := $(FORK_ROOT)/archive/v0.2.1-prep/upstream/libs

LIBUCOMMON    := $(UPSTREAM_LIBS)/uCommon
LIBNX_EXT     := $(UPSTREAM_LIBS)/libnx-ext/libnx-ext
LIBNX_IPCEXT  := $(UPSTREAM_LIBS)/libnx-ext/libnx-ipcext

.PHONY: all umenu libs clean

all: umenu

libs:
	@echo "[LIBS] building uCommon..."
	@$(MAKE) -C $(LIBUCOMMON) \
	    UL_DEFS="-DUL_MAJOR=0 -DUL_MINOR=7 -DUL_MICRO=0 -DUL_VERSION=\\\"0.7.0\\\""
	@echo "[LIBS] building libnx-ext..."
	@$(MAKE) -C $(LIBNX_EXT)
	@echo "[LIBS] building libnx-ipcext..."
	@$(MAKE) -C $(LIBNX_IPCEXT)

umenu: libs
	@echo "[UMENU] building uMenu (shaders + binary)..."
	@$(MAKE) -C projects/uMenu
	@echo "[UMENU] staging artifacts..."
	@mkdir -p $(OUT_DIR)/qos-shell/bin/uMenu
	@cp projects/uMenu/uMenu.nsp  $(OUT_DIR)/qos-shell/bin/uMenu/main
	@cp projects/uMenu/uMenu.npdm $(OUT_DIR)/qos-shell/bin/uMenu/main.npdm
	@echo "[UMENU] packing romfs.bin..."
	@build_romfs projects/uMenu/romfs $(OUT_DIR)/qos-shell/bin/uMenu/romfs.bin
	@echo "[UMENU] done — artifacts at $(OUT_DIR)/qos-shell/bin/uMenu/"

clean:
	@echo "[CLEAN] uMenu..."
	@$(MAKE) clean -C projects/uMenu
	@rm -rf $(OUT_DIR)
