// overlay_TestLayer.hpp — v3.1 Phase 2 Slice T1: prove uSystem can create
// a vi:m max-Z managed layer + render to it via a framebuffer + thread.
//
// Strategic context (per creator 2026-05-19): Q OS is methodically taking
// over the system stack layer by layer.  This is the FIRST persistent
// overlay layer owned by Q OS — the foundation for v3.1's Tesla-style
// window chrome (will host title bar / close button / minimize in T2+).
//
// T1 scope: ONE 100×100 red rectangle in the top-right corner of the
// screen.  Must remain visible:
//   1. On the uMenu desktop
//   2. While ANY foreground library applet runs (homebrew NRO, system
//      applet, etc.) — proves vi:m max-Z + all-stack registration works
//   3. Across applet transitions (uMenu → NRO → uMenu)
//
// Lives in uSystem (the qlaunch process) because uSystem survives applet
// transitions.  uMenu can't own the overlay because uMenu finalizes when
// it hands off to another LA.
//
// Failure-tolerant: any setup error logs WARN and returns; the overlay
// just doesn't render but uSystem boots normally.  Cannot brick uMenu.
//
// See docs/50_v3.1_phase2_implementation_plan.md §v3.1 architecture pivot
// for the Tesla overlay model rationale and T1-T5 sequencing.

#pragma once
#include <switch.h>
#include <ul/ul_Result.hpp>

namespace ul::system::overlay {

    /// One-shot init from uSystem's main, after applet/vi services are up.
    /// Creates the managed layer, registers it to all 8 ViLayerStacks,
    /// sets up the framebuffer, and spawns the render thread.
    ///
    /// Safe to call exactly once.  Subsequent calls are no-ops (idempotent).
    ///
    /// Returns the result of the first failed setup step (vi/nwindow/
    /// framebuffer/thread), or ResultSuccess if everything came up.  The
    /// caller logs but should NOT abort uSystem on failure — uMenu must
    /// still boot even if the overlay can't render.
    Result InitializeTestLayer();

    /// Tear down — stop the render thread, close framebuffer, destroy the
    /// managed layer.  Called from uSystem's __appExit.  Idempotent.
    void FinalizeTestLayer();

}  // namespace ul::system::overlay
