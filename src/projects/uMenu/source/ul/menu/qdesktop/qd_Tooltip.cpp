// qd_Tooltip.cpp — Shared hover-tooltip implementation (uMenu v1.8.27).
//
// B41/B42: tex_ is ALWAYS freed via pu::ui::render::DeleteTexture.
// Never SDL_DestroyTexture on RenderText output (LRU cache contract).

#include <ul/menu/qdesktop/qd_Tooltip.hpp>

namespace ul::menu::qdesktop {

// ── Layout constants ──────────────────────────────────────────────────────────
// Padding inside the background rect (px).
static constexpr int kPadX = 10;  // left + right padding each
static constexpr int kPadY = 6;   // top + bottom padding each

// Minimum gap above the anchor when prefer_above=true.
// The tooltip bottom lands at (anchor_y - kGapAbove).
static constexpr int kGapAbove = 6;

// Minimum gap below the anchor when rendering below (fallback).
static constexpr int kGapBelow = 6;

// Background colour — nearly opaque dark (matches hot-corner widget fill).
static constexpr u8 kBgR = 0x10u;
static constexpr u8 kBgG = 0x10u;
static constexpr u8 kBgB = 0x14u;
static constexpr u8 kBgA = 0xD0u;

// Text colour — white, fully opaque.
static constexpr pu::ui::Color kTextColor { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

// Screen bounds (1920×1080 fixed for Erista).
static constexpr int kScreenW = 1920;
static constexpr int kScreenH = 1080;

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdTooltip::QdTooltip()
    : visible_(false), tex_(nullptr), tex_w_(0), tex_h_(0),
      cached_text_(), bg_rect_{0,0,0,0}, text_x_(0), text_y_(0)
{}

QdTooltip::~QdTooltip()
{
    Hide();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void QdTooltip::FreeTex()
{
    if (tex_ != nullptr) {
        pu::ui::render::DeleteTexture(tex_);
        tex_ = nullptr;
        tex_w_ = 0;
        tex_h_ = 0;
    }
}

// ── Public interface ──────────────────────────────────────────────────────────

void QdTooltip::Show(SDL_Renderer * /*r*/,
                     const std::string &text,
                     s32 anchor_x, s32 anchor_y,
                     bool prefer_above)
{
    // Rebuild text texture only when text changes (avoids RenderText each frame).
    if (text != cached_text_ || tex_ == nullptr) {
        FreeTex();
        cached_text_ = text;
        if (!text.empty()) {
            tex_ = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                text,
                kTextColor);
            if (tex_ != nullptr) {
                SDL_QueryTexture(tex_, nullptr, nullptr, &tex_w_, &tex_h_);
            }
        }
    }

    if (tex_ == nullptr) {
        // Nothing to show — text failed to render.
        visible_ = false;
        return;
    }

    // ── Compute background rect ───────────────────────────────────────────────
    const int bg_w = tex_w_ + kPadX * 2;
    const int bg_h = tex_h_ + kPadY * 2;

    // Centre the tooltip horizontally on anchor_x, clamped to screen.
    int bg_x = anchor_x - bg_w / 2;
    if (bg_x < 0)                  bg_x = 0;
    if (bg_x + bg_w > kScreenW)    bg_x = kScreenW - bg_w;

    // Vertical position: prefer above anchor_y, fall back to below if clipped.
    int bg_y;
    if (prefer_above) {
        bg_y = anchor_y - kGapAbove - bg_h;
        if (bg_y < 0) {
            // Clipped above — render below instead.
            bg_y = anchor_y + kGapBelow;
        }
    } else {
        bg_y = anchor_y + kGapBelow;
        if (bg_y + bg_h > kScreenH) {
            // Clipped below — render above instead.
            bg_y = anchor_y - kGapAbove - bg_h;
            if (bg_y < 0) bg_y = 0;
        }
    }

    bg_rect_ = SDL_Rect { bg_x, bg_y, bg_w, bg_h };
    text_x_  = bg_x + kPadX;
    text_y_  = bg_y + kPadY;
    visible_ = true;
}

void QdTooltip::Hide()
{
    FreeTex();
    cached_text_.clear();
    visible_ = false;
    bg_rect_ = SDL_Rect { 0, 0, 0, 0 };
    text_x_  = 0;
    text_y_  = 0;
}

void QdTooltip::Render(SDL_Renderer *r) const
{
    if (!visible_ || tex_ == nullptr || r == nullptr) {
        return;
    }

    // Background — semi-transparent dark fill with blend mode.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, kBgR, kBgG, kBgB, kBgA);
    SDL_RenderFillRect(r, &bg_rect_);

    // 1 px white border at 25% alpha for definition.
    SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x40u);
    // top / bottom / left / right border lines
    SDL_Rect bt { bg_rect_.x, bg_rect_.y,              bg_rect_.w, 1 };
    SDL_Rect bb { bg_rect_.x, bg_rect_.y + bg_rect_.h - 1, bg_rect_.w, 1 };
    SDL_Rect bl { bg_rect_.x, bg_rect_.y,              1, bg_rect_.h };
    SDL_Rect br { bg_rect_.x + bg_rect_.w - 1, bg_rect_.y, 1, bg_rect_.h };
    SDL_RenderFillRect(r, &bt);
    SDL_RenderFillRect(r, &bb);
    SDL_RenderFillRect(r, &bl);
    SDL_RenderFillRect(r, &br);

    // Restore non-blend mode before text blit (text texture carries its own alpha).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Text blit.
    SDL_Rect tdst { text_x_, text_y_, tex_w_, tex_h_ };
    SDL_RenderCopy(r, tex_, nullptr, &tdst);
}

}  // namespace ul::menu::qdesktop
