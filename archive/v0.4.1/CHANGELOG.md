# uLaunch Q OS Fork — v0.4.1 Changelog

Date: 2026-04-18

## Summary

OOM crash fix for PlasmaWallpaperElement in uMenu (TID 0x0100000000001000).
Crash manifest: 2168-0002 (Data Abort) on main-menu transition after user-select.

---

## Root Cause (v0.4.0)

`OnRender()` called `std::vector<u32>(1280*720)` on every frame — 3,686,400 bytes
allocated and freed per frame at ~60 fps.  After a user-select transition the
library-applet memory pool (~50 MB) is fragmented; the allocation fails silently
(the vector constructor does not throw with libnx's allocator under -fno-exceptions,
it returns an empty/invalid internal state), and the subsequent 1x1 `RenderRectangleFill`
loop of 921,600 iterations derefs the invalid buffer pointer, triggering the Data Abort.

Additionally, 921,600 individual `RenderRectangleFill(1, 1)` calls per frame drove
per-frame SDL2 CPU overhead roughly 920x higher than necessary.

---

## Fix Applied (v0.4.1)

### 1. Single-allocation framebuffer

`pixel_buf` is now a `std::unique_ptr<u32[]>` member of `PlasmaWallpaperElement`,
allocated once in the constructor via `new(std::nothrow)` and reused every frame.
Zero heap allocation occurs during `OnRender()`.

Memory: 1280 x 720 x 4 = 3,686,400 bytes, one-time at applet startup.

### 2. SDL_Texture blit path (one draw call per frame)

`plasma_tex` is an `SDL_Texture*` member created once in the constructor using:

    SDL_CreateTexture(GetMainRenderer(), SDL_PIXELFORMAT_ARGB8888,
                      SDL_TEXTUREACCESS_STREAMING, 1280, 720)

Each `OnRender()`:
  (a) runs the 5 pixel-math passes into `pixel_buf`
  (b) calls `SDL_UpdateTexture(plasma_tex, nullptr, buf, WpW * sizeof(u32))`
  (c) calls `drawer->RenderTexture(plasma_tex, elem_x, elem_y)` — one draw call

This replaces 921,600 `RenderRectangleFill` calls with a single texture blit.

SDL texture internal allocation: ~4 MB GPU-resident, applet-pool-allocated at startup,
not during transitions.  Total new startup cost: approximately 7-8 MB at ctor time
instead of ~3.6 MB re-allocated every frame.

### 3. Safety fallback

If `SDL_CreateTexture` or `new(std::nothrow)` returns nullptr (pool exhausted at startup):
- `OnRender()` falls back to a single `RenderRectangleFill` covering the full screen
  in the deep-space base colour (`#0A0A14`).  One call, zero heap use, safe.
- Logged to stderr: `[PLASMA] WARNING: SDL_CreateTexture failed (...) — solid-colour fallback active`

### 4. Destructor

`~PlasmaWallpaperElement()` explicitly calls `SDL_DestroyTexture(plasma_tex)` before
the unique_ptr auto-frees the pixel buffer.  Logged to stderr at dtor.

### 5. Telemetry

Constructor logs: `[PLASMA] ctor tex=<ptr> buf=<ptr>`
Destructor logs: `[PLASMA] dtor tex=<ptr> buf=<ptr>`
Every-60-frame log retained; added `tex=<ptr>` field.

### 6. PLASMA_WALLPAPER_ENABLED guard

`#if PLASMA_WALLPAPER_ENABLED` / `#else` / `#endif` preserved.
Default is 1 (enabled).  Setting to 0 at compile time produces a no-op `OnRender`.

---

## Pass signatures

All render pass private methods changed from `std::vector<u32> &buf` to `u32 *buf`.
The public/Element interface is unchanged.

---

## Files changed

- `projects/uMenu/include/ul/menu/ui/ui_PlasmaWallpaperElement.hpp`
- `projects/uMenu/source/ul/menu/ui/ui_PlasmaWallpaperElement.cpp`

No other uMenu files, Rust crates, or staging scripts were touched.

---

## Build

Toolchain: devkitPro devkitA64, switch-sdl2 2.28.5-3
Flags: `-DUL_MAJOR=1 -DUL_MINOR=0 -DUL_MICRO=1 -DUL_VERSION='"1.0.1"'`
Command: `make -j8 -C projects/uMenu`

Build output: `build/exefs/main` (same path as v0.4.0)

---

## SHA256 Hashes

| Artifact | SHA256 |
|----------|--------|
| v0.4.0 SdOut/ulaunch/bin/uMenu/main (before) | `3bc65f5328bad7b091b547c7c4cbc5264bf0b085613739debfcf0580b7da469b` |
| v0.4.1 SdOut/ulaunch/bin/uMenu/main (after)  | `471d81bc9ac073deab9e252a1b8463b4e2749131cedc1e25ef1b273b8f11c859` |

---

## Remaining Concern

SDL_UpdateTexture pushes the pixel buffer over the bus every frame.  On a 720p
streaming texture at 60 fps this is approximately 221 MB/s of CPU-to-GPU memory
bandwidth.  The Switch's Tegra X1 shared-memory architecture means this competes
with the applet's RAM bandwidth rather than a discrete PCIe bus, so it is bounded
by DRAM throughput rather than PCIe.  In practice the Plutonium-based uMenu
already uses SDL_Texture for all other UI elements (images, text), so the
SDL_UpdateTexture call is consistent with existing framework usage and the one-call
blit is still orders of magnitude cheaper than 921,600 individual draw calls.

If per-frame SDL_UpdateTexture bandwidth becomes a measured bottleneck in a future
profile, the next step is to pre-bake the static frame at startup (the scene is
seeded from a fixed RNG with no animation) and call SDL_UpdateTexture exactly once
at construction, then blit the static texture each frame with zero CPU math.  That
would reduce OnRender() to a single RenderTexture call with no pixel writes.
The current design keeps the animation-ready structure (frame_count, prng_state)
in place so that animated variants remain a one-function change.
