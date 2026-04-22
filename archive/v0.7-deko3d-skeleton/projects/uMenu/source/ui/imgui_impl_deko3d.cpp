// imgui_impl_deko3d.cpp — deko3d render backend for ImGui
// Q OS uMenu v0.7 skeleton — Week 1 Foundation
//
// Derived from:
//   ftpd / mtheall imgui_deko3d.cpp (MIT, Copyright (C) 2024 Michael Theall)
//   scturtle/imgui_deko3d_example (MIT)
//
// Constants from PLAN-v0.7-deko3d-imgui.md §4 Week 1:
//   FB_NUM=2, FB_WIDTH=1280, FB_HEIGHT=720
//   CODEMEMSIZE=128KB, CMDMEMSIZE=1MB, IMAGE_POOL_SIZE=16MB
//
// v0.7.0-beta6: reverted BUG1 "fix" from beta5.
//   nwindowGetDefault() IS valid in LibraryApplet context: __nx_win_init() runs at
//   CRT startup AND is called explicitly at the end of __appInit in main.cpp, populating
//   libnx's g_defaultWin. The beta5 sphaira-style vi init sequence was incorrect —
//   the Horizon vi sysmodule rejects duplicate display opens from the same process
//   (result 2114-0009 = viOpenDefaultDisplay failed), causing the boot-loop.
//   Evidence: beta3 reached icon upload (swapchain had to have initialized); upstream
//   uLaunch SDL2 uses nwindowGetDefault(); libnx vi.c viCreateLayer internally calls
//   appletCreateManagedDisplayLayer when applet ARUID is present.
//   Fix: use nwindowGetDefault() directly. libnx owns the NWindow lifetime.

#include "imgui_impl_deko3d.h"

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include <switch.h>
#include <deko3d.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <optional>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr unsigned FB_NUM          = 2;
static constexpr unsigned FB_WIDTH        = 1280;
static constexpr unsigned FB_HEIGHT       = 720;
static constexpr uint32_t CODEMEMSIZE     = 128u * 1024u;    // 128 KB
static constexpr uint32_t CMDMEMSIZE      = 1u  * 1024u * 1024u; // 1 MB
static constexpr uint32_t IMAGE_POOL_SIZE = 16u * 1024u * 1024u; // 16 MB

// Maximum number of icon images that can be registered (font is slot 0).
// Slots 1..MAX_ICON_IMAGES are available for application icons.
static constexpr uint32_t MAX_ICON_IMAGES = 512u;

static constexpr uint32_t VTXBUF_SIZE = 1u * 1024u * 1024u; // 1 MB per slot
static constexpr uint32_t IDXBUF_SIZE = 1u * 1024u * 1024u; // 1 MB per slot

// ---------------------------------------------------------------------------
// Alignment helper
// ---------------------------------------------------------------------------
static constexpr uint32_t dk_align(uint32_t size, uint32_t align) {
    return (size + align - 1) & ~(align - 1);
}

// ---------------------------------------------------------------------------
// UBO layouts (match GLSL std140)
// ---------------------------------------------------------------------------
struct VertUBO {
    float projMtx[4][4];
};
struct FragUBO {
    uint32_t font;
    uint32_t _pad[3];
};

// ---------------------------------------------------------------------------
// Vertex attribute / buffer descriptors
// ---------------------------------------------------------------------------
static constexpr std::array<DkVtxAttribState, 3> VERTEX_ATTRIB_STATE = {{
    {0, 0, (uint32_t)offsetof(ImDrawVert, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    {0, 0, (uint32_t)offsetof(ImDrawVert, uv),  DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    {0, 0, (uint32_t)offsetof(ImDrawVert, col), DkVtxAttribSize_4x8,  DkVtxAttribType_Unorm, 0},
}};
static constexpr std::array<DkVtxBufferState, 1> VERTEX_BUFFER_STATE = {{
    {sizeof(ImDrawVert), 0},
}};

// ---------------------------------------------------------------------------
// Static backend state
// ---------------------------------------------------------------------------
namespace {

struct Deko3dState {
    // Core deko3d objects
    dk::UniqueDevice   device;
    dk::UniqueQueue    queue;

    // Memory pools
    dk::UniqueMemBlock cmdMemBlock;    // command buffer backing store
    dk::UniqueMemBlock codeMemBlock;   // shaders
    dk::UniqueMemBlock uboMemBlock;    // UBO (vert + frag)
    dk::UniqueMemBlock imageMemBlock;  // framebuffers + font texture pool
    dk::UniqueMemBlock fontStagingMemBlock; // transient upload block

    // Per-slot vertex/index buffers
    dk::UniqueMemBlock vtxMemBlock[FB_NUM];
    dk::UniqueMemBlock idxMemBlock[FB_NUM];

    // Framebuffer images
    dk::Image framebuffers[FB_NUM];

    // Swapchain
    dk::UniqueSwapchain swapchain;

    // Command buffer
    dk::UniqueCmdBuf cmdBuf;

    // Shaders
    dk::Shader vsh;
    dk::Shader fsh;

    // Font texture
    dk::UniqueMemBlock fontImageMemBlock;
    dk::Image          fontImage;

    // Descriptor sets — extended pool: 1 sampler + (1 + MAX_ICON_IMAGES) image slots.
    // Slot 0 = font texture, slots 1..MAX_ICON_IMAGES = application icons.
    dk::UniqueMemBlock descriptorMemBlock;
    dk::SamplerDescriptor samplerDescriptor;
    // (individual ImageDescriptors written directly into descriptorMemBlock)

    // Font texture handle
    DkResHandle fontHandle = 0;

    // Next free image descriptor slot (0 = font, first icon = 1)
    uint32_t nextImageSlot = 0;

    // Initialized flag
    bool initialized = false;
};

Deko3dState g_dk;

// Ortho matrix (row-major, maps [L,R]x[T,B] → clip space)
void BuildOrtho(float out[4][4], float L, float R, float T, float B) {
    memset(out, 0, sizeof(float) * 16);
    out[0][0] =  2.0f / (R - L);
    out[1][1] =  2.0f / (T - B);
    out[2][2] = -1.0f;
    out[3][0] = -(R + L) / (R - L);
    out[3][1] = -(T + B) / (T - B);
    out[3][3] =  1.0f;
}

bool LoadShaders() {
    // Compiled shaders live in romfs:/shaders/
    const char *paths[2] = {
        "romfs:/shaders/imgui_vsh.dksh",
        "romfs:/shaders/imgui_fsh.dksh",
    };
    dk::Shader *shaders[2] = { &g_dk.vsh, &g_dk.fsh };

    // Measure total code size
    uint32_t totalSize = DK_SHADER_CODE_UNUSABLE_SIZE;
    FILE *fps[2] = {};
    long sizes[2] = {};
    for (int i = 0; i < 2; i++) {
        fps[i] = fopen(paths[i], "rb");
        if (!fps[i]) {
            FILE *log = fopen("sdmc:/switch/qos-menu-init.log", "a");
            if (log) { fprintf(log, "[v0.7] BLOCKER: shader not found: %s\n", paths[i]); fclose(log); }
            return false;
        }
        fseek(fps[i], 0, SEEK_END);
        sizes[i] = ftell(fps[i]);
        rewind(fps[i]);
        totalSize += dk_align((uint32_t)sizes[i], DK_SHADER_CODE_ALIGNMENT);
    }

    g_dk.codeMemBlock =
        dk::MemBlockMaker{g_dk.device, dk_align(totalSize, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code)
            .create();

    auto *base = static_cast<uint8_t *>(g_dk.codeMemBlock.getCpuAddr());
    uint32_t offset = 0;
    for (int i = 0; i < 2; i++) {
        fread(base + offset, 1, (size_t)sizes[i], fps[i]);
        fclose(fps[i]);
        dk::ShaderMaker{g_dk.codeMemBlock, offset}.initialize(*shaders[i]);
        offset = dk_align(offset + (uint32_t)sizes[i], DK_SHADER_CODE_ALIGNMENT);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool ImGui_ImplDeko3d_Init() {
    auto &io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_deko3d";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    // ---- Create device ----
    g_dk.device = dk::DeviceMaker{}.create();

    // ---- Create graphics queue ----
    g_dk.queue = dk::QueueMaker{g_dk.device}
        .setFlags(DkQueueFlags_Graphics)
        .create();

    // ---- Allocate image memory pool (framebuffers + font atlas) ----
    g_dk.imageMemBlock =
        dk::MemBlockMaker{g_dk.device, IMAGE_POOL_SIZE}
            .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .create();
    uint32_t imagePoolOffset = 0;

    // ---- Create two framebuffer images ----
    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{g_dk.device}
        .setFlags(DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(FB_WIDTH, FB_HEIGHT)
        .initialize(fbLayout);

    const DkImage *fbPtrs[FB_NUM];
    for (unsigned i = 0; i < FB_NUM; i++) {
        imagePoolOffset = dk_align(imagePoolOffset, (uint32_t)fbLayout.getAlignment());
        g_dk.framebuffers[i].initialize(fbLayout, g_dk.imageMemBlock, imagePoolOffset);
        imagePoolOffset += (uint32_t)fbLayout.getSize();
        fbPtrs[i] = &g_dk.framebuffers[i];
    }

    // ---- Obtain default NWindow (populated by __nx_win_init in __appInit) ----
    // __nx_win_init() runs at CRT startup and is explicitly called at the end of
    // __appInit in main.cpp, which calls viInitialize → viOpenDefaultDisplay →
    // viCreateLayer → nwindowCreateFromLayer → nwindowSetDimensions(1280,720) and
    // stores the result in libnx's global g_defaultWin.  nwindowGetDefault() returns
    // a pointer to that global — it is never NULL in our LibraryApplet context.
    // libnx __nx_win_exit() (called from __appExit) tears it down; we do not own it.
    NWindow *win = nwindowGetDefault();
    if (!win) {
        FILE *log = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (log) {
            fprintf(log, "[v0.7.0-beta6] FATAL: nwindowGetDefault() returned NULL\n");
            fclose(log);
        }
        return false;
    }
    {
        FILE *log = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (log) {
            fprintf(log, "[v0.7.0-beta6] nwindowGetDefault OK dim=%ux%u\n",
                    (unsigned)win->width, (unsigned)win->height);
            fclose(log);
        }
    }

    // ---- Create swapchain using the default NWindow ----
    g_dk.swapchain = dk::SwapchainMaker{g_dk.device, win, fbPtrs, FB_NUM}.create();

    // ---- Create command buffer ----
    g_dk.cmdBuf = dk::CmdBufMaker{g_dk.device}.create();

    // Allocate command memory
    g_dk.cmdMemBlock =
        dk::MemBlockMaker{g_dk.device, dk_align(CMDMEMSIZE, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
    // The command buffer keeps using this backing store across frames, so it must
    // live for the backend lifetime rather than as a stack-local init temporary.
    g_dk.cmdBuf.addMemory(g_dk.cmdMemBlock, 0, g_dk.cmdMemBlock.getSize());

    // ---- Allocate UBO memblock ----
    const uint32_t uboSize =
        dk_align(sizeof(VertUBO), DK_UNIFORM_BUF_ALIGNMENT) +
        dk_align(sizeof(FragUBO), DK_UNIFORM_BUF_ALIGNMENT);
    g_dk.uboMemBlock =
        dk::MemBlockMaker{g_dk.device, dk_align(uboSize, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();

    // ---- Allocate per-slot vertex/index buffers ----
    for (unsigned i = 0; i < FB_NUM; i++) {
        g_dk.vtxMemBlock[i] =
            dk::MemBlockMaker{g_dk.device, dk_align(VTXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
                .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                .create();
        g_dk.idxMemBlock[i] =
            dk::MemBlockMaker{g_dk.device, dk_align(IDXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
                .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                .create();
    }

    // ---- Descriptor set: 1 sampler + (1 + MAX_ICON_IMAGES) image slots ----
    // Slot 0 = font; slots 1..MAX_ICON_IMAGES reserved for application icons.
    const uint32_t totalImageSlots = 1u + MAX_ICON_IMAGES;
    const uint32_t descSize =
        dk_align(sizeof(dk::SamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT) +
        dk_align(sizeof(dk::ImageDescriptor) * totalImageSlots, DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    g_dk.descriptorMemBlock =
        dk::MemBlockMaker{g_dk.device, dk_align(descSize, DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();

    auto *descBase = static_cast<uint8_t *>(g_dk.descriptorMemBlock.getCpuAddr());
    auto *samplerPtr = reinterpret_cast<dk::SamplerDescriptor *>(descBase);
    // Image descriptor array starts after the sampler
    auto *imageArray = reinterpret_cast<dk::ImageDescriptor *>(
        descBase + dk_align(sizeof(dk::SamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT));
    // Alias the first slot for the font (used below)
    auto *imagePtr = &imageArray[0];

    samplerPtr->initialize(
        dk::Sampler{}
            .setFilter(DkFilter_Linear, DkFilter_Linear)
            .setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

    // ---- Load shaders ----
    if (!LoadShaders()) {
        // Shaders not compiled yet — this is a known Week 1 blocker if uam hasn't been run.
        // Init continues so the rest of the infrastructure is confirmed working.
        // main.cpp will skip the render loop if !g_dk.initialized.
        FILE *log = fopen("sdmc:/switch/qos-menu-init.log", "a");
        if (log) { fputs("[v0.7] WARNING: shader load failed — render loop disabled\n", log); fclose(log); }
        return false;
    }

    // ---- Build font atlas + upload to GPU ----
    unsigned char *pixels = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &fw, &fh);

    // Staging block
    g_dk.fontStagingMemBlock =
        dk::MemBlockMaker{g_dk.device, dk_align((uint32_t)(fw * fh), DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
    memcpy(g_dk.fontStagingMemBlock.getCpuAddr(), pixels, (size_t)(fw * fh));

    // Font image in pool
    dk::ImageLayout fontLayout;
    dk::ImageLayoutMaker{g_dk.device}
        .setFlags(0)
        .setFormat(DkImageFormat_R8_Unorm)
        .setDimensions((uint32_t)fw, (uint32_t)fh)
        .initialize(fontLayout);

    const uint32_t fontAlign = (uint32_t)fontLayout.getAlignment();
    const uint32_t fontSize  = (uint32_t)fontLayout.getSize();
    g_dk.fontImageMemBlock =
        dk::MemBlockMaker{g_dk.device,
            dk_align(fontSize, fontAlign > DK_MEMBLOCK_ALIGNMENT ? fontAlign : DK_MEMBLOCK_ALIGNMENT)}
            .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .create();

    g_dk.fontImage.initialize(fontLayout, g_dk.fontImageMemBlock, 0);
    imagePtr->initialize(g_dk.fontImage);

    // Create font texture handle: image=0, sampler=0
    g_dk.fontHandle = dkMakeTextureHandle(0, 0);
    // v0.7.0-beta5: ImGui 1.92 replaced ImFontAtlas::SetTexID(ImTextureID) with the
    // ImTextureData-based flow.  The old path asserts TexRef._TexData != NULL then
    // immediately dereferences it — crashing when _TexData is null (legacy usage).
    // GetTexDataAsAlpha8() calls Build() which populates io.Fonts->TexData, so
    // TexData is always valid here.  We call SetTexID/SetStatus on the TexData object
    // directly; this is exactly what modern backends (imgui_impl_opengl3 etc.) do.
    IM_ASSERT(io.Fonts->TexData != nullptr && "Build() should have populated TexData");
    io.Fonts->TexData->SetTexID((ImTextureID)(ImU64)g_dk.fontHandle);
    io.Fonts->TexData->SetStatus(ImTextureStatus_OK);

    // Upload font texture
    dk::ImageView fontView{g_dk.fontImage};
    g_dk.cmdBuf.copyBufferToImage(
        {g_dk.fontStagingMemBlock.getGpuAddr(), 0, 0},
        fontView,
        {0, 0, 0, (uint32_t)fw, (uint32_t)fh, 1});
    g_dk.queue.submitCommands(g_dk.cmdBuf.finishList());
    g_dk.queue.waitIdle();

    // Bind descriptor sets — expose full pool so icon images are addressable
    g_dk.cmdBuf.bindSamplerDescriptorSet(g_dk.descriptorMemBlock.getGpuAddr(), 1);
    g_dk.cmdBuf.bindImageDescriptorSet(
        g_dk.descriptorMemBlock.getGpuAddr() +
            dk_align(sizeof(dk::SamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT),
        1u + MAX_ICON_IMAGES);
    g_dk.queue.submitCommands(g_dk.cmdBuf.finishList());
    g_dk.queue.waitIdle();

    // Font occupies slot 0; next free slot for icons starts at 1
    g_dk.nextImageSlot = 1;

    g_dk.initialized = true;
    return true;
}

void ImGui_ImplDeko3d_Shutdown() {
    if (!g_dk.initialized) return;

    g_dk.queue.waitIdle();

    g_dk.fontStagingMemBlock = nullptr;
    g_dk.fontImageMemBlock   = nullptr;
    g_dk.descriptorMemBlock  = nullptr;
    for (unsigned i = 0; i < FB_NUM; i++) {
        g_dk.vtxMemBlock[i] = nullptr;
        g_dk.idxMemBlock[i] = nullptr;
    }
    g_dk.cmdMemBlock = nullptr;
    g_dk.uboMemBlock  = nullptr;
    g_dk.codeMemBlock = nullptr;
    // Destroy swapchain before the NWindow it references is torn down.
    // The NWindow is owned by libnx (__nx_win_exit in __appExit tears it down).
    g_dk.swapchain    = nullptr;
    g_dk.imageMemBlock = nullptr;
    g_dk.cmdBuf       = nullptr;
    g_dk.queue        = nullptr;
    g_dk.device       = nullptr;
    g_dk.nextImageSlot = 0;
    g_dk.initialized  = false;
}

void ImGui_ImplDeko3d_NewFrame() {
    // Nothing to do here for the skeleton; display size is fixed at 1280x720.
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)FB_WIDTH, (float)FB_HEIGHT);
}

void ImGui_ImplDeko3d_RenderDrawData(ImDrawData *drawData) {
    if (!g_dk.initialized) return;
    if (!drawData || drawData->CmdListsCount <= 0) return;

    const unsigned width  = (unsigned)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const unsigned height = (unsigned)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (width == 0 || height == 0) return;

    // Acquire next framebuffer slot
    const int slot = g_dk.queue.acquireImage(g_dk.swapchain);
    if (slot < 0) return;

    // ---- Bind render target ----
    dk::ImageView fbView{g_dk.framebuffers[slot]};
    g_dk.cmdBuf.bindRenderTargets({&fbView});

    // ---- Clear ----
    g_dk.cmdBuf.clearColor(0, DkColorMask_RGBA, 0.09f, 0.09f, 0.09f, 1.0f);

    // ---- Setup projection ----
    const float L = drawData->DisplayPos.x;
    const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float T = drawData->DisplayPos.y;
    const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

    VertUBO vertUBO;
    BuildOrtho(vertUBO.projMtx, L, R, T, B);

    const uint32_t vertUBOAligned = dk_align(sizeof(VertUBO), DK_UNIFORM_BUF_ALIGNMENT);
    const uint32_t fragUBOAligned = dk_align(sizeof(FragUBO), DK_UNIFORM_BUF_ALIGNMENT);

    // ---- Render state ----
    g_dk.cmdBuf.setViewports(0, {DkViewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f}});
    g_dk.cmdBuf.bindShaders(DkStageFlag_GraphicsMask, {&g_dk.vsh, &g_dk.fsh});
    g_dk.cmdBuf.bindUniformBuffer(DkStage_Vertex, 0,
        g_dk.uboMemBlock.getGpuAddr(), vertUBOAligned);
    g_dk.cmdBuf.pushConstants(g_dk.uboMemBlock.getGpuAddr(), vertUBOAligned,
        0, sizeof(VertUBO), &vertUBO);
    g_dk.cmdBuf.bindUniformBuffer(DkStage_Fragment, 0,
        g_dk.uboMemBlock.getGpuAddr() + vertUBOAligned, fragUBOAligned);
    g_dk.cmdBuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    g_dk.cmdBuf.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    g_dk.cmdBuf.bindColorWriteState(dk::ColorWriteState{});
    g_dk.cmdBuf.bindDepthStencilState(dk::DepthStencilState{}.setDepthTestEnable(false));
    g_dk.cmdBuf.bindBlendStates(0, {dk::BlendState{}.setFactors(
        DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
        DkBlendFactor_InvSrcAlpha, DkBlendFactor_Zero)});
    g_dk.cmdBuf.bindVtxAttribState(VERTEX_ATTRIB_STATE);
    g_dk.cmdBuf.bindVtxBufferState(VERTEX_BUFFER_STATE);

    // ---- Grow buffers if needed ----
    {
        const size_t needVtx = (size_t)drawData->TotalVtxCount * sizeof(ImDrawVert);
        if (g_dk.vtxMemBlock[slot].getSize() < needVtx) {
            const size_t count = (size_t)(drawData->TotalVtxCount * 1.1f);
            g_dk.vtxMemBlock[slot] =
                dk::MemBlockMaker{g_dk.device, dk_align((uint32_t)(count * sizeof(ImDrawVert)), DK_MEMBLOCK_ALIGNMENT)}
                    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                    .create();
        }
        const size_t needIdx = (size_t)drawData->TotalIdxCount * sizeof(ImDrawIdx);
        if (g_dk.idxMemBlock[slot].getSize() < needIdx) {
            const size_t count = (size_t)(drawData->TotalIdxCount * 1.1f);
            g_dk.idxMemBlock[slot] =
                dk::MemBlockMaker{g_dk.device, dk_align((uint32_t)(count * sizeof(ImDrawIdx)), DK_MEMBLOCK_ALIGNMENT)}
                    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                    .create();
        }
    }

    auto *cpuVtx = static_cast<uint8_t *>(g_dk.vtxMemBlock[slot].getCpuAddr());
    auto *cpuIdx = static_cast<uint8_t *>(g_dk.idxMemBlock[slot].getCpuAddr());
    const DkGpuAddr gpuVtx = g_dk.vtxMemBlock[slot].getGpuAddr();
    const DkGpuAddr gpuIdx = g_dk.idxMemBlock[slot].getGpuAddr();

    g_dk.cmdBuf.bindVtxBuffer(0, gpuVtx, g_dk.vtxMemBlock[slot].getSize());
    g_dk.cmdBuf.bindIdxBuffer(DkIdxFormat_Uint16, gpuIdx);

    // ---- Draw command lists ----
    std::optional<DkResHandle> boundTex;
    const auto clipOff   = drawData->DisplayPos;
    const auto clipScale = drawData->FramebufferScale;

    size_t offsetVtx = 0;
    size_t offsetIdx = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const auto &cl = *drawData->CmdLists[n];

        const size_t vtxSize = (size_t)cl.VtxBuffer.Size * sizeof(ImDrawVert);
        const size_t idxSize = (size_t)cl.IdxBuffer.Size * sizeof(ImDrawIdx);

        memcpy(cpuVtx + offsetVtx, cl.VtxBuffer.Data, vtxSize);
        memcpy(cpuIdx + offsetIdx, cl.IdxBuffer.Data, idxSize);

        for (const auto &cmd : cl.CmdBuffer) {
            if (cmd.UserCallback) {
                g_dk.queue.submitCommands(g_dk.cmdBuf.finishList());
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState) {
                    // re-submit setup omitted for skeleton; Week 2 will wire this
                } else {
                    cmd.UserCallback(&cl, &cmd);
                }
                continue;
            }

            // Scissor
            ImVec4 clip{
                (cmd.ClipRect.x - clipOff.x) * clipScale.x,
                (cmd.ClipRect.y - clipOff.y) * clipScale.y,
                (cmd.ClipRect.z - clipOff.x) * clipScale.x,
                (cmd.ClipRect.w - clipOff.y) * clipScale.y,
            };
            if (clip.x >= (float)width || clip.y >= (float)height ||
                clip.z < 0.0f || clip.w < 0.0f) continue;
            if (clip.x < 0.0f) clip.x = 0.0f;
            if (clip.y < 0.0f) clip.y = 0.0f;
            if (clip.z > (float)width)  clip.z = (float)width;
            if (clip.w > (float)height) clip.w = (float)height;

            g_dk.cmdBuf.setScissors(0, {DkScissor{
                (unsigned)clip.x, (unsigned)clip.y,
                (unsigned)(clip.z - clip.x), (unsigned)(clip.w - clip.y)}});

            // Texture binding
            const DkResHandle texHandle = (DkResHandle)(ImU64)cmd.GetTexID();
            if (!boundTex || texHandle != *boundTex) {
                const bool isFont = (texHandle == g_dk.fontHandle);
                FragUBO fragUBO{isFont ? 1u : 0u, {0, 0, 0}};
                g_dk.cmdBuf.pushConstants(
                    g_dk.uboMemBlock.getGpuAddr() + vertUBOAligned, fragUBOAligned,
                    0, sizeof(FragUBO), &fragUBO);
                boundTex = texHandle;
                g_dk.cmdBuf.bindTextures(DkStage_Fragment, 0, {texHandle});
            }

            g_dk.cmdBuf.drawIndexed(DkPrimitive_Triangles,
                cmd.ElemCount, 1,
                (uint32_t)(cmd.IdxOffset + offsetIdx / sizeof(ImDrawIdx)),
                (int32_t)(cmd.VtxOffset  + offsetVtx / sizeof(ImDrawVert)),
                0);
        }

        offsetVtx += vtxSize;
        offsetIdx += idxSize;
    }

    g_dk.queue.submitCommands(g_dk.cmdBuf.finishList());
    g_dk.queue.presentImage(g_dk.swapchain, slot);
}

// ---------------------------------------------------------------------------
// Icon image registration (used by qos_icon_upload)
// ---------------------------------------------------------------------------

bool ImGui_ImplDeko3d_GetHandles(ImplDeko3dHandles *out) {
    if (!out || !g_dk.initialized) return false;
    out->dk_device           = static_cast<void *>(&g_dk.device);
    out->dk_queue            = static_cast<void *>(&g_dk.queue);
    out->image_memblock      = static_cast<void *>(&g_dk.imageMemBlock);
    out->image_memblock_size = IMAGE_POOL_SIZE;
    return true;
}

// Register a DkImage* with the extended descriptor pool.
// Writes a DkImageDescriptor into the next free slot (after font at slot 0).
// Rebinds the image descriptor set so the new slot is immediately addressable.
// Returns dkMakeTextureHandle(slot, 0) cast to ImTextureID, or 0 on failure.
ImTextureID ImGui_ImplDeko3d_RegisterImage(void *dk_image_ptr) {
    if (!g_dk.initialized) return 0;
    if (!dk_image_ptr) return 0;
    if (g_dk.nextImageSlot >= 1u + MAX_ICON_IMAGES) return 0;  // pool full

    auto *img = static_cast<dk::Image *>(dk_image_ptr);
    const uint32_t slot = g_dk.nextImageSlot++;

    // Write descriptor into the extended pool at the chosen slot.
    auto *descBase = static_cast<uint8_t *>(g_dk.descriptorMemBlock.getCpuAddr());
    auto *imageArray = reinterpret_cast<dk::ImageDescriptor *>(
        descBase + dk_align(sizeof(dk::SamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT));
    imageArray[slot].initialize(*img);

    // Re-bind the image descriptor set so the GPU sees the new entry.
    g_dk.cmdBuf.bindImageDescriptorSet(
        g_dk.descriptorMemBlock.getGpuAddr() +
            dk_align(sizeof(dk::SamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT),
        1u + MAX_ICON_IMAGES);
    g_dk.queue.submitCommands(g_dk.cmdBuf.finishList());
    g_dk.queue.waitIdle();

    // Sampler slot 0 (linear clamp) is shared for all icons.
    const DkResHandle handle = dkMakeTextureHandle(slot, 0);
    return (ImTextureID)(ImU64)handle;
}
