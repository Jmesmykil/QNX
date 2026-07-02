// qd_Frame.cpp — QdFrame implementation.
// See qd_Frame.hpp for design rationale and 02-ARCHITECTURE.md for full spec.
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_Frame.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // CHROME_TITLE_*PAD, CHROME_HINT_*PAD
#include <ul/menu/qdesktop/qd_NinePatch.hpp>
#include <ul/menu/qdesktop/qd_SvgRaster.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/Plutonium>
#include <ul/ul_Result.hpp>
#include <cmath>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Color accessors (resolve from active theme each call) ─────────────────────
#define kFrameBg        (::ul::menu::qdesktop::g_QdTheme.surface_glass)
#define kTitlebarBg     (::ul::menu::qdesktop::g_QdTheme.titlebar_inactive)
#define kTitleText      (::ul::menu::qdesktop::g_QdTheme.text_primary)
#define kAccent         (::ul::menu::qdesktop::g_QdTheme.accent)
#define kFocusRingCol   (::ul::menu::qdesktop::g_QdTheme.focus_ring)
#define kGridLine       (::ul::menu::qdesktop::g_QdTheme.grid_line)
#define kBtnClose       (::ul::menu::qdesktop::g_QdTheme.button_close)
#define kBtnMin         (::ul::menu::qdesktop::g_QdTheme.button_minimize)
#define kBtnMax         (::ul::menu::qdesktop::g_QdTheme.button_maximize)
#define kBtnRestore     (::ul::menu::qdesktop::g_QdTheme.button_restore)

static constexpr pu::ui::Color kShadow     = {   0,   0,   0, 0x80 };
static constexpr pu::ui::Color kCornerHover = { 0xFF, 0xFF, 0xFF, 0x40 };
static constexpr int kFocusRing     = 3;    // focus ring stroke width (px)
static constexpr int kFocusGlowStep = 3;    // px between each glow halo pass
static constexpr int kBtnGlyphRad   = 3;    // rounded corner radius for maximize glyph

// ── SDL draw helpers ──────────────────────────────────────────────────────────

void QdFrame::DrawCircle(SDL_Renderer* r, int cx, int cy, int rad, pu::ui::Color col) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void QdFrame::DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                               pu::ui::Color col, int req_rad) {
    const int rad = std::min({req_rad, w / 2, h / 2});
    if (rad <= 4) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
        SDL_Rect rr { x, y, w, h };
        SDL_RenderFillRect(r, &rr);
        return;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_Rect center = { x + rad, y, w - 2 * rad, h };
    SDL_RenderFillRect(r, &center);
    SDL_Rect left   = { x,           y + rad, rad, h - 2 * rad };
    SDL_Rect right  = { x + w - rad, y + rad, rad, h - 2 * rad };
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    DrawCircle(r, x + rad,         y + rad,         rad, col);
    DrawCircle(r, x + w - rad - 1, y + rad,         rad, col);
    DrawCircle(r, x + rad,         y + h - rad - 1, rad, col);
    DrawCircle(r, x + w - rad - 1, y + h - rad - 1, rad, col);
}

void QdFrame::DrawRoundedRectOutline(SDL_Renderer* r, int x, int y, int w, int h,
                                      int req_rad, pu::ui::Color col) {
    const int rad = std::min({req_rad, w / 2, h / 2});
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    if (rad <= 1) {
        SDL_Rect rr { x, y, w, h };
        SDL_RenderDrawRect(r, &rr);
        return;
    }
    SDL_RenderDrawLine(r, x + rad,     y,         x + w - rad - 1, y);
    SDL_RenderDrawLine(r, x + rad,     y + h - 1, x + w - rad - 1, y + h - 1);
    SDL_RenderDrawLine(r, x,           y + rad,   x,               y + h - rad - 1);
    SDL_RenderDrawLine(r, x + w - 1,   y + rad,   x + w - 1,       y + h - rad - 1);
    const int cx0 = x + rad, cx1 = x + w - rad - 1;
    const int cy0 = y + rad, cy1 = y + h - rad - 1;
    for (int dx = 0; dx <= rad; ++dx) {
        const int dy = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dx * dx)) + 0.5f);
        SDL_RenderDrawPoint(r, cx1 + dx, cy1 + dy);
        SDL_RenderDrawPoint(r, cx1 + dy, cy1 + dx);
        SDL_RenderDrawPoint(r, cx0 - dx, cy1 + dy);
        SDL_RenderDrawPoint(r, cx0 - dy, cy1 + dx);
        SDL_RenderDrawPoint(r, cx1 + dx, cy0 - dy);
        SDL_RenderDrawPoint(r, cx1 + dy, cy0 - dx);
        SDL_RenderDrawPoint(r, cx0 - dx, cy0 - dy);
        SDL_RenderDrawPoint(r, cx0 - dy, cy0 - dx);
    }
}

// ── Fallback code-draw chrome (used when SVG load fails) ──────────────────────

void QdFrame::PaintFallback(SDL_Renderer* r, const SDL_Rect& frame,
                             bool focused, u8 alpha) {
    const int fx = frame.x, fy = frame.y, fw = frame.w, fh = frame.h;
    // Shadow
    {
        pu::ui::Color sc = kShadow;
        sc.a = static_cast<u8>(sc.a * alpha / 255);
        DrawRoundedRect(r, fx + 6, fy + 6, fw, fh, sc, kBodyRadius);
    }
    // Body
    {
        pu::ui::Color border = focused ? kFocusRingCol : kTitlebarBg;
        border.a = static_cast<u8>(border.a * alpha / 255);
        DrawRoundedRect(r, fx, fy, fw, fh, border, kBodyRadius);
        pu::ui::Color bg = kFrameBg;
        bg.a = static_cast<u8>(bg.a * alpha / 255);
        DrawRoundedRect(r, fx + 1, fy + 1, fw - 2, fh - 2, bg, kBodyRadius);
    }
    // Titlebar fill
    {
        pu::ui::Color tbc = focused ? kFrameBg : kTitlebarBg;
        tbc.a = static_cast<u8>(tbc.a * alpha / 255);
        DrawRoundedRect(r, fx + 1, fy + 1, fw - 2, kTitlebarH, tbc, kBodyRadius);
    }
    // Titlebar separator
    {
        pu::ui::Color lc = kGridLine;
        lc.a = static_cast<u8>(lc.a * alpha / 255);
        SDL_SetRenderDrawColor(r, lc.r, lc.g, lc.b, lc.a);
        SDL_RenderDrawLine(r, fx + 1, fy + kTitlebarH, fx + fw - 2, fy + kTitlebarH);
    }
    // Status bar fill
    {
        const int bby = fy + fh - kStatusH;
        pu::ui::Color sbc = kTitlebarBg;
        sbc.a = static_cast<u8>(sbc.a * alpha / 255);
        DrawRoundedRect(r, fx + 1, bby, fw - 2, kStatusH, sbc, kBodyRadius);
        pu::ui::Color lc = kGridLine;
        lc.a = static_cast<u8>(lc.a * alpha / 255);
        SDL_SetRenderDrawColor(r, lc.r, lc.g, lc.b, lc.a);
        SDL_RenderDrawLine(r, fx + 1, bby, fx + fw - 2, bby);
    }
}

// ── Shared SVG source cache (A2-OPT-1) ────────────────────────────────────────
// One process-singleton entry per theme index.  All QdFrame instances share
// these textures; ~99 MB VRAM saved at N=100 windows (one 640×400 RGBA ~1 MB
// per theme vs one per window).
namespace {
    struct SvgCacheEntry {
        SDL_Texture* tex        = nullptr;
        bool         load_failed = false;
    };
    // 10 theme slots is the current design maximum (matching the SVG masters).
    static constexpr int kMaxThemes = 16;
    static SvgCacheEntry s_svg_cache[kMaxThemes];
} // anonymous namespace

SDL_Texture* GetSharedSvgSource(SDL_Renderer* r, int theme_idx) {
    if (theme_idx < 0 || theme_idx >= kMaxThemes) return nullptr;
    SvgCacheEntry& entry = s_svg_cache[theme_idx];

    if (entry.tex != nullptr)      return entry.tex;  // cache hit
    if (entry.load_failed)         return nullptr;     // cached miss

    const std::string path = "romfs:/window/q-os-" + std::to_string(theme_idx) + ".svg";
    entry.tex = RasterizeSvgFile(r, path, QdFrame::kSrcW, QdFrame::kSrcH);
    if (entry.tex != nullptr) return entry.tex;

    entry.load_failed = true;
    UL_LOG_WARN("qdesktop: QdFrame: SVG load failed for theme %d, using fallback", theme_idx);
    return nullptr;
}

void InvalidateSvgCache(int theme_idx) {
    if (theme_idx < 0 || theme_idx >= kMaxThemes) return;
    SvgCacheEntry& entry = s_svg_cache[theme_idx];
    if (entry.tex != nullptr) {
        SDL_DestroyTexture(entry.tex);
        entry.tex = nullptr;
    }
    entry.load_failed = false;
}

// ── Title texture ─────────────────────────────────────────────────────────────

void QdFrame::EnsureTitleTex(SDL_Renderer* /*r*/, const std::string& title) {
    if (title == title_cached_ && title_tex_ != nullptr) return;
    if (title_tex_) {
        pu::ui::render::DeleteTexture(title_tex_);
        title_tex_   = nullptr;
        title_tex_w_ = 0;
        title_tex_h_ = 0;
    }
    title_cached_ = title;
    if (!title.empty()) {
        title_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large),
            title, kTitleText);
        if (title_tex_) {
            SDL_QueryTexture(title_tex_, nullptr, nullptr, &title_tex_w_, &title_tex_h_);
        }
    }
}

// ── FreeTextures ──────────────────────────────────────────────────────────────

void QdFrame::FreeTextures() {
    // A2-OPT-1: per-instance SVG source is gone; shared cache is owned by the
    // process-singleton and released via InvalidateSvgCache / process exit.
    // Only free per-instance textures here.
    if (title_tex_) {
        pu::ui::render::DeleteTexture(title_tex_);
        title_tex_    = nullptr;
        title_tex_w_  = 0;
        title_tex_h_  = 0;
        title_cached_.clear();
    }
    // WIN-3: release disc + ring caches (render-thread safe — FreeTextures is
    // only called from Paint's owning path or shutdown, both on the render thread).
    InvalidateDiscCache();
}

// ── WIN-3: InvalidateDiscCache ────────────────────────────────────────────────

void QdFrame::InvalidateDiscCache() {
    for (int i = 0; i < kDiscCacheSlots; ++i) {
        if (disc_cache_tex_[i]) {
            SDL_DestroyTexture(disc_cache_tex_[i]);
            disc_cache_tex_[i] = nullptr;
        }
        disc_cache_key_[i].valid = false;
    }
    if (ring_cache_tex_) {
        SDL_DestroyTexture(ring_cache_tex_);
        ring_cache_tex_ = nullptr;
    }
    ring_cache_w_     = 0;
    ring_cache_h_     = 0;
    ring_cache_valid_ = false;
    // WIN-A: evict shadow texture alongside disc/ring caches.
    if (shadow_cache_tex_) {
        SDL_DestroyTexture(shadow_cache_tex_);
        shadow_cache_tex_ = nullptr;
    }
    shadow_cache_w_ = 0;
    shadow_cache_h_ = 0;
}

// ── WIN-SCALE-FIX-2: EvictLargeTextures ──────────────────────────────────────
// Releases only the window-sized shadow and ring textures (each ~4 MB at default
// 1280×800).  Disc textures (30×30 px each, negligible) are intentionally kept
// so they do not need to be re-baked when the window becomes visible again.

void QdFrame::EvictLargeTextures() {
    if (shadow_cache_tex_) {
        SDL_DestroyTexture(shadow_cache_tex_);
        shadow_cache_tex_ = nullptr;
    }
    shadow_cache_w_ = 0;
    shadow_cache_h_ = 0;

    if (ring_cache_tex_) {
        SDL_DestroyTexture(ring_cache_tex_);
        ring_cache_tex_ = nullptr;
    }
    ring_cache_w_     = 0;
    ring_cache_h_     = 0;
    ring_cache_valid_ = false;
}

// ── WIN-3: EnsureDiscTex ──────────────────────────────────────────────────────
// Bakes one disc slot into an SDL_TEXTUREACCESS_TARGET texture by redirecting
// the renderer, drawing at (cx=rad, cy=rad) origin, then restoring the target.
// Returns nullptr on SDL failure (caller falls back to direct draw).

SDL_Texture* QdFrame::EnsureDiscTex(SDL_Renderer* r,
                                     int idx, int glyph, bool is_restore,
                                     pu::ui::Color fill, BtnState state) {
    // Cache hit: same fill colour and already valid.
    DiscCacheKey& key = disc_cache_key_[idx];
    if (key.valid
            && key.fill.r == fill.r && key.fill.g == fill.g
            && key.fill.b == fill.b && key.fill.a == fill.a) {
        return disc_cache_tex_[idx];
    }

    // Evict stale texture.
    if (disc_cache_tex_[idx]) {
        SDL_DestroyTexture(disc_cache_tex_[idx]);
        disc_cache_tex_[idx] = nullptr;
    }

    // Create a kDiscDia × kDiscDia render-target texture.
    SDL_Texture* tex = SDL_CreateTexture(r,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         kDiscDia, kDiscDia);
    if (!tex) return nullptr;

    // Enable alpha blending on the texture so RenderCopy respects alpha.
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    // Save and redirect the render target.
    // WIN-SCALE-FIX-1: guard SetRenderTarget — on VRAM exhaustion it can fail
    // even after CreateTexture succeeded.  Free and return nullptr so the caller
    // falls back to direct draw rather than drawing into an undefined target.
    SDL_Texture* prev_target = SDL_GetRenderTarget(r);
    if (SDL_SetRenderTarget(r, tex) != 0) {
        UL_LOG_WARN("qdesktop: QdFrame disc EnsureDiscTex SetRenderTarget failed (%s)",
                    SDL_GetError());
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    // Clear to transparent.
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // Draw the disc centered at (rad, rad) inside the texture.
    // alpha=255 — we apply SetTextureAlphaMod at blit time.
    const int rad = kDiscDia / 2;
    PaintDisc(r, rad, rad, fill, glyph, is_restore, state, 255);

    // Restore the previous render target.
    SDL_SetRenderTarget(r, prev_target);

    disc_cache_tex_[idx] = tex;
    key.fill  = fill;
    key.valid = true;
    return tex;
}

// ── WIN-3: EnsureRingTex ──────────────────────────────────────────────────────
// Bakes the focus ring + glow into a texture sized
//   (win_w + 2*kFocusHaloMargin) × (win_h + 2*kFocusHaloMargin).
// The ring/glow are drawn offset by kFocusHaloMargin so the halo margin is
// captured.  At blit time the destination rect is offset by -kFocusHaloMargin
// to reproduce pixel-identical placement.
// Returns nullptr on SDL failure (caller falls back to direct draw).

SDL_Texture* QdFrame::EnsureRingTex(SDL_Renderer* r, int win_w, int win_h,
                                     pu::ui::Color col) {
    // Cache hit: valid flag set, same dimensions and same colour.
    if (ring_cache_valid_
            && ring_cache_w_ == win_w && ring_cache_h_ == win_h
            && ring_cache_col_.r == col.r && ring_cache_col_.g == col.g
            && ring_cache_col_.b == col.b && ring_cache_col_.a == col.a) {
        return ring_cache_tex_;
    }

    // Evict stale texture.
    if (ring_cache_tex_) {
        SDL_DestroyTexture(ring_cache_tex_);
        ring_cache_tex_ = nullptr;
    }

    const int tex_w = win_w + 2 * kFocusHaloMargin;
    const int tex_h = win_h + 2 * kFocusHaloMargin;

    SDL_Texture* tex = SDL_CreateTexture(r,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         tex_w, tex_h);
    if (!tex) return nullptr;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev_target = SDL_GetRenderTarget(r);
    // WIN-SCALE-FIX-1: guard SetRenderTarget for ring texture.
    if (SDL_SetRenderTarget(r, tex) != 0) {
        UL_LOG_WARN("qdesktop: QdFrame EnsureRingTex SetRenderTarget failed (%s)",
                    SDL_GetError());
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // Inside the texture coordinate space, the "window rect" is at
    // (kFocusHaloMargin, kFocusHaloMargin, win_w, win_h).
    // We reproduce the exact same draw calls as Paint() steps 2 + 4, with
    // x/y substituted so (fx, fy) → (kFocusHaloMargin, kFocusHaloMargin).
    const int ox = kFocusHaloMargin;
    const int oy = kFocusHaloMargin;

    // Step 2 — focus glow halo (two concentric translucent outlines outside body).
    {
        const u8 a_outer = 0x20;
        const u8 a_inner = 0x40;
        const int g2 = 2 * kFocusGlowStep;
        const int g1 = 1 * kFocusGlowStep;
        DrawRoundedRectOutline(r, ox - g2, oy - g2,
                               win_w + 2 * g2, win_h + 2 * g2,
                               kBodyRadius + g2, { col.r, col.g, col.b, a_outer });
        DrawRoundedRectOutline(r, ox - g1, oy - g1,
                               win_w + 2 * g1, win_h + 2 * g1,
                               kBodyRadius + g1, { col.r, col.g, col.b, a_inner });
    }

    // Step 4 — crisp focus ring (kFocusRing stacked outlines at body radius).
    {
        for (int i = 0; i < kFocusRing; i++) {
            DrawRoundedRectOutline(r, ox - i, oy - i,
                                   win_w + 2 * i, win_h + 2 * i,
                                   kBodyRadius + i, { col.r, col.g, col.b, col.a });
        }
    }

    SDL_SetRenderTarget(r, prev_target);

    ring_cache_tex_   = tex;
    ring_cache_w_     = win_w;
    ring_cache_h_     = win_h;
    ring_cache_col_   = col;
    ring_cache_valid_ = true;
    return tex;
}

// ── WIN-A: EnsureShadowTex ────────────────────────────────────────────────────
// Bakes the drop-shadow into an (win_w + kShadowOff) × (win_h + kShadowOff)
// SDL_TEXTUREACCESS_TARGET texture.  The shadow is drawn at (kShadowOff,
// kShadowOff, win_w, win_h) inside the texture, so a blit to (fx, fy, …) places
// the shadow exactly at (fx+kShadowOff, fy+kShadowOff) on screen — pixel-
// identical to the original DrawRoundedRect(r, fx+6, fy+6, fw, fh, sc, kBodyRadius).
// Baked at kShadow.a (0x80) with alpha=255 colours; per-frame alpha is applied via
// SetTextureAlphaMod at blit time, giving kShadow.a * alpha / 255 exactly.
// Returns nullptr on SDL failure (caller falls back to direct draw).

SDL_Texture* QdFrame::EnsureShadowTex(SDL_Renderer* r, int win_w, int win_h) {
    // Cache hit: same window dimensions.
    if (shadow_cache_tex_ != nullptr
            && shadow_cache_w_ == win_w && shadow_cache_h_ == win_h) {
        return shadow_cache_tex_;
    }

    // Evict stale texture.
    if (shadow_cache_tex_) {
        SDL_DestroyTexture(shadow_cache_tex_);
        shadow_cache_tex_ = nullptr;
    }

    const int tex_w = win_w + kShadowOff;
    const int tex_h = win_h + kShadowOff;

    SDL_Texture* tex = SDL_CreateTexture(r,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         tex_w, tex_h);
    if (!tex) return nullptr;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev_target = SDL_GetRenderTarget(r);
    // WIN-SCALE-FIX-1: guard SetRenderTarget for shadow texture.
    if (SDL_SetRenderTarget(r, tex) != 0) {
        UL_LOG_WARN("qdesktop: QdFrame EnsureShadowTex SetRenderTarget failed (%s)",
                    SDL_GetError());
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    // Draw shadow at full kShadow.a (0x80); per-frame alpha is applied at blit.
    DrawRoundedRect(r, kShadowOff, kShadowOff, win_w, win_h, kShadow, kBodyRadius);

    SDL_SetRenderTarget(r, prev_target);

    shadow_cache_tex_ = tex;
    shadow_cache_w_   = win_w;
    shadow_cache_h_   = win_h;
    return tex;
}

// ── Button layout (stateless, computed from frame rect) ───────────────────────

void QdFrame::ComputeButtonLayout(const SDL_Rect& frame,
                                   SDL_Rect& out_close,
                                   SDL_Rect& out_min,
                                   SDL_Rect& out_max) {
    const int rad = kDiscDia / 2;
    // Close:    top-left  (matches legacy TL corner button)
    const int top_cy = frame.y + kTitlebarH / 2;
    const int tl_cx  = frame.x + kDiscInset + rad;
    out_close = { tl_cx - rad, top_cy - rad, kDiscDia, kDiscDia };
    // Maximize: top-right (matches legacy TR corner button)
    const int tr_cx  = frame.x + frame.w - kDiscInset - rad;
    out_max   = { tr_cx - rad, top_cy - rad, kDiscDia, kDiscDia };
    // Minimize: bottom-left (matches legacy BL corner button)
    const int bot_cy = frame.y + frame.h - kStatusH / 2;
    const int bl_cx  = frame.x + kDiscInset + rad;
    out_min   = { bl_cx - rad, bot_cy - rad, kDiscDia, kDiscDia };
}

// ── Disc button paint ─────────────────────────────────────────────────────────

void QdFrame::PaintDisc(SDL_Renderer* r, int cx, int cy,
                         pu::ui::Color fill, int glyph, bool is_restore,
                         BtnState state, u8 alpha) {
    const int rad = (state == BtnState::Pressed)
                    ? (kDiscDia / 2 * 94 / 100)
                    : (kDiscDia / 2);

    pu::ui::Color disc_col = fill;
    disc_col.a = static_cast<u8>(disc_col.a * alpha / 255);
    DrawCircle(r, cx, cy, rad, disc_col);

    if (state == BtnState::Hover) {
        DrawCircle(r, cx, cy, rad, kCornerHover);
    }

    // Glyph: dark #0A0A14 on bright disc, ~58% of disc radius arm length
    const pu::ui::Color gly { 0x0A, 0x0A, 0x14, static_cast<u8>(230u * alpha / 255) };
    const int gr = (rad * 7) / 12;
    const int t  = 2;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, gly.r, gly.g, gly.b, gly.a);

    auto ThickLine = [&](int x0, int y0, int x1, int y1) {
        const int adx = std::abs(x1 - x0), ady = std::abs(y1 - y0);
        for (int o = -t; o <= t; ++o) {
            if (adx >= ady) SDL_RenderDrawLine(r, x0, y0 + o, x1, y1 + o);
            else            SDL_RenderDrawLine(r, x0 + o, y0, x1 + o, y1);
        }
    };

    auto StrokeRoundedSquare = [&](int sx, int sy, int side, int rr) {
        DrawRoundedRect(r, sx, sy, side, side, gly, rr);
        DrawRoundedRect(r, sx + t, sy + t, side - 2 * t, side - 2 * t, disc_col,
                        std::max(1, rr - t));
        SDL_SetRenderDrawColor(r, gly.r, gly.g, gly.b, gly.a);
    };

    switch (glyph) {
        case 0: // Close — X
            ThickLine(cx - gr, cy - gr, cx + gr, cy + gr);
            ThickLine(cx + gr, cy - gr, cx - gr, cy + gr);
            break;
        case 1: // Maximize / Restore
            if (is_restore) {
                const int side = (2 * gr * 5) / 6;
                const int off  = std::max(2, gr / 3);
                StrokeRoundedSquare(cx - gr + off, cy - gr,       side, kBtnGlyphRad);
                StrokeRoundedSquare(cx - gr,       cy - gr + off, side, kBtnGlyphRad);
            } else {
                StrokeRoundedSquare(cx - gr, cy - gr, 2 * gr, kBtnGlyphRad);
            }
            break;
        case 2: // Minimize — dash
        {
            SDL_Rect bar { cx - gr, cy - t, 2 * gr, 2 * t };
            SDL_RenderFillRect(r, &bar);
            break;
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// ── ComputeClientRect ─────────────────────────────────────────────────────────

SDL_Rect QdFrame::ComputeClientRect(const SDL_Rect& frame) const {
    return SDL_Rect {
        frame.x + kBorder,
        frame.y + kTitlebarH,
        frame.w - 2 * kBorder,
        frame.h - kTitlebarH - kStatusH
    };
}

// ── HitTest ───────────────────────────────────────────────────────────────────

FrameRegion QdFrame::HitTest(SDL_Point p, const SDL_Rect& frame) const {
    auto PointIn = [](SDL_Point pt, const SDL_Rect& rr) {
        return pt.x >= rr.x && pt.x < rr.x + rr.w &&
               pt.y >= rr.y && pt.y < rr.y + rr.h;
    };
    if (!PointIn(p, frame)) return FrameRegion::None;

    // 1. BR resize grip (kGrip × kGrip square at bottom-right corner)
    if (p.x >= frame.x + frame.w - kGrip && p.y >= frame.y + frame.h - kGrip)
        return FrameRegion::ResizeBR;

    // 2. Disc buttons (computed stateless; beats the caption band).
    //    v3.7.1: the touch zone is the visual disc inflated by kDiscHitPad on
    //    every side, so taps near a corner button still register (the discs
    //    were a little hard to hit at exactly disc-sized rects).
    SDL_Rect cr, mnr, mxr;
    ComputeButtonLayout(frame, cr, mnr, mxr);
    auto Inflate = [](const SDL_Rect& rr, int pad) {
        return SDL_Rect{ rr.x - pad, rr.y - pad, rr.w + 2 * pad, rr.h + 2 * pad };
    };
    if (PointIn(p, Inflate(cr,  kDiscHitPad))) return FrameRegion::Close;
    if (PointIn(p, Inflate(mxr, kDiscHitPad))) return FrameRegion::Maximize;
    if (PointIn(p, Inflate(mnr, kDiscHitPad))) return FrameRegion::Minimize;

    // 3. Caption (titlebar band) — drag + double-tap-maximize
    if (p.y < frame.y + kTitlebarH) return FrameRegion::Titlebar;

    // 4. Status bar (bottom band) — hint display, no actions
    if (p.y >= frame.y + frame.h - kStatusH) return FrameRegion::StatusBar;

    // 5. Content
    return FrameRegion::Client;
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void QdFrame::Paint(SDL_Renderer* r, const SDL_Rect& frame,
                    bool focused, bool maximized,
                    BtnState close_st, BtnState min_st, BtnState max_st,
                    int theme_idx,
                    const std::string& title,
                    SDL_Texture* hint_tex, int hint_w, int hint_h,
                    SDL_Texture* tip_tex,  int tip_w,  int tip_h,
                    u8 alpha)
{
    const int fx = frame.x, fy = frame.y, fw = frame.w, fh = frame.h;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── 1. Drop shadow — WIN-A cached blit ───────────────────────────────────
    // EnsureShadowTex bakes the shadow once into a (fw+kShadowOff)×(fh+kShadowOff)
    // texture keyed by {fw, fh}.  Blit with SetTextureAlphaMod(alpha) reproduces
    // kShadow.a * alpha / 255 exactly.  Falls back to direct draw on SDL failure.
    {
        SDL_Texture* stex = EnsureShadowTex(r, fw, fh);
        if (stex) {
            SDL_SetTextureAlphaMod(stex, alpha);
            SDL_Rect dst = { fx, fy, fw + kShadowOff, fh + kShadowOff };
            SDL_RenderCopy(r, stex, nullptr, &dst);
            SDL_SetTextureAlphaMod(stex, 255);
        } else {
            // Fallback: direct draw, pixel-identical to original.
            pu::ui::Color sc = kShadow;
            sc.a = static_cast<u8>(sc.a * alpha / 255);
            DrawRoundedRect(r, fx + kShadowOff, fy + kShadowOff, fw, fh, sc, kBodyRadius);
        }
    }

    // ── 2+4. Focus glow halo + crisp focus ring — WIN-3 cached blit ─────────
    // Both passes (glow halo and stacked outline ring) are baked together into
    // ring_cache_tex_ keyed by {win_w, win_h, focus_ring colour}.  The cached
    // texture is (fw + 2*kFocusHaloMargin) × (fh + 2*kFocusHaloMargin) so the
    // halo margin is fully captured; we blit it offset by -kFocusHaloMargin.
    // alpha is applied via SetTextureAlphaMod before the copy (same as title_tex_).
    // Falls back to direct draw if SDL_CreateTexture fails.
    bool ring_drawn_by_cache = false;
    if (focused) {
        const pu::ui::Color frc = kFocusRingCol;
        // The baked texture uses alpha values without the per-frame alpha
        // modulation (baked at full 255).  We apply the modulation at blit time.
        SDL_Texture* rtex = EnsureRingTex(r, fw, fh, frc);
        if (rtex) {
            SDL_SetTextureAlphaMod(rtex, alpha);
            SDL_Rect dst = { fx - kFocusHaloMargin,
                             fy - kFocusHaloMargin,
                             fw + 2 * kFocusHaloMargin,
                             fh + 2 * kFocusHaloMargin };
            SDL_RenderCopy(r, rtex, nullptr, &dst);
            SDL_SetTextureAlphaMod(rtex, 255);
            ring_drawn_by_cache = true;
        }
        // Fallback (SDL target texture unavailable): direct draw, matching
        // original pixel output exactly.
        if (!ring_drawn_by_cache) {
            const u8 a_outer = static_cast<u8>(0x20 * alpha / 255);
            const u8 a_inner = static_cast<u8>(0x40 * alpha / 255);
            const int g2 = 2 * kFocusGlowStep;
            const int g1 = 1 * kFocusGlowStep;
            DrawRoundedRectOutline(r, fx - g2, fy - g2, fw + 2 * g2, fh + 2 * g2,
                                   kBodyRadius + g2, { frc.r, frc.g, frc.b, a_outer });
            DrawRoundedRectOutline(r, fx - g1, fy - g1, fw + 2 * g1, fh + 2 * g1,
                                   kBodyRadius + g1, { frc.r, frc.g, frc.b, a_inner });
        }
    }

    // ── 3. Window chrome: nine-patch SVG or code-draw fallback ────────────────
    // A2-OPT-1: resolve from process-singleton shared cache (one texture per theme).
    SDL_Texture* src_tex = GetSharedSvgSource(r, theme_idx);
    if (src_tex != nullptr) {
        const NinePatchInsets ins { kInL, kInR, kInT, kInB };
        DrawNinePatch(r, src_tex, kSrcW, kSrcH, ins, frame, alpha);
    } else {
        PaintFallback(r, frame, focused, alpha);
    }

    // ── 4. (merged into step 2+4 above — WIN-3 ring+glow cached blit) ────────
    // The direct-draw fallback for the crisp focus ring is also in the block
    // above (ring_drawn_by_cache guard), so nothing to do here.
    if (focused && !ring_drawn_by_cache) {
        // This branch is only reached when EnsureRingTex returned nullptr AND
        // the halo fallback above did NOT draw the crisp ring yet.
        // Draw the crisp ring directly to complete the fallback path.
        const pu::ui::Color fc = kFocusRingCol;
        const u8 fa = static_cast<u8>(fc.a * alpha / 255);
        for (int i = 0; i < kFocusRing; i++) {
            DrawRoundedRectOutline(r, fx - i, fy - i, fw + 2 * i, fh + 2 * i,
                                   kBodyRadius + i, { fc.r, fc.g, fc.b, fa });
        }
    }

    // ── 5. Disc buttons — WIN-3 cached blit ──────────────────────────────────
    // Each disc is baked into a kDiscDia×kDiscDia texture keyed by
    // {glyph, BtnState, is_restore, fill colour}.  We RenderCopy it centred on
    // the computed (cx, cy), applying alpha via SetTextureAlphaMod.
    // Falls back to PaintDisc direct draw if EnsureDiscTex returns nullptr.
    {
        SDL_Rect cr, mnr, mxr;
        ComputeButtonLayout(frame, cr, mnr, mxr);
        const int close_cx = cr.x  + kDiscDia / 2;
        const int min_cx   = mnr.x + kDiscDia / 2;
        const int max_cx   = mxr.x + kDiscDia / 2;
        const int top_cy = fy + kTitlebarH / 2;
        const int bot_cy = fy + fh - kStatusH / 2;

        // Helper: blit one disc texture or fall back to direct draw.
        // `cx`, `cy` are the disc centre in screen space.
        auto BlitDisc = [&](int cx, int cy, pu::ui::Color fill,
                            int glyph, bool is_restore, BtnState st) {
            const int idx = DiscCacheIdx(glyph, st, is_restore);
            SDL_Texture* dtex = EnsureDiscTex(r, idx, glyph, is_restore, fill, st);
            if (dtex) {
                const int rad = kDiscDia / 2;
                SDL_Rect dst = { cx - rad, cy - rad, kDiscDia, kDiscDia };
                SDL_SetTextureAlphaMod(dtex, alpha);
                SDL_RenderCopy(r, dtex, nullptr, &dst);
                SDL_SetTextureAlphaMod(dtex, 255);
            } else {
                // Fallback: direct draw (same path as before WIN-3).
                PaintDisc(r, cx, cy, fill, glyph, is_restore, st, alpha);
            }
        };

        BlitDisc(close_cx, top_cy, kBtnClose,  0, false,    close_st);
        BlitDisc(max_cx,   top_cy, maximized ? kBtnRestore : kBtnMax,
                                               1, maximized, max_st);
        BlitDisc(min_cx,   bot_cy, kBtnMin,    2, false,    min_st);
    }

    // ── 6. Title text (centered in titlebar, avoiding disc cluster on right) ───
    EnsureTitleTex(r, title);
    if (title_tex_) {
        // Available band: CHROME_TITLE_LEFT/RIGHT_PAD each side
        // (kDiscInset=12 + kDiscDia=30 + 6 margin = 48px).
        // TL = close disc, TR = maximize disc — both must be avoided.
        // Constants live in qd_LayoutConstants.hpp (D1 fix).
        const int avail_w = fw - CHROME_TITLE_LEFT_PAD - CHROME_TITLE_RIGHT_PAD;
        if (avail_w > 0) {
            const int draw_w = std::min(title_tex_w_, avail_w);
            const int draw_x = fx + CHROME_TITLE_LEFT_PAD + (avail_w - draw_w) / 2;
            const int draw_y = fy + (kTitlebarH - title_tex_h_) / 2;
            SDL_SetTextureAlphaMod(title_tex_, alpha);
            SDL_Rect src_r = { 0, 0, draw_w, title_tex_h_ };
            SDL_Rect dst_r = { draw_x, draw_y, draw_w, title_tex_h_ };
            SDL_RenderCopy(r, title_tex_, &src_r, &dst_r);
            SDL_SetTextureAlphaMod(title_tex_, 255);
        }
    }

    // ── 7. Status bar hint / tooltip text ─────────────────────────────────────
    // tip_tex overrides hint_tex when a disc is hovered (button tooltip).
    SDL_Texture* bar_tex = tip_tex  ? tip_tex  : hint_tex;
    int          bar_w   = tip_tex  ? tip_w    : hint_w;
    int          bar_h   = tip_tex  ? tip_h    : hint_h;
    if (bar_tex && bar_h > 0) {
        const int bby      = fy + fh - kStatusH;
        // BL = minimize disc (48px left inset, disc now 30px); BR has no disc so 8px right only.
        // Constants live in qd_LayoutConstants.hpp (D2 fix).
        const int max_text_w = fw - CHROME_HINT_LEFT_PAD - CHROME_HINT_RIGHT_PAD;
        if (max_text_w > 0) {
            const int draw_w = std::min(bar_w, max_text_w);
            const int draw_x = fx + CHROME_HINT_LEFT_PAD + (max_text_w - draw_w) / 2;
            const int draw_y = bby + (kStatusH - bar_h) / 2;
            SDL_SetTextureAlphaMod(bar_tex, alpha);
            SDL_Rect src_r = { 0, 0, draw_w, bar_h };
            SDL_Rect dst_r = { draw_x, draw_y, draw_w, bar_h };
            SDL_RenderCopy(r, bar_tex, &src_r, &dst_r);
            SDL_SetTextureAlphaMod(bar_tex, 255);
        }
    }
}

} // namespace ul::menu::qdesktop
#endif // QDESKTOP_MODE
