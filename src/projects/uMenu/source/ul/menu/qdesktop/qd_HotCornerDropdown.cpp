// qd_HotCornerDropdown.cpp — Hot-corner dropdown panel (uMenu v1.9).
//
// All text textures are pre-rendered in Open() and blitted cheaply in Render().
// Close() frees every texture via pu::ui::render::DeleteTexture, which honours
// the Plutonium LRU cache contract (B41/B42 — callers must use DeleteTexture,
// never SDL_DestroyTexture, for textures returned by RenderText).
//
// Panel geometry (anchored to top-left, just right of the hot-corner widget):
//   Anchor x = LP_HOTCORNER_W (96)
//   Anchor y = 0
//   Panel width  = 280 px
//   Panel height = kDropdownItems * kRowH + 2*kPadV = 5*48+16 = 256 px
//   Row height   = 48 px (with 8 px top pad inside each row for text baseline)
//
// Palette:
//   Panel bg  : #0E1A33 at A=0xEA (navy, 92% opaque) — Q OS brand dark
//   Hover bg  : #1E2E4E at A=0xFF
//   Label text: #FFFFFF (enabled), #606060 (disabled)
//   Panel border: #00E5FF (cyan, 1 px)

#include <ul/menu/qdesktop/qd_HotCornerDropdown.hpp>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>     // LP_HOTCORNER_W, LP_HOTCORNER_H
#include <ul/menu/ui/ui_MenuApplication.hpp>      // MenuType
#include <ul/menu/ui/ui_Common.hpp>               // ShowPowerDialog
#include <string>

// Logging shim: match the pattern used across qdesktop (UL_LOG_INFO).
#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif

// Pulled from qd_DesktopIcons.cpp translation unit.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Palette ──────────────────────────────────────────────────────────────────
static constexpr pu::ui::Color kColorEnabled  { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
static constexpr pu::ui::Color kColorDisabled { 0x60u, 0x60u, 0x60u, 0xFFu };

// Panel background: navy #0E1A33, 92% opaque.
static constexpr SDL_Color kPanelBg  { 0x0Eu, 0x1Au, 0x33u, 0xEAu };
// Hover row: slightly lighter navy, fully opaque.
static constexpr SDL_Color kHoverBg  { 0x1Eu, 0x2Eu, 0x4Eu, 0xFFu };
// Border: cyan #00E5FF.
static constexpr SDL_Color kBorderFg { 0x00u, 0xE5u, 0xFFu, 0xFFu };

// ── Layout constants ─────────────────────────────────────────────────────────
static constexpr int kRowH     = 48;   // px per row
static constexpr int kPadV     = 8;    // top/bottom padding inside panel
static constexpr int kPadH     = 16;   // left text indent
static constexpr int kPanelW   = 280;  // panel width
static constexpr int kRadius   = 8;    // v1.9.4: rounded-corner radius (= kPadV
                                       // so first/last row hover rects stay
                                       // strictly inside the curved zone)

// ── Item metadata ────────────────────────────────────────────────────────────
static const char * const kItemLabels[kDropdownItems] = {
    "Open Launchpad",   // 0
    "Settings",         // 1
    "Power",            // 2
    "Notifications",    // 3 — disabled, v1.14
    "About",            // 4
};
static const bool kItemDisabled[kDropdownItems] = {
    false,  // 0 Launchpad
    false,  // 1 Settings
    false,  // 2 Power
    true,   // 3 Notifications (not yet implemented)
    false,  // 4 About
};

// ── Helpers ──────────────────────────────────────────────────────────────────

void QdHotCornerDropdown::MakeText(SDL_Renderer *r,
                                    pu::ui::DefaultFontSize font_size,
                                    const char *text,
                                    pu::ui::Color color,
                                    SDL_Texture **out_tex,
                                    int *out_w, int *out_h) {
    (void)r;
    *out_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(font_size), std::string(text), color);
    if (*out_tex != nullptr) {
        SDL_QueryTexture(*out_tex, nullptr, nullptr, out_w, out_h);
    } else {
        *out_w = 0;
        *out_h = 0;
    }
}

void QdHotCornerDropdown::FreeTexture(SDL_Texture **tex) {
    if (*tex != nullptr) {
        pu::ui::render::DeleteTexture(*tex);
        *tex = nullptr;
    }
}

void QdHotCornerDropdown::Blit(SDL_Renderer *r, SDL_Texture *tex,
                                int x, int y, int w, int h) {
    if (tex == nullptr || r == nullptr) return;
    const SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// v1.9.4: filled rounded rectangle.
//
// Rendered as: top + bottom curved-corner scanlines (one SDL_RenderFillRect
// per row across the curve, width derived from the circle equation) plus a
// single full-width body rect between them.  For radius=8 + panel 280×256 +
// SDL hardware acceleration this is 17 fill calls; cheap.  No textures
// allocated — keeps the panel inside the B41/B42-safe zone.
//
// The renderer's draw colour and blend mode are NOT modified; callers set
// them before calling.  This lets us reuse the helper for the cyan border
// ring (paint outer rect with cyan) and the navy panel body (paint inner
// rect with kPanelBg) without re-stating draw state inside the helper.
static void FillRoundedRect(SDL_Renderer *r, SDL_Rect rect, int radius) {
    if (r == nullptr || radius <= 0 || rect.w <= 2 * radius || rect.h <= 2 * radius) {
        // Fall back to plain rect when the radius doesn't fit the bounds.
        SDL_RenderFillRect(r, &rect);
        return;
    }
    // Top + bottom curved-corner scanlines.
    for (int dy = 0; dy < radius; ++dy) {
        // dx = how far horizontally the row extends past the inner straight
        // section, derived from the circle equation x² + y² = r².
        // yc = vertical distance from the corner's circle centre, in pixels.
        const double yc = static_cast<double>(radius - 1 - dy);
        const double dx_d = (static_cast<double>(radius) * static_cast<double>(radius))
                          - (yc * yc);
        const int dx = (dx_d <= 0.0) ? 0 : static_cast<int>(__builtin_sqrt(dx_d));
        const int xx = rect.x + radius - dx;
        const int ww = (rect.w - 2 * radius) + 2 * dx;
        SDL_Rect top_line { xx, rect.y + dy,                  ww, 1 };
        SDL_Rect bot_line { xx, rect.y + rect.h - 1 - dy,     ww, 1 };
        SDL_RenderFillRect(r, &top_line);
        SDL_RenderFillRect(r, &bot_line);
    }
    // Body — full width, height between top/bottom curved regions.
    SDL_Rect body { rect.x, rect.y + radius, rect.w, rect.h - 2 * radius };
    SDL_RenderFillRect(r, &body);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

QdHotCornerDropdown::QdHotCornerDropdown() = default;

QdHotCornerDropdown::~QdHotCornerDropdown() {
    Close();
}

void QdHotCornerDropdown::Open(SDL_Renderer *r) {
    Close();  // free any previous textures first

    // v1.9.2: Anchor the panel under the hot-corner widget so the panel acts as
    // a proper drop-down — left edge aligned at x=0, top edge just below the
    // widget at y=LP_HOTCORNER_H (72).  This fixes two v1.9.1 HW-test issues:
    //   (1) panel no longer overlaps the Plutonium time/date in the top bar
    //   (2) panel no longer competes with the corner widget for taps
    panel_x_ = 0;
    panel_y_ = LP_HOTCORNER_H;
    panel_w_ = kPanelW;
    panel_h_ = kDropdownItems * kRowH + 2 * kPadV;

    // Pre-render item labels.
    for (int i = 0; i < kDropdownItems; ++i) {
        disabled_[i] = kItemDisabled[i];
        row_y_[i]    = panel_y_ + kPadV + i * kRowH;
        row_h_[i]    = kRowH;

        const pu::ui::Color col = disabled_[i] ? kColorDisabled : kColorEnabled;
        MakeText(r, pu::ui::DefaultFontSize::Medium, kItemLabels[i],
                 col, &tex_item_[i], &item_w_[i], &item_h_[i]);
    }

    hovered_ = -1;
    open_ = true;
    // v1.9.3: outside-tap close is gated until we observe a no-touch frame.
    // See qd_HotCornerDropdown.hpp for the rationale.
    armed_for_outside_close_ = false;
    // v1.9.4: fire-on-touch-release is gated by an internal touch-active
    // tracking flag so mouse hover (UpdateHover) doesn't trip it.
    was_touch_active_internal_ = false;
    // v1.9.6: reset cursor position cache so the first UpdateHover call
    // after Open() always processes (sentinel != any real cursor pos).
    prev_cursor_x_ = -1;
    prev_cursor_y_ = -1;
    UL_LOG_INFO("qdesktop: hot-corner dropdown opened");
}

void QdHotCornerDropdown::Close() {
    for (int i = 0; i < kDropdownItems; ++i) {
        FreeTexture(&tex_item_[i]);
        item_w_[i] = 0;
        item_h_[i] = 0;
    }
    hovered_ = -1;
    open_ = false;
}

void QdHotCornerDropdown::Render(SDL_Renderer *r) {
    if (!open_ || r == nullptr) return;

    const SDL_Rect outer { panel_x_, panel_y_, panel_w_, panel_h_ };
    const SDL_Rect inner { panel_x_ + 1, panel_y_ + 1,
                           panel_w_ - 2, panel_h_ - 2 };

    // v1.9.4: rounded-corner panel.  Two-pass paint so the cyan border ring
    // is the same 1 px thickness everywhere including at the curved corners:
    //   pass 1 — fill the OUTER rounded rect with cyan
    //   pass 2 — fill the INNER rounded rect (1 px smaller per side) with
    //            the navy panel background colour
    // The 1 px halo of cyan that survives at the perimeter IS the border.

    // Pass 1: cyan outer rounded rect (the border ring).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, kBorderFg.r, kBorderFg.g, kBorderFg.b, kBorderFg.a);
    FillRoundedRect(r, outer, kRadius);

    // Pass 2: navy bg rounded rect (inset 1 px so the cyan ring shows).
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, kPanelBg.r, kPanelBg.g, kPanelBg.b, kPanelBg.a);
    FillRoundedRect(r, inner, kRadius - 1);

    // Hover highlight — rectangular.  Sits between row_y_[i] and row_y_[i]+row_h_[i]
    // which is fully inside the rounded zone since kPadV == kRadius (8) means
    // the first row's top edge equals the bottom of the top curve, etc.
    if (hovered_ >= 0 && hovered_ < kDropdownItems && !disabled_[hovered_]) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kHoverBg.r, kHoverBg.g, kHoverBg.b, kHoverBg.a);
        const SDL_Rect hr { panel_x_ + 1, row_y_[hovered_],
                            panel_w_ - 2, row_h_[hovered_] };
        SDL_RenderFillRect(r, &hr);
    }

    // Item labels — vertically centred in their row.
    for (int i = 0; i < kDropdownItems; ++i) {
        if (tex_item_[i] == nullptr) continue;
        const int text_y = row_y_[i] + (kRowH - item_h_[i]) / 2;
        Blit(r, tex_item_[i],
             panel_x_ + kPadH, text_y,
             item_w_[i], item_h_[i]);
    }
}

// v1.9.4: extracted from HandleInput's touch hover path so qd_DesktopIcons
// can drive hover from the mouse cursor on no-touch frames.  No-op if !open_.
//
// v1.9.6: short-circuit when the cursor has not moved since the previous
// call.  The desktop's OnInput dispatches UpdateHover every no-touch
// frame, but a stationary cursor outside the panel would otherwise
// overwrite a D-pad-set hovered_ back to -1 on every subsequent frame,
// making D-pad navigation flicker invisibly for one frame and then
// disappear.  Skipping no-movement frames lets D-pad's hovered_ assignment
// persist across frames until the user moves the mouse.
void QdHotCornerDropdown::UpdateHover(s32 x, s32 y) {
    if (!open_) return;

    if (x == prev_cursor_x_ && y == prev_cursor_y_) {
        // Cursor hasn't moved — leave hovered_ alone so D-pad navigation
        // (which writes hovered_ from HandleInput) is not stomped.
        return;
    }
    prev_cursor_x_ = x;
    prev_cursor_y_ = y;

    hovered_ = -1;
    if (x >= panel_x_ && x < panel_x_ + panel_w_) {
        for (int i = 0; i < kDropdownItems; ++i) {
            if (y >= row_y_[i] && y < row_y_[i] + row_h_[i]) {
                hovered_ = i;
                break;
            }
        }
    }
}

// v1.9.4: ZR-driven row activation.  Same dispatch logic the touch-release
// path runs (FireAction for enabled rows, Close for disabled).  Returns
// true if (x, y) hit a row; false if outside the panel.
bool QdHotCornerDropdown::TryClickAt(s32 x, s32 y) {
    if (!open_) return false;
    if (x < panel_x_ || x >= panel_x_ + panel_w_) return false;
    for (int i = 0; i < kDropdownItems; ++i) {
        if (y >= row_y_[i] && y < row_y_[i] + row_h_[i]) {
            if (!disabled_[i]) {
                FireAction(i);
            } else {
                Close();
            }
            return true;
        }
    }
    return false;
}

void QdHotCornerDropdown::FireAction(int i) {
    UL_LOG_INFO("qdesktop: hot-corner dropdown action %d fired", i);
    Close();
    if (g_MenuApplication == nullptr) return;
    switch (i) {
        case 0:  // Open Launchpad
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Launchpad);
            break;
        case 1:  // Settings
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Settings);
            break;
        case 2:  // Power
            ::ul::menu::ui::ShowPowerDialog();
            break;
        case 3:  // Notifications — disabled; should not be reachable
            break;
        case 4:  // About
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::About);
            break;
        default:
            break;
    }
}

bool QdHotCornerDropdown::HandleInput(u64 keys_down, u64 keys_held,
                                       s32 touch_x, s32 touch_y) {
    (void)keys_held;
    if (!open_) return false;

    // B or Plus dismisses without firing an action.
    if ((keys_down & HidNpadButton_B) != 0u ||
        (keys_down & HidNpadButton_Plus) != 0u) {
        UL_LOG_INFO("qdesktop: hot-corner dropdown dismissed via button");
        Close();
        return true;
    }

    // v1.9.4: D-pad navigation while the dropdown is open.
    //   Up   — focus previous enabled row (wraps to bottom)
    //   Down — focus next enabled row (wraps to top)
    //   A    — fire the focused row's action (no-op if no focus / disabled)
    // Skips disabled rows by walking until an enabled row is found or all
    // rows have been visited.
    if ((keys_down & (HidNpadButton_Up | HidNpadButton_Down)) != 0u) {
        const int dir = (keys_down & HidNpadButton_Up) ? -1 : +1;
        int n = (hovered_ < 0) ? (dir > 0 ? -1 : kDropdownItems) : hovered_;
        for (int tries = 0; tries < kDropdownItems; ++tries) {
            n += dir;
            if (n < 0) n = kDropdownItems - 1;
            if (n >= kDropdownItems) n = 0;
            if (!disabled_[n]) {
                hovered_ = n;
                UL_LOG_INFO("qdesktop: hot-corner dropdown D-pad focus -> %d", n);
                break;
            }
        }
        return true;
    }
    if ((keys_down & HidNpadButton_A) != 0u) {
        if (hovered_ >= 0 && hovered_ < kDropdownItems && !disabled_[hovered_]) {
            const int fired = hovered_;
            UL_LOG_INFO("qdesktop: hot-corner dropdown D-pad A fires row %d", fired);
            hovered_ = -1;
            FireAction(fired);
        }
        return true;
    }

    // Touch input: track hover and fire on lift (touch_x/y == -1 means no touch).
    if (touch_x >= 0 && touch_y >= 0) {
        was_touch_active_internal_ = true;

        // Update hover state — only when the touch is inside the panel.
        hovered_ = -1;
        if (touch_x >= panel_x_ && touch_x < panel_x_ + panel_w_) {
            for (int i = 0; i < kDropdownItems; ++i) {
                if (touch_y >= row_y_[i] && touch_y < row_y_[i] + row_h_[i]) {
                    hovered_ = i;
                    break;
                }
            }
        }

        // Outside-tap close is gated on armed_for_outside_close_.  This stays
        // false until the first no-touch frame after Open(), which means the
        // finger that opened the dropdown can linger anywhere on screen
        // without firing close.  The flag flips true in the no-touch branch
        // below.
        if (armed_for_outside_close_) {
            const bool outside_panel =
                (touch_x < panel_x_ || touch_x >= panel_x_ + panel_w_ ||
                 touch_y < panel_y_ || touch_y >= panel_y_ + panel_h_);
            if (outside_panel) {
                UL_LOG_INFO("qdesktop: hot-corner dropdown dismissed via outside tap");
                Close();
            }
        }
        return true;
    }

    // No touch this frame.
    // 1. Arm the outside-close gate — the finger has lifted at least once.
    armed_for_outside_close_ = true;

    // 2. v1.9.4: fire-on-release fires ONLY when the prior frame had an
    //    active touch.  Mouse-mode hover (UpdateHover) updates hovered_
    //    continuously without setting was_touch_active_internal_, so this
    //    branch correctly skips on cursor-only frames.
    const bool just_released = was_touch_active_internal_;
    was_touch_active_internal_ = false;

    if (just_released && hovered_ >= 0 && hovered_ < kDropdownItems) {
        const int fired = hovered_;
        hovered_ = -1;
        if (!disabled_[fired]) {
            FireAction(fired);
        } else {
            // Tap on a disabled item just closes.
            Close();
        }
        return true;
    }

    return true;  // dropdown is open — consume all input even if nothing matched
}

} // namespace ul::menu::qdesktop
