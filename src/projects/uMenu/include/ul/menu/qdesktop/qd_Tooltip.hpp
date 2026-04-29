// qd_Tooltip.hpp — Shared hover-tooltip primitive for uMenu v1.8.27.
//
// A single shared instance (owned by QdDesktopIconsElement as a value member)
// is used for both dock-icon tooltips and desktop-folder tooltips.
//
// Lifecycle:
//   Show(renderer, text, anchor_x, anchor_y, prefer_above)
//       - Frees any previously cached texture.
//       - Renders a new text texture for `text` via pu::ui::render::RenderText.
//       - Records geometry for the next Render call.
//       - Sets visible_ = true.
//   Hide()
//       - Frees the cached texture via pu::ui::render::DeleteTexture.
//       - Sets visible_ = false.
//   Render(renderer)
//       - No-op if !visible_ or tex_ == nullptr.
//       - Blits a dark semi-transparent background rect followed by the text.
//       - Does NOT call RenderText — all text prep happens in Show().
//   ~QdTooltip() — calls Hide().
//
// B41/B42 compliance:
//   tex_ is always freed with pu::ui::render::DeleteTexture, never
//   SDL_DestroyTexture.  RenderText returns an LRU-cache-owned pointer;
//   callers must use DeleteTexture (see render_Renderer.hpp line 677-682).
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <string>
#include <switch.h>

namespace ul::menu::qdesktop {

// ── QdTooltip ─────────────────────────────────────────────────────────────────
//
// Small text-label popup rendered above (or below) an anchor point.
// Background: dark semi-transparent filled rect (0x10, 0x10, 0x14, 0xD0).
// Text: white (0xFF, 0xFF, 0xFF, 0xFF), DefaultFontSize::Small.
// Padding: 10 px left/right, 6 px top/bottom.
// prefer_above: if the tooltip top would be < 0, render below instead.
//
// Memory: one SDL_Texture* (tex_) owned by this instance, freed via
// pu::ui::render::DeleteTexture on every Show() + Hide() + ~QdTooltip().
class QdTooltip {
public:
    QdTooltip();
    ~QdTooltip();

    // Non-copyable, non-movable (owns an SDL texture).
    QdTooltip(const QdTooltip &) = delete;
    QdTooltip &operator=(const QdTooltip &) = delete;
    QdTooltip(QdTooltip &&) = delete;
    QdTooltip &operator=(QdTooltip &&) = delete;

    // Returns whether the tooltip is currently visible.
    bool IsVisible() const { return visible_; }

    // Prepare the tooltip for the given text and anchor position.
    // anchor_x / anchor_y: centre-x and top-y of the element being hovered.
    // prefer_above: true = render the tooltip above anchor_y (normal);
    //               false = render below (or when clipped above).
    // If the same text as the previous call is passed, the cached texture is
    // reused (no RenderText call).  Safe to call every frame — cheap when the
    // text is unchanged.
    void Show(SDL_Renderer *r,
              const std::string &text,
              s32 anchor_x, s32 anchor_y,
              bool prefer_above);

    // Frees the cached texture and clears visible_. Safe to call when hidden.
    void Hide();

    // Blits the background rect and text texture. No-op if !visible_.
    // Call at the end of the parent element's OnRender, after all icon layers
    // and folder layers, but before the help overlay.
    void Render(SDL_Renderer *r) const;

private:
    // ── Helpers ───────────────────────────────────────────────────────────────
    // Free tex_ via DeleteTexture and null it. Safe if tex_ is already nullptr.
    void FreeTex();

    // ── State ─────────────────────────────────────────────────────────────────
    bool visible_ = false;

    // Cached text texture (LRU-owned — free with DeleteTexture only).
    SDL_Texture *tex_ = nullptr;
    int tex_w_ = 0;
    int tex_h_ = 0;

    // The text that was used to build tex_ (used to detect same-text re-calls).
    std::string cached_text_;

    // Computed render geometry (set in Show(), consumed in Render()).
    SDL_Rect bg_rect_  = {0, 0, 0, 0};   // background rect in screen coords
    int      text_x_   = 0;              // top-left x for text blit
    int      text_y_   = 0;              // top-left y for text blit
};

}  // namespace ul::menu::qdesktop
