// qd_NxlinkWindow.cpp — NXLink session panel for Q OS qdesktop.
// 480×260 translucent panel showing nxlink state + toggle button.
// Pattern: lazy SDL_Texture cache, click-vs-drag touch state machine —
// mirrors QdPowerButtonElement exactly.

#include <ul/menu/qdesktop/qd_NxlinkWindow.hpp>
#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Factory ────────────────────────────────────────────────────────────────────

/*static*/
QdNxlinkWindow::Ref QdNxlinkWindow::New(const QdTheme &theme) {
    return std::make_shared<QdNxlinkWindow>(theme);
}

// ── Constructor / Destructor ───────────────────────────────────────────────────

QdNxlinkWindow::QdNxlinkWindow(const QdTheme &theme)
    : theme_(theme),
      x_(0), y_(0),
      tex_title_(nullptr),
      tex_state_active_(nullptr),
      tex_state_inactive_(nullptr),
      tex_host_(nullptr),
      tex_btn_enable_(nullptr),
      tex_btn_disable_(nullptr),
      last_active_(false),
      btn_press_inside_(false),
      btn_down_x_(0), btn_down_y_(0)
{
    UL_LOG_INFO("qdesktop: QdNxlinkWindow ctor");
}

QdNxlinkWindow::~QdNxlinkWindow() {
    FreeTextures();
}

// ── Mutators ───────────────────────────────────────────────────────────────────

void QdNxlinkWindow::SetPos(s32 x, s32 y) {
    x_ = x;
    y_ = y;
}

// ── Texture lifecycle ──────────────────────────────────────────────────────────

void QdNxlinkWindow::FreeTextures() {
    auto destroy = [](SDL_Texture *&t) {
        pu::ui::render::DeleteTexture(t);
    };
    destroy(tex_title_);
    destroy(tex_state_active_);
    destroy(tex_state_inactive_);
    destroy(tex_host_);
    destroy(tex_btn_enable_);
    destroy(tex_btn_disable_);
}

void QdNxlinkWindow::EnsureTitleTexture() {
    if (tex_title_ != nullptr) { return; }
    tex_title_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge),
        "NXLink Session",
        theme_.text_primary);
}

void QdNxlinkWindow::EnsureStateTexture(bool active) {
    if (active) {
        if (tex_state_active_ != nullptr) { return; }
        // Green = button_maximize from QdTheme (0x4A,0xDE,0x80).
        tex_state_active_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Active",
            theme_.button_maximize);
    } else {
        if (tex_state_inactive_ != nullptr) { return; }
        // Grey = text_secondary (0x88,0x88,0xAA).
        tex_state_inactive_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Inactive",
            theme_.text_secondary);
    }
}

void QdNxlinkWindow::EnsureHostTexture(bool active) {
    if (tex_host_ != nullptr) { return; }
    if (!active) {
        // Host row is invisible when inactive — keep nullptr.
        return;
    }
    // nxlink redirects stdout over the socket; there is no public
    // getpeername helper exposed from qd_DevTools, so we display the
    // canonical description that is always true when nxlink is active.
    tex_host_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        "stdout redirected",
        theme_.text_secondary,
        static_cast<u32>(PANEL_W - ROW_TEXT_X * 2));
}

void QdNxlinkWindow::EnsureBtnTexture(bool active) {
    if (active) {
        if (tex_btn_disable_ != nullptr) { return; }
        // Accent cyan — matches QdTheme ACCENT (0x7D,0xD3,0xFC).
        tex_btn_disable_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Disable",
            theme_.accent);
    } else {
        if (tex_btn_enable_ != nullptr) { return; }
        tex_btn_enable_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            "Enable",
            theme_.accent);
    }
}

// ── RenderButton ──────────────────────────────────────────────────────────────
// Draws the toggle button rect + label at (abs_x + BTN_X_OFF, abs_y + ROW_BTN_Y).

void QdNxlinkWindow::RenderButton(SDL_Renderer *r,
                                   const s32 abs_x, const s32 abs_y,
                                   const bool active)
{
    const s32 bx = abs_x + BTN_X_OFF;
    const s32 by = abs_y + ROW_BTN_Y;

    // Background — surface_glass (0x12,0x12,0x2A) at 90% alpha.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const pu::ui::Color &bg = theme_.surface_glass;
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 0xE6u);
    SDL_Rect btn_rect { bx, by, BTN_W, BTN_H };
    SDL_RenderFillRect(r, &btn_rect);

    // Border — accent colour at full opacity.
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_RenderDrawRect(r, &btn_rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Label — centred inside the button rect.
    SDL_Texture *lbl = active ? tex_btn_disable_ : tex_btn_enable_;
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
//   1. Background panel — translucent dark (0x0A,0x0A,0x14 at 0xCC alpha).
//   2. Border — 1px accent ring.
//   3. Title row      — "NXLink Session".
//   4. State row      — "Active" (green) or "Inactive" (grey).
//   5. Host info row  — "stdout redirected" when active, blank when inactive.
//   6. Toggle button  — "Enable" or "Disable" centred in a bordered rect.

void QdNxlinkWindow::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                               const s32 origin_x, const s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) { return; }

    const s32 abs_x = x_ + origin_x;
    const s32 abs_y = y_ + origin_y;

    const bool active = dev::IsNxlinkActive();

    // Invalidate state-sensitive textures when nxlink state has changed.
    if (active != last_active_) {
        auto destroy = [](SDL_Texture *&t) {
            pu::ui::render::DeleteTexture(t);
        };
        destroy(tex_state_active_);
        destroy(tex_state_inactive_);
        destroy(tex_host_);
        destroy(tex_btn_enable_);
        destroy(tex_btn_disable_);
        last_active_ = active;
    }

    // ── 1. Background panel ────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const pu::ui::Color &dbg = theme_.desktop_bg;
    SDL_SetRenderDrawColor(r, dbg.r, dbg.g, dbg.b, 0xCCu); // 80% alpha
    SDL_Rect panel { abs_x, abs_y, PANEL_W, PANEL_H };
    SDL_RenderFillRect(r, &panel);

    // ── 2. Border (1px accent) ─────────────────────────────────────────────────
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0x99u);
    SDL_RenderDrawRect(r, &panel);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Lazy-build all textures for the current state ──────────────────────────
    EnsureTitleTexture();
    EnsureStateTexture(active);
    EnsureHostTexture(active);
    EnsureBtnTexture(active);

    // ── Helper: blit a texture left-aligned at (abs_x + ROW_TEXT_X, abs_y + row_y).
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
    blit(active ? tex_state_active_ : tex_state_inactive_, ROW_STATE_Y);

    // ── 5. Host info row (active only) ────────────────────────────────────────
    if (active) {
        blit(tex_host_, ROW_HOST_Y);
    }

    // ── 6. Toggle button ───────────────────────────────────────────────────────
    RenderButton(r, abs_x, abs_y, active);
}

// ── OnInput ────────────────────────────────────────────────────────────────────
//
// Touch: click-vs-drag state machine (mirrors QdPowerButtonElement).
//   TouchDown inside button rect → set btn_press_inside_, record down coords.
//   TouchMove outside rect or beyond tolerance → clear btn_press_inside_.
//   TouchUp with btn_press_inside_ true → DoToggle().
// A-button → DoToggle() (controller support).

static constexpr s32 NXLINK_CLICK_TOL_PX = 20;

void QdNxlinkWindow::OnInput(const u64 keys_down,
                              const u64 /*keys_up*/,
                              const u64 /*keys_held*/,
                              const pu::ui::TouchPoint touch_pos)
{
    // A-button toggle.
    if (keys_down & HidNpadButton_A) {
        DoToggle();
        return;
    }

    // Compute button absolute rect (x_/y_ already include panel position).
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
            const s32 tol_sq  = NXLINK_CLICK_TOL_PX * NXLINK_CLICK_TOL_PX;
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

void QdNxlinkWindow::DoToggle() {
    if (dev::IsNxlinkActive()) {
        dev::DisableNxlink();
        UL_LOG_INFO("qdesktop: NxlinkWindow — nxlink disabled via UI");
    } else {
        const bool ok = dev::TryEnableNxlink();
        UL_LOG_INFO("qdesktop: NxlinkWindow — TryEnableNxlink returned %d",
                    static_cast<int>(ok));
    }
    // State texture invalidation happens at the top of the next OnRender call
    // when last_active_ diverges from the new IsNxlinkActive() value.
}

} // namespace ul::menu::qdesktop
