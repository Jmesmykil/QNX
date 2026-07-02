// qd_MinimizedDockEntry.cpp — Minimized-window snapshot tile implementation.
// See qd_MinimizedDockEntry.hpp for design notes.
// Snapshot texture lifecycle: caller (QdWindowManager::MinimizeWindow) creates the
// texture via SDL_CreateTexture + SDL_SetRenderTarget capture, then passes ownership
// here. ~QdMinimizedDockEntry() frees it via pu::ui::render::DeleteTexture (B41/B42).

#include <ul/menu/qdesktop/qd_MinimizedDockEntry.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>   // v2.7.1 — palette tokens
#include <cstring>

namespace ul::menu::qdesktop {

// ── Color palette ─────────────────────────────────────────────────────────────
// v2.7.1 — macros resolve to g_QdTheme at call site so all 10 themes flip
// the minimized-window dock tile chrome (was hardcoded dark + cyan).
#define kTileBg       pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.r, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.g, ::ul::menu::qdesktop::g_QdTheme.titlebar_inactive.b, 0xE0u }
#define kTileFocusBg  pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.surface_glass.r,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.g,     ::ul::menu::qdesktop::g_QdTheme.surface_glass.b,     0xFFu }
#define kFocusRingCol pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.focus_ring.r,        ::ul::menu::qdesktop::g_QdTheme.focus_ring.g,        ::ul::menu::qdesktop::g_QdTheme.focus_ring.b,        0xFFu }
#define kNavyBg       pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.desktop_bg.r,        ::ul::menu::qdesktop::g_QdTheme.desktop_bg.g,        ::ul::menu::qdesktop::g_QdTheme.desktop_bg.b,        0xE0u }
#define kTitleCol     pu::ui::Color{ ::ul::menu::qdesktop::g_QdTheme.text_primary.r,      ::ul::menu::qdesktop::g_QdTheme.text_primary.g,      ::ul::menu::qdesktop::g_QdTheme.text_primary.b,      0xFFu }

// ── Ctor / dtor ───────────────────────────────────────────────────────────────

QdMinimizedDockEntry::QdMinimizedDockEntry(const std::string& title,
                                           SDL_Texture* snapshot,
                                           u64 program_id)
    : title_(title),
      snapshot_(snapshot),
      program_id_(program_id),
      tile_x_(0),
      tile_y_(0),
      focused_(false)
{}

QdMinimizedDockEntry::~QdMinimizedDockEntry() {
    if (snapshot_) {
        pu::ui::render::DeleteTexture(snapshot_);
        snapshot_ = nullptr;
    }
    // Per uMenu optimization audit F2.2: free cached label texture.
    if (label_tex_) {
        pu::ui::render::DeleteTexture(label_tex_);
        label_tex_ = nullptr;
    }
}

void QdMinimizedDockEntry::EnsureLabelTexture() const {
    // Build-once: title_ is fixed at construction, so the texture only
    // needs to be rasterized one time per entry instance.  Subsequent
    // Render() calls reuse the cached texture.
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

// ── DrawRoundedRect ───────────────────────────────────────────────────────────

void QdMinimizedDockEntry::DrawRoundedRect(SDL_Renderer* r,
                                            int x, int y, int w, int h,
                                            pu::ui::Color col) {
    // Corner radius = 8 px.  Draw as a plus-sign of three rects + four corner fans.
    constexpr int rad = 8;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);

    // Horizontal bar (full width, inner height)
    SDL_Rect hbar = { x, y + rad, w, h - 2 * rad };
    SDL_RenderFillRect(r, &hbar);

    // Vertical bar (inner width, full height)
    SDL_Rect vbar = { x + rad, y, w - 2 * rad, h };
    SDL_RenderFillRect(r, &vbar);

    // Four corner arcs approximated as small filled squares
    // (radius 4 means corner cutout is ≤ 4×4 pixels — barely visible at this scale)
    for (int cy = 0; cy < rad; ++cy) {
        for (int cx = 0; cx < rad; ++cx) {
            int dx = rad - 1 - cx;
            int dy = rad - 1 - cy;
            if (dx * dx + dy * dy <= rad * rad) {
                // Top-left
                SDL_RenderDrawPoint(r, x + cx, y + cy);
                // Top-right
                SDL_RenderDrawPoint(r, x + w - 1 - cx, y + cy);
                // Bottom-left
                SDL_RenderDrawPoint(r, x + cx, y + h - 1 - cy);
                // Bottom-right
                SDL_RenderDrawPoint(r, x + w - 1 - cx, y + h - 1 - cy);
            }
        }
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void QdMinimizedDockEntry::Render(SDL_Renderer* r) const {
    constexpr int W = static_cast<int>(SNAP_W) + 8;  // tile width  = snap + padding
    constexpr int H = static_cast<int>(SNAP_H) + 8;  // tile height = snap + padding

    const int tx = tile_x_;
    const int ty = tile_y_;

    // 1. Themed border — two-pass Q OS panel style.
    //    Pass 1: outer cyan rounded rect at tile bounds.
    //    Pass 2: inner navy rounded rect inset 1 px — creates a 1-px cyan border ring.
    //    Then snap + title composite on top of the navy fill.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    DrawRoundedRect(r, tx, ty, W, H, kFocusRingCol);           // outer cyan (1-px border)
    DrawRoundedRect(r, tx + 1, ty + 1, W - 2, H - 2, kNavyBg); // inner navy fill
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // 2. Snapshot blit (centred inside padding)
    if (snapshot_) {
        SDL_Rect dst = { tx + 4, ty + 4, static_cast<int>(SNAP_W), static_cast<int>(SNAP_H) };
        SDL_RenderCopy(r, snapshot_, nullptr, &dst);
    }

    // 3. Focus ring
    if (focused_) {
        SDL_SetRenderDrawColor(r, kFocusRingCol.r, kFocusRingCol.g,
                               kFocusRingCol.b, kFocusRingCol.a);
        constexpr int ring = static_cast<int>(FOCUS_RING_THICKNESS);
        for (int i = 0; i < ring; ++i) {
            SDL_Rect ring_rect = { tx - i, ty - i, W + 2 * i, H + 2 * i };
            SDL_RenderDrawRect(r, &ring_rect);
        }
    }

    // 4. Title label — cached SDL_Texture rebuilt-on-demand only when title
    // changes (which never happens at runtime — title is fixed at ctor).
    // Per uMenu optimization audit F2.2: previously this allocated +
    // destroyed an SDL_Texture every frame (60 Hz × N entries = ~360
    // allocs/sec of font-cache churn).  Now: build-once, reuse, free-in-dtor.
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

// ── PollEvent ─────────────────────────────────────────────────────────────────

QdMinimizedDockEntry::PollAction QdMinimizedDockEntry::PollEvent(
        u64 keys_down, u64 /*keys_up*/, u64 /*keys_held*/,
        pu::ui::TouchPoint touch_pos, s32 cx, s32 cy) {
    constexpr int W = static_cast<int>(SNAP_W) + 8;
    constexpr int H = static_cast<int>(SNAP_H) + 8;

    // ── ZL trigger — opens context menu when cursor is over the tile.
    // Same cursor-over-tile gating as QdSuspendedAppDockEntry (focused_ tracking
    // for minimized tiles isn't wired; cursor-over is the canonical gate).
    if (keys_down & HidNpadButton_ZL) {
        if (cx >= tile_x_ && cx < tile_x_ + W && cy >= tile_y_ && cy < tile_y_ + H) {
            return PollAction::OpenContextMenu;
        }
    }

    // ── Touch tap inside tile bounds fires restore (existing semantic).
    // We fire on_restore_requested here AND return PollAction::Restore so the
    // WM dispatch loop both sees the action (for return value) and the existing
    // callback wiring (set by MinimizeWindow → RestoreWindow) keeps working.
    if (!touch_pos.IsEmpty()) {
        const int tx = static_cast<int>(touch_pos.x);
        const int ty = static_cast<int>(touch_pos.y);
        if (tx >= tile_x_ && tx < tile_x_ + W &&
            ty >= tile_y_ && ty < tile_y_ + H) {
            if (on_restore_requested) {
                on_restore_requested(this);
            }
            return PollAction::Restore;
        }
    }

    return PollAction::None;
}

} // namespace ul::menu::qdesktop
