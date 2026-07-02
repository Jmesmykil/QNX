// qd_Frame.hpp — QdWindow chrome component (v3.7 rebuild).
// Single responsibility: paint ALL window chrome (shadow, glow, nine-patch SVG frame,
// focus ring, disc buttons, title text, hint/tooltip text) and answer hit-test queries.
// No content, no scroll, no animation — those stay in QdWindow.
#pragma once
#ifdef QDESKTOP_MODE
#include <SDL2/SDL.h>
#include <pu/ui/ui_Types.hpp>
#include <string>
#include <switch.h>  // u8, s32

namespace ul::menu::qdesktop {

// ── Hit-test region ────────────────────────────────────────────────────────────
enum class FrameRegion : uint8_t {
    None,       // outside the window
    ResizeBR,   // BR resize grip
    Close,      // close disc
    Minimize,   // minimize disc
    Maximize,   // maximize/restore disc
    Titlebar,   // caption band (drag + double-tap-maximize)
    StatusBar,  // status bar (hint display)
    Client,     // content area
};

// ── Button visual state ────────────────────────────────────────────────────────
enum class BtnState : uint8_t { Normal, Hover, Pressed };

// ── Shared SVG source cache (A2-OPT-1) ────────────────────────────────────────
// Returns the process-singleton rasterized 640×400 texture for theme_idx,
// loading it on first call.  Returns nullptr if the SVG load fails.
SDL_Texture* GetSharedSvgSource(SDL_Renderer* r, int theme_idx);

// Evict the cached texture for theme_idx (call on theme change so the next
// Paint reloads the new theme's SVG master).
void InvalidateSvgCache(int theme_idx);

// ── QdFrame ────────────────────────────────────────────────────────────────────
class QdFrame {
public:
    // Chrome geometry — design-system truth for v3.7 (SVG masters are authoritative).
    // QdWindow uses these in place of the legacy TITLEBAR_H / BOTTOM_BAR_H (both 42).
    static constexpr int kBorder     =  1;   // 1-px border inset (accent ring)
    static constexpr int kTitlebarH  = 40;   // fixed titlebar height (SVG rect h=40)
    static constexpr int kStatusH    = 34;   // fixed status-bar height (SVG rect h=34)
    static constexpr int kBodyRadius = 14;   // body corner radius (SVG rx=14)
    static constexpr int kDiscDia    = 30;   // button disc diameter (v3.7.1: 24→30, easier to hit)
    static constexpr int kDiscGap    =  8;   // gap between adjacent discs
    static constexpr int kDiscInset  = 12;   // right inset from frame edge to close disc
    static constexpr int kDiscHitPad =  8;   // v3.7.1: extra touch padding around each disc (hit-zone only, not drawn)
    static constexpr int kGrip       = 26;   // BR resize hit-zone (square) (v3.7.1: 18→26)
    // SVG source master dimensions (all 10 theme masters are 640×400).
    static constexpr int kSrcW = 640;
    static constexpr int kSrcH = 400;
    // Nine-patch insets matching the 640×400 guide lines (§3.2 of 02-ARCHITECTURE.md).
    static constexpr int kInL = 24;
    static constexpr int kInR = 24;
    static constexpr int kInT = 40;   // = kTitlebarH (top band = full titlebar)
    static constexpr int kInB = 34;   // = kStatusH   (bottom band = full status bar)

    // ── The barrier: how much room is inside ──────────────────────────────────
    // client.x = frame.x + 1
    // client.y = frame.y + 40
    // client.w = frame.w - 2
    // client.h = frame.h - 74
    SDL_Rect ComputeClientRect(const SDL_Rect& frame) const;

    // ── Paint ALL chrome into r ───────────────────────────────────────────────
    // Order: shadow → focus glow → nine-patch SVG (or code-draw fallback) →
    //        focus ring → discs → title text → hint/tooltip text.
    // theme_idx: active theme pack index (drives SVG source cache key).
    // tip_tex: if non-null, replaces hint_tex in the status bar (button tooltip).
    void Paint(SDL_Renderer* r, const SDL_Rect& frame,
               bool focused, bool maximized,
               BtnState close_st, BtnState min_st, BtnState max_st,
               int theme_idx,
               const std::string& title,
               SDL_Texture* hint_tex, int hint_w, int hint_h,
               SDL_Texture* tip_tex,  int tip_w,  int tip_h,
               u8 alpha);

    // ── Hit-test (priority: grip > discs > caption > status > client > none) ──
    FrameRegion HitTest(SDL_Point p, const SDL_Rect& frame) const;

    // Release cached textures (SVG source + title + WIN-3 disc/ring cache).
    // Call on theme change / shutdown.
    void FreeTextures();

    // WIN-3: Evict the disc-button and focus-ring texture caches.
    // WIN-A: Also evicts the drop-shadow texture cache.
    // Call whenever the active theme palette changes (button_close / button_minimize /
    // button_maximize / button_restore / focus_ring colours may have changed).
    void InvalidateDiscCache();

    // WIN-SCALE-FIX-2: Evict ONLY the large per-window textures (shadow + ring)
    // while leaving the disc cache intact (discs are tiny at 30×30 px each).
    // Called from QdWindow::EvictBakeTextures() when the window is non-visible.
    // Shadow is ~(win_w+6)×(win_h+6)×4 bytes; ring is ~(win_w+12)×(win_h+12)×4.
    // For a default 1280×800 window that is ~4.15 MB + ~4.20 MB = ~8.35 MB freed
    // in addition to the ~3.71 MB content bake in QdWindow, total ~12 MB/window.
    void EvictLargeTextures();

private:
    // Ensure title_tex_ matches `title`.
    void EnsureTitleTex(SDL_Renderer* r, const std::string& title);

    // Compute disc rects (right-anchored, centre-Y = frame.y + kTitlebarH/2).
    static void ComputeButtonLayout(const SDL_Rect& frame,
                                    SDL_Rect& out_close,
                                    SDL_Rect& out_min,
                                    SDL_Rect& out_max);

    // Draw one 24-px disc + glyph at (cx, cy).
    // glyph: 0=Close(X), 1=Maximize(square)/Restore(double-sq), 2=Minimize(dash)
    static void PaintDisc(SDL_Renderer* r, int cx, int cy,
                          pu::ui::Color fill, int glyph, bool is_restore,
                          BtnState state, u8 alpha);

    // SDL draw helpers (same algorithms as qd_Window.cpp originals).
    static void DrawCircle(SDL_Renderer* r, int cx, int cy, int rad, pu::ui::Color col);
    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col, int rad = 8);
    static void DrawRoundedRectOutline(SDL_Renderer* r, int x, int y, int w, int h,
                                       int rad, pu::ui::Color col);

    // Flat code-draw chrome used as fallback when SVG is unavailable.
    static void PaintFallback(SDL_Renderer* r, const SDL_Rect& frame,
                              bool focused, u8 alpha);

    // ── WIN-3: disc-button texture cache ─────────────────────────────────────
    // Each of the 3 buttons × 3 states = up to 9 textures (close also has a
    // Normal variant for the unfocused-window dim path; we cache all 9 slots).
    // Key: {glyph 0..2, BtnState, is_restore}.  Restore is only meaningful for
    // glyph==1 (maximize), so glyph 0 and 2 ignore it — we always store index
    // with is_restore=false for those.
    //
    // Disc textures are kDiscDia × kDiscDia, TEXTUREACCESS_TARGET.
    // Cache hit: RenderCopy at full alpha; caller applies SetTextureAlphaMod.

    // Flattened 3×3×2 = 18 slots: [glyph][state][is_restore]
    // glyph: 0=close,1=max/restore,2=min   state: 0=Normal,1=Hover,2=Pressed
    static constexpr int kDiscCacheSlots = 18;  // 3 glyphs × 3 states × 2 restore flags

    struct DiscCacheKey {
        pu::ui::Color fill;   // RGBA, the fill colour used to bake this slot
        bool          valid;  // true once baked
    };

    SDL_Texture*  disc_cache_tex_[kDiscCacheSlots] = {};  // zero-init = all nullptr
    DiscCacheKey  disc_cache_key_[kDiscCacheSlots] = {};  // valid=false by default

    // Slot index into the disc cache arrays.
    static constexpr int DiscCacheIdx(int glyph, BtnState st, bool is_restore) {
        // glyph: 0..2, state: 0..2, is_restore: 0..1  → 3*3*2=18 slots
        return (glyph * 3 + static_cast<int>(st)) * 2 + (is_restore ? 1 : 0);
    }

    // Ensure disc_cache_tex_[idx] is baked with the given parameters.
    // Returns the cached texture (never nullptr after a successful bake; on
    // SDL_CreateTexture failure we fall back to direct draw at the call site).
    SDL_Texture* EnsureDiscTex(SDL_Renderer* r,
                                int idx, int glyph, bool is_restore,
                                pu::ui::Color fill, BtnState state);

    // ── WIN-3: focus ring+glow texture cache ─────────────────────────────────
    // The focus ring+glow extends kFocusGlowStep*2 px outside the window rect
    // on every side (2 halo passes at kFocusGlowStep=3 each → 6 px total margin).
    // The cached texture is sized (w + 2*kFocusHaloMargin) × (h + 2*kFocusHaloMargin)
    // so the halo pixels are captured.  RenderCopy offsets by -kFocusHaloMargin.
    static constexpr int kFocusHaloMargin = 6;  // 2 × kFocusGlowStep (=3)

    SDL_Texture*    ring_cache_tex_   = nullptr;
    int             ring_cache_w_     = 0;     // window w this texture was baked for
    int             ring_cache_h_     = 0;     // window h this texture was baked for
    pu::ui::Color   ring_cache_col_   = {};    // focus_ring colour used to bake
    bool            ring_cache_valid_ = false; // true once a texture has been baked

    // Ensure ring_cache_tex_ is valid for the given window size + colour.
    // Returns the cached texture or nullptr on SDL failure (caller falls back).
    SDL_Texture* EnsureRingTex(SDL_Renderer* r, int win_w, int win_h,
                                pu::ui::Color col);

    // ── WIN-A: drop-shadow texture cache ─────────────────────────────────────
    // The shadow is drawn at (fx+kShadowOff, fy+kShadowOff, fw, fh), so a
    // texture anchored at (fx, fy) must be (fw + kShadowOff) × (fh + kShadowOff).
    // Baked at kShadow.a (full 0x80); per-frame alpha applied via
    // SetTextureAlphaMod — result is identical to kShadow.a * alpha / 255.
    // Key: {win_w, win_h} — alpha_base is applied at blit time, not baked in.
    static constexpr int kShadowOff = 6;  // shadow offset in px (matches fx+6, fy+6)

    SDL_Texture* shadow_cache_tex_ = nullptr;
    int          shadow_cache_w_   = 0;   // window w this texture was baked for
    int          shadow_cache_h_   = 0;   // window h this texture was baked for

    // Ensure shadow_cache_tex_ is valid for the given window size.
    // Returns the cached texture or nullptr on SDL failure (caller falls back).
    SDL_Texture* EnsureShadowTex(SDL_Renderer* r, int win_w, int win_h);

    // Lazy-rendered title text texture.
    SDL_Texture* title_tex_    = nullptr;
    int          title_tex_w_  = 0;
    int          title_tex_h_  = 0;
    std::string  title_cached_;
};

} // namespace ul::menu::qdesktop
#endif // QDESKTOP_MODE
