// qd_Transition.hpp — Q OS branded fade-transition texture for QDESKTOP_MODE.
//
// Replaces the upstream uLaunch Background.png that Plutonium's fade compositor
// would otherwise paint between every LoadMenu transition (login → desktop,
// desktop → settings, etc.). Cycle C5 (SP4.11) reduced it to a solid panel
// colour; cycle D4 (SP4.12) upgrades that solid colour to a procedurally
// generated 1280×720 ABGR8888 texture containing:
//
//     1. Vertical gradient — PANEL_BG_DARK (top) → PANEL_BG_DEEP (bottom)
//     2. Centered Q glyph — annular ring + tail in a cyan → lavender ramp
//        (#00E5FF → #A78BFA, the Filament/Q OS brand palette)
//
// The texture is generated lazily on first GetBrandFadeTexture() call (which
// runs during the first SetBackgroundFade() — i.e. the first LoadMenu after
// the SDL renderer is up). It is cached in a single-instance shared_ptr so
// every subsequent fade reuses the GPU upload.
//
// Falls back gracefully (returns nullptr) if SDL_CreateTexture or
// SDL_LockTexture fails; SetBackgroundFade then keeps the C5 solid-colour
// path so the desktop never loses its fade transition entirely.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>

namespace ul::menu::qdesktop {

    // Returns the cached branded fade texture. First call generates and
    // uploads; later calls return the same handle. Returns nullptr on
    // SDL failure (caller falls back to solid colour).
    pu::sdl2::TextureHandle::Ref GetBrandFadeTexture();

    // Releases the cached texture. Called on full Application shutdown.
    // Idempotent.
    void ReleaseBrandFadeTexture();

}  // namespace ul::menu::qdesktop
