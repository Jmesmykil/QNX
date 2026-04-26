// qd_UserCard.cpp — Q OS login-screen user profile card element.
// Full implementation of QdUserCardElement as declared in qd_UserCard.hpp.
// Rendering uses raw SDL2 (via pu::ui::render::GetMainRenderer()) consistent
// with the qd_DesktopIcons.cpp pattern.
// JPEG decode pipeline: SDL_RWFromConstMem → IMG_Load_RW(rw,1) → RGBA8888
//   → ABGR8888 → lock → SDL_CreateTexture(STATIC) → SDL_UpdateTexture.
// Circular avatar: Bresenham scan-line strips over avatar_tex_.
// Text: lazy-rasterise once via pu::ui::render::RenderText; cached in name_tex_
//   and hint_tex_; freed in destructor.

#include <ul/menu/qdesktop/qd_UserCard.hpp>
#include <ul/menu/ui/ui_Common.hpp>   // Cycle I: RenderTextAutoFit (system-wide text auto-fit)
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace ul::menu::qdesktop {

// ── Click tolerance (same value as qd_DesktopIcons) ─────────────────────────
static constexpr s32 CARD_CLICK_TOLERANCE_PX = 24;

// ── Factory ──────────────────────────────────────────────────────────────────

// static
QdUserCardElement::Ref QdUserCardElement::New(const QdTheme &theme,
                                               const AccountUid uid,
                                               const std::string &name,
                                               const u8 *icon_jpeg,
                                               size_t icon_jpeg_len)
{
    return std::make_shared<QdUserCardElement>(theme, uid, name,
                                               icon_jpeg, icon_jpeg_len);
}

// ── Constructor ───────────────────────────────────────────────────────────────

QdUserCardElement::QdUserCardElement(const QdTheme &theme,
                                     const AccountUid uid,
                                     const std::string &name,
                                     const u8 *icon_jpeg,
                                     size_t icon_jpeg_len)
    : theme_(theme), uid_(uid), name_(name)
{
    UL_LOG_INFO("qdesktop: QdUserCardElement ctor uid=%016llx name=%s",
                static_cast<unsigned long long>(uid_.uid[0]), name_.c_str());

    // Derive fallback colour from uid bytes (DJB2-inspired, mirrors
    // fallback_r_/g_/b_ initialisation comment in the header).
    // Use the lower 3 bytes of uid[0] for hue, then clamp to pleasant range.
    {
        const u64 u = uid_.uid[0];
        const u8 b0 = static_cast<u8>(u & 0xFFu);
        const u8 b1 = static_cast<u8>((u >> 8u) & 0xFFu);
        const u8 b2 = static_cast<u8>((u >> 16u) & 0xFFu);
        // Mix bytes into [0x30, 0xCF] range so the colour is always visible.
        fallback_r_ = static_cast<u8>(0x30u + (b0 % 0xA0u));
        fallback_g_ = static_cast<u8>(0x30u + (b1 % 0xA0u));
        fallback_b_ = static_cast<u8>(0x30u + (b2 % 0xA0u));
    }

    if (icon_jpeg != nullptr && icon_jpeg_len > 0) {
        if (!DecodeAvatar(icon_jpeg, icon_jpeg_len)) {
            UL_LOG_INFO("qdesktop: QdUserCardElement: avatar decode failed — will use fallback");
        }
    }
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdUserCardElement::~QdUserCardElement() {
    // SDL_DestroyTexture is safe for nullptr — but guard explicitly for clarity.
    if (avatar_tex_ != nullptr) {
        SDL_DestroyTexture(avatar_tex_);
        avatar_tex_ = nullptr;
    }
    if (name_tex_ != nullptr) {
        SDL_DestroyTexture(name_tex_);
        name_tex_ = nullptr;
    }
    if (hint_tex_ != nullptr) {
        SDL_DestroyTexture(hint_tex_);
        hint_tex_ = nullptr;
    }
    if (glyph_tex_ != nullptr) {
        SDL_DestroyTexture(glyph_tex_);
        glyph_tex_ = nullptr;
    }
}

// ── SetPos ────────────────────────────────────────────────────────────────────

void QdUserCardElement::SetPos(s32 x, s32 y) {
    x_ = x;
    y_ = y;
}

// ── SetFocused ────────────────────────────────────────────────────────────────

void QdUserCardElement::SetFocused(bool focused) {
    focused_ = focused;
}

// ── SetOnSelect ───────────────────────────────────────────────────────────────

void QdUserCardElement::SetOnSelect(OnSelectFn cb) {
    on_select_ = std::move(cb);
}

// ── DecodeAvatar ──────────────────────────────────────────────────────────────
// Decodes JPEG bytes into a 160×160 ABGR8888 SDL_TEXTUREACCESS_STATIC texture.
// On any failure: leaves avatar_tex_ == nullptr (fallback will be used by render).
// Returns true on success.

bool QdUserCardElement::DecodeAvatar(const u8 *jpeg, size_t jpeg_len) {
    // Wrap the in-memory JPEG bytes; IMG_Load_RW frees rw when freesrc=1.
    SDL_RWops *rw = SDL_RWFromConstMem(jpeg, static_cast<int>(jpeg_len));
    if (rw == nullptr) {
        UL_LOG_INFO("qdesktop: DecodeAvatar: SDL_RWFromConstMem failed: %s",
                    SDL_GetError());
        return false;
    }

    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);
    // rw is now freed regardless — do not touch.
    if (raw == nullptr) {
        UL_LOG_INFO("qdesktop: DecodeAvatar: IMG_Load_RW failed: %s",
                    IMG_GetError());
        return false;
    }

    // First conversion: normalise to RGBA8888.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);
    if (rgba == nullptr) {
        UL_LOG_INFO("qdesktop: DecodeAvatar: RGBA8888 convert failed: %s",
                    SDL_GetError());
        return false;
    }

    // Second conversion: RGBA8888 → ABGR8888 to fix byte-order on AArch64 LE.
    // (Matches the LoadJpegIconToCache double-convert pattern in qd_DesktopIcons.)
    SDL_Surface *abgr = SDL_ConvertSurfaceFormat(rgba, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rgba);
    if (abgr == nullptr) {
        UL_LOG_INFO("qdesktop: DecodeAvatar: ABGR8888 convert failed: %s",
                    SDL_GetError());
        return false;
    }

    if (SDL_LockSurface(abgr) != 0) {
        UL_LOG_INFO("qdesktop: DecodeAvatar: SDL_LockSurface failed: %s",
                    SDL_GetError());
        SDL_FreeSurface(abgr);
        return false;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        SDL_UnlockSurface(abgr);
        SDL_FreeSurface(abgr);
        UL_LOG_INFO("qdesktop: DecodeAvatar: no renderer available");
        return false;
    }

    // Avatar diameter = 2 * AVATAR_RADIUS = 160 px.
    const int AVATAR_DIAM = AVATAR_RADIUS * 2;

    // Create a STATIC texture at 160×160. We upload the decoded pixels via
    // SDL_UpdateTexture; no lock/unlock cycle on the texture itself.
    SDL_Texture *tex = SDL_CreateTexture(r,
                                         SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC,
                                         AVATAR_DIAM, AVATAR_DIAM);
    if (tex == nullptr) {
        SDL_UnlockSurface(abgr);
        SDL_FreeSurface(abgr);
        UL_LOG_INFO("qdesktop: DecodeAvatar: SDL_CreateTexture failed: %s",
                    SDL_GetError());
        return false;
    }

    // Source surface may not be 160×160 — blit-scale via a temporary surface.
    // We create a 160×160 ABGR8888 scratch surface, SDL_BlitScaled from abgr,
    // then upload the scratch pixels.
    SDL_Surface *scaled = SDL_CreateRGBSurfaceWithFormat(
        0, AVATAR_DIAM, AVATAR_DIAM, 32, SDL_PIXELFORMAT_ABGR8888);
    if (scaled == nullptr) {
        SDL_DestroyTexture(tex);
        SDL_UnlockSurface(abgr);
        SDL_FreeSurface(abgr);
        UL_LOG_INFO("qdesktop: DecodeAvatar: SDL_CreateRGBSurface failed: %s",
                    SDL_GetError());
        return false;
    }

    SDL_Rect dst_rect { 0, 0, AVATAR_DIAM, AVATAR_DIAM };
    SDL_BlitScaled(abgr, nullptr, scaled, &dst_rect);
    SDL_UnlockSurface(abgr);
    SDL_FreeSurface(abgr);

    if (SDL_LockSurface(scaled) != 0) {
        SDL_FreeSurface(scaled);
        SDL_DestroyTexture(tex);
        UL_LOG_INFO("qdesktop: DecodeAvatar: SDL_LockSurface(scaled) failed: %s",
                    SDL_GetError());
        return false;
    }

    SDL_UpdateTexture(tex, nullptr, scaled->pixels, scaled->pitch);
    SDL_UnlockSurface(scaled);
    SDL_FreeSurface(scaled);

    // Enable blending so the circular clip rows alpha-blend correctly.
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    avatar_tex_ = tex;
    UL_LOG_INFO("qdesktop: DecodeAvatar: ok avatar_tex_=%p", static_cast<void*>(tex));
    return true;
}

// ── EnsureTextTextures ────────────────────────────────────────────────────────
// Lazy-rasterises name_tex_, hint_tex_, and (when avatar missing) glyph_tex_.
// Safe to call every frame — no-op once all required textures are allocated.

void QdUserCardElement::EnsureTextTextures(SDL_Renderer * /*r*/) {
    // name label — Cycle I auto-fit: shrinks font from Large -> Small until the
    // rasterised width fits in CARD_W - 16. Long account names like
    // "Jamesmykil" no longer truncate to "Jamesm..." per user feedback.
    if (name_tex_ == nullptr && !name_.empty()) {
        const pu::ui::Color white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        name_tex_ = ::ul::menu::ui::RenderTextAutoFit(
            name_, white,
            static_cast<u32>(CARD_W - 16),
            pu::ui::DefaultFontSize::Large);
    }

    // hint label — Small text_secondary ("Tap to log in").
    if (hint_tex_ == nullptr) {
        const pu::ui::Color hint_clr {
            theme_.text_secondary.r,
            theme_.text_secondary.g,
            theme_.text_secondary.b,
            0xFFu
        };
        hint_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            "Tap to log in", hint_clr,
            static_cast<u32>(CARD_W - 16));
    }

    // fallback glyph — first letter of name, Medium white (only needed when
    // avatar_tex_ is null; still rasterise so we don't repeatedly try).
    if (glyph_tex_ == nullptr && !name_.empty() && avatar_tex_ == nullptr) {
        const std::string glyph_str(1u, name_[0]);
        const pu::ui::Color white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
        glyph_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            glyph_str, white);
    }
}

// ── RenderCircularAvatar ──────────────────────────────────────────────────────
// Clips avatar_tex_ (160×160) into a circle of the specified radius centred at
// (cx, cy) in screen coordinates.  Technique: Bresenham scan-line loop — for
// each row y ∈ [-r, +r] compute half-width x_span = √(r²−y²), then blit one
// horizontal strip from the avatar texture to the matching screen strip.

void QdUserCardElement::RenderCircularAvatar(SDL_Renderer *r,
                                              s32 cx, s32 cy,
                                              s32 radius) const
{
    if (avatar_tex_ == nullptr || r == nullptr) {
        return;
    }

    const s32 r2 = radius * radius;
    const int AVATAR_DIAM = radius * 2;

    for (s32 dy = -radius; dy <= radius; ++dy) {
        // Half-width of the chord at this row.
        const s32 dx = static_cast<s32>(
            std::sqrt(static_cast<float>(r2 - dy * dy)));
        if (dx <= 0) {
            continue;
        }

        // Screen coordinates of this horizontal strip.
        const s32 sx  = cx - dx;
        const s32 sy  = cy + dy;
        const s32 sw  = dx * 2;

        // Source strip inside the 160×160 texture.
        // avatar_tex_ is 160×160; the circle centre in texture space is (r, r).
        const s32 tx  = radius - dx;
        const s32 ty  = radius + dy;   // row in [0, DIAM)
        const s32 tw  = sw;
        const s32 th  = 1;

        // Clamp source rect to texture bounds.
        if (ty < 0 || ty >= AVATAR_DIAM) {
            continue;
        }
        const s32 tx_clamped  = std::max(s32(0), tx);
        const s32 tx_right    = std::min(s32(AVATAR_DIAM), tx + tw);
        const s32 tw_clamped  = tx_right - tx_clamped;
        if (tw_clamped <= 0) {
            continue;
        }
        // Adjust screen x for any left-side clamp.
        const s32 sx_adjusted = sx + (tx_clamped - tx);

        SDL_Rect src { tx_clamped, ty, tw_clamped, th };
        SDL_Rect dst { sx_adjusted, sy, tw_clamped, th };
        SDL_RenderCopy(r, avatar_tex_, &src, &dst);
    }
}

// ── RenderFallbackAvatar ──────────────────────────────────────────────────────
// Draws a solid-colour filled circle using the same Bresenham scan-line approach,
// then centres glyph_tex_ (first letter) over it.

void QdUserCardElement::RenderFallbackAvatar(SDL_Renderer *r,
                                              s32 cx, s32 cy,
                                              s32 radius)
{
    if (r == nullptr) {
        return;
    }

    // Filled circle with the uid-derived fallback colour.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, fallback_r_, fallback_g_, fallback_b_, 0xFFu);

    const s32 r2 = radius * radius;
    for (s32 dy = -radius; dy <= radius; ++dy) {
        const s32 dx = static_cast<s32>(
            std::sqrt(static_cast<float>(r2 - dy * dy)));
        if (dx <= 0) {
            continue;
        }
        SDL_Rect row { cx - dx, cy + dy, dx * 2, 1 };
        SDL_RenderFillRect(r, &row);
    }

    // Glyph overlay (lazy-rasterised, may be null if name is empty).
    if (glyph_tex_ != nullptr) {
        int gw = 0, gh = 0;
        SDL_QueryTexture(glyph_tex_, nullptr, nullptr, &gw, &gh);
        SDL_Rect gdst {
            cx - gw / 2,
            cy - gh / 2,
            gw, gh
        };
        SDL_RenderCopy(r, glyph_tex_, nullptr, &gdst);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdUserCardElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                  const s32 origin_x, const s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    // Lazy-rasterise text textures (no-op if already done).
    EnsureTextTextures(r);

    // Cycle H1 (touch+position fix): SetPos() stores absolute 1920x1080 layout
    // coords. OnInput's HitsRegion compares touch_pos (also absolute layout
    // coords per QdCursor::OnInput note) directly against x_/y_. Adding
    // origin_x/origin_y here would render at a different position than the
    // hit-test box, breaking touch and visibly mis-positioning the card when
    // any non-zero origin is passed. Render at bare x_/y_ so render and
    // hit-test agree exactly.
    (void)origin_x;
    (void)origin_y;
    const s32 abs_x = x_;
    const s32 abs_y = y_;

    // Hover state: treat as hovered if touch was active inside card last frame.
    // (focused_ is set externally by the layout for D-pad navigation.)
    const bool highlight = focused_ || hovered_;

    // ── 1. Card panel: 108% dst rect when highlighted ─────────────────────
    // Scale factor: 108% means dst is CARD_W*1.08 × CARD_H*1.08 centred on card.
    const s32 panel_w = highlight
        ? static_cast<s32>(CARD_W * 108 / 100)
        : CARD_W;
    const s32 panel_h = highlight
        ? static_cast<s32>(CARD_H * 108 / 100)
        : CARD_H;
    const s32 panel_x = abs_x - (panel_w - CARD_W) / 2;
    const s32 panel_y = abs_y - (panel_h - CARD_H) / 2;

    // Translucent dark background (surface_glass at ~75% alpha).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r,
        theme_.surface_glass.g,
        theme_.surface_glass.b,
        0xBFu);  // 0xBF ≈ 75% opacity
    SDL_Rect panel_rect { panel_x, panel_y, panel_w, panel_h };
    SDL_RenderFillRect(r, &panel_rect);

    // ── 2. Border ─────────────────────────────────────────────────────────
    // Highlighted: cursor_fill (bright).  Normal: accent (dim tint).
    if (highlight) {
        SDL_SetRenderDrawColor(r,
            theme_.cursor_fill.r,
            theme_.cursor_fill.g,
            theme_.cursor_fill.b,
            0xFFu);
    } else {
        SDL_SetRenderDrawColor(r,
            theme_.accent.r,
            theme_.accent.g,
            theme_.accent.b,
            0x80u);  // 50% alpha for the normal-state tint
    }
    // Draw 2-pixel border using four filled rect strips (SDL_RenderDrawRect is
    // 1px only — use two overlapping outlines for a 2px effect).
    {
        SDL_Rect b1 { panel_x,     panel_y,     panel_w, panel_h };
        SDL_Rect b2 { panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2 };
        SDL_RenderDrawRect(r, &b1);
        SDL_RenderDrawRect(r, &b2);
    }

    // Restore blend mode.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 3. Circular avatar (or fallback) ──────────────────────────────────
    // Avatar centre in absolute screen coords.
    const s32 avatar_cx = abs_x + CARD_W / 2;
    const s32 avatar_cy = abs_y + AVATAR_CENTER_Y;

    if (avatar_tex_ != nullptr) {
        RenderCircularAvatar(r, avatar_cx, avatar_cy, AVATAR_RADIUS);
    } else {
        RenderFallbackAvatar(r, avatar_cx, avatar_cy, AVATAR_RADIUS);
    }

    // ── 4. Account name ───────────────────────────────────────────────────
    if (name_tex_ != nullptr) {
        int nw = 0, nh = 0;
        SDL_QueryTexture(name_tex_, nullptr, nullptr, &nw, &nh);
        SDL_Rect ndst {
            abs_x + (CARD_W - nw) / 2,
            abs_y + NAME_Y_BELOW_AVATAR,
            nw, nh
        };
        SDL_RenderCopy(r, name_tex_, nullptr, &ndst);
    }

    // ── 5. Tap hint ───────────────────────────────────────────────────────
    if (hint_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_tex_, nullptr, nullptr, &hw, &hh);
        SDL_Rect hdst {
            abs_x + (CARD_W - hw) / 2,
            abs_y + CARD_H - HINT_BOTTOM_PAD - hh,
            hw, hh
        };
        SDL_RenderCopy(r, hint_tex_, nullptr, &hdst);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────
// A-button + focused_  →  fire on_select_ immediately.
// Touch click-vs-drag state machine — matches the qd_DesktopIcons pattern.
//   TouchDown:  record origin; do NOT fire.
//   TouchMove:  update last position; do NOT fire.
//   TouchUp:    fire only when (a) was pressed inside, (b) lift hits card,
//               (c) displacement ≤ CARD_CLICK_TOLERANCE_PX.

void QdUserCardElement::OnInput(const u64 keys_down,
                                 const u64 keys_up,
                                 const u64 keys_held,
                                 const pu::ui::TouchPoint touch_pos)
{
    (void)keys_up;
    (void)keys_held;

    // ── A-button ─────────────────────────────────────────────────────────────
    if ((keys_down & HidNpadButton_A) && focused_) {
        UL_LOG_INFO("qdesktop: UserCard A-button uid=%016llx",
                    static_cast<unsigned long long>(uid_.uid[0]));
        if (on_select_) {
            on_select_(uid_);
        }
        return;
    }

    // ── Touch state machine ───────────────────────────────────────────────────

    const bool touch_active_now = !touch_pos.IsEmpty();

    if (touch_active_now) {
        const s32 tx = touch_pos.x;
        const s32 ty = touch_pos.y;

        if (!was_touch_active_) {
            // TouchDown — record origin.
            // Only set press_inside_ if the down point is within this card.
            const bool inside = touch_pos.HitsRegion(x_, y_, CARD_W, CARD_H);
            press_inside_  = inside;
            down_x_        = tx;
            down_y_        = ty;
            last_touch_x_  = tx;
            last_touch_y_  = ty;
            hovered_       = inside;
            if (inside) {
                UL_LOG_INFO("qdesktop: UserCard touch_down x=%d y=%d", tx, ty);
            }
        } else {
            // TouchMove — track position and hover.
            last_touch_x_ = tx;
            last_touch_y_ = ty;
            hovered_ = touch_pos.HitsRegion(x_, y_, CARD_W, CARD_H);
        }
    } else if (was_touch_active_) {
        // TouchUp
        if (press_inside_) {
            const bool lift_inside = pu::ui::TouchHitsRegion(
                last_touch_x_, last_touch_y_, x_, y_, CARD_W, CARD_H);

            const s32 dx = last_touch_x_ - down_x_;
            const s32 dy = last_touch_y_ - down_y_;
            const s32 dist_sq = dx * dx + dy * dy;
            const s32 tol     = CARD_CLICK_TOLERANCE_PX;
            const s32 tol_sq  = tol * tol;

            if (lift_inside && dist_sq <= tol_sq) {
                UL_LOG_INFO("qdesktop: UserCard touch click uid=%016llx",
                            static_cast<unsigned long long>(uid_.uid[0]));
                if (on_select_) {
                    on_select_(uid_);
                }
            }
        }
        press_inside_ = false;
        hovered_      = false;
    }

    was_touch_active_ = touch_active_now;
}

} // namespace ul::menu::qdesktop
