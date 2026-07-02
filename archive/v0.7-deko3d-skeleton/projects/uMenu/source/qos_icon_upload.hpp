// qos_icon_upload.hpp — deko3d icon upload pipeline for Q OS uMenu v0.7
// Decodes JPEG/PNG (via stb_image) → RGBA8 → DkImage → ImTextureID.
// Ownership of every DkImage lives in this module; callers never free.
#pragma once

#include "imgui/imgui.h"
#include <cstdint>
#include <cstddef>

namespace qos {

// ---------------------------------------------------------------------------
// One-time init — call ONCE after deko3d device + transfer queue are ready.
// ---------------------------------------------------------------------------
struct IconUploadInitParams {
    void  *dk_device;             // DkDevice  — opaque, cast internally
    void  *dk_queue;              // DkQueue   — transfer queue for uploads
    void  *image_memblock;        // DkMemBlock backing image pool
    size_t image_memblock_size;   // total bytes in that block

    // Register a DkImage* with the deko3d ImGui backend's descriptor pool
    // and return the ImTextureID (integer DkResHandle) to hand to ImGui.
    // Implementor must write a DkImageDescriptor into the shared pool and
    // call dkMakeTextureHandle(imageSlot, samplerSlot).
    ImTextureID (*register_image_cb)(void *dk_image);
};

void InitIconUploader(const IconUploadInitParams &p);
void ShutdownIconUploader();

// ---------------------------------------------------------------------------
// Upload paths
// ---------------------------------------------------------------------------

// Upload a pre-decoded RGBA8 buffer (width * height * 4 bytes).
// Returns 0 on failure.
ImTextureID UploadRGBA8(const uint8_t *rgba, int width, int height);

// Decode JPEG/PNG from memory with stb_image, then upload.
// Returns 0 on failure (decode or upload).
ImTextureID UploadEncoded(const uint8_t *data, size_t len);

// Cache-aware upload keyed by a 64-bit tag (e.g. title ID).
// If the tag was already uploaded, returns the cached ImTextureID without
// decoding or re-uploading. Returns 0 on failure.
ImTextureID GetOrUpload(uint64_t tag, const uint8_t *data, size_t len);

// Invalidate all cached icons and rewind the image pool bump allocator.
// Call on HOME resume or when the app list changes.
void FlushIconCache();

} // namespace qos
