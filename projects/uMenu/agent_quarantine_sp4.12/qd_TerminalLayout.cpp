// qd_TerminalLayout.cpp — Implementation of live telemetry log tail (dock slot 1).
// Reads up to QD_TERMINAL_TAIL_BYTES from QD_TERMINAL_LOG_PATH on every Reload()
// call (called automatically every QD_TERMINAL_REFRESH_FRAMES frames).
// Word-wraps at QD_TERMINAL_WRAP_COLS and keeps at most QD_TERMINAL_MAX_LINES.
// Lazy per-line SDL_Texture* cache with CACHE_SLACK eviction window.
#include <ul/menu/qdesktop/qd_TerminalLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/ul_Log.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

// Global menu application instance (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdTerminalLayout::QdTerminalLayout(const QdTheme &theme)
    : theme_(theme)
{
    // Pre-load the log on construction so first frame has data.
    Reload();
}

QdTerminalLayout::~QdTerminalLayout() {
    FreeAllLineTextures();
    if (header_tex_) {
        SDL_DestroyTexture(header_tex_);
        header_tex_ = nullptr;
    }
    if (footer_tex_) {
        SDL_DestroyTexture(footer_tex_);
        footer_tex_ = nullptr;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void QdTerminalLayout::Reload() {
    // Destroy cached line textures and the static header/footer before
    // rebuilding lines_ from a fresh file read.
    FreeAllLineTextures();
    if (header_tex_) { SDL_DestroyTexture(header_tex_); header_tex_ = nullptr; }
    if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }

    FILE *f = fopen(QD_TERMINAL_LOG_PATH, "rb");
    if (f == nullptr) {
        file_missing_ = true;
        raw_buf_ = "(log file not found: ";
        raw_buf_ += QD_TERMINAL_LOG_PATH;
        raw_buf_ += ')';
        BuildLines();
        return;
    }
    file_missing_ = false;

    // Seek to end to get file size.
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        raw_buf_ = "(fseek SEEK_END failed)";
        BuildLines();
        return;
    }

    long file_sz = ftell(f);
    if (file_sz < 0) {
        fclose(f);
        raw_buf_ = "(ftell failed)";
        BuildLines();
        return;
    }

    // Seek back to read the last TAIL_BYTES (or the whole file if smaller).
    long read_start = 0;
    if (static_cast<size_t>(file_sz) > QD_TERMINAL_TAIL_BYTES) {
        read_start = file_sz - static_cast<long>(QD_TERMINAL_TAIL_BYTES);
    }
    if (fseek(f, read_start, SEEK_SET) != 0) {
        fclose(f);
        raw_buf_ = "(fseek SEEK_SET failed)";
        BuildLines();
        return;
    }

    const size_t bytes_to_read = static_cast<size_t>(file_sz - read_start);
    raw_buf_.resize(bytes_to_read);
    const size_t n = fread(&raw_buf_[0], 1, bytes_to_read, f);
    fclose(f);
    raw_buf_.resize(n);  // trim if read returned fewer bytes

    BuildLines();
}

void QdTerminalLayout::ResetAndOpen() {
    Reload();
    // Scroll to bottom (most recent).
    const int total = static_cast<int>(lines_.size());
    scroll_top_ = std::max(0, total - TERMINAL_VISIBLE_LINES);
    focus_line_ = scroll_top_;
}

// ── BuildLines ────────────────────────────────────────────────────────────────

void QdTerminalLayout::BuildLines() {
    lines_.clear();

    const char *src  = raw_buf_.data();
    const size_t len = raw_buf_.size();
    size_t pos = 0;

    while (pos < len) {
        // Find next newline or end-of-buffer.
        const char *nl = static_cast<const char *>(memchr(src + pos, '\n', len - pos));
        size_t seg_end = (nl != nullptr) ? static_cast<size_t>(nl - src) : len;

        // Extract this logical line segment.
        std::string seg(src + pos, seg_end - pos);
        // Strip trailing \r if present (Windows-style CRLF).
        if (!seg.empty() && seg.back() == '\r') {
            seg.pop_back();
        }

        // Hard-wrap at QD_TERMINAL_WRAP_COLS.
        if (seg.size() <= QD_TERMINAL_WRAP_COLS) {
            lines_.push_back(std::move(seg));
        } else {
            size_t offset = 0;
            while (offset < seg.size()) {
                const size_t chunk = std::min(QD_TERMINAL_WRAP_COLS, seg.size() - offset);
                lines_.push_back(seg.substr(offset, chunk));
                offset += chunk;
            }
        }

        pos = (nl != nullptr) ? seg_end + 1 : len;
    }

    // Drop oldest lines to stay within MAX_LINES.
    if (lines_.size() > QD_TERMINAL_MAX_LINES) {
        const size_t drop = lines_.size() - QD_TERMINAL_MAX_LINES;
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<ptrdiff_t>(drop));
    }

    ClampScroll();
}

// ── ClampScroll ───────────────────────────────────────────────────────────────

void QdTerminalLayout::ClampScroll() {
    const int total = static_cast<int>(lines_.size());
    const int max_top = std::max(0, total - TERMINAL_VISIBLE_LINES);
    if (scroll_top_ > max_top) scroll_top_ = max_top;
    if (scroll_top_ < 0)       scroll_top_ = 0;
    if (focus_line_ < scroll_top_) focus_line_ = scroll_top_;
    const int vis_bot = scroll_top_ + TERMINAL_VISIBLE_LINES - 1;
    if (focus_line_ > vis_bot) focus_line_ = vis_bot;
    if (focus_line_ >= total)  focus_line_ = std::max(0, total - 1);
}

// ── Line texture cache ────────────────────────────────────────────────────────

SDL_Texture *QdTerminalLayout::EnsureLineTex(int idx) {
    auto it = line_tex_cache_.find(idx);
    if (it != line_tex_cache_.end()) {
        return it->second;
    }
    if (idx < 0 || static_cast<size_t>(idx) >= lines_.size()) {
        return nullptr;
    }
    const std::string &text = lines_[static_cast<size_t>(idx)];
    SDL_Texture *tex = nullptr;
    if (!text.empty()) {
        tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            text,
            theme_.text_primary,
            static_cast<u32>(1920 - TERMINAL_GUTTER_W - 16));
    }
    line_tex_cache_[idx] = tex;
    return tex;
}

void QdTerminalLayout::EvictDistantTextures() {
    const int keep_lo = scroll_top_ - LINE_CACHE_SLACK;
    const int keep_hi = scroll_top_ + TERMINAL_VISIBLE_LINES + LINE_CACHE_SLACK;
    for (auto it = line_tex_cache_.begin(); it != line_tex_cache_.end(); ) {
        if (it->first < keep_lo || it->first > keep_hi) {
            if (it->second != nullptr) {
                SDL_DestroyTexture(it->second);
            }
            it = line_tex_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void QdTerminalLayout::FreeAllLineTextures() {
    for (auto &kv : line_tex_cache_) {
        if (kv.second != nullptr) {
            SDL_DestroyTexture(kv.second);
        }
    }
    line_tex_cache_.clear();
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdTerminalLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                 const s32 origin_x, const s32 origin_y)
{
    // Auto-refresh from disk.
    ++frame_counter_;
    if (frame_counter_ >= QD_TERMINAL_REFRESH_FRAMES) {
        frame_counter_ = 0;
        Reload();
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const s32 ax = origin_x;
    const s32 ay = origin_y;

    // ── 1. Full-screen dark overlay ───────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x06u, 0x06u, 0x10u, 0xF4u);
    SDL_Rect bg { ax, ay, 1920, 1080 };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 2. Header bar ─────────────────────────────────────────────────────────
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g,
                           theme_.topbar_bg.b, 0xF0u);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Rect hbar { ax, ay, 1920, TERMINAL_HEADER_H };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (header_tex_ == nullptr) {
        const int total = static_cast<int>(lines_.size());
        char hbuf[256];
        snprintf(hbuf, sizeof(hbuf),
                 "Terminal — %s  [%d lines]  [Up/Down] Scroll  [L/R] ±10  [ZL/ZR] Page  [Y] Reload  [A] Copy  [B] Back",
                 QD_TERMINAL_LOG_PATH, total);
        header_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string(hbuf),
            file_missing_ ? theme_.button_close : theme_.accent,
            static_cast<u32>(1920 - 16));
    }
    if (header_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(header_tex_, nullptr, nullptr, &hw, &hh);
        SDL_Rect hdst { ax + 8, ay + (TERMINAL_HEADER_H - hh) / 2, hw, hh };
        SDL_RenderCopy(r, header_tex_, nullptr, &hdst);
    }

    // ── 3. Gutter background ──────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x10u, 0x10u, 0x22u, 0xC0u);
    SDL_Rect gutter_bg { ax, ay + TERMINAL_HEADER_H, TERMINAL_GUTTER_W,
                         1080 - TERMINAL_HEADER_H - TERMINAL_FOOTER_H };
    SDL_RenderFillRect(r, &gutter_bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 4. Visible lines ──────────────────────────────────────────────────────
    EvictDistantTextures();
    const int total = static_cast<int>(lines_.size());
    const s32 content_x = ax + TERMINAL_GUTTER_W + 8;
    const s32 content_w = 1920 - TERMINAL_GUTTER_W - 16;
    const s32 content_start_y = ay + TERMINAL_HEADER_H + 4;

    for (int i = 0; i < TERMINAL_VISIBLE_LINES; ++i) {
        const int line_idx = scroll_top_ + i;
        if (line_idx >= total) {
            break;
        }
        const s32 row_y = content_start_y + static_cast<s32>(i) * TERMINAL_LINE_H;

        // 4a. Focus highlight.
        if (line_idx == focus_line_) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r,
                theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0x28u);
            SDL_Rect hl { ax + TERMINAL_GUTTER_W, row_y, 1920 - TERMINAL_GUTTER_W, TERMINAL_LINE_H };
            SDL_RenderFillRect(r, &hl);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }

        // 4b. Gutter: right-aligned line number.
        {
            char num_buf[16];
            snprintf(num_buf, sizeof(num_buf), "%4d", line_idx + 1);
            SDL_Texture *num_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(num_buf),
                theme_.text_secondary,
                static_cast<u32>(TERMINAL_GUTTER_W - 4));
            if (num_tex != nullptr) {
                int nw = 0, nh = 0;
                SDL_QueryTexture(num_tex, nullptr, nullptr, &nw, &nh);
                const s32 gx = ax + TERMINAL_GUTTER_W - 4 - nw;
                const s32 gy = row_y + (TERMINAL_LINE_H - nh) / 2;
                SDL_Rect ndst { gx, gy, nw, nh };
                SDL_RenderCopy(r, num_tex, nullptr, &ndst);
                SDL_DestroyTexture(num_tex);
            }
        }

        // 4c. Line body.
        SDL_Texture *body_tex = EnsureLineTex(line_idx);
        if (body_tex != nullptr) {
            int bw = 0, bh = 0;
            SDL_QueryTexture(body_tex, nullptr, nullptr, &bw, &bh);
            const s32 clamped_w = std::min(bw, content_w);
            SDL_Rect src_clip { 0, 0, clamped_w, bh };
            const s32 cy = row_y + (TERMINAL_LINE_H - bh) / 2;
            SDL_Rect dst { content_x, cy, clamped_w, bh };
            SDL_RenderCopy(r, body_tex, &src_clip, &dst);
        }
    }

    // ── 5. Footer / status bar ────────────────────────────────────────────────
    const s32 footer_y = ay + 1080 - TERMINAL_FOOTER_H;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g,
                           theme_.topbar_bg.b, 0xD0u);
    SDL_Rect fbar { ax, footer_y, 1920, TERMINAL_FOOTER_H };
    SDL_RenderFillRect(r, &fbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (footer_tex_ == nullptr) {
        char fbuf[256];
        snprintf(fbuf, sizeof(fbuf),
                 "Line %d / %d    Scroll %d    [A] Copy focused line to clipboard",
                 focus_line_ + 1, total, scroll_top_);
        footer_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string(fbuf),
            theme_.text_secondary,
            static_cast<u32>(1920 - 16));
    }
    if (footer_tex_ != nullptr) {
        int fw = 0, fh = 0;
        SDL_QueryTexture(footer_tex_, nullptr, nullptr, &fw, &fh);
        SDL_Rect fdst { ax + 8, footer_y + (TERMINAL_FOOTER_H - fh) / 2, fw, fh };
        SDL_RenderCopy(r, footer_tex_, nullptr, &fdst);
    }

    // ── 6. Scrollbar ─────────────────────────────────────────────────────────
    if (total > TERMINAL_VISIBLE_LINES) {
        const s32 sb_x = ax + 1920 - 6;
        const s32 sb_h = 1080 - TERMINAL_HEADER_H - TERMINAL_FOOTER_H;
        const s32 sb_y = ay + TERMINAL_HEADER_H;

        // Track.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x30u, 0x30u, 0x50u, 0xA0u);
        SDL_Rect sb_track { sb_x, sb_y, 4, sb_h };
        SDL_RenderFillRect(r, &sb_track);

        // Thumb.
        const float frac_start = static_cast<float>(scroll_top_) / static_cast<float>(total);
        const float frac_end   = static_cast<float>(scroll_top_ + TERMINAL_VISIBLE_LINES)
                                  / static_cast<float>(total);
        const s32 thumb_y = sb_y + static_cast<s32>(frac_start * static_cast<float>(sb_h));
        const s32 thumb_h = std::max(8, static_cast<s32>((frac_end - frac_start) * static_cast<float>(sb_h)));
        SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xC0u);
        SDL_Rect sb_thumb { sb_x, thumb_y, 4, thumb_h };
        SDL_RenderFillRect(r, &sb_thumb);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdTerminalLayout::OnInput(const u64 keys_down, const u64 /*keys_up*/,
                                const u64 /*keys_held*/,
                                const pu::ui::TouchPoint /*touch_pos*/)
{
    const int total = static_cast<int>(lines_.size());

    if (keys_down & HidNpadButton_AnyDown) {
        scroll_top_++;
        focus_line_++;
        ClampScroll();
        // Invalidate footer (line position changed).
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_AnyUp) {
        scroll_top_--;
        focus_line_--;
        ClampScroll();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_L) {
        scroll_top_  = std::max(0, scroll_top_  - 10);
        focus_line_  = std::max(0, focus_line_  - 10);
        ClampScroll();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_R) {
        scroll_top_  = std::min(std::max(0, total - TERMINAL_VISIBLE_LINES), scroll_top_  + 10);
        focus_line_  = std::min(total - 1, focus_line_ + 10);
        ClampScroll();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_ZL) {
        scroll_top_  = std::max(0, scroll_top_  - TERMINAL_VISIBLE_LINES);
        focus_line_  = std::max(0, focus_line_  - TERMINAL_VISIBLE_LINES);
        ClampScroll();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_ZR) {
        scroll_top_  = std::min(std::max(0, total - TERMINAL_VISIBLE_LINES),
                                scroll_top_  + TERMINAL_VISIBLE_LINES);
        focus_line_  = std::min(total - 1, focus_line_ + TERMINAL_VISIBLE_LINES);
        ClampScroll();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
    }
    if (keys_down & HidNpadButton_Y) {
        // Force reload — also resets the header texture so it picks up new line count.
        Reload();
        if (footer_tex_) { SDL_DestroyTexture(footer_tex_); footer_tex_ = nullptr; }
        UL_LOG_INFO("qdesktop:terminal: manual reload triggered");
    }
    if (keys_down & HidNpadButton_A) {
        // Copy focused line to notification toast.
        if (focus_line_ >= 0 && static_cast<size_t>(focus_line_) < lines_.size()) {
            const std::string &line = lines_[static_cast<size_t>(focus_line_)];
            if (g_MenuApplication) {
                g_MenuApplication->ShowNotification(line.empty() ? "(empty line)" : line);
            }
        }
    }
    if (keys_down & HidNpadButton_B) {
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
    }
}

} // namespace ul::menu::qdesktop
