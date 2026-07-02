// overlay_TestLayer.cpp — implementation of T1 test-rectangle overlay.
// See overlay_TestLayer.hpp for design rationale.
//
// Recipe sourced from libtesla (WerWolv/libtesla) — proven production
// pattern for vi:m max-Z managed layer + nwindow + framebuffer:
//
//   1. viInitialize(Manager)
//   2. viOpenDefaultDisplay
//   3. viGetZOrderCountMax
//   4. viCreateManagedLayer(applet_resource_user_id)
//   5. viCreateLayer
//   6. viSetLayerSize / Position / Z
//   7. AddToLayerStack(layer_id, stack) ×8 — IManagerDisplayService cmd 6000
//   8. nwindowCreateFromLayer
//   9. framebufferCreate(RGBA_8888, double-buffered)
//   10. framebufferMakeLinear
//   11. Render thread: framebufferBegin → fill rect → framebufferEnd → vsync wait

#include <ul/system/overlay/overlay_TestLayer.hpp>
#include <ul/system/overlay/overlay_Renderer.hpp>
#include <ul/system/overlay/overlay_FontCache.hpp>
#include <ul/system/overlay/overlay_Actions.hpp>
#include <atomic>
#include <cstring>
#include <thread>

// libnx vi global — `viCreateLayer` reads this to open an existing managed
// layer instead of creating a new one (libnx vi.c source).  Tesla writes
// directly into this via viCreateManagedLayer's 4th param; we mirror that.
extern "C" u64 __nx_vi_layer_id;

namespace ul::system::overlay {

namespace {

    // ── Layer geometry ───────────────────────────────────────────────────
    // Logical layer size = 1280×720 (handheld).  Framebuffer is the same
    // size for a 1:1 mapping.  Tesla uses a smaller 448×720 framebuffer
    // scaled with FitToLayer (~74% memory savings); we may switch later
    // once chrome geometry stabilizes.
    constexpr u32 kScreenW = 1280;
    constexpr u32 kScreenH = 720;
    constexpr u32 kFbW     = 1280;
    constexpr u32 kFbH     = 720;

    // All ViLayerStack values per libnx vi.h.  Tesla adds the overlay to
    // ALL of these so it shows up regardless of what's being captured
    // (screenshots, recording, last-frame transition, etc.).
    constexpr ViLayerStack kAllStacks[] = {
        ViLayerStack_Default,
        ViLayerStack_Lcd,
        ViLayerStack_Screenshot,
        ViLayerStack_Recording,
        ViLayerStack_LastFrame,
        ViLayerStack_Arbitrary,
        ViLayerStack_ApplicationForDebug,
        ViLayerStack_Null,
    };
    constexpr size_t kAllStacksCount = sizeof(kAllStacks) / sizeof(kAllStacks[0]);

    // ── State ────────────────────────────────────────────────────────────
    std::atomic_bool g_Initialized   = false;
    std::atomic_bool g_RenderRunning = false;
    std::thread      g_RenderThread;

    ViDisplay   g_Display      = {};
    ViLayer     g_Layer        = {};
    NWindow     g_Window       = {};
    Framebuffer g_Framebuffer  = {};
    Event       g_VsyncEvent   = {};
    // managed layer id lives in the libnx global `__nx_vi_layer_id`
    // (we wrote into it via viCreateManagedLayer's 4th param)

    // ── IPC helper: IManagerDisplayService cmd 6000 AddToLayerStack ───────
    // libnx exposes viGetSession_IManagerDisplayService() but not a
    // viAddToLayerStack wrapper, so we dispatch the IPC ourselves.
    // Signature per switchbrew: IN { u32 stack, u64 layer_id }, no OUT.
    Result AddToLayerStack(ViLayerStack stack, u64 layer_id) {
        Service *srv = viGetSession_IManagerDisplayService();
        if (srv == nullptr) {
            return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        }
        const struct {
            u32 stack;
            u64 layer_id;
        } in = { (u32)stack, layer_id };
        return serviceDispatchIn(srv, 6000, in);
    }

    // ── T2.0 chrome border geometry ──────────────────────────────────────
    // 4-pixel cyan border outline around the entire screen.  Aligns with
    // qd_WmConstants.hpp's FOCUS_RING_THICKNESS=3 + a 1-pixel safety
    // margin so the stroke is clearly visible on a 1280×720 display
    // (and after FitToLayer scaling, on a 1920×1080 docked display).
    //
    // The border draws around the FULL framebuffer.  Future T5 will inset
    // it when a "window mode" config gates which area gets framed.
    constexpr s32 kBorderThickness = 4;
    constexpr Color4444 kBorderColor = kColorAccentCyan;

    // ── T2.2 title bar geometry ─────────────────────────────────────────
    // Sits just below the border (kBorderThickness=4) at y=4..46.
    // TITLEBAR_H=42 mirrors qd_WmConstants.hpp for visual continuity with
    // uMenu's in-applet QdWindow chrome — eventually the two unify.
    constexpr s32 kTitlebarH = 42;
    constexpr s32 kTitlebarY = kBorderThickness;          // y=4
    constexpr Color4444 kTitlebarBg = kColorSurfaceDark;  // opaque navy
    constexpr s32 kTitleTextSize = 26;                    // px height
    constexpr Color4444 kTitleTextColor = kColorWhite;
    constexpr s32 kTitleTextLeftPad = 16;                 // px from titlebar left

    // ── T2.3 right-side action buttons ──────────────────────────────────
    // Close X + minimize stacked on the right side of the title bar.
    // Each button is a square hit-box; the glyph renders centered inside.
    //
    // Layout (from right edge of titlebar working inward):
    //   [ 12px pad ] [ CLOSE 32×32 ] [ 8px gap ] [ MIN 32×32 ] [ 12px pad ]
    //
    // Hit-box rects are exposed as constexpr so T3 can reuse them for
    // touch-point hit-testing without duplicating geometry math.
    constexpr s32 kBtnSize     = 32;
    constexpr s32 kBtnPadRight = 12;  // right edge of titlebar to right edge of close button
    constexpr s32 kBtnGap      = 8;   // gap between close and minimize

    constexpr s32 kCloseBtnX = (s32)kFbW - kBorderThickness - kBtnPadRight - kBtnSize;
    constexpr s32 kCloseBtnY = kTitlebarY + (kTitlebarH - kBtnSize) / 2;  // vertical center
    constexpr s32 kMinBtnX   = kCloseBtnX - kBtnGap - kBtnSize;
    constexpr s32 kMinBtnY   = kCloseBtnY;

    // Glyphs.  ✕ (U+2715) is in the Standard shared font; renders crisp
    // at 22px.  − (U+2212) for minimize — clean horizontal stroke.
    // Both glyphs are in the standard pl:u shared font (not NintendoExt;
    // NintendoExt is for physical controller-button symbols).
    constexpr const char *kCloseGlyph = "\xE2\x9C\x95";  // ✕ UTF-8
    constexpr const char *kMinGlyph   = "\xE2\x88\x92";  // − UTF-8
    constexpr s32 kBtnGlyphSize       = 22;
    constexpr Color4444 kBtnGlyphColor = kColorWhite;
    // Hover/idle background (for T3+ visual feedback; for T2.3 we just
    // render the glyph with no bg, keeping the titlebar's navy).
    // Future T3 will paint a hover bg here when touch is over the button.

    // ── T3.0 hit-test helper ────────────────────────────────────────────
    static inline bool PointInRect(s32 px, s32 py, s32 rx, s32 ry, s32 rw, s32 rh) {
        return (px >= rx) && (px < rx + rw) && (py >= ry) && (py < ry + rh);
    }

    // ── T3.0 touch input poll ───────────────────────────────────────────
    //
    // DISABLED 2026-05-19 (rollback): function kept around for T3.0.1
    // resurrection but not called from RenderLoop — see the disabled
    // call site there.  Marked [[maybe_unused]] so -Werror=unused-function
    // doesn't break the build.
    //
    // Per Swarm C's guidance: ordinary hidGetTouchScreenStates() — NOT
    // hid:sys.  We're a non-exclusive consumer: the underlying app still
    // gets its touch events normally; we only react if a press lands on
    // OUR chrome button rects.  Press-edge detection so each tap fires
    // exactly once.
    //
    // Touchscreen coordinates are in the LCD's 1280×720 grid (handheld);
    // our overlay framebuffer is also 1280×720 → coordinates match 1:1.
    // (Docked mode has no touchscreen, so this code is a no-op there.)
    [[maybe_unused]] void PollTouchAndDispatch() {
        static bool s_prev_touched = false;
        HidTouchScreenState state = {};
        const s32 ret = hidGetTouchScreenStates(&state, 1);
        const bool now_touched = (ret > 0) && (state.count > 0);

        if (now_touched && !s_prev_touched) {
            // Rising edge — exactly one fire per tap.
            const s32 tx = (s32)state.touches[0].x;
            const s32 ty = (s32)state.touches[0].y;

            if (PointInRect(tx, ty, kCloseBtnX, kCloseBtnY, kBtnSize, kBtnSize)) {
                g_RequestClose.store(true, std::memory_order_release);
                UL_LOG_INFO("overlay: touch press hit CLOSE button at (%d,%d) — request set", tx, ty);
            } else if (PointInRect(tx, ty, kMinBtnX, kMinBtnY, kBtnSize, kBtnSize)) {
                g_RequestMinimize.store(true, std::memory_order_release);
                UL_LOG_INFO("overlay: touch press hit MINIMIZE button at (%d,%d) — request set", tx, ty);
            }
        }
        s_prev_touched = now_touched;
    }

    // ── Render thread: draws chrome + test rect every vsync ──────────────
    //
    // Tesla pattern: vsync wait → framebufferBegin → draw → framebufferEnd.
    // Vsync timeout is generous (1s) — applet transitions can stall vsync.
    // On timeout we retry next frame instead of exiting.
    void RenderLoop() {
        UL_LOG_INFO("overlay: render thread starting (T3.0 with touch input)");
        Renderer renderer;
        u64 frame_counter = 0;
        u64 vsync_fail_counter = 0;

        while (g_RenderRunning.load(std::memory_order_relaxed)) {
            // 100 ms timeout — still 6× nominal 16.67 ms frame time, gives
            // plenty of headroom for compositor stalls without making a
            // single stall waste 60 nominal frames worth of wall time
            // (which a 1 s timeout did).  Per audit uSystem-4.5.
            const Result wait_rc = eventWait(&g_VsyncEvent, 100'000'000ULL);
            if (R_FAILED(wait_rc)) {
                if (++vsync_fail_counter % 60 == 1) {
                    UL_LOG_WARN("overlay: vsync wait rc=0x%08X (counter=%lu)",
                                wait_rc, vsync_fail_counter);
                }
                continue;
            }

            // T3.0 ROLLBACK 2026-05-19: touch polling DISABLED.
            //
            // Previous T3.0 build called hidGetTouchScreenStates from this
            // thread, which crashed Atmosphère at boot because uSystem
            // doesn't initialize hid (uMenu does — but uSystem and uMenu
            // are different processes).  Calling hid functions without
            // hidInitialize triggers a kernel abort → uSystem dies → AMS
            // fatal report shown to user.
            //
            // The fix is to call hidInitialize() properly during overlay
            // setup, but we need to validate it doesn't conflict with
            // uMenu's hid session.  T3.0.1 will revisit carefully.
            // PollTouchAndDispatch();

            u32 stride = 0;
            u8 *fb = static_cast<u8*>(framebufferBegin(&g_Framebuffer, &stride));
            if (fb == nullptr) {
                continue;
            }

            // Hand off to the Renderer abstraction.  All draws go through
            // primitives (FillRect, StrokeRect) — the Renderer handles
            // bounds checking and the per-pixel u16 write.
            renderer.Begin(fb, stride, kFbW, kFbH);

            // Per uSystem optimization audit 4.1 — region-clear, not full-frame.
            //
            // Static chrome overwrites the same pixels every frame.  The
            // transparent non-chrome region stays transparent because nothing
            // writes there.  We only need to Clear the FIRST few frames to
            // handle whatever garbage GPU memory holds at framebufferCreate
            // time (RGBA_4444 double-buffered → 2 buffers to initialize, so
            // we clear the first 3 frames to be safe across vsync swaps).
            //
            // Savings: 1.84 MB × ~60 Hz = 110 MB/s memory bandwidth eliminated
            // from steady state, down to one-shot init cost.
            static u32 s_initial_clears_remaining = 3;
            if (s_initial_clears_remaining > 0) {
                renderer.Clear(kColorTransparent);
                --s_initial_clears_remaining;
            }

            // T2.0: thin cyan border outline around the entire screen.
            renderer.StrokeRect(0, 0, (s32)kFbW, (s32)kFbH, kBorderThickness, kBorderColor);

            // T2.2: filled title bar at top, inside the border.
            // y=4..46 across the full width (minus the border).
            renderer.FillRect(kBorderThickness, kTitlebarY,
                              (s32)kFbW - 2 * kBorderThickness, kTitlebarH,
                              kTitlebarBg);
            // Thin cyan separator line under the title bar (gives a clean
            // visual edge between chrome and content below).
            renderer.FillRect(kBorderThickness, kTitlebarY + kTitlebarH - 1,
                              (s32)kFbW - 2 * kBorderThickness, 1,
                              kBorderColor);

            // T2.2: title text — vertically centered in the title bar.
            // Baseline calc: bar mid-y + a small descent-aware offset.
            if (SharedFontCache().IsReady()) {
                // Layout metrics are constant once FontCache is ready —
                // lazy-init once and reuse every frame.  Was: 2 Ascent +
                // 2 MeasureString calls per frame (~4 cache lookups).
                // Now: 4 calls total, period.  Per audit uSystem-4.8.
                static bool s_metrics_inited = false;
                static s32  s_title_baseline = 0;
                static s32  s_close_x        = 0;
                static s32  s_min_x          = 0;
                static s32  s_btn_baseline   = 0;
                if (!s_metrics_inited) {
                    const s32 ascent = SharedFontCache().Ascent(kTitleTextSize);
                    s_title_baseline = kTitlebarY + kTitlebarH / 2 + ascent / 2 - 2;

                    const s32 btn_ascent     = SharedFontCache().Ascent(kBtnGlyphSize);
                    const s32 close_glyph_w  = SharedFontCache().MeasureString(kBtnGlyphSize, kCloseGlyph);
                    const s32 min_glyph_w    = SharedFontCache().MeasureString(kBtnGlyphSize, kMinGlyph);
                    s_close_x = kCloseBtnX + (kBtnSize - close_glyph_w) / 2;
                    s_min_x   = kMinBtnX   + (kBtnSize - min_glyph_w)   / 2;
                    s_btn_baseline = kCloseBtnY + kBtnSize / 2 + btn_ascent / 2 - 2;

                    s_metrics_inited = true;
                }

                SharedFontCache().DrawString(renderer,
                                             kBorderThickness + kTitleTextLeftPad,
                                             s_title_baseline,
                                             kTitleTextSize,
                                             "Q OS",
                                             kTitleTextColor);
                SharedFontCache().DrawString(renderer,
                                             s_close_x, s_btn_baseline,
                                             kBtnGlyphSize, kCloseGlyph, kBtnGlyphColor);
                SharedFontCache().DrawString(renderer,
                                             s_min_x,   s_btn_baseline,
                                             kBtnGlyphSize, kMinGlyph,   kBtnGlyphColor);
            }

            framebufferEnd(&g_Framebuffer);

            ++frame_counter;
            // Throttled to every 60 s (3600 frames @ 60Hz) — was every 5 s,
            // which produced ~17K entries/day for a steady-state log.
            if (frame_counter == 1 || frame_counter % 3600 == 0) {
                UL_LOG_INFO("overlay: rendered frame %lu (stride=%u, T2.0 chrome)",
                            frame_counter, stride);
            }
        }
        UL_LOG_INFO("overlay: render thread exiting (total frames=%lu, vsync_fails=%lu)",
                    frame_counter, vsync_fail_counter);
    }

}  // namespace

namespace {
    // Per uSystem audit 5.1 — stage tracker for partial-init cleanup.
    //
    // Previous design: each early-exit just `return rc`'d without rolling
    // back what was already opened.  If e.g. framebufferCreate failed at
    // Stage_NWindow we'd leak viInitialize + display + managed-layer +
    // layer + vsync-event + nwindow.  This left vi in a partial state
    // visible to anything else in uSystem that touched vi (e.g., the
    // capssc thread).
    //
    // New design: track which resources are open via this enum; on early
    // exit call CleanupPartial(stage_reached) which unwinds in reverse.
    enum InitStage : int {
        Stage_None                = 0,
        Stage_ViInit              = 1,  // viInitialize done (NOTE: we do
                                        // not viExit on cleanup — uSystem
                                        // owns vi for process lifetime)
        Stage_DisplayOpen         = 2,  // viOpenDefaultDisplay done
        Stage_ManagedLayerCreated = 3,  // viCreateManagedLayer done +
                                        // __nx_vi_layer_id is non-zero
        Stage_LayerCreated        = 4,  // viCreateLayer (opened the
                                        // managed layer into g_Layer)
        Stage_VsyncEventOpen      = 5,  // viGetDisplayVsyncEvent done
        Stage_NWindowCreated      = 6,  // nwindowCreateFromLayer done
        Stage_FramebufferCreated  = 7,  // framebufferCreate done
                                        // (linearize succeeded means we
                                        // proceed to thread spawn — by
                                        // then we use the full-init
                                        // teardown path)
    };

    // Unwind resources opened up to and including `stage`.
    void CleanupPartial(InitStage stage) {
        if (stage >= Stage_FramebufferCreated) {
            framebufferClose(&g_Framebuffer);
        }
        if (stage >= Stage_NWindowCreated) {
            nwindowClose(&g_Window);
        }
        if (stage >= Stage_VsyncEventOpen) {
            eventClose(&g_VsyncEvent);
        }
        if (stage >= Stage_LayerCreated) {
            viCloseLayer(&g_Layer);
        }
        if (stage >= Stage_ManagedLayerCreated) {
            viDestroyManagedLayer(&g_Layer);
            // Reset libnx vi global so the next Initialize attempt doesn't
            // reuse a destroyed id (mirrors the full-finalize fix from 1.11).
            __nx_vi_layer_id = 0;
        }
        if (stage >= Stage_DisplayOpen) {
            viCloseDisplay(&g_Display);
        }
        // Stage_ViInit deliberately NOT torn down — see header comment on
        // the enum.  uSystem owns vi for process lifetime.
    }
}  // namespace

Result InitializeTestLayer() {
    if (g_Initialized.load(std::memory_order_acquire)) {
        UL_LOG_INFO("overlay: InitializeTestLayer already done, ignoring repeat call");
        return ResultSuccess;
    }

    UL_LOG_INFO("overlay: InitializeTestLayer starting (T1 v2 — RGBA_4444 16bpp, __nx_vi_layer_id fix)");

    InitStage stage = Stage_None;

    // ── viInitialize(Manager) ────────────────────────────────────────────
    // Tesla's pattern.  vi globals are refcount-bumped on repeat calls so
    // this is safe even if uSystem's libnx default-window setup already
    // initialized vi.
    const Result vi_rc = viInitialize(ViServiceType_Manager);
    UL_LOG_INFO("overlay: viInitialize(Manager) rc=0x%08X", vi_rc);
    if (R_FAILED(vi_rc)) {
        // CleanupPartial not called — we haven't opened anything yet.
        return vi_rc;
    }
    stage = Stage_ViInit;

    // ── Open default display ─────────────────────────────────────────────
    Result rc = viOpenDefaultDisplay(&g_Display);
    UL_LOG_INFO("overlay: viOpenDefaultDisplay rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_DisplayOpen;

    // ── Get max-Z value for "render on top of everything" ────────────────
    s32 max_z = 0;
    rc = viGetZOrderCountMax(&g_Display, &max_z);
    UL_LOG_INFO("overlay: viGetZOrderCountMax rc=0x%08X max_z=%d", rc, max_z);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    // No new resource — stage unchanged.

    // ── Create managed layer — writes layer_id INTO __nx_vi_layer_id global ──
    // This is the CRITICAL FIX (per libnx vi.c source via swarm research).
    // viCreateLayer() reads `__nx_vi_layer_id` to know which managed layer
    // to open.  If __nx_vi_layer_id is 0 when viCreateLayer runs, it
    // creates a SEPARATE second layer via appletCreateManagedDisplayLayer,
    // leaking one managed layer + doubling parcel/binder allocations.
    // Tesla writes directly into the global as the 4th param here.
    const u64 aruid = appletGetAppletResourceUserId();
    rc = viCreateManagedLayer(&g_Display, (ViLayerFlags)0, aruid, &__nx_vi_layer_id);
    UL_LOG_INFO("overlay: viCreateManagedLayer aruid=0x%016lX rc=0x%08X layer_id=0x%016lX",
                aruid, rc, __nx_vi_layer_id);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_ManagedLayerCreated;

    // ── Open the (just-created) managed layer ────────────────────────────
    // viCreateLayer reads __nx_vi_layer_id and opens THAT layer (no second
    // create).  See libnx nx/source/services/vi.c viCreateLayer impl.
    rc = viCreateLayer(&g_Display, &g_Layer);
    UL_LOG_INFO("overlay: viCreateLayer (opens managed) rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_LayerCreated;

    // ── Set scaling + size + position + Z ────────────────────────────────
    // viSetLayerScalingMode + viSetLayerSize MUST precede Z/stack-add per
    // Tesla.  Failures here are soft (Tesla treats them the same way).
    rc = viSetLayerScalingMode(&g_Layer, ViScalingMode_FitToLayer);
    UL_LOG_INFO("overlay: viSetLayerScalingMode rc=0x%08X", rc);

    rc = viSetLayerSize(&g_Layer, kScreenW, kScreenH);
    UL_LOG_INFO("overlay: viSetLayerSize(%u,%u) rc=0x%08X", kScreenW, kScreenH, rc);

    rc = viSetLayerPosition(&g_Layer, 0.0f, 0.0f);
    UL_LOG_INFO("overlay: viSetLayerPosition(0,0) rc=0x%08X", rc);

    rc = viSetLayerZ(&g_Layer, max_z);
    UL_LOG_INFO("overlay: viSetLayerZ(%d) rc=0x%08X", max_z, rc);

    // ── Register the layer on all 8 ViLayerStacks ────────────────────────
    // Tesla pattern: only by being in ALL stacks does the overlay appear
    // over screenshots, recording, last-frame transitions, etc.
    for (size_t i = 0; i < kAllStacksCount; ++i) {
        const Result stack_rc = AddToLayerStack(kAllStacks[i], __nx_vi_layer_id);
        UL_LOG_INFO("overlay: AddToLayerStack[%zu] stack=%d rc=0x%08X",
                    i, (int)kAllStacks[i], stack_rc);
        // Non-fatal — Null/Lcd may legitimately reject on some HOS revs.
    }

    // ── Vsync event ──────────────────────────────────────────────────────
    rc = viGetDisplayVsyncEvent(&g_Display, &g_VsyncEvent);
    UL_LOG_INFO("overlay: viGetDisplayVsyncEvent rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_VsyncEventOpen;

    // ── NWindow + Framebuffer (RGBA_4444 — half memory of RGBA_8888) ─────
    rc = nwindowCreateFromLayer(&g_Window, &g_Layer);
    UL_LOG_INFO("overlay: nwindowCreateFromLayer rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_NWindowCreated;

    rc = framebufferCreate(&g_Framebuffer, &g_Window, kFbW, kFbH,
                           PIXEL_FORMAT_RGBA_4444, 2);
    UL_LOG_INFO("overlay: framebufferCreate(%ux%u, RGBA_4444, 2 bufs) rc=0x%08X",
                kFbW, kFbH, rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    stage = Stage_FramebufferCreated;

    rc = framebufferMakeLinear(&g_Framebuffer);
    UL_LOG_INFO("overlay: framebufferMakeLinear rc=0x%08X", rc);
    if (R_FAILED(rc)) {
        CleanupPartial(stage);
        return rc;
    }
    // Linear shadow doesn't add a new stage — it's an attribute of
    // g_Framebuffer (released by framebufferClose).

    // ── Initialize font cache (stbtt + pl:u Standard shared font) ────────
    // Non-fatal: if pl/stbtt setup fails, the render loop just skips text
    // (border + test rect still render).  Logs which step failed.
    const Result font_rc = SharedFontCache().Initialize();
    UL_LOG_INFO("overlay: SharedFontCache().Initialize rc=0x%08X ready=%d",
                font_rc, SharedFontCache().IsReady() ? 1 : 0);

    // ── Spawn render thread ──────────────────────────────────────────────
    g_RenderRunning.store(true, std::memory_order_relaxed);
    g_RenderThread = std::thread(&RenderLoop);

    g_Initialized.store(true, std::memory_order_release);
    UL_LOG_INFO("overlay: InitializeTestLayer ✓ done — chrome border + test rect + text");
    return ResultSuccess;
}

void FinalizeTestLayer() {
    if (!g_Initialized.exchange(false, std::memory_order_acq_rel)) {
        return;  // never initialized OR already finalized — no-op
    }

    UL_LOG_INFO("overlay: FinalizeTestLayer starting");
    g_RenderRunning.store(false, std::memory_order_relaxed);
    if (g_RenderThread.joinable()) {
        g_RenderThread.join();
    }

    // Render thread is joined — safe to tear down the font cache (no more
    // DrawString calls in flight).
    SharedFontCache().Finalize();

    framebufferClose(&g_Framebuffer);
    nwindowClose(&g_Window);
    eventClose(&g_VsyncEvent);
    viCloseLayer(&g_Layer);
    viDestroyManagedLayer(&g_Layer);
    viCloseDisplay(&g_Display);
    // Reset libnx vi global so a subsequent InitializeTestLayer (hot-reload
    // path) doesn't try to reuse a destroyed managed-layer id.  Per audit
    // finding uSystem-1.11.
    __nx_vi_layer_id = 0;
    viExit();
    UL_LOG_INFO("overlay: FinalizeTestLayer ✓ done");
}

}  // namespace ul::system::overlay
