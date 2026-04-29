// qd_MinimizedDockEntry.cpp — Minimized-window snapshot tile implementation.
// See qd_MinimizedDockEntry.hpp for design notes.
// Snapshot texture lifecycle: caller (QdWindowManager::MinimizeWindow) creates the
// texture via SDL_CreateTexture + SDL_SetRenderTarget capture, then passes ownership
// here. ~QdMinimizedDockEntry() frees it via pu::ui::render::DeleteTexture (B41/B42).

#include <ul/menu/qdesktop/qd_MinimizedDockEntry.hpp>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Color palette ─────────────────────────────────────────────────────────────

static constexpr pu::ui::Color kTileBg       = { 0x22, 0x22, 0x24, 0xE0 }; // dark translucent
static constexpr pu::ui::Color kTileFocusBg  = { 0x30, 0x30, 0x36, 0xFF }; // slightly lighter
static constexpr pu::ui::Color kFocusRingCol = { 0x00, 0xE5, 0xFF, 0xFF }; // cyan #00E5FF
static constexpr pu::ui::Color kTitleCol     = { 0xEE, 0xEE, 0xEE, 0xFF }; // near-white

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
}

// ── DrawRoundedRect ───────────────────────────────────────────────────────────

void QdMinimizedDockEntry::DrawRoundedRect(SDL_Renderer* r,
                                            int x, int y, int w, int h,
                                            pu::ui::Color col) {
    // Corner radius = 4 px.  Draw as a plus-sign of three rects + four corner fans.
    constexpr int rad = 4;
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

    // 1. Background tile
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    pu::ui::Color bg_col = focused_ ? kTileFocusBg : kTileBg;
    DrawRoundedRect(r, tx, ty, W, H, bg_col);
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

    // 4. Title label (rendered directly via SDL_RenderCopy on a small cached texture).
    // Title is short — render via Plutonium helper inline.
    // We avoid storing a per-entry cached label texture to keep the dtor simple
    // (snapshot_ is the only owned texture).  RenderText allocates a one-frame texture
    // per call; this is acceptable given the dock band renders at 60 fps with ≤6 entries.
    if (!title_.empty()) {
        // Use a small font (16 pt via the Plutonium shared font).
        // Clip label to tile width minus 2-px margin on each side.
        int label_max_w = W - 4;
        SDL_Texture* label_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
            title_,
            kTitleCol);
        if (label_tex) {
            int lw = 0, lh = 0;
            SDL_QueryTexture(label_tex, nullptr, nullptr, &lw, &lh);
            // Clamp width
            int src_w = (lw > label_max_w) ? label_max_w : lw;
            SDL_Rect src_rect = { 0, 0, src_w, lh };
            // Render below snapshot centred horizontally, 2 px from bottom of tile
            int label_x = tx + (W - src_w) / 2;
            int label_y = ty + H - lh - 2;
            // Keep label inside tile vertically
            if (label_y < ty) label_y = ty;
            SDL_Rect dst_rect = { label_x, label_y, src_w, lh };
            SDL_RenderCopy(r, label_tex, &src_rect, &dst_rect);
            pu::ui::render::DeleteTexture(label_tex);
        }
    }
}

// ── PollEvent ─────────────────────────────────────────────────────────────────

bool QdMinimizedDockEntry::PollEvent(u64 /*keys_down*/, u64 /*keys_up*/,
                                      u64 /*keys_held*/,
                                      pu::ui::TouchPoint touch_pos) {
    constexpr int W = static_cast<int>(SNAP_W) + 8;
    constexpr int H = static_cast<int>(SNAP_H) + 8;

    if (touch_pos.IsEmpty()) {
        return false;
    }

    // Touch-tap inside tile bounds fires restore.
    const int tx = static_cast<int>(touch_pos.x);
    const int ty = static_cast<int>(touch_pos.y);
    if (tx >= tile_x_ && tx < tile_x_ + W &&
        ty >= tile_y_ && ty < tile_y_ + H) {
        if (on_restore_requested) {
            on_restore_requested(this);
        }
        return true;
    }

    return false;
}

} // namespace ul::menu::qdesktop
