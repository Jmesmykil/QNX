// qd_LogFlushWindow.cpp — Telemetry flush panel for Q OS qdesktop.
// 480×180 translucent panel: title, description, "Flush Now" button,
// "Last flush: HH:MM:SS" status line.
// Pattern: lazy SDL_Texture cache, click-vs-drag touch state machine —
// mirrors QdPowerButtonElement exactly.

#include <ul/menu/qdesktop/qd_LogFlushWindow.hpp>
#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace ul::menu::qdesktop {

// ── Factory ────────────────────────────────────────────────────────────────────

/*static*/
QdLogFlushWindow::Ref QdLogFlushWindow::New(const QdTheme &theme) {
    return std::make_shared<QdLogFlushWindow>(theme);
}

// ── Constructor / Destructor ───────────────────────────────────────────────────

QdLogFlushWindow::QdLogFlushWindow(const QdTheme &theme)
    : theme_(theme),
      x_(0), y_(0),
      tex_title_(nullptr),
      tex_desc_(nullptr),
      tex_btn_(nullptr),
      tex_last_flush_(nullptr),
      last_flush_time_(0),
      btn_press_inside_(false),
      btn_down_x_(0), btn_down_y_(0)
{
    UL_LOG_INFO("qdesktop: QdLogFlushWindow ctor");
}

QdLogFlushWindow::~QdLogFlushWindow() {
    FreeTextures();
}

// ── Mutators ───────────────────────────────────────────────────────────────────

void QdLogFlushWindow::SetPos(s32 x, s32 y) {
    x_ = x;
    y_ = y;
}

// ── Texture lifecycle ──────────────────────────────────────────────────────────

void QdLogFlushWindow::FreeTextures() {
    auto destroy = [](SDL_Texture *&t) {
        pu::ui::render::DeleteTexture(t);
    };
    destroy(tex_title_);
    destroy(tex_desc_);
    destroy(tex_btn_);
    destroy(tex_last_flush_);
}

void QdLogFlushWindow::EnsureTitleTexture() {
    if (tex_title_ != nullptr) { return; }
    tex_title_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge),
        "Flush Telemetry",
        theme_.text_primary);
}

void QdLogFlushWindow::EnsureDescTexture() {
    if (tex_desc_ != nullptr) { return; }
    tex_desc_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        "Sync all log channels + fdatasync SD ring",
        theme_.text_secondary,
        static_cast<u32>(PANEL_W - ROW_TEXT_X * 2));
}

void QdLogFlushWindow::EnsureBtnTexture() {
    if (tex_btn_ != nullptr) { return; }
    tex_btn_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        "Flush Now",
        theme_.accent);
}

void QdLogFlushWindow::RebuildLastFlushTexture() {
    // Free the old texture unconditionally before rebuilding.
    if (tex_last_flush_ != nullptr) {
        pu::ui::render::DeleteTexture(tex_last_flush_);
    }

    char buf[48];
    if (last_flush_time_ == 0) {
        (void)std::strncpy(buf, "No flush yet", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    } else {
        // Format "Last flush: HH:MM:SS" using the wall-clock of the flush.
        struct tm tm_info;
        const time_t t = last_flush_time_;
#ifdef __SWITCH__
        // libnx provides localtime_r.
        localtime_r(&t, &tm_info);
#else
        // POSIX fallback for host unit-test build.
        const struct tm *p = localtime(&t);
        if (p != nullptr) {
            tm_info = *p;
        } else {
            (void)std::strncpy(buf, "Last flush: --:--:--", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            tex_last_flush_ = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                buf,
                theme_.text_secondary);
            return;
        }
#endif
        (void)std::snprintf(buf, sizeof(buf),
                            "Last flush: %02d:%02d:%02d",
                            tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    }

    tex_last_flush_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        buf,
        theme_.text_secondary);
}

// ── OnRender ───────────────────────────────────────────────────────────────────
//
// Render order:
//   1. Background panel — translucent dark at 0xCC alpha.
//   2. Border — 1px accent ring.
//   3. Title row   — "Flush Telemetry".
//   4. Desc row    — "Sync all log channels + fdatasync SD ring".
//   5. Flush button.
//   6. Last-flush status line — right of the button.

void QdLogFlushWindow::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                 const s32 origin_x, const s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) { return; }

    const s32 abs_x = x_ + origin_x;
    const s32 abs_y = y_ + origin_y;

    // ── 1. Background panel ────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const pu::ui::Color &dbg = theme_.desktop_bg;
    SDL_SetRenderDrawColor(r, dbg.r, dbg.g, dbg.b, 0xCCu);
    SDL_Rect panel { abs_x, abs_y, PANEL_W, PANEL_H };
    SDL_RenderFillRect(r, &panel);

    // ── 2. Border ─────────────────────────────────────────────────────────────
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0x99u);
    SDL_RenderDrawRect(r, &panel);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Lazy-build static textures ─────────────────────────────────────────────
    EnsureTitleTexture();
    EnsureDescTexture();
    EnsureBtnTexture();

    // Build last-flush texture on first render (shows "No flush yet").
    if (tex_last_flush_ == nullptr) {
        RebuildLastFlushTexture();
    }

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

    // ── 4. Description ─────────────────────────────────────────────────────────
    blit(tex_desc_, ROW_DESC_Y);

    // ── 5. Flush button ────────────────────────────────────────────────────────
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

        if (tex_btn_ != nullptr) {
            int lw = 0, lh = 0;
            SDL_QueryTexture(tex_btn_, nullptr, nullptr, &lw, &lh);
            const s32 lx = bx + (BTN_W - lw) / 2;
            const s32 ly = by + (BTN_H - lh) / 2;
            const SDL_Rect dst { lx, ly, lw, lh };
            SDL_RenderCopy(r, tex_btn_, nullptr, &dst);
        }
    }

    // ── 6. Last-flush status line — right of the button ────────────────────────
    // Positioned at (BTN_X_OFF + BTN_W + 16, ROW_BTN_Y) vertically centred.
    if (tex_last_flush_ != nullptr) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex_last_flush_, nullptr, nullptr, &tw, &th);
        const s32 status_x = abs_x + BTN_X_OFF + BTN_W + 16;
        const s32 status_y = abs_y + ROW_BTN_Y + (BTN_H - th) / 2;
        const s32 max_w    = PANEL_W - (BTN_X_OFF + BTN_W + 16) - ROW_TEXT_X;
        if (max_w > 0) {
            const s32 cw = std::min(tw, max_w);
            const SDL_Rect src { 0, 0, cw, th };
            const SDL_Rect dst { status_x, status_y, cw, th };
            SDL_RenderCopy(r, tex_last_flush_, &src, &dst);
        }
    }
}

// ── OnInput ────────────────────────────────────────────────────────────────────

static constexpr s32 FLUSH_CLICK_TOL_PX = 20;

void QdLogFlushWindow::OnInput(const u64 keys_down,
                                const u64 /*keys_up*/,
                                const u64 /*keys_held*/,
                                const pu::ui::TouchPoint touch_pos)
{
    if (keys_down & HidNpadButton_A) {
        DoFlush();
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
            const s32 tol_sq  = FLUSH_CLICK_TOL_PX * FLUSH_CLICK_TOL_PX;
            if (!inside || dist_sq > tol_sq) {
                btn_press_inside_ = false;
            }
        }
    } else {
        if (btn_press_inside_) {
            btn_press_inside_ = false;
            DoFlush();
        }
    }
}

// ── DoFlush ───────────────────────────────────────────────────────────────────

void QdLogFlushWindow::DoFlush() {
    dev::FlushAllChannels();
    last_flush_time_ = ::time(nullptr);
    // Rebuild the status texture immediately so it shows the correct time
    // on the very next frame without a stale "No flush yet" flash.
    RebuildLastFlushTexture();
    UL_LOG_INFO("qdesktop: LogFlushWindow — flush complete at %ld",
                static_cast<long>(last_flush_time_));
}

} // namespace ul::menu::qdesktop
