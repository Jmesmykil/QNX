// qos_icon_upload.cpp — deko3d icon upload pipeline for Q OS uMenu v0.7
//
// Pipeline:
//   NsApplicationControlData JPEG (256x256)
//     → stb_image decode → RGBA8 heap buffer
//     → CPU-uncached staging DkMemBlock
//     → dkCmdBufCopyBufferToImage + queue submit/waitIdle
//     → DkImage in bump-allocated image pool
//     → register_image_cb → ImTextureID (DkResHandle)
//
// Image pool strategy: linear bump allocator over the caller-supplied
// DkMemBlock.  FlushIconCache() rewinds the cursor to 0 (no per-image
// free; cache is all-or-nothing on flush).
//
// All 256x256 RGBA8 images are padded to 0x200 alignment as required by
// the deko3d image allocator (must be >= DkImageLayout::getAlignment()).

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb/stb_image.h"

#include "qos_icon_upload.hpp"

#include <deko3d.hpp>
#include <switch.h>      // DkDevice / DkQueue / DkMemBlock C types

#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t ICON_W          = 256;
static constexpr uint32_t ICON_H          = 256;
static constexpr uint32_t ICON_RGBA_BYTES = ICON_W * ICON_H * 4u;
// Minimum alignment for our image sub-allocations.  deko3d requires the
// allocation offset to be a multiple of DkImageLayout::getAlignment(), which
// for a tiled 256x256 RGBA8 image is typically 0x200.  We use 0x200 and also
// align up the image's own footprint to 0x200 for simplicity.
static constexpr uint32_t ICON_POOL_ALIGN = 0x200u;

static constexpr uint32_t dk_align32(uint32_t v, uint32_t a) {
    return (v + a - 1u) & ~(a - 1u);
}

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
namespace {

struct IconUploaderState {
    // Raw handles (not owning — lifetimes managed by the caller)
    DkDevice   device = nullptr;
    DkQueue    queue  = nullptr;
    DkMemBlock imagePool = nullptr;
    size_t     imagePoolSize = 0;

    // Bump allocator cursor (bytes from start of imagePool)
    uint32_t poolCursor = 0;

    // Callback into the deko3d ImGui backend
    ImTextureID (*register_cb)(void *dk_image) = nullptr;

    // Cache: title-ID tag → ImTextureID
    std::unordered_map<uint64_t, ImTextureID> cache;

    // All dk::Image heap objects — freed on FlushIconCache / Shutdown
    std::vector<dk::Image *> imageObjects;

    bool ready = false;
};

IconUploaderState g_uploader;

// ---------------------------------------------------------------------------
// Internal: allocate `size` bytes from the image pool at `align` alignment.
// Returns the byte offset, or UINT32_MAX on OOM.
// ---------------------------------------------------------------------------
static uint32_t BumpAlloc(uint32_t size, uint32_t align) {
    const uint32_t cursor = dk_align32(g_uploader.poolCursor, align);
    if ((size_t)(cursor + size) > g_uploader.imagePoolSize) {
        FILE *log = fopen("sdmc:/switch/qos-icon-upload.log", "a");
        if (log) { fputs("[qos_icon_upload] OOM: image pool exhausted\n", log); fclose(log); }
        return UINT32_MAX;
    }
    g_uploader.poolCursor = cursor + size;
    return cursor;
}

// ---------------------------------------------------------------------------
// Internal: core RGBA8 → dk::Image upload.
// `out_image` must point to a dk::Image with lifetime >= the pool.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool DoUpload(dk::Image *out_image, const uint8_t *rgba, int w, int h) {
    if (!out_image || !rgba || w <= 0 || h <= 0) return false;
    if (!g_uploader.ready) return false;

    const uint32_t pixelBytes = (uint32_t)(w * h * 4);

    // ---- Build image layout ----
    dk::ImageLayout layout;
    dk::ImageLayoutMaker{g_uploader.device}
        .setFlags(0)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions((uint32_t)w, (uint32_t)h)
        .initialize(layout);

    const uint32_t imgAlign = (uint32_t)layout.getAlignment();
    const uint32_t imgSize  = (uint32_t)layout.getSize();
    const uint32_t useAlign = imgAlign > ICON_POOL_ALIGN ? imgAlign : ICON_POOL_ALIGN;
    const uint32_t useSize  = dk_align32(imgSize, useAlign);

    // ---- Allocate from pool ----
    const uint32_t offset = BumpAlloc(useSize, useAlign);
    if (offset == UINT32_MAX) return false;

    // ---- Place dk::Image in pool (DkMemBlock handle passed directly) ----
    out_image->initialize(layout, g_uploader.imagePool, offset);

    // ---- Staging memblock (CPU-uncached, per-upload, freed after waitIdle) ----
    dk::UniqueMemBlock staging =
        dk::MemBlockMaker{g_uploader.device,
            dk_align32(pixelBytes, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
    if (!staging) return false;

    memcpy(staging.getCpuAddr(), rgba, pixelBytes);

    // ---- Record + submit copy ----
    dk::UniqueCmdBuf cmdbuf = dk::CmdBufMaker{g_uploader.device}.create();
    dk::UniqueMemBlock cmdmem =
        dk::MemBlockMaker{g_uploader.device,
            dk_align32(4096u, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
    if (!cmdbuf || !cmdmem) return false;

    cmdbuf.addMemory(cmdmem, 0, cmdmem.getSize());

    DkCopyBuf src{};
    src.addr       = staging.getGpuAddr();
    src.rowLength  = 0;  // 0 = tightly packed
    src.imageHeight = 0;

    DkImageRect rect{};
    rect.x = 0; rect.y = 0; rect.z = 0;
    rect.width  = (uint32_t)w;
    rect.height = (uint32_t)h;
    rect.depth  = 1;

    // dk::ImageView takes a dk::Image (C++ wrapper).  out_image IS a dk::Image*.
    dk::ImageView view{*out_image};
    cmdbuf.copyBufferToImage(src, view, rect);

    // DkQueue and DkDevice are opaque pointer handles; dk::Queue wraps them.
    // Cast via the C API directly rather than reinterpret_cast.
    ::dkQueueSubmitCommands(g_uploader.queue, cmdbuf.finishList());
    ::dkQueueWaitIdle(g_uploader.queue);

    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace qos {

void InitIconUploader(const IconUploadInitParams &p) {
    g_uploader.device        = static_cast<DkDevice>(p.dk_device);
    g_uploader.queue         = static_cast<DkQueue>(p.dk_queue);
    g_uploader.imagePool     = static_cast<DkMemBlock>(p.image_memblock);
    g_uploader.imagePoolSize = p.image_memblock_size;
    g_uploader.register_cb   = p.register_image_cb;
    g_uploader.poolCursor    = 0;
    g_uploader.cache.clear();
    g_uploader.imageObjects.clear();
    g_uploader.ready = (g_uploader.device   != nullptr &&
                        g_uploader.queue    != nullptr &&
                        g_uploader.imagePool != nullptr &&
                        g_uploader.register_cb != nullptr &&
                        g_uploader.imagePoolSize > 0);
}

void ShutdownIconUploader() {
    // Free all dk::Image heap objects before clearing
    for (dk::Image *img : g_uploader.imageObjects) delete img;
    g_uploader.imageObjects.clear();
    g_uploader.cache.clear();
    g_uploader.poolCursor    = 0;
    g_uploader.ready         = false;
    g_uploader.device        = nullptr;
    g_uploader.queue         = nullptr;
    g_uploader.imagePool     = nullptr;
    g_uploader.register_cb   = nullptr;
}

ImTextureID UploadRGBA8(const uint8_t *rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return 0;
    if (!g_uploader.ready) return 0;

    // Allocate a DkImage slot in a static pool.  Because images live until
    // FlushIconCache(), we keep them in a vector backed by the bump allocator.
    // We allocate the dk::Image *struct* on the heap (small, ~80 bytes) and let
    // the underlying pixel data live in the pool MemBlock.
    auto *img = new (std::nothrow) dk::Image{};
    if (!img) return 0;

    if (!DoUpload(img, rgba, width, height)) {
        delete img;
        return 0;
    }

    const ImTextureID tid = g_uploader.register_cb(img);
    if (tid == 0) {
        delete img;
        return 0;
    }
    g_uploader.imageObjects.push_back(img);
    return tid;
}

ImTextureID UploadEncoded(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 0;

    int w = 0, h = 0, channels = 0;
    uint8_t *rgba = stbi_load_from_memory(
        data, (int)len, &w, &h, &channels, 4 /*force RGBA*/);
    if (!rgba) {
        FILE *log = fopen("sdmc:/switch/qos-icon-upload.log", "a");
        if (log) {
            fprintf(log, "[qos_icon_upload] stbi_load failed: %s\n",
                    stbi_failure_reason());
            fclose(log);
        }
        return 0;
    }

    const ImTextureID tid = UploadRGBA8(rgba, w, h);
    stbi_image_free(rgba);
    return tid;
}

ImTextureID GetOrUpload(uint64_t tag, const uint8_t *data, size_t len) {
    auto it = g_uploader.cache.find(tag);
    if (it != g_uploader.cache.end()) return it->second;

    const ImTextureID tid = UploadEncoded(data, len);
    if (tid != 0) {
        g_uploader.cache.emplace(tag, tid);
    }
    return tid;
}

void FlushIconCache() {
    // Wait for any in-flight renders to drain so stale DkImage objects
    // are no longer referenced by the GPU before we free / reuse them.
    if (g_uploader.ready && g_uploader.queue) {
        ::dkQueueWaitIdle(g_uploader.queue);
    }

    // Free heap-allocated DkImage structs (pixel data lived in pool MemBlock).
    for (DkImage *img : g_uploader.imageObjects) delete img;
    g_uploader.imageObjects.clear();

    // Rewind the bump allocator — pixel data in the pool is now reclaimable.
    g_uploader.cache.clear();
    g_uploader.poolCursor = 0;
}

} // namespace qos
