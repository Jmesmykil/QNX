// imgui_impl_deko3d.h — deko3d render backend for ImGui
// Q OS uMenu v0.7 skeleton — Week 1 Foundation
//
// Reference: scturtle/imgui_deko3d_example (MIT)
//            ftpd/mtheall imgui_deko3d.cpp (MIT)
//
// Constants match the plan spec:
//   FB_NUM = 2, FB_WIDTH = 1280, FB_HEIGHT = 720
//   CODEMEMSIZE = 128 KB, CMDMEMSIZE = 1 MB, IMAGE_POOL_SIZE = 16 MB
//
// v0.7.0-beta5: NWindow created via appletCreateManagedDisplayLayer (sphaira pattern).
//   nwindowGetDefault() returns NULL in standalone LibraryApplet context (no hbloader).
//   The managed NWindow is stored inside the backend and outlives the swapchain.
#pragma once

#include "../imgui/imgui.h"

// Forward-declare ImDrawData to avoid pulling imgui.h into every TU
struct ImDrawData;

// -----------------------------------------------------------------
// Init / shutdown
// Call ImGui_ImplDeko3d_Init() ONCE after ImGui::CreateContext().
// Call ImGui_ImplDeko3d_Shutdown() before appletExit().
// -----------------------------------------------------------------
bool ImGui_ImplDeko3d_Init();
void ImGui_ImplDeko3d_Shutdown();

// Call at the start of each frame (after ImGui_ImplSwitch_NewFrame).
void ImGui_ImplDeko3d_NewFrame();

// Call after ImGui::Render() with ImGui::GetDrawData().
void ImGui_ImplDeko3d_RenderDrawData(ImDrawData *draw_data);

// ---------------------------------------------------------------------------
// Icon image registration — used by qos_icon_upload
// ---------------------------------------------------------------------------
// Accessor for backend internals needed by the icon upload pipeline.
// Returns false if the backend is not yet initialized.
struct ImplDeko3dHandles {
    void  *dk_device;          // DkDevice (opaque void*)
    void  *dk_queue;           // DkQueue (opaque void*)
    void  *image_memblock;     // DkMemBlock for the image pool
    size_t image_memblock_size; // total bytes in the image pool block
};
bool ImGui_ImplDeko3d_GetHandles(ImplDeko3dHandles *out);

// Register a DkImage* with the backend's extended descriptor pool.
// Writes a DkImageDescriptor at the next free slot and returns a
// dkMakeTextureHandle(imageSlot, 0) handle cast to ImTextureID.
// Returns 0 on failure (pool full or backend not init).
ImTextureID ImGui_ImplDeko3d_RegisterImage(void *dk_image);
