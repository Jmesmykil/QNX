// qd_Transition.hpp — Q OS themed loading splash for QDESKTOP_MODE.
//
// Replaces the upstream uLaunch Background.png that Plutonium's fade compositor
// would otherwise paint between every LoadMenu transition (login → desktop,
// desktop → settings, etc.). Cycle C5 (SP4.11) reduced it to a solid panel
// colour; cycle D4 (SP4.12) made it a flat themed gradient.
//
// v3.6 (loading-splash pass — creator directive 2026-06-12):
//   The flat gradient read as a "weird blue screen" during the login →
//   desktop hand-off. It is now a real, tasteful loading splash drawn
//   entirely from SDL_Renderer primitives + the existing Plutonium font
//   path (NO new romfs assets). Two entry points share one look:
//
//     1. GetBrandFadeTexture() — bakes a STATIC splash into a cached
//        1280×720 ABGR8888 texture that Plutonium's fade compositor blits
//        during a LoadMenu fade (it has no per-frame hook, so the dots are
//        drawn at a fixed phase here). Contents:
//          • subtle vertical theme gradient (desktop_bg depth → accent mix)
//          • centred "Q OS" wordmark (Plutonium RenderText, theme text)
//          • a row of loading dots in the accent colour
//          • a small "Loading…" caption
//
//     2. DrawLoadingSplashFrame(renderer, tick) — the ANIMATED version,
//        drawn live every frame from a caller's loading loop (mirrors
//        qd_Theme::DrawThemeTransitionFrame's one-shot present pattern).
//        Same gradient + wordmark + caption, but the dot row pulses based
//        on `tick` so the splash visibly moves across a longer load.
//
//   NOTE: the splash CONTENT (wordmark text, dot animation, caption) is a
//   sensible DEFAULT — the creator has not yet supplied a specific design
//   for "the loading splash for specific stuff", so this is intentionally
//   clean and theme-driven rather than bespoke.
//
// The static texture is generated lazily on first GetBrandFadeTexture()
// call (during the first SetBackgroundFade() after the SDL renderer is up)
// and cached in a single-instance shared_ptr so every later fade reuses the
// GPU upload.
//
// Falls back gracefully (returns nullptr / paints the bare gradient) if
// SDL_CreateTexture, SDL_SetRenderTarget or RenderText fail; SetBackgroundFade
// then keeps the solid-colour path so the desktop never loses its fade.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

    // Returns the cached themed loading-splash texture. First call generates
    // and uploads; later calls return the same handle. Returns nullptr on
    // SDL failure (caller falls back to solid colour).
    pu::sdl2::TextureHandle::Ref GetBrandFadeTexture();

    // Releases the cached texture. Called on full Application shutdown.
    // Idempotent.
    void ReleaseBrandFadeTexture();

    // Draws the ANIMATED loading splash directly to `r` for ONE frame, then
    // presents it. `tick` is a free-running frame/loop counter — the loading
    // dots pulse with it so repeated calls visibly animate. Use this from a
    // live loading loop where a real renderer is available (as opposed to the
    // baked GetBrandFadeTexture() used by Plutonium's fade compositor).
    // Reads g_QdTheme for all colours. No-op if `r` is null.
    void DrawLoadingSplashFrame(SDL_Renderer *r, u32 tick);

    // Runs the animated loading splash for up to `max_ms` milliseconds at
    // ~60 Hz, blocking the calling thread.  Call this AFTER smi::Launch* and
    // BEFORE FadeOutToNonLibraryApplet()/Finalize() to cover the NRO-body
    // read gap (the 0.5–2.5 s the user would otherwise see a frozen frame).
    //
    // Time-bounded (not NRO-ready-signal bounded) because uMenu cannot observe
    // NRO readiness: the launch IPC is fire-and-forget and uMenu Finalizes
    // itself immediately after.  Tune `max_ms` on HW; 2000 ms is a safe
    // starting point that covers even slow SD reads without hanging too long.
    //
    // Gets the renderer via pu::ui::render::GetMainRenderer() — same path
    // used by GetBrandFadeTexture() and DrawThemeTransitionFrame.  No-op
    // (returns immediately) if the renderer is null.
    void RunLoadingSplash(u32 max_ms);

}  // namespace ul::menu::qdesktop
