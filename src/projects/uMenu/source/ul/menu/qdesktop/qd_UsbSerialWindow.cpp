// qd_UsbSerialWindow.cpp — USB Serial (CDC-ACM) panel for Q OS qdesktop.
// 480×260 translucent panel: title, state (Active/Inactive/Blocked), toggle.
// Pattern: lazy SDL_Texture cache, click-vs-drag touch state machine —
// mirrors QdPowerButtonElement exactly.

#include <ul/menu/qdesktop/qd_UsbSerialWindow.hpp>
#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Factory ────────────────────────────────────────────────────────────────────

/*static*/
QdUsbSerialWindow::Ref QdUsbSerialWindow::New(const QdTheme &theme) {
    return std::make_shared<QdUsbSerialWindow>(theme);
}

// ── Constructor / Destructor ───────────────────────────────────────────────────

QdUsbSerialWindow::QdUsbSerialWindow(const QdTheme &theme)
    : theme_(theme),
      x_(0), y_(0),
      tex_title_(nullptr),
      tex_state_active_(nullptr),
      tex_state_inactive_(nullptr),
      tex_state_blocked_(nullptr),
      tex_btn_enable_(nullptr),
      tex_btn_disable_(nullptr),
      last_state_(UsbState::Inactive),
      last_enable_failed_(false),
      btn_press_inside_(false),
      btn_down_x_(0), btn_down_y_(0)
{
    UL_LOG_INFO("qdesktop: QdUsbSerialWindow ctor");
}

QdUsbSerialWindow::~QdUsbSerialWindow() {
    FreeTextures();
}

// ── Mutators ───────────────────────────────────────────────────────────────────

void QdUsbSerialWindow::SetPos(s32 x, s32 y) {
    x_ = x;
    y_ = y;
}

// ── Texture lifecycle ──────────────────────────────────────────────────────────

void QdUsbSerialWindow::FreeTextures() {
    auto destroy = [](SDL_Texture *&t) {
        if (t != nullptr) { SDL_DestroyTexture(t); t = nullptr; }
    };
    destroy(tex_title_);
    destroy(tex_state_active_);
    destroy(tex_state_inactive_);
    destroy(tex_state_blocked_);
    destroy(tex_btn_enable_);
    destroy(tex_btn_disable_);
}

void QdUsbSerialWindow::EnsureTitleTexture() {
    if (tex_title_ != nullptr) { return; }
    tex_title_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge),
        "USB Serial (CDC-ACM)",
        theme_.text_primary);
}

void QdUsbSerialWindow::EnsureStateTextures() {
    // Build all three state textures up-front so swapping states is O(1).
    if (tex_state_active_ == nullptr) {
        // Green — button_maximize (0x4A,0xDE,0x80).
        tex_state_active_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Active",
            theme_.button_maximize);
    }
    if (tex_state_inactive_ == nullptr) {
        // Grey — text_secondary (0x88,0x88,0xAA).
        tex_state_inactive_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Inactive",
            theme_.text_secondary);
    }
    if (tex_state_blocked_ == nullptr) {
        // Red — button_close (0xF8,0x71,0x71) for the blocked/error state.
        tex_state_blocked_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Blocked (UMS busy)",
            theme_.button_close);
    }
}

void QdUsbSerialWindow::EnsureBtnTextures() {
    if (tex_btn_enable_ == nullptr) {
        tex_btn_enable_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Enable",
            theme_.accent);
    }
    if (tex_btn_disable_ == nullptr) {
        tex_btn_disable_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Disable",
            theme_.accent);
    }
}

// ── RenderButton ──────────────────────────────────────────────────────────────

void QdUsbSerialWindow::RenderButton(SDL_Renderer *r,
                                      const s32 abs_x, const s32 abs_y,
                                      const UsbState st)
{
    const s32 bx = abs_x + BTN_X_OFF;
    const s32 by = abs_y + ROW_BTN_Y;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const pu::ui::Color &bg = theme_.surface_glass;
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 0xE6u);
    SDL_Rect btn_rect { bx, by, BTN_W, BTN_H };
    SDL_RenderFillRect(r, &btn_rect);

    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_RenderDrawRect(r, &btn_rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Active → show "Disable"; anything else → "Enable".
    SDL_Texture *lbl = (st == UsbState::Active) ? tex_btn_disable_ : tex_btn_enable_;
    if (lbl != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(lbl, nullptr, nullptr, &lw, &lh);
        const s32 lx = bx + (BTN_W - lw) / 2;
        const s32 ly = by + (BTN_H - lh) / 2;
        const SDL_Rect dst { lx, ly, lw, lh };
        SDL_RenderCopy(r, lbl, nullptr, &dst);
    }
}

// ── OnRender ───────────────────────────────────────────────────────────────────
//
// Render order:
//   1. Background panel — translucent dark at 0xCC alpha.
//   2. Border — 1px accent ring.
//   3. Title row  — "USB Serial (CDC-ACM)".
//   4. State row  — "Active" / "Inactive" / "Blocked (UMS busy)".
//   5. Toggle button.

void QdUsbSerialWindow::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                  const s32 origin_x, const s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) { return; }

    const s32 abs_x = x_ + origin_x;
    const s32 abs_y = y_ + origin_y;

    // Compute the current display state.
    UsbState cur_state;
    if (dev::IsUsbSerialActive()) {
        cur_state = UsbState::Active;
    } else if (last_enable_failed_) {
        cur_state = UsbState::Blocked;
    } else {
        cur_state = UsbState::Inactive;
    }

    // Invalidate button label textures when state crosses Active/non-Active boundary.
    if (cur_state != last_state_) {
        auto destroy = [](SDL_Texture *&t) {
            if (t != nullptr) { SDL_DestroyTexture(t); t = nullptr; }
        };
        // State textures are all three built at once; only blow them if not yet built.
        destroy(tex_btn_enable_);
        destroy(tex_btn_disable_);
        last_state_ = cur_state;
    }

    // ── 1. Background panel ────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const pu::ui::Color &dbg = theme_.desktop_bg;
    SDL_SetRenderDrawColor(r, dbg.r, dbg.g, dbg.b, 0xCCu);
    SDL_Rect panel { abs_x, abs_y, PANEL_W, PANEL_H };
    SDL_RenderFillRect(r, &panel);

    // ── 2. Border (1px accent) ─────────────────────────────────────────────────
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0x99u);
    SDL_RenderDrawRect(r, &panel);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Lazy-build textures ────────────────────────────────────────────────────
    EnsureTitleTexture();
    EnsureStateTextures();
    EnsureBtnTextures();

    auto blit = [&](SDL_Texture *tex, s32 row_y) {
        if (tex == nullptr) { return; }
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        const s32 max_w = PANEL_W - ROW_TEXT_X * 2;
        const s32 cw    = std::min(tw, max_w);
        const SDL_Rect src { 0, 0, cw, th };
        const SDL_Rect dst { abs_x + ROW_TEXT_X, abs_y + row_y, cw, th };
        SDL_RenderCopy(r, tex, &src, &dst);
    };

    // ── 3. Title ───────────────────────────────────────────────────────────────
    blit(tex_title_, ROW_TITLE_Y);

    // ── 4. State row ───────────────────────────────────────────────────────────
    SDL_Texture *state_tex = nullptr;
    switch (cur_state) {
        case UsbState::Active:   state_tex = tex_state_active_;   break;
        case UsbState::Blocked:  state_tex = tex_state_blocked_;  break;
        case UsbState::Inactive: state_tex = tex_state_inactive_; break;
    }
    blit(state_tex, ROW_STATE_Y);

    // ── 5. Toggle button ───────────────────────────────────────────────────────
    RenderButton(r, abs_x, abs_y, cur_state);
}

// ── OnInput ────────────────────────────────────────────────────────────────────

static constexpr s32 USB_CLICK_TOL_PX = 20;

void QdUsbSerialWindow::OnInput(const u64 keys_down,
                                 const u64 /*keys_up*/,
                                 const u64 /*keys_held*/,
                                 const pu::ui::TouchPoint touch_pos)
{
    if (keys_down & HidNpadButton_A) {
        DoToggle();
        return;
    }

    const s32 bx = x_ + BTN_X_OFF;
    const s32 by = y_ + ROW_BTN_Y;

    const bool is_active = (touch_pos.x != 0 || touch_pos.y != 0);

    if (is_active) {
        const bool inside = (touch_pos.x >= bx && touch_pos.x < bx + BTN_W &&
                             touch_pos.y >= by && touch_pos.y < by + BTN_H);

        if (!btn_press_inside_) {
            if (inside) {
                btn_press_inside_ = true;
                btn_down_x_ = touch_pos.x;
                btn_down_y_ = touch_pos.y;
            }
        } else {
            const s32 dx = touch_pos.x - btn_down_x_;
            const s32 dy = touch_pos.y - btn_down_y_;
            const s32 dist_sq = dx * dx + dy * dy;
            const s32 tol_sq  = USB_CLICK_TOL_PX * USB_CLICK_TOL_PX;
            if (!inside || dist_sq > tol_sq) {
                btn_press_inside_ = false;
            }
        }
    } else {
        if (btn_press_inside_) {
            btn_press_inside_ = false;
            DoToggle();
        }
    }
}

// ── DoToggle ──────────────────────────────────────────────────────────────────

void QdUsbSerialWindow::DoToggle() {
    if (dev::IsUsbSerialActive()) {
        dev::DisableUsbSerial();
        last_enable_failed_ = false;
        UL_LOG_INFO("qdesktop: UsbSerialWindow — USB serial disabled via UI");
    } else {
        const bool ok = dev::TryEnableUsbSerial();
        last_enable_failed_ = !ok;
        UL_LOG_INFO("qdesktop: UsbSerialWindow — TryEnableUsbSerial returned %d",
                    static_cast<int>(ok));
    }
}

} // namespace ul::menu::qdesktop
