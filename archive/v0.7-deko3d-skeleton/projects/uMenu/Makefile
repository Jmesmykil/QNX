#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# EXEFS_SRC is the optional input directory containing data copied into exefs
# ROMFS is the romfs directory — shader .dksh files land here
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source source/ul source/ul/menu source/ul/menu/am source/ul/menu/smi source/ul/menu/smi/sf source/ul/menu/bt source/imgui source/ui source/stb
DATA		:=	data
INCLUDES	:=	include
EXEFS_SRC	:=	exefs_src
ROMFS		:=	romfs

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -Wl,-wrap,libappletLaunch -Wl,-wrap,libappletStart

CFLAGS	:=	-g -O2 -ffunction-sections -Wall -Werror $(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ $(UL_DEFS) -DUL_MAJOR=0 -DUL_MINOR=7 -DUL_MICRO=0 -DUL_VERSION=\"0.7.0\" -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++23

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

FORK_ROOT		:=	$(abspath $(CURDIR)/../../../..)
UPSTREAM_LIBS	:=	$(FORK_ROOT)/archive/v0.2.1-prep/upstream/libs
UPSTREAM_LIBS_REL	:=	../../../v0.2.1-prep/upstream/libs
LIBUCOMMON		:=	$(UPSTREAM_LIBS)/uCommon
LIBNX_EXT		:=	$(UPSTREAM_LIBS)/libnx-ext/libnx-ext
LIBNX_IPCEXT	:=	$(UPSTREAM_LIBS)/libnx-ext/libnx-ipcext
INCLUDES 		+=	$(UPSTREAM_LIBS_REL)/json/single_include/nlohmann $(UPSTREAM_LIBS_REL)/zip/src
# ImGui + UI backend headers
INCLUDES		+=	source/imgui source/ui
# stb_image header (used by qos_icon_upload)
INCLUDES		+=	source/stb

# v0.7: All SDL2 / Plutonium / Mesa / audio deps removed.
# Renderer is now deko3d (Switch low-level GPU API).
# Kept: uCommon IPC infrastructure, libnx, bz2/z for zip/json utils.
LIBS	:= -ldeko3d -lm -luCommon -lnx-ipcext -lnx-ext -lbz2 -lz -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX) $(LIBUCOMMON) $(LIBNX_EXT) $(LIBNX_IPCEXT)


#---------------------------------------------------------------------------------
# Shader compilation — GLSL → .dksh via uam
# Compiled shaders are placed into romfs/shaders/ so they're accessible via
# romfs:/shaders/<name>.dksh at runtime.
#---------------------------------------------------------------------------------
UAM		:=	$(DEVKITPRO)/tools/bin/uam
SHADERDIR	:=	$(CURDIR)/source/shaders
ROMFS_SHADERS	:=	$(CURDIR)/romfs/shaders

SHADER_VERT_SRC	:=	$(SHADERDIR)/imgui_vsh.glsl
SHADER_FRAG_SRC	:=	$(SHADERDIR)/imgui_fsh.glsl
SHADER_VERT_OUT	:=	$(ROMFS_SHADERS)/imgui_vsh.dksh
SHADER_FRAG_OUT	:=	$(ROMFS_SHADERS)/imgui_fsh.dksh

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export BUILD_EXEFS_SRC := $(TOPDIR)/$(EXEFS_SRC)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all shaders

#---------------------------------------------------------------------------------
all: shaders $(BUILD)

# Compile GLSL shaders to .dksh before the main build so romfs is populated.
shaders: $(SHADER_VERT_OUT) $(SHADER_FRAG_OUT)

$(ROMFS_SHADERS):
	@mkdir -p $(ROMFS_SHADERS)

$(SHADER_VERT_OUT): $(SHADER_VERT_SRC) | $(ROMFS_SHADERS)
	@echo "  UAM  $(notdir $<) -> $(notdir $@)"
	@$(UAM) -s vert -o $@ $<

$(SHADER_FRAG_OUT): $(SHADER_FRAG_SRC) | $(ROMFS_SHADERS)
	@echo "  UAM  $(notdir $<) -> $(notdir $@)"
	@$(UAM) -s frag -o $@ $<

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) -C $(LIBUCOMMON) UL_DEFS="-DUL_MAJOR=0 -DUL_MINOR=7 -DUL_MICRO=0 -DUL_VERSION=\\\"0.7.0\\\""
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@$(MAKE) clean -C $(LIBUCOMMON)
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).npdm $(TARGET).nso $(TARGET).elf
	@rm -f $(SHADER_VERT_OUT) $(SHADER_FRAG_OUT)

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------

all	:	$(OUTPUT).nsp

ifeq ($(strip $(APP_JSON)),)
$(OUTPUT).nsp	:	$(OUTPUT).nso
else
$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm
endif

$(OUTPUT).nso	:	$(OUTPUT).elf

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
