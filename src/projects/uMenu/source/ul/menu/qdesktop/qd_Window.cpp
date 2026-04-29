// qd_Window.cpp — Generic window primitive implementation.
// See qd_Window.hpp for design notes.
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <cmath>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Color palette ─────────────────────────────────────────────────────────────

static constexpr pu::ui::Color kWinBg         = { 0x1A, 0x1A, 0x1A, 0xFF }; // dark chrome bg
static constexpr pu::ui::Color kTitlebarBg     = { 0x2A, 0x2A, 0x2A, 0xFF };
static constexpr pu::ui::Color kTitlebarBgFoc  = { 0x2E, 0x2E, 0x3C, 0xFF }; // slight blue tint when focused
static constexpr pu::ui::Color kTitleText      = { 0xE0, 0xE0, 0xE0, 0xFF };
static constexpr pu::ui::Color kFocusRingCol   = { 0x00, 0xE5, 0xFF, 0xFF }; // cyan
static constexpr pu::ui::Color kShadow         = {  0,   0,   0,  0x80 };
static constexpr pu::ui::Color kTrafficClose   = { 0xFF, 0x5F, 0x57, 0xFF }; // red
static constexpr pu::ui::Color kTrafficMin     = { 0xFE, 0xBC, 0x2E, 0xFF }; // yellow
static constexpr pu::ui::Color kTrafficMax     = { 0x28, 0xC8, 0x41, 0xFF }; // green
static constexpr pu::ui::Color kTrafficDim     = { 0x60, 0x60, 0x60, 0xFF }; // unfocused
static constexpr pu::ui::Color kContentBorder  = { 0x44, 0x44, 0x44, 0xFF };

// ── Factory ───────────────────────────────────────────────────────────────────

QdWindow::Ref QdWindow::New(const std::string& title,
                             std::shared_ptr<pu::ui::elm::Element> elem,
                             s32 x, s32 y, s32 w, s32 h)
{
    auto win = std::shared_ptr<QdWindow>(new QdWindow());
    win->title_    = title;
    win->content_  = std::move(elem);
    win->win_x_    = x;
    win->win_y_    = y;
    win->win_w_    = std::max(w, static_cast<s32>(WIN_MIN_W));
    win->win_h_    = std::max(h, static_cast<s32>(WIN_MIN_H));
    win->focused_  = true;
    win->state_    = WindowState::Normal;
    return win;
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdWindow::~QdWindow() {
    FreeTextures();
}

// ── Texture lifecycle ─────────────────────────────────────────────────────────

void QdWindow::FreeTextures() {
    // Use pu::ui::render::DeleteTexture per B41/B42 render-cache contract.
    if (titlebar_tex_) {
        pu::ui::render::DeleteTexture(titlebar_tex_);
        titlebar_tex_   = nullptr;
        titlebar_tex_w_ = 0;
        titlebar_tex_h_ = 0;
    }
    // fbo_ is always nullptr in v1.10; included for completeness.
    if (fbo_) {
        pu::ui::render::DeleteTexture(fbo_);
        fbo_ = nullptr;
    }
}

void QdWindow::EnsureTitlebarTexture(pu::ui::render::Renderer::Ref& drawer) {
    // Invalidate if window width changed since last render.
    if (titlebar_tex_ && titlebar_tex_w_ != win_w_) {
        FreeTextures();
    }
    if (titlebar_tex_) {
        return; // still valid
    }

    // Render title text into a texture using Plutonium's RenderText helper.
    // Font: Large (closest to macOS titlebar size at 1920×1080).
    titlebar_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large),
        title_,
        kTitleText);

    if (titlebar_tex_) {
        SDL_QueryTexture(titlebar_tex_, nullptr, nullptr,
                         &titlebar_tex_w_, &titlebar_tex_h_);
    }
}

// ── SDL drawing helpers ───────────────────────────────────────────────────────

void QdWindow::DrawCircle(SDL_Renderer* r, int cx, int cy, int rad,
                          pu::ui::Color col)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = static_cast<int>(sqrtf(static_cast<float>(rad * rad - dy * dy)));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void QdWindow::DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                               pu::ui::Color col)
{
    constexpr int kRad = 6;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);

    // Fill center band and side bands
    SDL_Rect center = { x + kRad, y, w - 2 * kRad, h };
    SDL_RenderFillRect(r, &center);
    SDL_Rect left   = { x,            y + kRad, kRad, h - 2 * kRad };
    SDL_Rect right  = { x + w - kRad, y + kRad, kRad, h - 2 * kRad };
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);

    // Four corner circles
    DrawCircle(r, x + kRad,         y + kRad,         kRad, col);
    DrawCircle(r, x + w - kRad - 1, y + kRad,         kRad, col);
    DrawCircle(r, x + kRad,         y + h - kRad - 1, kRad, col);
    DrawCircle(r, x + w - kRad - 1, y + h - kRad - 1, kRad, col);
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void QdWindow::OnRender(pu::ui::render::Renderer::Ref& drawer, s32 /*x*/, s32 /*y*/) {
    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // Advance animation if running.
    if (state_ == WindowState::Minimizing || state_ == WindowState::Restoring) {
        AdvanceAnimation();
    }

    // Hidden states — nothing to paint.
    if (state_ == WindowState::Minimized || state_ == WindowState::Closing) {
        return;
    }

    const int wx = win_x_;
    const int wy = win_y_;
    const int ww = win_w_;
    const int wh = win_h_;
    const int tbh = kTitlebarH;

    // Apply alpha during animation.
    const u8 alpha = anim_alpha_;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // ── Drop shadow ───────────────────────────────────────────────────────────
    {
        pu::ui::Color shadow = kShadow;
        shadow.a = static_cast<u8>(static_cast<int>(shadow.a) * alpha / 255);
        SDL_Rect sr = { wx + 6, wy + 6, ww, wh };
        SDL_SetRenderDrawColor(r, shadow.r, shadow.g, shadow.b, shadow.a);
        SDL_RenderFillRect(r, &sr);
    }

    // ── Window background ─────────────────────────────────────────────────────
    {
        pu::ui::Color bg = kWinBg;
        bg.a = static_cast<u8>(static_cast<int>(bg.a) * alpha / 255);
        DrawRoundedRect(r, wx, wy, ww, wh, bg);
    }

    // ── Titlebar strip ────────────────────────────────────────────────────────
    {
        pu::ui::Color tbc = focused_ ? kTitlebarBgFoc : kTitlebarBg;
        tbc.a = static_cast<u8>(static_cast<int>(tbc.a) * alpha / 255);
        DrawRoundedRect(r, wx, wy, ww, tbh, tbc);
    }

    // ── Focus ring (only when focused) ────────────────────────────────────────
    if (focused_) {
        pu::ui::Color fc = kFocusRingCol;
        fc.a = static_cast<u8>(static_cast<int>(fc.a) * alpha / 255);
        SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, fc.a);
        for (int i = 0; i < kFocusRing; i++) {
            SDL_Rect ring = { wx - i, wy - i, ww + 2 * i, wh + 2 * i };
            SDL_RenderDrawRect(r, &ring);
        }
    }

    // ── Content border ────────────────────────────────────────────────────────
    {
        pu::ui::Color bdc = kContentBorder;
        bdc.a = static_cast<u8>(static_cast<int>(bdc.a) * alpha / 255);
        SDL_Rect br = { wx, wy + tbh, ww, wh - tbh };
        SDL_SetRenderDrawColor(r, bdc.r, bdc.g, bdc.b, bdc.a);
        SDL_RenderDrawRect(r, &br);
    }

    // ── Traffic light buttons ─────────────────────────────────────────────────
    // Positions recomputed each frame (window could have moved).
    const int tc_x = wx + kTrafficLeft;
    const int tc_y = wy + kTrafficY;

    // Close (red)
    const int cx = tc_x;
    const int mx = cx + kTrafficGap;
    const int ax = mx + kTrafficGap;

    pu::ui::Color close_col = focused_ ? kTrafficClose : kTrafficDim;
    pu::ui::Color min_col   = focused_ ? kTrafficMin   : kTrafficDim;
    pu::ui::Color max_col   = focused_ ? kTrafficMax   : kTrafficDim;
    close_col.a = alpha; min_col.a = alpha; max_col.a = alpha;

    DrawCircle(r, cx, tc_y, kTrafficR, close_col);
    DrawCircle(r, mx, tc_y, kTrafficR, min_col);
    DrawCircle(r, ax, tc_y, kTrafficR, max_col);

    // Store hit rects for PollEvent (center ± radius + slop).
    const int sl = kTrafficSlop;
    traffic_close_ = { cx - kTrafficR - sl, tc_y - kTrafficR - sl,
                       2 * (kTrafficR + sl), 2 * (kTrafficR + sl) };
    traffic_min_   = { mx - kTrafficR - sl, tc_y - kTrafficR - sl,
                       2 * (kTrafficR + sl), 2 * (kTrafficR + sl) };
    traffic_max_   = { ax - kTrafficR - sl, tc_y - kTrafficR - sl,
                       2 * (kTrafficR + sl), 2 * (kTrafficR + sl) };

    // ── Title text ────────────────────────────────────────────────────────────
    EnsureTitlebarTexture(drawer);
    if (titlebar_tex_) {
        // Center in titlebar, clipped to available width (avoid traffic lights).
        const int text_max_x = ax + kTrafficR + kTrafficSlop + 8; // right edge of max button
        const int text_area_w = ww - text_max_x;
        int tx_w = titlebar_tex_w_;
        if (tx_w > text_area_w) tx_w = text_area_w;
        const int tx = wx + text_max_x + (text_area_w - tx_w) / 2;
        const int ty = wy + (tbh - titlebar_tex_h_) / 2;
        SDL_SetTextureAlphaMod(titlebar_tex_, alpha);
        SDL_Rect dst = { tx, ty, tx_w, titlebar_tex_h_ };
        SDL_Rect src = { 0, 0, tx_w, titlebar_tex_h_ };
        SDL_RenderCopy(r, titlebar_tex_, &src, &dst);
        SDL_SetTextureAlphaMod(titlebar_tex_, 255);
    }

    // ── Content element ───────────────────────────────────────────────────────
    // Only render content in Normal or animation states (not Minimized/Closing).
    if (content_ && (state_ == WindowState::Normal ||
                     state_ == WindowState::Minimizing ||
                     state_ == WindowState::Restoring))
    {
        // Content area starts just below the titlebar.
        const int cx_pos = wx;
        const int cy_pos = wy + tbh;

        // Clip rendering to content area so content can't bleed into titlebar or outside.
        SDL_Rect clip = { cx_pos, cy_pos, ww, wh - tbh };
        SDL_RenderSetClipRect(r, &clip);

        // Delegate to content element. Passes absolute screen coordinates.
        content_->OnRender(drawer, cx_pos, cy_pos);

        // Remove clip rect.
        SDL_RenderSetClipRect(r, nullptr);
    }
}

// ── Animation ─────────────────────────────────────────────────────────────────

void QdWindow::AdvanceAnimation() {
    anim_frame_++;

    if (anim_frame_ > kAnimFrames) {
        anim_frame_ = kAnimFrames;
    }

    const float t  = static_cast<float>(anim_frame_) / static_cast<float>(kAnimFrames);

    if (state_ == WindowState::Minimizing) {
        // Ease-in quadratic: t^2
        const float t2 = t * t;

        // Interpolate from original geometry toward dock tile (SNAP_W × SNAP_H).
        const float from_x = static_cast<float>(anim_orig_x_);
        const float from_y = static_cast<float>(anim_orig_y_);
        const float from_w = static_cast<float>(anim_orig_w_);
        const float from_h = static_cast<float>(anim_orig_h_);
        const float to_x   = static_cast<float>(anim_target_x_);
        const float to_y   = static_cast<float>(anim_target_y_);
        const float to_w   = static_cast<float>(SNAP_W);
        const float to_h   = static_cast<float>(SNAP_H);

        win_x_ = static_cast<s32>(from_x + (to_x - from_x) * t2);
        win_y_ = static_cast<s32>(from_y + (to_y - from_y) * t2);
        win_w_ = static_cast<s32>(from_w + (to_w - from_w) * t2);
        win_h_ = static_cast<s32>(from_h + (to_h - from_h) * t2);

        anim_alpha_ = static_cast<u8>(255.0f * (1.0f - t));

        if (anim_frame_ >= kAnimFrames) {
            // Animation complete — signal manager via callback.
            state_ = WindowState::Minimized;
            if (on_minimize_requested) {
                on_minimize_requested(this);
            }
        }
    } else if (state_ == WindowState::Restoring) {
        // Ease-out quadratic: 1 - (1-t)^2
        const float inv = 1.0f - t;
        const float t2  = 1.0f - inv * inv;

        const float from_x = static_cast<float>(anim_target_x_);
        const float from_y = static_cast<float>(anim_target_y_);
        const float from_w = static_cast<float>(SNAP_W);
        const float from_h = static_cast<float>(SNAP_H);
        const float to_x   = static_cast<float>(anim_orig_x_);
        const float to_y   = static_cast<float>(anim_orig_y_);
        const float to_w   = static_cast<float>(anim_orig_w_);
        const float to_h   = static_cast<float>(anim_orig_h_);

        win_x_ = static_cast<s32>(from_x + (to_x - from_x) * t2);
        win_y_ = static_cast<s32>(from_y + (to_y - from_y) * t2);
        win_w_ = static_cast<s32>(from_w + (to_w - from_w) * t2);
        win_h_ = static_cast<s32>(from_h + (to_h - from_h) * t2);

        anim_alpha_ = static_cast<u8>(255.0f * t);

        if (anim_frame_ >= kAnimFrames) {
            // Restore complete — snap to final geometry.
            win_x_   = anim_orig_x_;
            win_y_   = anim_orig_y_;
            win_w_   = anim_orig_w_;
            win_h_   = anim_orig_h_;
            state_   = WindowState::Normal;
            anim_alpha_ = 255;
            // Invalidate titlebar texture — size may have changed during animation.
            FreeTextures();
        }
    }
}

// ── Animation setup ───────────────────────────────────────────────────────────

void QdWindow::BeginMinimizeAnimation() {
    // Capture original geometry for the lerp.
    anim_orig_x_ = win_x_;
    anim_orig_y_ = win_y_;
    anim_orig_w_ = win_w_;
    anim_orig_h_ = win_h_;
    anim_frame_  = 0;
    anim_alpha_  = 255;
    state_       = WindowState::Minimizing;
}

void QdWindow::SetMinimizeTarget(s32 target_x, s32 target_y) {
    anim_target_x_ = target_x;
    anim_target_y_ = target_y;
}

void QdWindow::BeginRestoreAnimation(s32 target_x, s32 target_y,
                                     s32 target_w, s32 target_h,
                                     s32 dock_x, s32 dock_y)
{
    // Restore starts from dock tile position.
    win_x_       = dock_x;
    win_y_       = dock_y;
    win_w_       = static_cast<s32>(SNAP_W);
    win_h_       = static_cast<s32>(SNAP_H);

    anim_orig_x_ = target_x;
    anim_orig_y_ = target_y;
    anim_orig_w_ = target_w;
    anim_orig_h_ = target_h;
    anim_target_x_ = dock_x;
    anim_target_y_ = dock_y;
    anim_frame_  = 0;
    anim_alpha_  = 0;
    state_       = WindowState::Restoring;
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool QdWindow::PollEvent(u64 keys_down, u64 /*keys_up*/, u64 /*keys_held*/,
                         pu::ui::TouchPoint touch_pos)
{
    // Ignore input while animating.
    if (state_ != WindowState::Normal) {
        return false;
    }

    const bool has_touch = (touch_pos.x >= 0 && touch_pos.y >= 0);
    const int  tx = static_cast<int>(touch_pos.x);
    const int  ty = static_cast<int>(touch_pos.y);

    // ── Touch-down: start titlebar drag or hit a traffic light ────────────────
    if (has_touch && !dragging_titlebar_) {
        // Traffic lights: close
        if (tx >= traffic_close_.x && tx < traffic_close_.x + traffic_close_.w &&
            ty >= traffic_close_.y && ty < traffic_close_.y + traffic_close_.h)
        {
            // Release event will fire close — on touch-down here we just consume.
            return true;
        }
        // Traffic lights: minimize
        if (tx >= traffic_min_.x && tx < traffic_min_.x + traffic_min_.w &&
            ty >= traffic_min_.y && ty < traffic_min_.y + traffic_min_.h)
        {
            return true;
        }
        // Traffic lights: maximize (reserved v1.11 — consume but no action)
        if (tx >= traffic_max_.x && tx < traffic_max_.x + traffic_max_.w &&
            ty >= traffic_max_.y && ty < traffic_max_.y + traffic_max_.h)
        {
            return true;
        }

        // Titlebar drag zone: y in [win_y_, win_y_ + kTitlebarH),
        // x in [win_x_, win_x_ + win_w_)
        if (tx >= win_x_ && tx < win_x_ + win_w_ &&
            ty >= win_y_ && ty < win_y_ + kTitlebarH)
        {
            dragging_titlebar_  = true;
            drag_start_touch_x_ = tx;
            drag_start_touch_y_ = ty;
            drag_start_win_x_   = win_x_;
            drag_start_win_y_   = win_y_;
            return true;
        }

        // Hit test: is touch within the full window rect?
        if (tx >= win_x_ && tx < win_x_ + win_w_ &&
            ty >= win_y_ && ty < win_y_ + win_h_)
        {
            // Touch is in the content area — consume (delegate handled by content elem).
            return true;
        }

        return false;
    }

    // ── Touch-move: update drag ────────────────────────────────────────────────
    if (dragging_titlebar_ && has_touch) {
        const int dx = tx - drag_start_touch_x_;
        const int dy = ty - drag_start_touch_y_;

        s32 new_x = drag_start_win_x_ + dx;
        s32 new_y = drag_start_win_y_ + dy;

        // Clamp: titlebar top must stay at or below TOPBAR_H.
        if (new_y < kMinY) new_y = kMinY;

        // Clamp: titlebar bottom must not exceed drag threshold (minimize zone).
        // We clamp to threshold - kTitlebarH so that just touching the threshold
        // triggers minimize rather than going past it.
        if (new_y + kTitlebarH > kDragThresh) {
            new_y = kDragThresh - kTitlebarH;
        }

        win_x_ = new_x;
        win_y_ = new_y;

        // Check minimize trigger.
        if (GetTitlebarBottomY() >= kDragThresh) {
            dragging_titlebar_ = false;
            if (on_minimize_requested) {
                on_minimize_requested(this);
            }
            return true;
        }

        return true;
    }

    // ── Touch-release ─────────────────────────────────────────────────────────
    if (!has_touch && dragging_titlebar_) {
        dragging_titlebar_ = false;
        return false; // drag ended without triggering minimize
    }

    // ── Touch-release on traffic lights (no drag active) ─────────────────────
    // We detect release by: touch was previously inside a button (via keys_down),
    // but Plutonium doesn't give us persistent touch IDs cleanly.
    // Use: if no-touch this frame and keys_down has nothing, check last known touch.
    // Simplified for v1.10: fire actions on touch-down (already consumed above).
    // Close is fired on touch-down for responsiveness.
    (void)keys_down;

    return false;
}

} // namespace ul::menu::qdesktop
