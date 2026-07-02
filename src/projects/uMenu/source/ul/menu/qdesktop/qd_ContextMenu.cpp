// qd_ContextMenu.cpp — ZL context-menu overlay (uMenu v1.10.3+, Z2.0 submenu support).
//
// Panel geometry:
//   Row height : kRowH (48 px)
//   Pad V      : kPadV (8 px) top + bottom inside border
//   Pad H      : kPadH (16 px) left text indent
//   Panel width: kPanelW (280 px) — matches QdHotCornerDropdown
//   Radius     : kRadius (8 px)
//
// Submenu (Z2.0):
//   - Submenu panel sits at parent_right + kSubmenuGap (4 px); flips to
//     parent_left - kSubmenuGap - kPanelW if no room on the right.
//   - y aligns with parent row's y, clamped to keep on-screen.
//   - Same kRowH / kPadV / kPanelW / kRadius as the parent panel.
//
// Colours read from g_QdTheme for theme-pack switching.

#include <ul/menu/qdesktop/qd_ContextMenu.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_Audio.hpp>

#include <cmath>

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

// ── Layout + palette constants ────────────────────────────────────────────────

static constexpr int kRowH        = 48;
static constexpr int kPadV        = 8;
static constexpr int kPadH        = 16;
static constexpr int kPanelW      = 280;
static constexpr int kRadius      = 8;
static constexpr int kSubmenuGap  = 4;     // px between parent and submenu panels
static constexpr const char* kChevronGlyph = "\xE2\x96\xB8";  // ▸ U+25B8 (UTF-8)

#define kPanelBg  SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.surface_glass.r,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.g,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.b,     0xEAu }
#define kHoverBg  SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.r, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.g, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.b, 0xFFu }
#define kBorderFg SDL_Color{ ::ul::menu::qdesktop::g_QdTheme.accent.r,            ::ul::menu::qdesktop::g_QdTheme.accent.g,            ::ul::menu::qdesktop::g_QdTheme.accent.b,            0xFFu }
static constexpr pu::ui::Color kColorEnabled { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

// ── Helpers ───────────────────────────────────────────────────────────────────

void QdContextMenu::FillRoundedRect(SDL_Renderer *r, SDL_Rect rect, int radius) {
    if (r == nullptr || radius <= 0
            || rect.w <= 2 * radius || rect.h <= 2 * radius) {
        SDL_RenderFillRect(r, &rect);
        return;
    }
    for (int dy = 0; dy < radius; ++dy) {
        const double yc  = static_cast<double>(radius - 1 - dy);
        const double dx_d = (static_cast<double>(radius) * static_cast<double>(radius))
                           - (yc * yc);
        const int dx = (dx_d <= 0.0) ? 0 : static_cast<int>(sqrt(dx_d));
        const int xx = rect.x + radius - dx;
        const int ww = (rect.w - 2 * radius) + 2 * dx;
        SDL_Rect top_line { xx, rect.y + dy,              ww, 1 };
        SDL_Rect bot_line { xx, rect.y + rect.h - 1 - dy, ww, 1 };
        SDL_RenderFillRect(r, &top_line);
        SDL_RenderFillRect(r, &bot_line);
    }
    SDL_Rect body { rect.x, rect.y + radius, rect.w, rect.h - 2 * radius };
    SDL_RenderFillRect(r, &body);
}

void QdContextMenu::MakeText(SDL_Renderer *r,
                              const char *text,
                              pu::ui::Color color,
                              SDL_Texture **out_tex,
                              int *out_w, int *out_h) {
    (void)r;
    *out_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        std::string(text), color);
    if (*out_tex != nullptr) {
        SDL_QueryTexture(*out_tex, nullptr, nullptr, out_w, out_h);
    } else {
        *out_w = 0;
        *out_h = 0;
    }
}

void QdContextMenu::FreeTexture(SDL_Texture **tex) {
    if (*tex != nullptr) {
        pu::ui::render::DeleteTexture(*tex);
        *tex = nullptr;
    }
}

void QdContextMenu::Blit(SDL_Renderer *r, SDL_Texture *tex,
                          int x, int y, int w, int h) {
    if (tex == nullptr || r == nullptr) return;
    const SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

QdContextMenu::QdContextMenu() = default;

QdContextMenu::~QdContextMenu() {
    Close();
}

// Legacy string-vector overload — delegates to the MenuItem overload so
// existing call sites keep working without any change.
void QdContextMenu::Open(SDL_Renderer *r,
                         const std::vector<std::string> &items,
                         s32 anchor_x, s32 anchor_y) {
    std::vector<QdContextMenuItem> mi;
    mi.reserve(items.size());
    for (const auto &s : items) {
        mi.push_back(QdContextMenuItem{ s, {}, false });
    }
    Open(r, mi, anchor_x, anchor_y);
}

void QdContextMenu::Open(SDL_Renderer *r,
                         const std::vector<QdContextMenuItem> &items,
                         s32 anchor_x, s32 anchor_y) {
    Close();

    item_count_ = static_cast<int>(items.size());
    if (item_count_ > kMaxItems) item_count_ = kMaxItems;
    if (item_count_ <= 0) return;

    items_.assign(items.begin(), items.begin() + item_count_);

    panel_w_ = kPanelW;
    panel_h_ = item_count_ * kRowH + 2 * kPadV;

    // Clamp anchor to keep the panel on-screen.
    panel_x_ = static_cast<int>(anchor_x);
    panel_y_ = static_cast<int>(anchor_y);
    if (panel_x_ + panel_w_ > static_cast<int>(SCREEN_W)) {
        panel_x_ = static_cast<int>(SCREEN_W) - panel_w_;
    }
    if (panel_x_ < 0) panel_x_ = 0;
    if (panel_y_ + panel_h_ > static_cast<int>(SCREEN_H)) {
        panel_y_ = static_cast<int>(SCREEN_H) - panel_h_;
    }
    if (panel_y_ < 0) panel_y_ = 0;

    for (int i = 0; i < item_count_; ++i) {
        row_y_[i] = panel_y_ + kPadV + i * kRowH;
        MakeText(r, items_[i].label.c_str(),
                 kColorEnabled,
                 &tex_item_[i], &item_tex_w_[i], &item_tex_h_[i]);

        // Pre-render chevron texture for rows with submenus.
        if (!items_[i].submenu_items.empty()) {
            MakeText(r, kChevronGlyph, kColorEnabled,
                     &tex_chevron_[i],
                     &chevron_tex_w_[i], &chevron_tex_h_[i]);
        }
        // QoL-D2 — pre-render keybind hint texture if specified.  Rendered
        // in a slightly dimmer color so the primary label dominates.
        if (!items_[i].key_hint.empty()) {
            static constexpr pu::ui::Color kHintColor{ 0xC0u, 0xC0u, 0xC0u, 0xFFu };
            MakeText(r, items_[i].key_hint.c_str(), kHintColor,
                     &tex_hint_[i],
                     &hint_tex_w_[i], &hint_tex_h_[i]);
        }
    }

    hovered_                   = 0;
    selected_parent_index_     = -1;
    selected_sub_index_        = -1;
    was_touch_active_internal_ = false;
    armed_for_outside_close_   = false;
    skip_first_lift_           = false;   // QoL-T1 v2 — fresh slate each Open
    submenu_open_              = false;
    submenu_parent_index_      = -1;
    submenu_item_count_        = 0;
    submenu_hovered_           = 0;
    open_                      = true;
    QdAudio::Play(DesktopSfxEvent::CtxMenuOpen);
}

void QdContextMenu::Close() {
    const bool was_open = open_;
    CloseSubmenu();
    for (int i = 0; i < kMaxItems; ++i) {
        FreeTexture(&tex_item_[i]);
        FreeTexture(&tex_chevron_[i]);
        FreeTexture(&tex_hint_[i]);
        chevron_tex_w_[i] = 0;
        chevron_tex_h_[i] = 0;
        hint_tex_w_[i]    = 0;
        hint_tex_h_[i]    = 0;
    }
    items_.clear();
    open_       = false;
    item_count_ = 0;
    if (was_open) {
        QdAudio::Play(DesktopSfxEvent::CtxMenuClose);
    }
}

// QoL-T1 v2 / BUG-7 — mark this open instance as needing first-lift skip.
// The fire-on-release path will consume the next touch lift (the one that
// ends the long-press finger) WITHOUT firing a row — menu stays open for
// a subsequent tap-confirm.  Mirrors the "must keep finger pressed"
// hardening applied to the hot-corner dropdowns.
void QdContextMenu::SetSkipFirstLift() {
    skip_first_lift_           = true;
    // Mark touch as having been active so the fire-on-release branch is
    // reached on the next no-touch frame (where the skip is then consumed).
    was_touch_active_internal_ = true;
}

// ── Submenu open/close ────────────────────────────────────────────────────────

void QdContextMenu::OpenSubmenu(SDL_Renderer *r, int parent_index) {
    if (parent_index < 0 || parent_index >= item_count_) return;
    if (items_[parent_index].submenu_items.empty()) return;

    CloseSubmenu();   // replace any existing open submenu

    submenu_parent_index_ = parent_index;
    submenu_item_count_   = static_cast<int>(items_[parent_index].submenu_items.size());
    if (submenu_item_count_ > kMaxSubItems) submenu_item_count_ = kMaxSubItems;
    if (submenu_item_count_ <= 0) return;

    submenu_panel_w_ = kPanelW;
    submenu_panel_h_ = submenu_item_count_ * kRowH + 2 * kPadV;

    // Default: open to the right of the parent panel.  Flip left if no room.
    const int right_anchor = panel_x_ + panel_w_ + kSubmenuGap;
    const int left_anchor  = panel_x_ - kSubmenuGap - submenu_panel_w_;
    if (right_anchor + submenu_panel_w_ <= static_cast<int>(SCREEN_W)) {
        submenu_panel_x_ = right_anchor;
    } else if (left_anchor >= 0) {
        submenu_panel_x_ = left_anchor;
    } else {
        // Neither side fits — clamp to right edge.
        submenu_panel_x_ = static_cast<int>(SCREEN_W) - submenu_panel_w_;
        if (submenu_panel_x_ < 0) submenu_panel_x_ = 0;
    }

    // y aligns with parent row; clamp to keep on screen.
    submenu_panel_y_ = row_y_[parent_index];
    if (submenu_panel_y_ + submenu_panel_h_ > static_cast<int>(SCREEN_H)) {
        submenu_panel_y_ = static_cast<int>(SCREEN_H) - submenu_panel_h_;
    }
    if (submenu_panel_y_ < 0) submenu_panel_y_ = 0;

    for (int i = 0; i < submenu_item_count_; ++i) {
        submenu_row_y_[i] = submenu_panel_y_ + kPadV + i * kRowH;
        MakeText(r,
                 items_[parent_index].submenu_items[static_cast<size_t>(i)].c_str(),
                 kColorEnabled,
                 &submenu_tex_item_[i],
                 &submenu_item_tex_w_[i],
                 &submenu_item_tex_h_[i]);
    }

    submenu_hovered_ = 0;
    submenu_open_    = true;
    UL_LOG_INFO("qdesktop: context-menu submenu opened (parent=%d, items=%d)",
                parent_index, submenu_item_count_);
}

void QdContextMenu::CloseSubmenu() {
    for (int i = 0; i < kMaxSubItems; ++i) {
        FreeTexture(&submenu_tex_item_[i]);
        submenu_item_tex_w_[i] = 0;
        submenu_item_tex_h_[i] = 0;
    }
    submenu_open_         = false;
    submenu_parent_index_ = -1;
    submenu_item_count_   = 0;
    submenu_hovered_      = 0;
}

// ── Render ────────────────────────────────────────────────────────────────────

void QdContextMenu::Render(SDL_Renderer *r) const {
    if (!open_ || r == nullptr) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // QoL-V1 — soft drop-shadow.  Two passes at different offsets/alphas
    // for a subtle layered depth cue.  Drawn before the border so it sits
    // beneath the panel chrome.  Pure-black shadow at low alpha; the blend
    // mode is already BLEND from above.
    {
        const SDL_Rect shadow_far  { panel_x_ - 1 + 6, panel_y_ - 1 + 8,
                                      panel_w_ + 2, panel_h_ + 2 };
        SDL_SetRenderDrawColor(r, 0x00, 0x00, 0x00, 0x40);
        FillRoundedRect(r, shadow_far, kRadius + 2);
        const SDL_Rect shadow_near { panel_x_ - 1 + 3, panel_y_ - 1 + 4,
                                      panel_w_ + 2, panel_h_ + 2 };
        SDL_SetRenderDrawColor(r, 0x00, 0x00, 0x00, 0x60);
        FillRoundedRect(r, shadow_near, kRadius + 1);
    }

    // 1-px cyan border ring (outer rect).
    const SDL_Rect outer { panel_x_ - 1, panel_y_ - 1,
                            panel_w_ + 2, panel_h_ + 2 };
    SDL_SetRenderDrawColor(r,
        kBorderFg.r, kBorderFg.g, kBorderFg.b, kBorderFg.a);
    FillRoundedRect(r, outer, kRadius + 1);

    // Navy panel body.
    const SDL_Rect body_rect { panel_x_, panel_y_, panel_w_, panel_h_ };
    SDL_SetRenderDrawColor(r,
        kPanelBg.r, kPanelBg.g, kPanelBg.b, kPanelBg.a);
    FillRoundedRect(r, body_rect, kRadius);

    // Hover highlight for hovered row (top-level).  Skip if submenu open
    // and hovered row is the parent — the highlight stays on the row that
    // opened the submenu to anchor the eye.
    if (hovered_ >= 0 && hovered_ < item_count_) {
        const SDL_Rect hover_rect { panel_x_, row_y_[hovered_],
                                    panel_w_,  kRowH };
        SDL_SetRenderDrawColor(r,
            kHoverBg.r, kHoverBg.g, kHoverBg.b, kHoverBg.a);
        SDL_RenderFillRect(r, &hover_rect);
    }

    // Item labels.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (int i = 0; i < item_count_; ++i) {
        if (tex_item_[i] != nullptr) {
            const int ty = row_y_[i] + (kRowH - item_tex_h_[i]) / 2;
            Blit(r, tex_item_[i],
                 panel_x_ + kPadH, ty,
                 item_tex_w_[i], item_tex_h_[i]);
        }
        // Chevron for submenu rows — right-aligned.
        // QoL-D2: when a keybind hint is also present, the chevron sits
        // at the rightmost edge and the hint floats just to its left.
        int right_x = panel_x_ + panel_w_ - kPadH;
        if (tex_chevron_[i] != nullptr) {
            const int cx = right_x - chevron_tex_w_[i];
            const int cy = row_y_[i] + (kRowH - chevron_tex_h_[i]) / 2;
            Blit(r, tex_chevron_[i],
                 cx, cy,
                 chevron_tex_w_[i], chevron_tex_h_[i]);
            right_x = cx - 6;   // leave a small gap before any hint
        }
        if (tex_hint_[i] != nullptr) {
            const int hx = right_x - hint_tex_w_[i];
            const int hy = row_y_[i] + (kRowH - hint_tex_h_[i]) / 2;
            Blit(r, tex_hint_[i], hx, hy, hint_tex_w_[i], hint_tex_h_[i]);
        }
    }

    // ── Submenu panel (renders on top of parent) ─────────────────────────────
    if (submenu_open_) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        const SDL_Rect sub_outer { submenu_panel_x_ - 1, submenu_panel_y_ - 1,
                                    submenu_panel_w_ + 2, submenu_panel_h_ + 2 };
        SDL_SetRenderDrawColor(r,
            kBorderFg.r, kBorderFg.g, kBorderFg.b, kBorderFg.a);
        FillRoundedRect(r, sub_outer, kRadius + 1);

        const SDL_Rect sub_body { submenu_panel_x_, submenu_panel_y_,
                                   submenu_panel_w_, submenu_panel_h_ };
        SDL_SetRenderDrawColor(r,
            kPanelBg.r, kPanelBg.g, kPanelBg.b, kPanelBg.a);
        FillRoundedRect(r, sub_body, kRadius);

        if (submenu_hovered_ >= 0 && submenu_hovered_ < submenu_item_count_) {
            const SDL_Rect sub_hover {
                submenu_panel_x_, submenu_row_y_[submenu_hovered_],
                submenu_panel_w_, kRowH };
            SDL_SetRenderDrawColor(r,
                kHoverBg.r, kHoverBg.g, kHoverBg.b, kHoverBg.a);
            SDL_RenderFillRect(r, &sub_hover);
        }

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        for (int i = 0; i < submenu_item_count_; ++i) {
            if (submenu_tex_item_[i] == nullptr) continue;
            const int ty = submenu_row_y_[i] + (kRowH - submenu_item_tex_h_[i]) / 2;
            Blit(r, submenu_tex_item_[i],
                 submenu_panel_x_ + kPadH, ty,
                 submenu_item_tex_w_[i], submenu_item_tex_h_[i]);
        }
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool QdContextMenu::HandleInput(u64 keys_down, u64 keys_up,
                                 s32 cx, s32 cy,
                                 s32 touch_x, s32 touch_y) {
    if (!open_) return false;

    (void)keys_up;

    const bool touch_active = (touch_x >= 0 && touch_y >= 0);

    // Arming: first no-touch frame after Open() so the opening press
    // doesn't immediately trigger outside-close on the same frame.
    if (!armed_for_outside_close_ && !touch_active) {
        armed_for_outside_close_ = true;
    }

    // ── Touch lift handling (fire-on-release) ────────────────────────────────
    if (armed_for_outside_close_ && was_touch_active_internal_ && !touch_active) {
        // QoL-T1 v2 / BUG-7 — if the menu opened while a touch was active
        // (long-press path), the FIRST lift detected here is the lift that
        // released the opening finger.  Consume it without action; menu
        // stays open for a separate tap-confirm.  Subsequent lifts fall
        // through to normal fire-on-release.
        if (skip_first_lift_) {
            skip_first_lift_           = false;
            was_touch_active_internal_ = false;
            UL_LOG_INFO("qdesktop: context-menu first-lift skipped (long-press hold-open)");
            return true;
        }
        // Use cx/cy as last known touch position (touch_x/y is -1 on lift frame).
        const bool inside_parent =
            (cx >= panel_x_ && cx < panel_x_ + panel_w_ &&
             cy >= panel_y_ && cy < panel_y_ + panel_h_);
        const bool inside_submenu =
            submenu_open_ &&
            (cx >= submenu_panel_x_ && cx < submenu_panel_x_ + submenu_panel_w_ &&
             cy >= submenu_panel_y_ && cy < submenu_panel_y_ + submenu_panel_h_);

        if (!inside_parent && !inside_submenu) {
            // Outside both panels: cancel everything.
            selected_parent_index_ = -1;
            selected_sub_index_    = -1;
            UL_LOG_INFO("qdesktop: context-menu outside-touch close");
            was_touch_active_internal_ = false;
            Close();
            return true;
        }

        if (submenu_open_) {
            if (inside_submenu) {
                // Confirm submenu row.
                for (int i = 0; i < submenu_item_count_; ++i) {
                    if (cy >= submenu_row_y_[i] && cy < submenu_row_y_[i] + kRowH) {
                        selected_parent_index_ = submenu_parent_index_;
                        selected_sub_index_    = i;
                        UL_LOG_INFO("qdesktop: context-menu submenu touch-confirm parent=%d sub=%d",
                                    selected_parent_index_, selected_sub_index_);
                        was_touch_active_internal_ = false;
                        Close();
                        return true;
                    }
                }
                // Inside submenu but no row — ignore lift.
                was_touch_active_internal_ = false;
                return true;
            }
            // inside_parent (with submenu open): close submenu only.
            UL_LOG_INFO("qdesktop: context-menu tap-on-parent closes submenu");
            CloseSubmenu();
            was_touch_active_internal_ = false;
            return true;
        }

        // No submenu open — top-level dispatch.
        for (int i = 0; i < item_count_; ++i) {
            if (cy >= row_y_[i] && cy < row_y_[i] + kRowH) {
                if (!items_.empty() && !items_[i].submenu_items.empty()) {
                    // Row has submenu: open it.
                    hovered_ = i;
                    OpenSubmenu(pu::ui::render::GetMainRenderer(), i);
                    was_touch_active_internal_ = false;
                    return true;
                }
                selected_parent_index_ = i;
                selected_sub_index_    = -1;
                UL_LOG_INFO("qdesktop: context-menu touch-confirm idx=%d", selected_parent_index_);
                was_touch_active_internal_ = false;
                Close();
                return true;
            }
        }
        // Inside parent but no row — cancel.
        selected_parent_index_ = -1;
        selected_sub_index_    = -1;
        was_touch_active_internal_ = false;
        Close();
        return true;
    }

    // Update touch latch for next frame.
    was_touch_active_internal_ = touch_active;

    // ── Mouse-mode hover update ──────────────────────────────────────────────
    if (submenu_open_) {
        if (cx >= submenu_panel_x_ && cx < submenu_panel_x_ + submenu_panel_w_) {
            for (int i = 0; i < submenu_item_count_; ++i) {
                if (cy >= submenu_row_y_[i] && cy < submenu_row_y_[i] + kRowH) {
                    submenu_hovered_ = i;
                    break;
                }
            }
        }
    } else {
        if (cx >= panel_x_ && cx < panel_x_ + panel_w_) {
            for (int i = 0; i < item_count_; ++i) {
                if (cy >= row_y_[i] && cy < row_y_[i] + kRowH) {
                    hovered_ = i;
                    break;
                }
            }
        }
    }

    // ── D-pad navigation ─────────────────────────────────────────────────────
    if (submenu_open_) {
        if (keys_down & HidNpadButton_Up) {
            if (submenu_hovered_ > 0) --submenu_hovered_;
            return true;
        }
        if (keys_down & HidNpadButton_Down) {
            if (submenu_hovered_ < submenu_item_count_ - 1) ++submenu_hovered_;
            return true;
        }
        if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
            selected_parent_index_ = submenu_parent_index_;
            selected_sub_index_    = submenu_hovered_;
            UL_LOG_INFO("qdesktop: context-menu submenu confirm parent=%d sub=%d",
                        selected_parent_index_, selected_sub_index_);
            Close();
            return true;
        }
        if ((keys_down & HidNpadButton_B) ||
            (keys_down & HidNpadButton_ZL) ||
            (keys_down & HidNpadButton_Left)) {
            UL_LOG_INFO("qdesktop: context-menu submenu cancelled (back to parent)");
            CloseSubmenu();
            return true;
        }
        return true;   // submenu open — consume all input
    }

    // Top-level d-pad.
    if (keys_down & HidNpadButton_Up) {
        if (hovered_ > 0) --hovered_;
        return true;
    }
    if (keys_down & HidNpadButton_Down) {
        if (hovered_ < item_count_ - 1) ++hovered_;
        return true;
    }

    if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
        if (!items_.empty() && hovered_ >= 0 && hovered_ < item_count_
                && !items_[hovered_].submenu_items.empty()) {
            // Activate submenu instead of confirming.
            OpenSubmenu(pu::ui::render::GetMainRenderer(), hovered_);
            return true;
        }
        selected_parent_index_ = hovered_;
        selected_sub_index_    = -1;
        UL_LOG_INFO("qdesktop: context-menu confirm idx=%d", selected_parent_index_);
        Close();
        return true;
    }

    if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_ZL)) {
        selected_parent_index_ = -1;
        selected_sub_index_    = -1;
        UL_LOG_INFO("qdesktop: context-menu cancelled");
        Close();
        return true;
    }

    return true;  // open_ — consume all input while menu is visible
}

} // namespace ul::menu::qdesktop
