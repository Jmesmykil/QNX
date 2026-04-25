// qd_PowerButton.cpp — QdPowerButtonElement implementation.
// Part of the Q OS qdesktop login screen power-action row.
// Owned file: do not edit from other agents.

#include <ul/menu/qdesktop/qd_PowerButton.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

namespace ul::menu::qdesktop {

// ── Glyph table ────────────────────────────────────────────────────────────────
// Maps Kind → single ASCII character rendered as the icon body.
// Uses unambiguous ASCII so it works on any TTF font that covers Basic Latin.
// Restart='R', Shutdown='P', Sleep='Z', Hekate='H', Custom='*'
/*static*/
char QdPowerButtonElement::GlyphChar(Kind k) {
    switch (k) {
        case Kind::Restart:  return 'R';
        case Kind::Shutdown: return 'P';
        case Kind::Sleep:    return 'Z';
        case Kind::Hekate:   return 'H';
        case Kind::Custom:   return '*';
    }
    return '*'; // unreachable but satisfies -Wreturn-type
}

// ── Factory ────────────────────────────────────────────────────────────────────

/*static*/
QdPowerButtonElement::Ref
QdPowerButtonElement::New(const QdTheme &theme, Kind kind, const std::string &label)
{
    return std::make_shared<QdPowerButtonElement>(theme, kind, label);
}

// ── Constructor / Destructor ───────────────────────────────────────────────────

QdPowerButtonElement::QdPowerButtonElement(const QdTheme &theme,
                                           Kind kind,
                                           const std::string &label)
    : theme_(theme), kind_(kind), label_(label),
      x_(0), y_(0), enabled_(true), focused_(false),
      on_click_(nullptr),
      glyph_tex_(nullptr), label_tex_(nullptr),
      press_inside_(false), down_x_(0), down_y_(0)
{
    UL_LOG_INFO("qdesktop: QdPowerButtonElement ctor kind=%d label='%s'",
                static_cast<int>(kind_), label_.c_str());
}

QdPowerButtonElement::~QdPowerButtonElement() {
    FreeTextures();
}

// ── Mutators ───────────────────────────────────────────────────────────────────

void QdPowerButtonElement::SetPos(s32 x, s32 y) {
    x_ = x;
    y_ = y;
}

void QdPowerButtonElement::SetOnClick(OnClickFn cb) {
    on_click_ = std::move(cb);
}

void QdPowerButtonElement::SetEnabled(bool e) {
    enabled_ = e;
}

void QdPowerButtonElement::SetFocused(bool f) {
    focused_ = f;
}

// ── Texture lifecycle ──────────────────────────────────────────────────────────

void QdPowerButtonElement::FreeTextures() {
    if (glyph_tex_ != nullptr) {
        SDL_DestroyTexture(glyph_tex_);
        glyph_tex_ = nullptr;
    }
    if (label_tex_ != nullptr) {
        SDL_DestroyTexture(label_tex_);
        label_tex_ = nullptr;
    }
}

void QdPowerButtonElement::EnsureGlyphTexture() {
    if (glyph_tex_ != nullptr) {
        return;
    }
    const char ch = GlyphChar(kind_);
    const std::string glyph_str(1, ch);
    // White glyph; the dark panel already provides contrast.
    const pu::ui::Color white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    glyph_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge),
        glyph_str, white);
    // glyph_tex_ may be nullptr if the font is not available; OnRender guards below.
}

void QdPowerButtonElement::EnsureLabelTexture() {
    if (label_tex_ != nullptr) {
        return;
    }
    if (label_.empty()) {
        return;
    }
    // Label uses theme text_primary colour.
    const pu::ui::Color fg = theme_.text_primary;
    // Soft-cap width so the label never overflows the 180px button.
    label_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        label_, fg,
        static_cast<u32>(BTN_W - 16)); // 8px margin each side
    // label_tex_ may be nullptr if font unavailable; OnRender guards below.
}

// ── OnRender ───────────────────────────────────────────────────────────────────
//
// Render order:
//   1. Background panel — translucent black, 70% alpha (0xB3).
//   2. Border           — 1px, accent colour normally; focus_ring when focused_.
//   3. Glyph            — left-aligned, vertically centred in the glyph zone.
//   4. Label            — right of glyph zone, vertically centred.
//   5. Disabled overlay — 50% alpha black rect over the whole panel when !enabled_.
//
// Layout within the 180×64 rect:
//   Glyph zone : x_abs .. x_abs+56, full height (32×32 glyph centred inside)
//   Label zone : x_abs+56 .. x_abs+BTN_W-8, full height

void QdPowerButtonElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                    const s32 origin_x, const s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) {
        return;
    }

    const s32 abs_x = x_ + origin_x;
    const s32 abs_y = y_ + origin_y;

    // ── 1. Background panel (translucent black) ───────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // 0xB3 ≈ 70% alpha — matches spec "translucent dark panel"
    SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xB3u);
    SDL_Rect panel { abs_x, abs_y, BTN_W, BTN_H };
    SDL_RenderFillRect(r, &panel);

    // ── 2. Border (1px) ───────────────────────────────────────────────────────
    {
        const pu::ui::Color &bc = focused_ ? theme_.focus_ring : theme_.accent;
        SDL_SetRenderDrawColor(r, bc.r, bc.g, bc.b, focused_ ? 0xFFu : 0x99u);
        SDL_Rect border { abs_x, abs_y, BTN_W, BTN_H };
        SDL_RenderDrawRect(r, &border);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Lazy-build text textures ──────────────────────────────────────────────
    EnsureGlyphTexture();
    EnsureLabelTexture();

    // ── 3. Glyph (left zone: x_abs .. x_abs+56) ──────────────────────────────
    static constexpr s32 GLYPH_ZONE_W = 56;  // left block reserved for glyph
    if (glyph_tex_ != nullptr) {
        int gw = 0, gh = 0;
        SDL_QueryTexture(glyph_tex_, nullptr, nullptr, &gw, &gh);
        // Centre the glyph inside the 56×64 glyph zone.
        const s32 gx = abs_x + (GLYPH_ZONE_W - gw) / 2;
        const s32 gy = abs_y + (BTN_H - gh) / 2;
        SDL_Rect gdst { gx, gy, gw, gh };
        SDL_RenderCopy(r, glyph_tex_, nullptr, &gdst);
    }

    // ── 4. Label (right zone: x_abs+56 .. x_abs+BTN_W-8) ────────────────────
    static constexpr s32 LABEL_START_X = 56;  // offset from button left edge
    static constexpr s32 LABEL_MARGIN_R = 8;  // right margin
    if (label_tex_ != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(label_tex_, nullptr, nullptr, &lw, &lh);
        // Left-align inside label zone; vertically centre.
        const s32 label_zone_w = BTN_W - LABEL_START_X - LABEL_MARGIN_R;
        const s32 clamped_lw   = std::min(lw, label_zone_w);
        const s32 lx = abs_x + LABEL_START_X;
        const s32 ly = abs_y + (BTN_H - lh) / 2;
        const SDL_Rect lsrc { 0, 0, clamped_lw, lh };
        const SDL_Rect ldst { lx, ly, clamped_lw, lh };
        SDL_RenderCopy(r, label_tex_, &lsrc, &ldst);
    }

    // ── 5. Disabled overlay (50% alpha black) ─────────────────────────────────
    if (!enabled_) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0x80u); // 50% alpha
        SDL_RenderFillRect(r, &panel);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}

// ── OnInput ────────────────────────────────────────────────────────────────────
//
// Touch: click-vs-drag state machine matching QdDesktopIconsElement.
//   TouchDown  inside rect → set press_inside_, record down_x_/down_y_.
//   TouchMove  outside rect → clear press_inside_ (drag cancel).
//   TouchUp    with press_inside_ true → fire on_click_.
//
// A-button when focused_ → fire on_click_.
//
// All input is silently dropped when !enabled_.

static constexpr s32 POWER_CLICK_TOLERANCE_PX = 20;

void QdPowerButtonElement::OnInput(const u64 keys_down,
                                   const u64 /*keys_up*/,
                                   const u64 /*keys_held*/,
                                   const pu::ui::TouchPoint touch_pos)
{
    if (!enabled_) {
        press_inside_ = false;
        return;
    }

    // ── A-button when focused ──────────────────────────────────────────────────
    if (focused_ && (keys_down & HidNpadButton_A)) {
        if (on_click_) {
            on_click_();
        }
        return;
    }

    // ── Touch handling ─────────────────────────────────────────────────────────
    const bool is_active = (touch_pos.x != 0 || touch_pos.y != 0);

    if (is_active) {
        const bool inside = (touch_pos.x >= x_ && touch_pos.x < x_ + BTN_W &&
                             touch_pos.y >= y_ && touch_pos.y < y_ + BTN_H);

        if (!press_inside_) {
            // Potential TouchDown: finger just entered an active state.
            if (inside) {
                press_inside_ = true;
                down_x_ = touch_pos.x;
                down_y_ = touch_pos.y;
            }
        } else {
            // Finger is held; check for drag-cancel.
            const s32 dx = touch_pos.x - down_x_;
            const s32 dy = touch_pos.y - down_y_;
            // Use squared comparison to avoid sqrt — matches DesktopIcons pattern.
            const s32 dist_sq = dx * dx + dy * dy;
            const s32 tol_sq  = POWER_CLICK_TOLERANCE_PX * POWER_CLICK_TOLERANCE_PX;
            if (!inside || dist_sq > tol_sq) {
                press_inside_ = false;
            }
        }
    } else {
        // TouchUp (touch no longer active).
        if (press_inside_) {
            press_inside_ = false;
            if (on_click_) {
                on_click_();
            }
        }
    }
}

} // namespace ul::menu::qdesktop
