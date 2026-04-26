// qd_TextViewer.cpp — QdTextViewer implementation.
// Full-screen scrollable text viewer for Q OS qdesktop Stage 3.
//
// File reading:   fopen + fread up to MAX_READ_BYTES; close immediately.
//                 Files larger than MAX_READ_BYTES get an [OUTPUT TRUNCATED] footer.
// Line wrapping:  hard wrap at WRAP_COLS characters per line.
// Line cache:     one SDL_Texture per visible line (lazy); evicted when scroll
//                 moves the line outside [scroll_top_-CACHE_SLACK,
//                 scroll_top_+VISIBLE_LINES+CACHE_SLACK].
// Rendering:      left gutter (line number, 5 chars), right body (line text).
//                 Both rendered in Small font over a near-opaque dark overlay.
// Input:          D-pad Up/Down = ±1 line; ZL/ZR = ±VISIBLE_LINES; B = Close().

#include <ul/menu/qdesktop/qd_TextViewer.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>

namespace ul::menu::qdesktop {

// ── Factory ───────────────────────────────────────────────────────────────────

/*static*/
QdTextViewer::Ref QdTextViewer::New(const QdTheme &theme) {
    return std::make_shared<QdTextViewer>(theme);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdTextViewer::QdTextViewer(const QdTheme &theme)
    : theme_(theme)
{
    UL_LOG_INFO("qdesktop: QdTextViewer ctor");
}

QdTextViewer::~QdTextViewer() {
    FreeAllTextures();
}

// ── FreeAllTextures ───────────────────────────────────────────────────────────

void QdTextViewer::FreeAllTextures() {
    for (auto &kv : line_tex_cache_) {
        if (kv.second != nullptr) {
            SDL_DestroyTexture(kv.second);
        }
    }
    line_tex_cache_.clear();

    if (header_tex_ != nullptr) {
        SDL_DestroyTexture(header_tex_);
        header_tex_ = nullptr;
    }
    if (footer_tex_ != nullptr) {
        SDL_DestroyTexture(footer_tex_);
        footer_tex_ = nullptr;
    }
}

// ── EvictDistantTextures ──────────────────────────────────────────────────────

void QdTextViewer::EvictDistantTextures() {
    const int keep_lo = scroll_top_ - CACHE_SLACK;
    const int keep_hi = scroll_top_ + VISIBLE_LINES + CACHE_SLACK;

    std::vector<int> to_remove;
    to_remove.reserve(8);
    for (auto &kv : line_tex_cache_) {
        if (kv.first < keep_lo || kv.first > keep_hi) {
            to_remove.push_back(kv.first);
        }
    }
    for (int idx : to_remove) {
        SDL_DestroyTexture(line_tex_cache_[idx]);
        line_tex_cache_.erase(idx);
    }
}

// ── Close ────────────────────────────────────────────────────────────────────

void QdTextViewer::Close() {
    open_ = false;
    FreeAllTextures();
    lines_.clear();
    raw_text_.clear();
    filename_.clear();
    scroll_top_ = 0;
    truncated_  = false;
    UL_LOG_INFO("qdesktop: QdTextViewer closed");
}

// ── BuildLines ───────────────────────────────────────────────────────────────
// Split raw_text_ by newline, then hard-wrap each physical line at WRAP_COLS.

void QdTextViewer::BuildLines() {
    lines_.clear();

    const char *src = raw_text_.c_str();
    const char *end = src + raw_text_.size();

    while (src < end) {
        // Find next newline.
        const char *nl = static_cast<const char *>(memchr(src, '\n', static_cast<size_t>(end - src)));
        const char *line_end = (nl != nullptr) ? nl : end;

        std::string physical(src, line_end);

        // Hard-wrap at WRAP_COLS.
        if (physical.empty()) {
            lines_.push_back("");
        } else {
            size_t pos = 0;
            while (pos < physical.size()) {
                size_t take = std::min(WRAP_COLS, physical.size() - pos);
                lines_.push_back(physical.substr(pos, take));
                pos += take;
            }
        }

        src = (nl != nullptr) ? (nl + 1) : end;
    }
}

// ── LoadFile ─────────────────────────────────────────────────────────────────

bool QdTextViewer::LoadFile(const char *path) {
    // Tear down previous state.
    FreeAllTextures();
    lines_.clear();
    raw_text_.clear();
    filename_.clear();
    scroll_top_ = 0;
    truncated_  = false;
    open_       = false;

    if (path == nullptr || path[0] == '\0') {
        UL_LOG_INFO("qdesktop: QdTextViewer::LoadFile: null/empty path");
        return false;
    }

    // Derive basename for the header.
    {
        const char *slash = strrchr(path, '/');
        filename_ = (slash != nullptr) ? (slash + 1) : path;
    }

    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        UL_LOG_INFO("qdesktop: QdTextViewer::LoadFile: fopen failed for '%s'", path);
        return false;
    }

    // Read up to MAX_READ_BYTES.
    raw_text_.resize(MAX_READ_BYTES);
    const size_t got = fread(raw_text_.data(), 1, MAX_READ_BYTES, f);

    // Check whether we hit the cap by trying to read one more byte.
    if (got == MAX_READ_BYTES) {
        char probe = 0;
        if (fread(&probe, 1, 1, f) == 1) {
            truncated_ = true;
        }
    }
    fclose(f);

    raw_text_.resize(got);

    // Replace any embedded NUL bytes with space to keep TTF happy.
    for (char &c : raw_text_) {
        if (c == '\0') {
            c = ' ';
        }
    }

    if (truncated_) {
        raw_text_ += "\n[OUTPUT TRUNCATED at 1 MiB]";
    }

    BuildLines();
    open_ = true;

    UL_LOG_INFO("qdesktop: QdTextViewer::LoadFile: '%s' %zu bytes %d lines truncated=%d",
                filename_.c_str(), got, static_cast<int>(lines_.size()), truncated_ ? 1 : 0);
    return true;
}

// ── EnsureLineTex ─────────────────────────────────────────────────────────────

SDL_Texture *QdTextViewer::EnsureLineTex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines_.size())) {
        return nullptr;
    }

    auto it = line_tex_cache_.find(idx);
    if (it != line_tex_cache_.end()) {
        return it->second;
    }

    const std::string &text = lines_[static_cast<size_t>(idx)];
    SDL_Texture *tex = nullptr;

    if (!text.empty()) {
        tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            text,
            theme_.text_primary,
            static_cast<u32>(CONTENT_W));
    }
    // tex may be nullptr for blank lines — that is intentional (skip render).
    line_tex_cache_[idx] = tex;
    return tex;
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdTextViewer::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                             const s32 origin_x, const s32 origin_y)
{
    if (!open_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const s32 abs_x = origin_x;
    const s32 abs_y = origin_y;

    // ── 1. Full-screen dark overlay (near-opaque) ─────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x08u, 0x08u, 0x12u, 0xF0u);  // 94% opacity
    SDL_Rect bg { abs_x, abs_y, VIEWER_W, VIEWER_H };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 2. Gutter background (slightly lighter strip) ─────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x18u, 0x18u, 0x2Cu, 0xE0u);
    SDL_Rect gutter_bg { abs_x, abs_y, GUTTER_W, VIEWER_H };
    SDL_RenderFillRect(r, &gutter_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 3. Header row: filename ───────────────────────────────────────────────
    if (header_tex_ == nullptr && !filename_.empty()) {
        const std::string header_text = filename_ + "  [B] Close  [ZL/ZR] Page  [Up/Down] Line";
        header_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            header_text,
            theme_.accent,
            static_cast<u32>(VIEWER_W - 16));
    }
    if (header_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(header_tex_, nullptr, nullptr, &hw, &hh);
        SDL_Rect hdst { abs_x + 8, abs_y + 4, hw, hh };
        SDL_RenderCopy(r, header_tex_, nullptr, &hdst);
    }

    // ── 4. Separator line under header ────────────────────────────────────────
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0x60u);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect sep { abs_x, abs_y + LINE_H + 4, VIEWER_W, 1 };
    SDL_RenderFillRect(r, &sep);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 5. Visible lines ──────────────────────────────────────────────────────
    // Content starts below header + separator.
    const s32 content_start_y = abs_y + LINE_H + 8;

    const int total_lines = static_cast<int>(lines_.size());

    // Evict textures for lines far from view before rendering.
    EvictDistantTextures();

    for (int i = 0; i < VISIBLE_LINES; ++i) {
        const int line_idx = scroll_top_ + i;
        if (line_idx >= total_lines) {
            break;
        }

        const s32 row_y = content_start_y + static_cast<s32>(i) * LINE_H;

        // ── 5a. Gutter: right-aligned line number (1-based) ─────────────────
        {
            const int display_num = line_idx + 1;
            char num_buf[16];
            // Format up to 5 characters, right-justified in gutter.
            snprintf(num_buf, sizeof(num_buf), "%5d", display_num);

            SDL_Texture *num_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(num_buf),
                theme_.text_secondary,
                static_cast<u32>(GUTTER_W - 4));
            if (num_tex != nullptr) {
                int nw = 0, nh = 0;
                SDL_QueryTexture(num_tex, nullptr, nullptr, &nw, &nh);
                // Right-align inside gutter (GUTTER_W - 4 is right edge).
                const s32 gx = abs_x + GUTTER_W - 4 - nw;
                const s32 gy = row_y + (LINE_H - nh) / 2;
                SDL_Rect ndst { gx, gy, nw, nh };
                SDL_RenderCopy(r, num_tex, nullptr, &ndst);
                SDL_DestroyTexture(num_tex);
            }
        }

        // ── 5b. Line body ──────────────────────────────────────────────────
        SDL_Texture *body_tex = EnsureLineTex(line_idx);
        if (body_tex != nullptr) {
            int bw = 0, bh = 0;
            SDL_QueryTexture(body_tex, nullptr, nullptr, &bw, &bh);
            const s32 clamped_bw = std::min(bw, CONTENT_W);
            const s32 bx = abs_x + CONTENT_X;
            const s32 by = row_y + (LINE_H - bh) / 2;
            const SDL_Rect bsrc { 0, 0, clamped_bw, bh };
            const SDL_Rect bdst { bx, by, clamped_bw, bh };
            SDL_RenderCopy(r, body_tex, &bsrc, &bdst);
        }
    }

    // ── 6. Scrollbar indicator ────────────────────────────────────────────────
    if (total_lines > VISIBLE_LINES) {
        const s32 bar_total_h = VIEWER_H - LINE_H - 16;
        const float frac_top = static_cast<float>(scroll_top_) /
                               static_cast<float>(total_lines);
        const float frac_size = static_cast<float>(VISIBLE_LINES) /
                                static_cast<float>(total_lines);
        const s32 bar_y = abs_y + LINE_H + 8 +
                          static_cast<s32>(frac_top * static_cast<float>(bar_total_h));
        const s32 bar_h = std::max(s32(8),
                          static_cast<s32>(frac_size * static_cast<float>(bar_total_h)));

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xA0u);
        SDL_Rect scroll_bar { abs_x + VIEWER_W - 6, bar_y, 4, bar_h };
        SDL_RenderFillRect(r, &scroll_bar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────
// B → Close (checked first, higher priority than ZL page-up).
// ZL → page up (VISIBLE_LINES lines).
// ZR → page down.
// D-pad Up → one line up.
// D-pad Down → one line down.

void QdTextViewer::OnInput(const u64 keys_down,
                            const u64 /*keys_up*/,
                            const u64 /*keys_held*/,
                            const pu::ui::TouchPoint /*touch_pos*/)
{
    if (!open_) {
        return;
    }

    const int total_lines = static_cast<int>(lines_.size());
    const int max_scroll  = std::max(0, total_lines - VISIBLE_LINES);

    // B closes — highest priority.
    if (keys_down & HidNpadButton_B) {
        Close();
        return;
    }

    // ZL = page up.
    if (keys_down & HidNpadButton_ZL) {
        scroll_top_ = std::max(0, scroll_top_ - VISIBLE_LINES);
        return;
    }

    // ZR = page down.
    if (keys_down & HidNpadButton_ZR) {
        scroll_top_ = std::min(max_scroll, scroll_top_ + VISIBLE_LINES);
        return;
    }

    // D-pad Up = one line up.
    if (keys_down & HidNpadButton_Up) {
        scroll_top_ = std::max(0, scroll_top_ - 1);
        return;
    }

    // D-pad Down = one line down.
    if (keys_down & HidNpadButton_Down) {
        scroll_top_ = std::min(max_scroll, scroll_top_ + 1);
        return;
    }
}

} // namespace ul::menu::qdesktop
