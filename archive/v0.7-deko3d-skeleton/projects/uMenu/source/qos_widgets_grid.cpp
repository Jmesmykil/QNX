#include "qos_widgets_grid.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <cstring>

namespace qos {

// ─── Setters ──────────────────────────────────────────────────────────────────

void TileGrid::SetTiles(std::vector<TileEntry> tiles) {
    tiles_ = std::move(tiles);
    // Clamp cursor in case the new tile list is shorter.
    if (!tiles_.empty()) {
        cursor_ = std::max(0, std::min(cursor_, static_cast<int>(tiles_.size()) - 1));
    } else {
        cursor_ = 0;
    }
    // Recompute scroll so the cursor row is still visible.
    int cursor_row = (tiles_.empty() ? 0 : cursor_ / std::max(1, opts_.cols));
    scroll_row_ = std::max(0, std::min(scroll_row_, cursor_row));
    if (cursor_row >= scroll_row_ + opts_.rows_visible)
        scroll_row_ = cursor_row - opts_.rows_visible + 1;
}

void TileGrid::SetOptions(const TileGridOpts &opts) {
    opts_ = opts;
}

void TileGrid::SetActivateCallback(TileActivateCb cb) {
    activate_cb_ = std::move(cb);
}

// ─── Accessors ────────────────────────────────────────────────────────────────

int TileGrid::GetCursorIndex() const {
    return cursor_;
}

void TileGrid::SetCursorIndex(int idx) {
    if (tiles_.empty()) {
        cursor_ = 0;
        return;
    }
    cursor_ = std::max(0, std::min(idx, static_cast<int>(tiles_.size()) - 1));
    // Adjust scroll to keep cursor visible.
    int row = cursor_ / std::max(1, opts_.cols);
    if (row < scroll_row_)
        scroll_row_ = row;
    else if (row >= scroll_row_ + opts_.rows_visible)
        scroll_row_ = row - opts_.rows_visible + 1;
}

// ─── Navigation ───────────────────────────────────────────────────────────────

bool TileGrid::OnInput(bool up, bool down, bool left, bool right,
                       bool activate_a, bool back_b) {
    if (tiles_.empty())
        return false;

    const int n     = static_cast<int>(tiles_.size());
    const int cols  = std::max(1, opts_.cols);
    int       new_c = cursor_;

    if (left  && new_c > 0)           new_c -= 1;
    if (right && new_c < n - 1)       new_c += 1;
    if (up    && new_c - cols >= 0)   new_c -= cols;
    if (down  && new_c + cols < n)    new_c += cols;

    cursor_ = new_c;

    // Scroll to keep cursor row visible.
    int row = cursor_ / cols;
    if (row < scroll_row_)
        scroll_row_ = row;
    else if (row >= scroll_row_ + opts_.rows_visible)
        scroll_row_ = row - opts_.rows_visible + 1;

    if (activate_a) {
        if (activate_cb_)
            activate_cb_(tiles_[cursor_]);
        return true;
    }

    // back_b does not activate; consumed = false.
    (void)back_b;
    return false;
}

// ─── Rendering ────────────────────────────────────────────────────────────────

bool TileGrid::Render() {
    if (tiles_.empty())
        return false;

    bool activated = false;

    ImDrawList *dl     = ImGui::GetWindowDrawList();
    ImVec2      origin = ImGui::GetCursorScreenPos();

    const int   cols        = std::max(1, opts_.cols);
    const float tile_sz     = opts_.tile_size_px;
    const float gap         = opts_.tile_gap_px;
    const float label_h     = opts_.label_h_px;
    const float cell_w      = tile_sz + gap;
    const float cell_h      = tile_sz + label_h + gap;
    const int   rows_vis    = std::max(1, opts_.rows_visible);
    const int   total_rows  = (static_cast<int>(tiles_.size()) + cols - 1) / cols;
    const int   start_row   = std::max(0, std::min(scroll_row_, total_rows - rows_vis));
    const int   end_row     = std::min(total_rows, start_row + rows_vis);

    // Reserve vertical space so ImGui knows the widget height.
    const float total_h = static_cast<float>(rows_vis) * cell_h;
    const float total_w = static_cast<float>(cols)  * cell_w - gap;
    ImGui::Dummy(ImVec2(total_w, total_h));

    for (int row = start_row; row < end_row; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= static_cast<int>(tiles_.size()))
                break;

            const TileEntry &tile = tiles_[idx];
            const bool       focused = (idx == cursor_);

            // Tile top-left in screen space.
            float sx = origin.x + static_cast<float>(col) * cell_w;
            float sy = origin.y + static_cast<float>(row - start_row) * cell_h;

            ImVec2 tile_min(sx, sy);
            ImVec2 tile_max(sx + tile_sz, sy + tile_sz);

            // Background.
            dl->AddRectFilled(tile_min, tile_max, opts_.tile_bg, 8.0f);

            // Icon or placeholder.
            if (tile.icon != 0) {
                dl->AddImage(tile.icon, tile_min, tile_max);
            } else {
                // Placeholder: rounded-rect fill already drawn; add centered first char.
                if (!tile.label.empty()) {
                    // Grab the first UTF-8 code unit as a null-terminated string.
                    char ch[5] = {};
                    // Copy up to 4 bytes of the first character.
                    int bytes = 1;
                    unsigned char c0 = static_cast<unsigned char>(tile.label[0]);
                    if      (c0 >= 0xF0) bytes = 4;
                    else if (c0 >= 0xE0) bytes = 3;
                    else if (c0 >= 0xC0) bytes = 2;
                    for (int b = 0; b < bytes && b < static_cast<int>(tile.label.size()); ++b)
                        ch[b] = tile.label[static_cast<std::string::size_type>(b)];

                    ImFont *font   = ImGui::GetFont();
                    float   fsz    = tile_sz * 0.4f;
                    ImVec2  tsz    = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, ch, ch + bytes);
                    ImVec2  tpos(tile_min.x + (tile_sz - tsz.x) * 0.5f,
                                 tile_min.y + (tile_sz - tsz.y) * 0.5f);
                    dl->AddText(font, fsz, tpos, IM_COL32(255, 255, 255, 200), ch, ch + bytes);
                }
            }

            // Border — 2px for focused, 1px for normal.
            if (focused) {
                dl->AddRect(tile_min, tile_max, opts_.tile_focus, 8.0f, 0, 2.0f);
            } else {
                dl->AddRect(tile_min, tile_max, opts_.tile_border, 8.0f, 0, 1.0f);
            }

            // Label under the tile.
            if (!tile.label.empty()) {
                ImVec2 label_min(sx, sy + tile_sz);
                ImVec2 label_max(sx + tile_sz, sy + tile_sz + label_h);

                ImFont *font = ImGui::GetFont();
                float   fsz  = ImGui::GetFontSize(); // default font size
                const char *text_begin = tile.label.c_str();
                const char *text_end   = text_begin + tile.label.size();

                // Ellipsize: measure and trim until it fits.
                const float max_w = tile_sz - 4.0f;
                ImVec2 full_sz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text_begin, text_end);

                if (full_sz.x <= max_w) {
                    // Fits — center it.
                    float tx = label_min.x + (tile_sz - full_sz.x) * 0.5f;
                    float ty = label_min.y + (label_h - full_sz.y) * 0.5f;
                    dl->AddText(font, fsz, ImVec2(tx, ty), opts_.label_color, text_begin, text_end);
                } else {
                    // Need to ellipsize. Binary-search the cut point.
                    const char *ellipsis    = "...";
                    ImVec2      e_sz        = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, ellipsis);
                    float       avail       = max_w - e_sz.x;
                    if (avail < 0.0f) avail = 0.0f;

                    // Walk forward byte-by-byte until we exceed available width.
                    const char *cut = text_begin;
                    while (cut < text_end) {
                        // Advance one UTF-8 character.
                        unsigned char b = static_cast<unsigned char>(*cut);
                        int step = 1;
                        if      (b >= 0xF0) step = 4;
                        else if (b >= 0xE0) step = 3;
                        else if (b >= 0xC0) step = 2;
                        const char *next = cut + step;
                        if (next > text_end) break;
                        ImVec2 sz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text_begin, next);
                        if (sz.x > avail) break;
                        cut = next;
                    }

                    // Build clipped string + ellipsis.
                    char buf[256];
                    int  copy_len = static_cast<int>(cut - text_begin);
                    if (copy_len > 252) copy_len = 252;
                    std::memcpy(buf, text_begin, static_cast<std::size_t>(copy_len));
                    std::memcpy(buf + copy_len, ellipsis, 4); // includes NUL

                    ImVec2 clipped_sz = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, buf);
                    float tx = label_min.x + (tile_sz - clipped_sz.x) * 0.5f;
                    float ty = label_min.y + (label_h - clipped_sz.y) * 0.5f;
                    dl->AddText(font, fsz, ImVec2(tx, ty), opts_.label_color, buf);
                }
            }
        }
    }

    return activated;
}

} // namespace qos
