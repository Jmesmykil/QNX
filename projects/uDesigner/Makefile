# ─────────────────────────────────────────────────────────────────────────────
# LEGACY UPSTREAM REFERENCE — DO NOT MAINTAIN
#
# This is upstream XorTroll's uLaunch theme designer (C++ + Dear ImGui + GLFW
# compiled to WebAssembly via Emscripten). It is preserved on disk as the
#
# Per creator directive 2026-04-25T17:55Z:
#   "Udesigner could be rebuilt from the ground up because we will let them
#    theme Q OS themselves later down the road."
#
# Per creator memory `feedback_all_apps_must_be_swift`:
#   replaced, not maintained.
#
# Do NOT modify. Do NOT add to the `package:` target in src/Makefile. Do NOT
# build in CI. The `legacy-companion-archive` target in src/Makefile zips this
# tree to archive/legacy-companion-apps/ for posterity.
#
# See docs/ROADMAP.md "Post-1.0 native Mac companions" for the version chain.
# ─────────────────────────────────────────────────────────────────────────────

NAME			:=	udesigner
OUTPUT_DIR		:=	../../docs
OUTPUT			:=	$(NAME)
INDEX_HTML		:=	source/index.html
INCLUDE			:=	-Iinclude -I$(CURDIR)/../../libs/zip -I$(CURDIR)/../../libs/json/single_include/nlohmann -I$(CURDIR)/../../libs/stb
SOURCES			:=	$(shell find $(CURDIR)/source/ -type f -name "*.cpp") $(CURDIR)/../../libs/zip/src/zip.c
ASSETS			:=	assets

IMGUI_DIR		:=	../../libs/imgui

CXX				:=	emcc
CXX_FLAGS		:=	-O1 -fexceptions

SOURCES			+=	$(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

LIBS			:=	-lGL
CXX_EMS_FLAGS	:=	-s USE_WEBGL2=1 -s USE_GLFW=3 -s FULL_ES3=1 -s WASM=1 -s RETAIN_COMPILER_SETTINGS -s ALLOW_MEMORY_GROWTH -s ASSERTIONS -s EXPORTED_FUNCTIONS="[_main, _malloc]" -s EXPORTED_RUNTIME_METHODS='["ccall", "HEAPU8"]' -s ERROR_ON_UNDEFINED_SYMBOLS=1

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(SOURCES)
	mkdir -p $(OUTPUT_DIR)
	$(CXX) $(SOURCES) $(CXX_FLAGS) -o $(OUTPUT_DIR)/$(OUTPUT).js $(LIBS) $(CXX_EMS_FLAGS) --preload-file $(ASSETS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(INCLUDE)
	cp $(INDEX_HTML) $(OUTPUT_DIR)/index.html

clean:
	rm -rf $(OUTPUT_DIR)
