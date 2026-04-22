// imconfig_qos.h — Q OS fork-owned ImGui config overrides
// Keep this file; do NOT merge back into vanilla imconfig.h.
#pragma once

// Disable tools that add binary size on Switch
#define IMGUI_DISABLE_DEBUG_TOOLS

// Use 16-bit draw indices (default is 16 — explicit for clarity)
// Switch GPU has no issue with 16-bit indices at 1280x720
// #define ImDrawIdx unsigned int  // keep default (uint16_t)

// Assert override — use libnx diagAbortWithResult on fatal ImGui assertions
// (keeps crash consistent with the rest of uMenu)
#include <switch.h>
#define IM_ASSERT(_EXPR) \
    do { if (!(_EXPR)) { diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_BadInput)); } } while(0)
