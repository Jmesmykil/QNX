// qd_Cursor.cpp — Q OS desktop "Liquid Glass Bubble" cursor (v3.0.0).
//
// Programmatic SDL2 design:
//   • 44×44 ABGR8888 streaming texture, pre-built once in the constructor.
//   • 18 px outer radius glass bubble, cyan #00E5FF at alpha=110 (body fill).
//   • 1 px anti-aliased bright-cyan outline (two-pass Wu approximation):
//       - Pass 1: radius+1 (19 px) at alpha=80 — soft AA halo ring.
//       - Pass 2: exact radius (18 px) at alpha=255 — fully-opaque crisp edge.
//   • Centre crosshair: 5 px black disc + 2 px white dot layered on top.
//     The 2 px white tip is ringed by 3 px of black — visible on any background.
//   • No upper-left highlight (canvas too small at 18 px radius).
//   • Hot-spot at texture centre (22, 22): blit at (cx - 22, cy - 22).
//
// Colours are theme-driven: body = g_QdTheme.cursor_fill, outline =
// g_QdTheme.cursor_outline, right-click body = g_QdTheme.cursor_right_click.
//
// Per-frame cost: one SDL_RenderCopy on a 44×44 texture (zero per-pixel work).
//
// API surface: identical to v1.0.0; qd_Input.cpp and ui_MainMenuLayout.cpp
// require zero changes.

#include <ul/menu/qdesktop/qd_Cursor.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <cmath>

namespace ul::menu::qdesktop {

// ── Theme-change dirty flag ─────────────────────────────────────────────────
// Set by SetActivePalettePack() in qd_Theme.cpp whenever the live palette
// (g_QdTheme) changes.  Consumed in OnRender to rebuild the cursor textures with
// the new theme colours.  Mirrors g_wallpaper_dirty (qd_Wallpaper.cpp).
std::atomic<bool> g_cursor_dirty{false};

// ── Pixel helpers ─────────────────────────────────────────────────────────────

// ABGR8888 byte layout (as SDL stores it on Switch / little-endian ARM):
//   byte 0 = A, byte 1 = B, byte 2 = G, byte 3 = R
// We build a u32 in host byte order via SDL_MapRGBA so it lands correctly
// regardless of endianness.  We pre-compute the pixel values in
// BuildCursorTexture using SDL_MapRGBA with the texture's pixel format.

// Alpha-composite src_rgba over an existing ABGR8888 pixel (dst) in-place.
// Standard Porter-Duff "src over" with pre-multiplied intermediate.
static inline void BlendPixel(u32 *dst,
                               u8 sr, u8 sg, u8 sb, u8 sa,
                               const SDL_PixelFormat *fmt) {
    if (sa == 0) {
        return;
    }
    if (sa == 255) {
        *dst = SDL_MapRGBA(fmt, sr, sg, sb, 255);
        return;
    }
    u8 dr, dg, db, da;
    SDL_GetRGBA(*dst, fmt, &dr, &dg, &db, &da);

    // src-over composite
    const u32 one_minus_sa = 255u - sa;
    const u8 out_a = static_cast<u8>(sa + (da * one_minus_sa) / 255u);
    if (out_a == 0) {
        *dst = 0;
        return;
    }
    const u8 out_r = static_cast<u8>((sr * sa + dr * da * one_minus_sa / 255u) / out_a);
    const u8 out_g = static_cast<u8>((sg * sa + dg * da * one_minus_sa / 255u) / out_a);
    const u8 out_b = static_cast<u8>((sb * sa + db * da * one_minus_sa / 255u) / out_a);
    *dst = SDL_MapRGBA(fmt, out_r, out_g, out_b, out_a);
}

// Draw a filled circle of solid colour (no blending) for interior regions
// where alpha is constant.  Uses the Bresenham midpoint algorithm for speed.
static void FillCircleSolid(u32 *pixels, int pitch_u32,
                             int cx, int cy, int radius,
                             u8 r, u8 g, u8 b, u8 a,
                             const SDL_PixelFormat *fmt) {
    const u32 colour = SDL_MapRGBA(fmt, r, g, b, a);
    int x = 0;
    int y = radius;
    int d = 1 - radius;
    // Horizontal scan-fill across the two symmetric halves.
    auto hline = [&](int lx, int rx, int iy) {
        if (lx < 0) lx = 0;
        if (rx >= CURSOR_TEX_SIZE) rx = CURSOR_TEX_SIZE - 1;
        if (iy < 0 || iy >= CURSOR_TEX_SIZE) return;
        u32 *row = pixels + iy * pitch_u32;
        for (int px = lx; px <= rx; ++px) {
            row[px] = colour;
        }
    };
    while (x <= y) {
        hline(cx - y, cx + y, cy - x);
        hline(cx - y, cx + y, cy + x);
        hline(cx - x, cx + x, cy - y);
        hline(cx - x, cx + x, cy + y);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

// Blend a circle outline at the given radius onto the pixel buffer.
// Each pixel on the theoretical circle circumference is alpha-blended in.
// Uses Bresenham to enumerate the outline pixels exactly once per octant.
static void BlendCircleOutline(u32 *pixels, int pitch_u32,
                                int cx, int cy, int radius,
                                u8 r, u8 g, u8 b, u8 a,
                                const SDL_PixelFormat *fmt) {
    if (a == 0) return;
    int x = 0;
    int y = radius;
    int d = 1 - radius;
    auto plot8 = [&](int ox, int oy) {
        // 8 octant-symmetric points
        const int pts[8][2] = {
            {cx + ox, cy + oy}, {cx - ox, cy + oy},
            {cx + ox, cy - oy}, {cx - ox, cy - oy},
            {cx + oy, cy + ox}, {cx - oy, cy + ox},
            {cx + oy, cy - ox}, {cx - oy, cy - ox},
        };
        for (auto &pt : pts) {
            int px = pt[0], py = pt[1];
            if (px < 0 || px >= CURSOR_TEX_SIZE) continue;
            if (py < 0 || py >= CURSOR_TEX_SIZE) continue;
            BlendPixel(&pixels[py * pitch_u32 + px], r, g, b, a, fmt);
        }
    };
    while (x <= y) {
        plot8(x, y);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

// Blend a filled disc (all pixels inside radius inclusive) onto the buffer.
// Used for the upper-left highlight and centre dot — blended over existing content.
static void BlendFilledDisc(u32 *pixels, int pitch_u32,
                             int cx, int cy, int radius,
                             u8 r, u8 g, u8 b, u8 a,
                             const SDL_PixelFormat *fmt) {
    if (a == 0) return;
    int x = 0;
    int y = radius;
    int d = 1 - radius;
    auto bline = [&](int lx, int rx, int iy) {
        if (iy < 0 || iy >= CURSOR_TEX_SIZE) return;
        if (lx < 0) lx = 0;
        if (rx >= CURSOR_TEX_SIZE) rx = CURSOR_TEX_SIZE - 1;
        u32 *row = pixels + iy * pitch_u32;
        for (int px = lx; px <= rx; ++px) {
            BlendPixel(&row[px], r, g, b, a, fmt);
        }
    };
    while (x <= y) {
        bline(cx - y, cx + y, cy - x);
        bline(cx - y, cx + y, cy + x);
        bline(cx - x, cx + x, cy - y);
        bline(cx - x, cx + x, cy + y);
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            --y;
        }
        ++x;
    }
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdCursorElement::QdCursorElement(const QdTheme &theme)
    : theme_(theme),
      cursor_tex_(nullptr),
      cursor_tex_rclick_(nullptr),
      current_x_(960),   // screen centre X (1920 / 2)
      current_y_(540),   // screen centre Y (1080 / 2)
      visible_(true),
      right_click_active_(false)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r != nullptr) {
        BuildCursorTexture(r);
    } else {
        UL_LOG_WARN("qdesktop: Cursor ctor — main renderer not ready; "
                    "texture will be built on first OnRender call");
    }
}

QdCursorElement::~QdCursorElement() {
    if (cursor_tex_ != nullptr) {
        SDL_DestroyTexture(cursor_tex_);
        cursor_tex_ = nullptr;
    }
    if (cursor_tex_rclick_ != nullptr) {
        SDL_DestroyTexture(cursor_tex_rclick_);
        cursor_tex_rclick_ = nullptr;
    }
}

// ── BuildOneCursorTexture ───────────────────────────────────────────────────
// Builds one glass-bubble cursor texture with the given body + outline colours.
// Geometry, alphas, and the black/white centre marker are fixed (the cursor's
// design); only the body/outline RGB vary so we can produce the normal and
// right-click variants from the same code.

SDL_Texture *QdCursorElement::BuildOneCursorTexture(SDL_Renderer *r,
                                                    pu::ui::Color body,
                                                    pu::ui::Color outline,
                                                    bool is_right_click) {
    // Create a 44×44 streaming ABGR8888 texture with alpha blending.
    SDL_Texture *tex = SDL_CreateTexture(r,
                                          SDL_PIXELFORMAT_ABGR8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          CURSOR_TEX_SIZE,
                                          CURSOR_TEX_SIZE);
    if (tex == nullptr) {
        UL_LOG_WARN("qdesktop: Cursor BuildOneCursorTexture — SDL_CreateTexture failed: %s",
                    SDL_GetError());
        return nullptr;
    }

    // Enable per-pixel alpha blending on the texture (src-over onto the scene).
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    // Lock for pixel-level write access.
    void *raw_pixels = nullptr;
    int   pitch      = 0;
    if (SDL_LockTexture(tex, nullptr, &raw_pixels, &pitch) != 0) {
        UL_LOG_WARN("qdesktop: Cursor BuildOneCursorTexture — SDL_LockTexture failed: %s",
                    SDL_GetError());
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    u32 *pixels    = static_cast<u32 *>(raw_pixels);
    int  pitch_u32 = pitch / static_cast<int>(sizeof(u32));

    // Retrieve the SDL_PixelFormat so SDL_MapRGBA / SDL_GetRGBA give the
    // correct ABGR8888 byte ordering on this platform.
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
    if (fmt == nullptr) {
        UL_LOG_WARN("qdesktop: Cursor BuildOneCursorTexture — SDL_AllocFormat failed");
        SDL_UnlockTexture(tex);
        SDL_DestroyTexture(tex);
        return nullptr;
    }

    // ── Step 0: clear entire texture to fully transparent ──────────────────
    const u32 transparent = SDL_MapRGBA(fmt, 0, 0, 0, 0);
    for (int i = 0; i < CURSOR_TEX_SIZE * pitch_u32; ++i) {
        pixels[i] = transparent;
    }

    const int cx = CURSOR_TEX_CENTRE;   // 22
    const int cy = CURSOR_TEX_CENTRE;   // 22

    // ── Step 1: glass body — filled circle, theme cursor_fill at alpha=110 ──
    // alpha=110 ≈ 43% opacity — visible while still showing the desktop through
    // the bubble (glass effect preserved). RGB from the theme body colour.
    FillCircleSolid(pixels, pitch_u32,
                    cx, cy, CURSOR_RADIUS,
                    body.r, body.g, body.b,
                    110,
                    fmt);

    // ── Step 2: anti-aliased outline — two-pass Wu approximation (theme) ───
    // Pass 1: radius+1 (19 px) at alpha=80 — soft outer AA halo ring.
    BlendCircleOutline(pixels, pitch_u32,
                       cx, cy, CURSOR_RADIUS + 1,
                       outline.r, outline.g, outline.b,
                       80,
                       fmt);
    // Pass 2: exact radius (18 px) at alpha=255 — fully-opaque crisp boundary.
    BlendCircleOutline(pixels, pitch_u32,
                       cx, cy, CURSOR_RADIUS,
                       outline.r, outline.g, outline.b,
                       255,
                       fmt);
    // v3.7 contrast-survival edge: a cursor_fill (light) ring just OUTSIDE the
    // crisp dark edge.  A dark cursor_outline alone vanishes on a matching-dark
    // wallpaper (creator: "no circle around it") — this light edge keeps the
    // bubble's outline readable on dark backgrounds, while the dark edge keeps it
    // readable on light ones.  (body == cursor_fill for both variants.)
    BlendCircleOutline(pixels, pitch_u32,
                       cx, cy, CURSOR_RADIUS + 2,
                       body.r, body.g, body.b, 160, fmt);
    BlendCircleOutline(pixels, pitch_u32,
                       cx, cy, CURSOR_RADIUS + 3,
                       body.r, body.g, body.b, 70, fmt);

    // ── Step 3: centre core — cursor_outline disc + ACCENT dot (CURSOR-SPEC §2) ─
    // The core carries the brand accent (a "different colour" inside the glass),
    // ringed by cursor_outline so the exact click-point survives on any bg.
    const auto &core_ring = g_QdTheme.cursor_outline;  // always the theme outline
    const auto &core_dot  = g_QdTheme.accent;          // themed accent centre
    BlendFilledDisc(pixels, pitch_u32,
                    cx, cy, CURSOR_DOT_OUTER_RADIUS,
                    core_ring.r, core_ring.g, core_ring.b,
                    255,
                    fmt);
    BlendFilledDisc(pixels, pitch_u32,
                    cx, cy, CURSOR_DOT_INNER_RADIUS,
                    core_dot.r, core_dot.g, core_dot.b,
                    255,
                    fmt);

    // ── Step 4: right-click badge (CURSOR-SPEC §3) ─────────────────────────────
    // A cursor_right_click disc (Ø6) at the lower-right marks context-action mode.
    // `outline` is cursor_right_click for the right-click variant.
    if (is_right_click) {
        BlendFilledDisc(pixels, pitch_u32,
                        cx + 7, cy + 7, 3,
                        outline.r, outline.g, outline.b,
                        255,
                        fmt);
    }

    SDL_FreeFormat(fmt);
    SDL_UnlockTexture(tex);
    return tex;
}

// ── BuildCursorTexture (both variants from the live theme) ──────────────────────

void QdCursorElement::BuildCursorTexture(SDL_Renderer *r) {
    // Read the LIVE palette, not the captured theme_ copy: the cursor element
    // persists across theme switches, so g_QdTheme is the current truth and the
    // g_cursor_dirty flag (set in SetActivePalettePack) drives this rebuild.
    const QdTheme &t = g_QdTheme;
    // Normal: body=cursor_fill, ring=cursor_outline.  Right-click: body STAYS
    // cursor_fill so the silhouette is constant (CURSOR-SPEC §3); only the ring
    // (cursor_right_click) + the badge signal the mode.
    cursor_tex_        = BuildOneCursorTexture(r, t.cursor_fill, t.cursor_outline,     false);
    cursor_tex_rclick_ = BuildOneCursorTexture(r, t.cursor_fill, t.cursor_right_click, true);
    UL_LOG_INFO("qdesktop: Cursor textures built — fill #%02X%02X%02X / outline "
                "#%02X%02X%02X / right-click #%02X%02X%02X (radius=%d)",
                t.cursor_fill.r, t.cursor_fill.g, t.cursor_fill.b,
                t.cursor_outline.r, t.cursor_outline.g, t.cursor_outline.b,
                t.cursor_right_click.r, t.cursor_right_click.g, t.cursor_right_click.b,
                CURSOR_RADIUS);
}

// ── SetCursorPos ──────────────────────────────────────────────────────────────

void QdCursorElement::SetCursorPos(s32 x, s32 y) {
    if (x < 0 || x >= CURSOR_SCREEN_W) {
        return;
    }
    if (y < 0 || y >= CURSOR_SCREEN_H) {
        return;
    }
    current_x_ = x;
    current_y_ = y;
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdCursorElement::OnInput(const u64 keys_down, const u64 keys_up,
                               const u64 keys_held,
                               const pu::ui::TouchPoint touch_pos) {
    (void)keys_down;
    (void)keys_up;

    // Right-click cursor state: while the secondary (ZL) button is held — the
    // same button qd_Input maps to right-click (qd_Input.cpp) — render the
    // right-click-coloured texture for clear visual feedback.
    right_click_active_ = (keys_held & HidNpadButton_ZL) != 0;

    // Plutonium delivers coords already in 1920×1080 layout space.
    // TouchPoint::IsEmpty() returns true (x<0 && y<0) for "no finger on screen".
    if (!touch_pos.IsEmpty()) {
        SetCursorPos(touch_pos.x, touch_pos.y);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdCursorElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                const s32 /*x*/, const s32 /*y*/) {
    if (!visible_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        UL_LOG_WARN("qdesktop: Cursor OnRender got NULL main renderer");
        return;
    }

    // Theme changed? Drop both textures so they re-bake with the new palette
    // (mirrors the wallpaper's g_wallpaper_dirty consume in qd_Wallpaper.cpp).
    if (g_cursor_dirty.exchange(false, std::memory_order_acq_rel)) {
        if (cursor_tex_ != nullptr)        { SDL_DestroyTexture(cursor_tex_);        cursor_tex_ = nullptr; }
        if (cursor_tex_rclick_ != nullptr) { SDL_DestroyTexture(cursor_tex_rclick_); cursor_tex_rclick_ = nullptr; }
        UL_LOG_INFO("qdesktop: Cursor dirty flag consumed — re-baking for new theme");
    }

    // If the constructor ran before the renderer was ready (or we just dropped
    // the textures on a theme change), build both now.
    if (cursor_tex_ == nullptr || cursor_tex_rclick_ == nullptr) {
        BuildCursorTexture(r);
        if (cursor_tex_ == nullptr) {
            // BuildCursorTexture already logged; nothing to render.
            return;
        }
    }

    // Use the right-click variant while the secondary button is held; fall back
    // to the normal texture (also if the right-click variant failed to build).
    SDL_Texture *tex = (right_click_active_ && cursor_tex_rclick_ != nullptr)
                           ? cursor_tex_rclick_
                           : cursor_tex_;

    // Hot-spot is at texture centre (CURSOR_TEX_CENTRE, CURSOR_TEX_CENTRE);
    // shift the top-left corner so that centre pixel lands on (current_x_, current_y_).
    SDL_Rect dst;
    dst.x = current_x_ - CURSOR_TEX_CENTRE;
    dst.y = current_y_ - CURSOR_TEX_CENTRE;
    dst.w = CURSOR_TEX_SIZE;
    dst.h = CURSOR_TEX_SIZE;
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

} // namespace ul::menu::qdesktop
