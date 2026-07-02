// qd_Transition.cpp — implementation of qd_Transition.hpp.
//
// The themed vertical GRADIENT is still produced in a single
// SDL_LockTexture/UnlockTexture CPU pass at 1280×720 native resolution
// (SDL_RenderCopy upscales to 1920×1080 at blit time, identical to
// qd_Wallpaper). No libm, no dynamic alloc in that pass.
//
// v3.6 (loading-splash pass — creator directive 2026-06-12):
//   The flat gradient ("weird blue screen") becomes a real loading splash.
//   On top of the baked gradient we composite — via a second
//   SDL_SetRenderTarget pass — a centred "Q OS" wordmark + a row of loading
//   dots + a "Loading…" caption, all using the existing Plutonium text path
//   (pu::ui::render::RenderText) and SDL primitives. NO new romfs assets.
//
//   The wordmark/dots/caption cannot be drawn inside the LockTexture CPU
//   loop (RenderText needs the GPU renderer), so the flow is:
//       1. SDL_CreateTexture(STREAMING) + Lock/Unlock → bake the gradient
//          (the historical path, unchanged below).
//       2. Re-create as a TARGET texture, copy the gradient in, then
//          SDL_SetRenderTarget and draw the wordmark + dots + caption over
//          it. This mirrors the render-to-target idiom already used in
//          qd_DesktopIcons.cpp / qd_WindowManager.cpp.
//   If step 2's render target is unsupported, we keep the bare gradient
//   (graceful — the splash just loses its overlay, never the whole fade).
//
//   v2.9.8 history (kept for context): the hardcoded cyan→lavender Q ring
//   was removed in favour of a theme-sourced gradient. v3.6 re-introduces a
//   Q OS wordmark, but as theme-coloured TEXT (not a hardcoded brand ramp),
//   so it still rebrands per theme. The splash content is a sensible DEFAULT
//   pending the creator's specific design intent.
//
// Pixel format: SDL_PIXELFORMAT_ABGR8888 == byte order [R,G,B,A] in RAM on
// AArch64 little-endian.  Same as qd_Wallpaper.

#include <ul/menu/qdesktop/qd_Transition.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>   // v2.9.8 — g_QdTheme for brand colors
#include <ul/ul_Result.hpp>          // UL_LOG_INFO / UL_LOG_WARN
#include <pu/ui/render/render_SDL2.hpp>
#include <SDL2/SDL.h>
#include <string>
#include <switch/arm/counter.h>   // armGetSystemTick / armTicksToNs
#include <switch/kernel/svc.h>    // svcSleepThread

namespace ul::menu::qdesktop {

    namespace {

        // ── Texture geometry ───────────────────────────────────────────────
        // 1280×720 = 3.5 MB at RGBA8888; matches qd_Wallpaper, fits the
        // Switch GPU pool alongside Plutonium's framebuffers.
        constexpr u32 BRAND_W = 1280;
        constexpr u32 BRAND_H = 720;

        // v2.9.8 — gradient endpoints sourced from g_QdTheme at texture build
        // time.  Top of the screen = theme's deepest surface tone
        // (desktop_bg → darker than wallpaper); bottom of the screen =
        // accent.  Produces a "fade from dark → theme accent" splash that
        // reads as the theme's signature colour without any Q OS reference.

        // ── Cached handle (single-instance over Application lifetime) ──────
        pu::sdl2::TextureHandle::Ref g_brand_fade_tex {};

        // ── File-local helpers ────────────────────────────────────────────

        // Linear interp 0..255 with 0..255 fraction (frac=0 → a, frac=255 → b).
        u8 LerpU8(u8 a, u8 b, u32 frac255) {
            const u32 fa = 255U - frac255;
            return static_cast<u8>(
                (static_cast<u32>(a) * fa + static_cast<u32>(b) * frac255) / 255U
            );
        }

        // v2.9.8 — clamp value to [lo, hi] without libm.
        u8 ClampU8(s32 v) {
            if (v < 0) return 0u;
            if (v > 255) return 255u;
            return static_cast<u8>(v);
        }

        // v2.9.8 — write a single ABGR8888 pixel using a vertical gradient
        // sourced from the active theme palette.  Top of the texture = a
        // darkened-bg (theme depth), bottom = accent-mixed-with-bg.  No Q
        // glyph, no Q OS branding — the loading screen is now a pure colour
        // fade that matches whatever theme is active.
        void WriteThemedPixel(u8 *p, s32 y,
                              const pu::ui::Color &top,
                              const pu::ui::Color &bot) {
            const u32 v_frac = (BRAND_H > 1U)
                ? (static_cast<u32>(y) * 255U) / (BRAND_H - 1U)
                : 0U;
            // ABGR8888 little-endian = bytes [R,G,B,A]
            p[0] = LerpU8(top.r, bot.r, v_frac);
            p[1] = LerpU8(top.g, bot.g, v_frac);
            p[2] = LerpU8(top.b, bot.b, v_frac);
            p[3] = 0xFFu;
        }

        // ── v3.6 loading-splash overlay helpers ───────────────────────────

        // Splash geometry, defined against the native 1280×720 texture/screen
        // (Plutonium upscales to 1080p at blit). Kept here so the baked
        // texture and the live DrawLoadingSplashFrame agree pixel-for-pixel.
        constexpr s32 SPLASH_W       = static_cast<s32>(BRAND_W);   // 1280
        constexpr s32 SPLASH_H       = static_cast<s32>(BRAND_H);   // 720
        constexpr s32 DOT_COUNT      = 3;        // 3-dot loading row
        constexpr s32 DOT_RADIUS     = 7;        // base dot radius
        constexpr s32 DOT_GAP        = 34;       // centre-to-centre spacing
        constexpr s32 DOT_ROW_DY     = 86;       // dot row offset below centre
        constexpr s32 CAPTION_DY     = 132;      // caption offset below centre

        // v3.6 — compute the two themed gradient endpoints from g_QdTheme.
        // Top = desktop_bg darkened (depth); bottom = accent mixed back toward
        // desktop_bg so the fade stays in-palette. Shared by the baked texture
        // (WriteThemedPixel loop) and the live frame (SDL gradient strips).
        void ThemeGradientEndpoints(pu::ui::Color &out_top,
                                    pu::ui::Color &out_bot) {
            const auto &accent = ::ul::menu::qdesktop::g_QdTheme.accent;
            const auto &bg     = ::ul::menu::qdesktop::g_QdTheme.desktop_bg;
            // Top: 60 % desktop_bg + 40 % black — keeps colour identity but
            // darkens into depth at the top edge.
            out_top = pu::ui::Color {
                ClampU8(static_cast<s32>(bg.r) * 60 / 100),
                ClampU8(static_cast<s32>(bg.g) * 60 / 100),
                ClampU8(static_cast<s32>(bg.b) * 60 / 100),
                0xFFu
            };
            // Bottom: 60 % accent + 40 % desktop_bg — accent-tinted but not a
            // blown-out saturated brand colour at the bottom edge.
            out_bot = pu::ui::Color {
                ClampU8((static_cast<s32>(accent.r) * 60 + static_cast<s32>(bg.r) * 40) / 100),
                ClampU8((static_cast<s32>(accent.g) * 60 + static_cast<s32>(bg.g) * 40) / 100),
                ClampU8((static_cast<s32>(accent.b) * 60 + static_cast<s32>(bg.b) * 40) / 100),
                0xFFu
            };
        }

        // v3.6 — filled circle via horizontal spans (no libm; integer
        // mid-point test). Used for the loading dots. Caller sets the draw
        // colour + blend mode first.
        void FillCircle(SDL_Renderer *r, s32 cx, s32 cy, s32 radius) {
            if (radius <= 0) {
                return;
            }
            for (s32 dy = -radius; dy <= radius; ++dy) {
                // half-width of the span at this row: floor(sqrt(r^2 - dy^2)).
                const s32 rr = radius * radius - dy * dy;
                if (rr < 0) {
                    continue;
                }
                s32 dx = 0;
                while ((dx + 1) * (dx + 1) <= rr) {
                    ++dx;
                }
                SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
            }
        }

        // v3.6 — paint the gradient as horizontal strips straight onto the
        // current render target (used by the LIVE frame, which has no baked
        // streaming texture). 16-px bands keep it cheap (~45 fills/frame).
        void FillThemeGradient(SDL_Renderer *r,
                               const pu::ui::Color &top,
                               const pu::ui::Color &bot) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            constexpr s32 kBand = 16;
            for (s32 y = 0; y < SPLASH_H; y += kBand) {
                const u32 frac = (SPLASH_H > 1)
                    ? (static_cast<u32>(y) * 255U) / static_cast<u32>(SPLASH_H - 1)
                    : 0U;
                SDL_SetRenderDrawColor(r,
                    LerpU8(top.r, bot.r, frac),
                    LerpU8(top.g, bot.g, frac),
                    LerpU8(top.b, bot.b, frac),
                    0xFFu);
                SDL_Rect band { 0, y, SPLASH_W, kBand };
                SDL_RenderFillRect(r, &band);
            }
        }

        // v3.6 — blit a RenderText texture centred horizontally at `centre_y`
        // (its top edge), then release it via DeleteTexture (RenderText
        // returns an LRU-cache-owned pointer — never SDL_DestroyTexture).
        // No-op if text is empty or rasterisation fails. Returns the rendered
        // height (0 on failure) so callers can stack lines.
        s32 BlitCentredText(SDL_Renderer *r,
                            const std::string &font_path,
                            const std::string &text,
                            const pu::ui::Color &clr,
                            s32 centre_y) {
            if (text.empty()) {
                return 0;
            }
            SDL_Texture *tex = pu::ui::render::RenderText(font_path, text, clr);
            if (tex == nullptr) {
                return 0;
            }
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            const SDL_Rect dst { (SPLASH_W - tw) / 2, centre_y, tw, th };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            pu::ui::render::DeleteTexture(tex);
            return static_cast<s32>(th);
        }

        // v3.6 — draw the animated loading-dot row centred horizontally, its
        // centre at vertical `cy`. `phase` selects which dot is "lit" (0..2);
        // pass a fixed phase for the static baked texture and a tick-derived
        // phase for the live frame. The lit dot is full accent + slightly
        // larger; the others are accent at reduced alpha — a gentle pulse.
        void DrawLoadingDots(SDL_Renderer *r, s32 cy, s32 phase) {
            const auto &accent = ::ul::menu::qdesktop::g_QdTheme.accent;
            const s32 total_w = (DOT_COUNT - 1) * DOT_GAP;
            const s32 start_x = (SPLASH_W - total_w) / 2;
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            for (s32 i = 0; i < DOT_COUNT; ++i) {
                const bool lit = (i == (phase % DOT_COUNT));
                const u8 a = lit ? 0xFFu : 0x66u;          // pulse via alpha
                const s32 rad = lit ? (DOT_RADIUS + 2) : DOT_RADIUS;
                SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, a);
                FillCircle(r, start_x + i * DOT_GAP, cy, rad);
            }
        }

        // v3.6 — draw the full splash overlay (wordmark + dots + caption) onto
        // the current render target, centred on the 1280×720 field. `phase`
        // animates the dot row. Shared by the baked texture (phase fixed) and
        // the live frame (phase from tick). Reads g_QdTheme for text colours.
        void DrawSplashOverlay(SDL_Renderer *r, s32 phase) {
            const std::string large_font =
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
            const std::string small_font =
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

            const auto &txt = ::ul::menu::qdesktop::g_QdTheme.text_primary;
            const auto &txt2 = ::ul::menu::qdesktop::g_QdTheme.text_secondary;

            // Wordmark — measure first so we can sit it just above centre with
            // the dots/caption stacked below the geometric mid-line.
            const std::string wordmark = "Q OS";
            SDL_Texture *wm = pu::ui::render::RenderText(large_font, wordmark, txt);
            int ww = 0, wh = 0;
            if (wm != nullptr) {
                SDL_QueryTexture(wm, nullptr, nullptr, &ww, &wh);
                const SDL_Rect dst {
                    (SPLASH_W - ww) / 2, (SPLASH_H / 2) - wh - 10, ww, wh
                };
                SDL_RenderCopy(r, wm, nullptr, &dst);
                pu::ui::render::DeleteTexture(wm);
            }

            // Loading dots + caption, stacked below the centre line.
            DrawLoadingDots(r, (SPLASH_H / 2) + DOT_ROW_DY, phase);
            BlitCentredText(r, small_font, "Loading\xE2\x80\xA6", txt2,
                            (SPLASH_H / 2) + CAPTION_DY);
        }

    }  // namespace

    pu::sdl2::TextureHandle::Ref GetBrandFadeTexture() {
        if (g_brand_fade_tex != nullptr) {
            return g_brand_fade_tex;
        }

        SDL_Renderer *r = pu::ui::render::GetMainRenderer();
        if (r == nullptr) {
            UL_LOG_WARN("qdesktop: GetBrandFadeTexture: NULL main renderer "
                        "— caller will fall back to solid color");
            return nullptr;
        }

        UL_LOG_INFO("qdesktop: SDL_CreateTexture(BRAND_FADE) %ux%u ABGR8888 STREAMING",
                    static_cast<unsigned>(BRAND_W),
                    static_cast<unsigned>(BRAND_H));
        SDL_Texture *tex = SDL_CreateTexture(r,
                                              SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              static_cast<int>(BRAND_W),
                                              static_cast<int>(BRAND_H));
        if (tex == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_CreateTexture(BRAND_FADE) failed: %s",
                        SDL_GetError());
            return nullptr;
        }

        void *locked = nullptr;
        int locked_pitch = 0;
        const int lock_rc = SDL_LockTexture(tex, nullptr, &locked, &locked_pitch);
        if (lock_rc != 0 || locked == nullptr) {
            UL_LOG_WARN("qdesktop: SDL_LockTexture(BRAND_FADE) rc=%d: %s",
                        lock_rc, SDL_GetError());
            SDL_DestroyTexture(tex);
            return nullptr;
        }

        // v2.9.8/v3.6 — theme-sourced gradient endpoints (shared with the live
        // DrawLoadingSplashFrame via ThemeGradientEndpoints). Top = darkened
        // desktop_bg (depth), bottom = accent mixed back toward desktop_bg.
        // Compute once outside the per-pixel loop.
        pu::ui::Color grad_top {};
        pu::ui::Color grad_bot {};
        ThemeGradientEndpoints(grad_top, grad_bot);

        u8 *buf = static_cast<u8*>(locked);
        for (s32 y = 0; y < static_cast<s32>(BRAND_H); ++y) {
            u8 *row = buf + (static_cast<s32>(locked_pitch) * y);
            for (s32 x = 0; x < static_cast<s32>(BRAND_W); ++x) {
                (void)x;
                WriteThemedPixel(row + x * 4, y, grad_top, grad_bot);
            }
        }

        SDL_UnlockTexture(tex);

        // v3.6 — composite the loading-splash overlay (Q OS wordmark + loading
        // dots + "Loading…" caption) on top of the baked gradient. RenderText
        // needs the GPU renderer, so we can't do this in the LockTexture loop
        // above; instead we draw into a second TARGET texture. The gradient
        // `tex` is the source; `splash` becomes the cached result.
        SDL_Texture *splash = SDL_CreateTexture(r,
                                                SDL_PIXELFORMAT_ABGR8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                static_cast<int>(BRAND_W),
                                                static_cast<int>(BRAND_H));
        bool composited = false;
        if (splash != nullptr) {
            SDL_Texture *prev_target = SDL_GetRenderTarget(r);
            if (SDL_SetRenderTarget(r, splash) == 0) {
                // Lay down the gradient, then the overlay. Phase 0 = first dot
                // lit (the baked texture is static; the fade compositor has no
                // per-frame hook, so the animation lives in
                // DrawLoadingSplashFrame instead).
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_RenderCopy(r, tex, nullptr, nullptr);
                DrawSplashOverlay(r, /*phase=*/0);
                SDL_SetRenderTarget(r, prev_target);
                composited = true;
            } else {
                // Render-target unsupported on this backend — drop the target
                // texture and keep the bare gradient (still a clean fade).
                UL_LOG_WARN("qdesktop: brand-fade splash SetRenderTarget failed:"
                            " %s — keeping bare gradient", SDL_GetError());
                SDL_DestroyTexture(splash);
                splash = nullptr;
            }
        } else {
            UL_LOG_WARN("qdesktop: SDL_CreateTexture(BRAND_FADE splash TARGET)"
                        " failed: %s — keeping bare gradient", SDL_GetError());
        }

        if (composited && splash != nullptr) {
            // Cache the composited splash; the gradient was only an
            // intermediate, so free it now.
            SDL_DestroyTexture(tex);
            g_brand_fade_tex = pu::sdl2::TextureHandle::New(splash);
            UL_LOG_INFO("qdesktop: brand-fade %ux%u themed loading splash — "
                        "gradient #%02X%02X%02X→#%02X%02X%02X + Q OS wordmark + "
                        "loading dots + caption (v3.6, default content)",
                        static_cast<unsigned>(BRAND_W),
                        static_cast<unsigned>(BRAND_H),
                        grad_top.r, grad_top.g, grad_top.b,
                        grad_bot.r, grad_bot.g, grad_bot.b);
        } else {
            // Fallback: cache the bare gradient (pre-v3.6 behaviour).
            g_brand_fade_tex = pu::sdl2::TextureHandle::New(tex);
            UL_LOG_INFO("qdesktop: brand-fade %ux%u themed gradient ONLY "
                        "(splash overlay unavailable) — top=#%02X%02X%02X "
                        "bot=#%02X%02X%02X (v3.6 fallback)",
                        static_cast<unsigned>(BRAND_W),
                        static_cast<unsigned>(BRAND_H),
                        grad_top.r, grad_top.g, grad_top.b,
                        grad_bot.r, grad_bot.g, grad_bot.b);
        }
        return g_brand_fade_tex;
    }

    void ReleaseBrandFadeTexture() {
        // shared_ptr -> 0; ~TextureHandle calls SDL_DestroyTexture.
        g_brand_fade_tex.reset();
    }

    // ── DrawLoadingSplashFrame ──────────────────────────────────────────────
    //
    // v3.6 — the ANIMATED loading splash, drawn live to `r` for one frame and
    // presented. Unlike GetBrandFadeTexture() (a static texture handed to
    // Plutonium's fade compositor, which has no per-frame hook), this is meant
    // to be called repeatedly from a loading loop with a rising `tick` so the
    // dot row visibly pulses across the load. Same look as the baked texture:
    // themed gradient + Q OS wordmark + loading dots + "Loading…" caption.
    // Reads g_QdTheme directly. No-op if the renderer is null.
    void DrawLoadingSplashFrame(SDL_Renderer *r, u32 tick) {
        if (r == nullptr) {
            UL_LOG_WARN("qdesktop: DrawLoadingSplashFrame: NULL renderer — skip");
            return;
        }

        // Themed gradient background (drawn as horizontal strips straight to
        // the framebuffer — no intermediate texture needed for the live path).
        pu::ui::Color grad_top {};
        pu::ui::Color grad_bot {};
        ThemeGradientEndpoints(grad_top, grad_bot);
        FillThemeGradient(r, grad_top, grad_bot);

        // Overlay with an animated dot phase. Advance roughly every ~12 frames
        // (~5 steps/sec at 60 Hz) so the pulse is calm, not strobing.
        const s32 phase = static_cast<s32>((tick / 12U) % static_cast<u32>(DOT_COUNT));
        DrawSplashOverlay(r, phase);

        // Present this frame now — caller drives the loop / cadence.
        SDL_RenderPresent(r);
    }

    // ── RunLoadingSplash ───────────────────────────────────────────────────
    //
    // v3.6 — time-bounded animated splash loop.  Drives DrawLoadingSplashFrame
    // at ~60 Hz for up to `max_ms` milliseconds, covering the NRO-body read gap
    // that occurs between smi::Launch* (fire-and-forget IPC) and
    // FadeOutToNonLibraryApplet()/Finalize().  The user sees an animated splash
    // instead of a frozen last uMenu frame.
    //
    // We do NOT pump applet messages inside the loop: a HOME press during this
    // window would already deadlock the handoff; keeping the loop tight and
    // max_ms modest is the correct mitigation.  Finalize()'s kill is the
    // clean exit.
    //
    // Time source: armGetSystemTick / armTicksToNs (already in use throughout
    // qd_DesktopIcons.cpp — no new dependency for that TU).
    void RunLoadingSplash(u32 max_ms) {
        SDL_Renderer *r = pu::ui::render::GetMainRenderer();
        if (r == nullptr) {
            UL_LOG_WARN("qdesktop: RunLoadingSplash: NULL renderer — skip");
            return;
        }

        UL_LOG_INFO("qdesktop: RunLoadingSplash: entering splash loop "
                    "(max_ms=%u)", static_cast<unsigned>(max_ms));

        const u64 deadline_ns =
            static_cast<u64>(max_ms) * 1'000'000ULL;
        const u64 start_tick = armGetSystemTick();
        u32 tick = 0U;

        while (true) {
            const u64 elapsed_ns =
                armTicksToNs(armGetSystemTick() - start_tick);
            if (elapsed_ns >= deadline_ns) {
                break;
            }
            DrawLoadingSplashFrame(r, tick);
            ++tick;
            // ~60 Hz cadence: sleep 16 ms between frames.
            svcSleepThread(16'000'000ULL);
        }

        UL_LOG_INFO("qdesktop: RunLoadingSplash: exiting after %u frames",
                    static_cast<unsigned>(tick));
    }

}  // namespace ul::menu::qdesktop
