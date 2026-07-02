// qd_SuspendedAppDockEntry.cpp — implementation.
//
// See qd_SuspendedAppDockEntry.hpp for design notes.  Layout/draw geometry
// intentionally mirrors qd_MinimizedDockEntry so the two entry kinds line up
// pixel-perfect in the dock band.

#include <ul/menu/qdesktop/qd_SuspendedAppDockEntry.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Color palette (matches qd_MinimizedDockEntry — same dock-tile chrome) ────
#define kTileFocusBg  pu::ui::Color{ g_QdTheme.surface_glass.r,     g_QdTheme.surface_glass.g,     g_QdTheme.surface_glass.b,     0xFFu }
#define kFocusRingCol pu::ui::Color{ g_QdTheme.focus_ring.r,        g_QdTheme.focus_ring.g,        g_QdTheme.focus_ring.b,        0xFFu }
#define kNavyBg       pu::ui::Color{ g_QdTheme.desktop_bg.r,        g_QdTheme.desktop_bg.g,        g_QdTheme.desktop_bg.b,        0xE0u }
#define kTitleCol     pu::ui::Color{ g_QdTheme.text_primary.r,      g_QdTheme.text_primary.g,      g_QdTheme.text_primary.b,      0xFFu }

// ── Ctor / dtor ──────────────────────────────────────────────────────────────

QdSuspendedAppDockEntry::QdSuspendedAppDockEntry(u64 program_id,
                                                 const std::string& title,
                                                 SDL_Texture* icon_tex)
    : program_id_(program_id),
      title_(title),
      icon_tex_(icon_tex)
{}

QdSuspendedAppDockEntry::~QdSuspendedAppDockEntry() {
    // icon_tex_ is BORROWED from QdNsIconCache — do NOT free.
    if (label_tex_) {
        pu::ui::render::DeleteTexture(label_tex_);
        label_tex_ = nullptr;
    }
}

void QdSuspendedAppDockEntry::EnsureLabelTexture() const {
    if (label_tex_ != nullptr || title_.empty()) {
        return;
    }
    label_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        title_,
        kTitleCol);
    if (label_tex_ != nullptr) {
        SDL_QueryTexture(label_tex_, nullptr, nullptr, &label_w_, &label_h_);
    }
}

// ── DrawRoundedRect (identical to QdMinimizedDockEntry — could extract later) ─

void QdSuspendedAppDockEntry::DrawRoundedRect(SDL_Renderer* r,
                                               int x, int y, int w, int h,
                                               pu::ui::Color col) {
    constexpr int rad = 8;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    SDL_Rect hbar = { x, y + rad, w, h - 2 * rad };
    SDL_RenderFillRect(r, &hbar);
    SDL_Rect vbar = { x + rad, y, w - 2 * rad, h };
    SDL_RenderFillRect(r, &vbar);
    for (int cy = 0; cy < rad; ++cy) {
        for (int cx = 0; cx < rad; ++cx) {
            const int dx = rad - 1 - cx;
            const int dy = rad - 1 - cy;
            if (dx * dx + dy * dy <= rad * rad) {
                SDL_RenderDrawPoint(r, x + cx,         y + cy);
                SDL_RenderDrawPoint(r, x + w - 1 - cx, y + cy);
                SDL_RenderDrawPoint(r, x + cx,         y + h - 1 - cy);
                SDL_RenderDrawPoint(r, x + w - 1 - cx, y + h - 1 - cy);
            }
        }
    }
}

// ── Render ──────────────────────────────────────────────────────────────────

void QdSuspendedAppDockEntry::Render(SDL_Renderer* r) const {
    constexpr int W = static_cast<int>(SNAP_W) + 8;
    constexpr int H = static_cast<int>(SNAP_H) + 8;
    const int tx = tile_x_;
    const int ty = tile_y_;

    // 1. Two-pass border (outer cyan + inner navy) — same chrome as
    // QdMinimizedDockEntry so the two kinds line up visually.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    DrawRoundedRect(r, tx,     ty,     W,     H,     kFocusRingCol);
    DrawRoundedRect(r, tx + 1, ty + 1, W - 2, H - 2, kNavyBg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // 2. Icon blit — centred inside padding.  Differs from QdMinimizedDockEntry
    // which draws a snapshot SDL_Texture; here we draw the NACP JPEG icon.
    if (icon_tex_) {
        SDL_Rect dst = { tx + 4, ty + 4, static_cast<int>(SNAP_W), static_cast<int>(SNAP_H) };
        SDL_RenderCopy(r, icon_tex_, nullptr, &dst);
    }

    // 3. Focus ring (cyan, FOCUS_RING_THICKNESS px) — same as minimized entry.
    if (focused_) {
        SDL_SetRenderDrawColor(r, kFocusRingCol.r, kFocusRingCol.g,
                               kFocusRingCol.b, kFocusRingCol.a);
        constexpr int ring = static_cast<int>(FOCUS_RING_THICKNESS);
        for (int i = 0; i < ring; ++i) {
            SDL_Rect ring_rect = { tx - i, ty - i, W + 2 * i, H + 2 * i };
            SDL_RenderDrawRect(r, &ring_rect);
        }
    }

    // 4. Title label — cached.
    if (!title_.empty()) {
        EnsureLabelTexture();
        if (label_tex_ != nullptr) {
            const int label_max_w = W - 4;
            const int src_w = (label_w_ > label_max_w) ? label_max_w : label_w_;
            SDL_Rect src_rect = { 0, 0, src_w, label_h_ };
            int label_x = tx + (W - src_w) / 2;
            int label_y = ty + H - label_h_ - 2;
            if (label_y < ty) label_y = ty;
            SDL_Rect dst_rect = { label_x, label_y, src_w, label_h_ };
            SDL_RenderCopy(r, label_tex_, &src_rect, &dst_rect);
        }
    }
}

// ── PollEvent ───────────────────────────────────────────────────────────────

QdSuspendedAppDockEntry::PollAction QdSuspendedAppDockEntry::PollEvent(
        u64 keys_down, u64 /*keys_up*/, u64 /*keys_held*/,
        pu::ui::TouchPoint touch_pos,
        s32 cx, s32 cy) {
    constexpr int W = static_cast<int>(SNAP_W) + 8;
    constexpr int H = static_cast<int>(SNAP_H) + 8;
    const s32 tx = tile_x_;
    const s32 ty = tile_y_;

    // ZL trigger — fires when the software cursor is over this tile.  We
    // don't gate on focused_ because focus tracking for suspended-app
    // entries isn't wired (HOS invariant of one suspended app at a time
    // makes it unnecessary).
    if (keys_down & HidNpadButton_ZL) {
        if (cx >= tx && cx < tx + W && cy >= ty && cy < ty + H) {
            return PollAction::OpenContextMenu;
        }
    }

    // Touch hit-test — anywhere inside the tile rect.
    if (!touch_pos.IsEmpty()) {
        if (touch_pos.x >= tx && touch_pos.x < tx + W &&
            touch_pos.y >= ty && touch_pos.y < ty + H) {
            return PollAction::Resume;
        }
    }

    return PollAction::None;
}

}  // namespace ul::menu::qdesktop
