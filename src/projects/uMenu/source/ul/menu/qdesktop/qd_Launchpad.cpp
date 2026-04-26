// qd_Launchpad.cpp — Full-screen app-grid overlay for Q OS uMenu (v1.0.0).
// Ported from tools/mock-nro-desktop-gui/src/launchpad.rs (v1.1.0).
//
// Integration note:
//   QdDesktopIconsElement::icons_ is a private member.  This .cpp uses a
//   friend-declaration approach to read icons_ directly.  To enable this, add
//   the following line to qd_DesktopIcons.hpp, inside the
//   QdDesktopIconsElement class declaration (private section):
//
//     friend class QdLaunchpadElement;
//
//   This is the minimal, correct approach — the Launchpad and DesktopIcons are
//   intentionally tightly coupled (Launchpad is a subordinate view of the same
//   data model).  The alternative (adding a public GetIcon(size_t) accessor) is
//   equally valid; in that case replace the direct icons_[] accesses below with
//   calls to that accessor.

#include <ul/menu/qdesktop/qd_Launchpad.hpp>
#include <ul/ul_Result.hpp>                         // UL_LOG_INFO
#include <pu/ui/render/render_Renderer.hpp>          // pu::ui::render::GetMainRenderer
#include <pu/ui/ui_Types.hpp>                        // pu::ui::GetDefaultFont / DefaultFontSize

#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>

// libnx HID constants.
#include <switch.h>

namespace ul::menu::qdesktop {

// ── Constructor ───────────────────────────────────────────────────────────────

QdLaunchpadElement::QdLaunchpadElement(const QdTheme &theme)
    : theme_(theme),
      is_open_(false),
      pending_launch_(false),
      desktop_icons_ptr_(nullptr),
      focus_filtered_(0),
      filter_dirty_(false),
      frame_tick_(0),
      icon_cache_(nullptr)
{
    // items_, filtered_idxs_, query_ default-initialise to empty.
    // Texture vectors start empty — slots are pushed in Open().
}

// ── Destructor ────────────────────────────────────────────────────────────────

QdLaunchpadElement::~QdLaunchpadElement() {
    FreeAllTextures();
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdLaunchpadElement::AdvanceTick() {
    ++frame_tick_;
}

// ── Open ─────────────────────────────────────────────────────────────────────
//
// Snapshot the current icon list from QdDesktopIconsElement.  The icons_ array
// is private; this implementation uses the friend declaration described at the
// top of this file.  Sort Application entries alpha-first, NROs alpha-second,
// and Builtins in dock_slot order.

void QdLaunchpadElement::Open(QdDesktopIconsElement *desktop_icons) {
    if (!desktop_icons) {
        return;
    }

    // Free textures from any previous open cycle before overwriting items_.
    FreeAllTextures();
    items_.clear();
    filtered_idxs_.clear();
    query_.clear();
    focus_filtered_ = 0;
    pending_launch_  = false;
    filter_dirty_    = false;
    desktop_icons_ptr_ = desktop_icons;  // retained until Close() so DispatchPendingLaunch can fire
    icon_cache_      = &desktop_icons->cache_;

    // Deep-copy every icon entry into items_.
    // Uses the friend-declared access to icons_[] and icon_count_.
    const size_t n = desktop_icons->icon_count_;
    items_.reserve(n);

    for (size_t i = 0u; i < n; ++i) {
        const NroEntry &src = desktop_icons->icons_[i];
        LpItem it;

        // Copy fields with explicit null-termination safety.
        strncpy(it.name,      src.name,      sizeof(it.name)      - 1u);
        it.name[sizeof(it.name) - 1u] = '\0';

        it.glyph  = src.glyph;
        it.bg_r   = src.bg_r;
        it.bg_g   = src.bg_g;
        it.bg_b   = src.bg_b;

        strncpy(it.nro_path,  src.nro_path,  sizeof(it.nro_path)  - 1u);
        it.nro_path[sizeof(it.nro_path) - 1u] = '\0';

        strncpy(it.icon_path, src.icon_path, sizeof(it.icon_path) - 1u);
        it.icon_path[sizeof(it.icon_path) - 1u] = '\0';

        it.app_id       = src.app_id;
        it.is_builtin   = src.is_builtin;
        it.dock_slot    = src.dock_slot;
        it.icon_category = src.icon_category;

        // Map IconCategory to LpSortKind for the grid ordering pass.
        switch (src.icon_category) {
            case IconCategory::Nintendo:  it.sort_kind = LpSortKind::Nintendo;  break;
            case IconCategory::Homebrew:  it.sort_kind = LpSortKind::Homebrew;  break;
            case IconCategory::Extras:    it.sort_kind = LpSortKind::Extras;    break;
            case IconCategory::Payloads:  it.sort_kind = LpSortKind::Extras;    break;
            case IconCategory::Builtin:   it.sort_kind = LpSortKind::Builtin;   break;
        }

        it.desktop_idx = i;  // preserve back-reference for FocusedDesktopIdx()
        items_.push_back(it);
    }

    // Sort: Nintendo (alpha) -> Homebrew (alpha) -> Extras (alpha) ->
    //       Builtin (dock_slot order).
    // std::stable_sort preserves original order within equal-key groups, so
    // builtins retain their dock_slot ordering from the construction pass.
    std::stable_sort(items_.begin(), items_.end(),
        [](const LpItem &a, const LpItem &b) -> bool {
            // Primary: LpSortKind ascending (Nintendo=0, Homebrew=1, Extras=2, Builtin=3).
            if (a.sort_kind != b.sort_kind) {
                return static_cast<u8>(a.sort_kind) < static_cast<u8>(b.sort_kind);
            }
            // Secondary: within Builtin, order by dock_slot.
            if (a.sort_kind == LpSortKind::Builtin) {
                return a.dock_slot < b.dock_slot;
            }
            // Secondary: within Nintendo/Homebrew/Extras, sort alpha (case-insensitive).
            const char *na = a.name;
            const char *nb = b.name;
            while (*na && *nb) {
                const int ca = std::tolower(static_cast<unsigned char>(*na));
                const int cb = std::tolower(static_cast<unsigned char>(*nb));
                if (ca != cb) {
                    return ca < cb;
                }
                ++na; ++nb;
            }
            return *na == '\0' && *nb != '\0';
        }
    );

    // Pre-size per-slot texture vectors to items_.size() with nullptr / false.
    const size_t sz = items_.size();
    name_tex_.assign(sz, nullptr);
    glyph_tex_.assign(sz, nullptr);
    icon_tex_.assign(sz, nullptr);
    icon_loaded_.assign(sz, false);

    // Build the initial (unfiltered) filtered index list.
    RebuildFilter();

    is_open_ = true;

    // Count by category for the log line.
    size_t nintendo_count = 0u, homebrew_count = 0u,
           extras_count = 0u, builtin_count = 0u;
    for (const LpItem &it : items_) {
        switch (it.sort_kind) {
            case LpSortKind::Nintendo:  ++nintendo_count;  break;
            case LpSortKind::Homebrew:  ++homebrew_count;  break;
            case LpSortKind::Extras:    ++extras_count;    break;
            case LpSortKind::Builtin:   ++builtin_count;   break;
        }
    }
    UL_LOG_INFO("qdesktop: Launchpad opened -- nintendo=%zu homebrew=%zu extras=%zu builtins=%zu total=%zu",
                nintendo_count, homebrew_count, extras_count, builtin_count, sz);
}

// ── Close ─────────────────────────────────────────────────────────────────────

void QdLaunchpadElement::Close() {
    // Free every cached SDL texture before clearing items_ — the vectors must
    // still be alive while FreeAllTextures walks them.
    FreeAllTextures();

    items_.clear();
    filtered_idxs_.clear();
    query_.clear();
    focus_filtered_    = 0;
    pending_launch_    = false;
    filter_dirty_      = false;
    icon_cache_        = nullptr;
    desktop_icons_ptr_ = nullptr;
    is_open_           = false;

    UL_LOG_INFO("qdesktop: Launchpad closed");
}

// ── DispatchPendingLaunch ────────────────────────────────────────────────────
//
// Fires the launch for the currently focused item by forwarding to
// QdDesktopIconsElement::LaunchIcon. The friend declaration on
// QdDesktopIconsElement (see qd_DesktopIcons.hpp) grants access to the private
// LaunchIcon entry point; no public widening of the desktop icons API is
// required.
//
// Safe to call when the Launchpad is closed, when the focused index is
// invalid, or when desktop_icons_ptr_ is null. In any of those cases the
// function is a no-op so the host can call it unconditionally after seeing
// IsPendingLaunch() return true.

void QdLaunchpadElement::DispatchPendingLaunch() {
    if (desktop_icons_ptr_ == nullptr) {
        UL_LOG_WARN("qdesktop: Launchpad DispatchPendingLaunch: desktop_icons_ptr_ is null");
        return;
    }
    const size_t idx = FocusedDesktopIdx();
    if (idx == SIZE_MAX) {
        UL_LOG_WARN("qdesktop: Launchpad DispatchPendingLaunch: no valid focused idx");
        return;
    }
    UL_LOG_INFO("qdesktop: Launchpad DispatchPendingLaunch idx=%zu", idx);
    desktop_icons_ptr_->LaunchIcon(idx);
}

// ── PushQueryChar / PopQueryChar / ClearQuery ─────────────────────────────────

void QdLaunchpadElement::PushQueryChar(char c) {
    query_ += c;
    filter_dirty_ = true;
    // Rebuild now so FilteredCount() is accurate before OnRender.
    RebuildFilter();
    // Clamp focus to the new (possibly shorter) filtered set.
    const size_t n = FilteredCount();
    if (n == 0u) {
        focus_filtered_ = 0u;
    } else if (focus_filtered_ >= n) {
        focus_filtered_ = n - 1u;
    }
}

void QdLaunchpadElement::PopQueryChar() {
    if (!query_.empty()) {
        query_.pop_back();
        filter_dirty_ = true;
        RebuildFilter();
        const size_t n = FilteredCount();
        if (n == 0u) {
            focus_filtered_ = 0u;
        } else if (focus_filtered_ >= n) {
            focus_filtered_ = n - 1u;
        }
    }
}

void QdLaunchpadElement::ClearQuery() {
    query_.clear();
    filter_dirty_ = true;
    RebuildFilter();
    focus_filtered_ = 0u;
}

// ── FocusedDesktopIdx ─────────────────────────────────────────────────────────

size_t QdLaunchpadElement::FocusedDesktopIdx() const {
    if (!is_open_ || filtered_idxs_.empty()) {
        return SIZE_MAX;
    }
    if (focus_filtered_ >= filtered_idxs_.size()) {
        return SIZE_MAX;
    }
    const size_t item_idx = filtered_idxs_[focus_filtered_];
    if (item_idx >= items_.size()) {
        return SIZE_MAX;
    }
    return items_[item_idx].desktop_idx;
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdLaunchpadElement::OnInput(u64 keys_down, u64 /*keys_up*/, u64 /*keys_held*/,
                                  pu::ui::TouchPoint /*touch_pos*/)
{
    if (!is_open_) {
        return;
    }

    // Clear the pending-launch flag at the top of each input frame so the host
    // sees a fresh edge-triggered signal from A/ZR.
    pending_launch_ = false;

    // ── B / Plus: close ───────────────────────────────────────────────────────
    if ((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
        Close();
        SetVisible(false);
        return;
    }

    const size_t n = FilteredCount();

    // ── StickL: backspace on query ────────────────────────────────────────────
    if (keys_down & HidNpadButton_StickL) {
        PopQueryChar();
        // After filter change, re-read n for navigation below.
        return;
    }

    if (n == 0u) {
        return;  // Nothing to navigate.
    }

    // ── D-pad navigation (clamp at edges — no wrapping, per spec) ────────────
    if (keys_down & HidNpadButton_Up) {
        if (focus_filtered_ >= static_cast<size_t>(LP_COLS)) {
            focus_filtered_ -= static_cast<size_t>(LP_COLS);
        } else {
            focus_filtered_ = 0u;
        }
    }
    if (keys_down & HidNpadButton_Down) {
        const size_t stepped = focus_filtered_ + static_cast<size_t>(LP_COLS);
        focus_filtered_ = (stepped < n) ? stepped : (n - 1u);
    }
    if (keys_down & HidNpadButton_Left) {
        if (focus_filtered_ > 0u) {
            focus_filtered_ -= 1u;
        }
    }
    if (keys_down & HidNpadButton_Right) {
        if (focus_filtered_ + 1u < n) {
            focus_filtered_ += 1u;
        }
    }

    // ── A / ZR: launch focused item ───────────────────────────────────────────
    if ((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
        if (focus_filtered_ < n) {
            pending_launch_ = true;
            // Do NOT call Close() here — the host must first call
            // desktop_icons->LaunchIcon(FocusedDesktopIdx()), then Close().
            // We mark the intent; the host dispatches on IsPendingLaunch().
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdLaunchpadElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                   s32 /*x*/, s32 /*y*/)
{
    if (!is_open_) {
        return;
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (!r) {
        return;
    }

    // Rebuild the filter if anything changed since the last frame.
    if (filter_dirty_) {
        RebuildFilter();
        filter_dirty_ = false;
    }

    // ── 1. Full-screen opaque background ──────────────────────────────────────
    // topbar_bg = (0x0C, 0x0C, 0x20), matching the Launchpad spec and the
    // Rust paint_launchpad fill_rect call.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r,
        theme_.topbar_bg.r,
        theme_.topbar_bg.g,
        theme_.topbar_bg.b,
        0xFFu);
    SDL_Rect full { 0, 0, 1920, 1080 };
    SDL_RenderFillRect(r, &full);

    // ── 2. Hot-corner widget (top-left 60×48 px launcher button) ─────────────
    // Draws a slightly lighter rectangle so the user can see the tap target.
    SDL_SetRenderDrawColor(r,
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.r + 0x18)),
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.g + 0x18)),
        static_cast<u8>(std::min(255, (int)theme_.topbar_bg.b + 0x18)),
        0xFFu);
    SDL_Rect hc { 0, 0, LP_HOTCORNER_W, LP_HOTCORNER_H };
    SDL_RenderFillRect(r, &hc);
    // 1px accent border on the right and bottom of the hot-corner.
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_Rect hcbr { LP_HOTCORNER_W - 1, 0, 1, LP_HOTCORNER_H };
    SDL_Rect hcbb { 0, LP_HOTCORNER_H - 1, LP_HOTCORNER_W, 1 };
    SDL_RenderFillRect(r, &hcbr);
    SDL_RenderFillRect(r, &hcbb);
    // Render "Q" glyph inside hot-corner to indicate Launchpad.
    {
        static SDL_Texture *hc_tex = nullptr;
        if (!hc_tex) {
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            hc_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "Q", wh);
        }
        if (hc_tex) {
            int tw = 0, th = 0;
            SDL_QueryTexture(hc_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect td { (LP_HOTCORNER_W - tw) / 2, (LP_HOTCORNER_H - th) / 2, tw, th };
            SDL_RenderCopy(r, hc_tex, nullptr, &td);
        }
    }

    // ── 3. Search bar ─────────────────────────────────────────────────────────
    // Background: surface_glass (0x12, 0x12, 0x2A) with 80% alpha.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r,
        theme_.surface_glass.g,
        theme_.surface_glass.b,
        0xCCu);
    SDL_Rect search_bg { LP_SEARCH_BAR_X, LP_SEARCH_BAR_Y,
                         LP_SEARCH_BAR_W, LP_SEARCH_BAR_H };
    SDL_RenderFillRect(r, &search_bg);

    // 1px accent border around the search bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g, theme_.accent.b, 0xFFu);
    SDL_Rect search_ring { LP_SEARCH_BAR_X - 1, LP_SEARCH_BAR_Y - 1,
                           LP_SEARCH_BAR_W + 2, LP_SEARCH_BAR_H + 2 };
    SDL_RenderDrawRect(r, &search_ring);

    // Render search query text + blinking caret.
    {
        std::string display_text = query_;
        // Caret blinks at 30-frame phase: visible when (frame_tick_ / 30) % 2 == 0.
        const bool caret_visible = ((frame_tick_ / 30) % 2) == 0;
        if (caret_visible) {
            display_text += '|';
        }

        if (!display_text.empty()) {
            // Re-render every frame (query text can change).
            const pu::ui::Color tc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            SDL_Texture *qt = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                display_text, tc,
                static_cast<u32>(LP_SEARCH_BAR_W - 16));
            if (qt) {
                int tw = 0, th = 0;
                SDL_QueryTexture(qt, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, qt, nullptr, &td);
                SDL_DestroyTexture(qt);
            }
        } else if (caret_visible) {
            // Empty query: render just the caret at the left edge of the bar.
            const pu::ui::Color tc { 0xE0u, 0xE0u, 0xF0u, 0xFFu };
            SDL_Texture *ct = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "|", tc);
            if (ct) {
                int tw = 0, th = 0;
                SDL_QueryTexture(ct, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, ct, nullptr, &td);
                SDL_DestroyTexture(ct);
            }
        }

        // Placeholder hint when query is empty and caret is hidden.
        if (query_.empty() && !caret_visible) {
            const pu::ui::Color hint_col { 0x88u, 0x88u, 0xAAu, 0xFFu };
            SDL_Texture *ht = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "Search...", hint_col);
            if (ht) {
                int tw = 0, th = 0;
                SDL_QueryTexture(ht, nullptr, nullptr, &tw, &th);
                const s32 ty = LP_SEARCH_BAR_Y + (LP_SEARCH_BAR_H - th) / 2;
                SDL_Rect td { LP_SEARCH_BAR_X + 8, ty, tw, th };
                SDL_RenderCopy(r, ht, nullptr, &td);
                SDL_DestroyTexture(ht);
            }
        }
    }

    // ── 4. Section headers ────────────────────────────────────────────────────
    // Walk the filtered list and emit a section label wherever LpSortKind
    // transitions.  The first row header sits at y = LP_GRID_Y - 24 above the
    // first cell row; subsequent headers are emitted inline (painted in empty
    // band above each new section row).
    //
    // We collect the unique set of kinds present in the filtered list, then
    // render a header label 24 px above the top of the first cell for that
    // kind.  The section header for the first kind is always at LP_GRID_Y - 24.
    {
        LpSortKind last_section = static_cast<LpSortKind>(0xFFu); // sentinel
        const size_t nf = filtered_idxs_.size();
        for (size_t vpos = 0u; vpos < nf; ++vpos) {
            const size_t item_idx = filtered_idxs_[vpos];
            if (item_idx >= items_.size()) continue;
            const LpItem &it = items_[item_idx];
            if (it.sort_kind == last_section) continue;

            last_section = it.sort_kind;

            // Compute the cell row for this visual position.
            s32 cx = 0, cy = 0;
            CellXY(vpos, cx, cy);
            const s32 header_y = cy - 28; // 28 px above the cell top

            const char *label = SectionLabel(it.sort_kind);
            const pu::ui::Color sc { theme_.text_secondary.r, theme_.text_secondary.g,
                                     theme_.text_secondary.b, 0xFFu };
            SDL_Texture *st = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                label, sc);
            if (st) {
                int sw = 0, sh = 0;
                SDL_QueryTexture(st, nullptr, nullptr, &sw, &sh);
                SDL_Rect sd { LP_GRID_X, header_y, sw, sh };
                SDL_RenderCopy(r, st, nullptr, &sd);
                SDL_DestroyTexture(st);
            }
        }
    }

    // ── 5. Icon grid ──────────────────────────────────────────────────────────
    const size_t nf = filtered_idxs_.size();
    for (size_t vpos = 0u; vpos < nf; ++vpos) {
        const size_t item_idx = filtered_idxs_[vpos];
        if (item_idx >= items_.size()) { continue; }

        s32 cx = 0, cy = 0;
        CellXY(vpos, cx, cy);

        // Cull cells that would render below the status line (1080 - 48 = 1032).
        if (cy + LP_CELL_H > 1032) { continue; }

        PaintCell(r, items_[item_idx], item_idx, cx, cy,
                  (vpos == focus_filtered_));
    }

    // ── 6. Status line ────────────────────────────────────────────────────────
    size_t nintendo_count = 0u, homebrew_count = 0u,
           extras_count = 0u, builtin_count = 0u;
    for (const LpItem &it : items_) {
        switch (it.sort_kind) {
            case LpSortKind::Nintendo:  ++nintendo_count;  break;
            case LpSortKind::Homebrew:  ++homebrew_count;  break;
            case LpSortKind::Extras:    ++extras_count;    break;
            case LpSortKind::Builtin:   ++builtin_count;   break;
        }
    }
    PaintStatusLine(r, nintendo_count, homebrew_count, extras_count, builtin_count);
}

// ── RebuildFilter ─────────────────────────────────────────────────────────────

void QdLaunchpadElement::RebuildFilter() {
    filtered_idxs_.clear();
    if (query_.empty()) {
        // No query: all items are visible.
        filtered_idxs_.reserve(items_.size());
        for (size_t i = 0u; i < items_.size(); ++i) {
            filtered_idxs_.push_back(i);
        }
    } else {
        // Case-insensitive ASCII substring search.
        // Mirrors launchpad.rs Launchpad::filtered_indices():
        //   q = query.to_ascii_lowercase()
        //   item.name.to_ascii_lowercase().contains(&q[..])
        char q_lower[64];
        const size_t qlen = std::min(query_.size(), sizeof(q_lower) - 1u);
        for (size_t i = 0u; i < qlen; ++i) {
            q_lower[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(query_[i])));
        }
        q_lower[qlen] = '\0';

        for (size_t i = 0u; i < items_.size(); ++i) {
            const char *name = items_[i].name;
            // Build lower-cased version of the item name.
            char name_lower[64];
            const size_t nlen = std::min(strnlen(name, sizeof(items_[i].name)),
                                         sizeof(name_lower) - 1u);
            for (size_t j = 0u; j < nlen; ++j) {
                name_lower[j] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(name[j])));
            }
            name_lower[nlen] = '\0';

            // Substring search — strstr on the lowercased pair.
            if (strstr(name_lower, q_lower) != nullptr) {
                filtered_idxs_.push_back(i);
            }
        }
    }
    filter_dirty_ = false;
}

// ── FilteredCount ─────────────────────────────────────────────────────────────

size_t QdLaunchpadElement::FilteredCount() const {
    return filtered_idxs_.size();
}

// ── CellXY ────────────────────────────────────────────────────────────────────
// Compute the top-left screen pixel of the grid cell at visual position vpos.
// Matches lp_cell_xy() from launchpad.rs (scaled ×1.5 to 1920×1080).
//
//   col = vpos % LP_COLS
//   row = vpos / LP_COLS
//   x   = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X)
//   y   = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y)

// static
void QdLaunchpadElement::CellXY(size_t vpos, s32 &out_x, s32 &out_y) {
    const s32 col = static_cast<s32>(vpos % static_cast<size_t>(LP_COLS));
    const s32 row = static_cast<s32>(vpos / static_cast<size_t>(LP_COLS));
    out_x = LP_GRID_X + col * (LP_CELL_W + LP_GAP_X);
    out_y = LP_GRID_Y + row * (LP_CELL_H + LP_GAP_Y);
}

// ── PaintCell ────────────────────────────────────────────────────────────────
// Paints one grid cell at (cx, cy) using the same pattern as
// QdDesktopIconsElement::PaintIconCell, adapted for the Launchpad's square
// LP_ICON_W × LP_ICON_H icon art area.
//
// Layout within the LP_CELL_W × LP_CELL_H cell:
//   icon art rect: (cx + (LP_CELL_W - LP_ICON_W)/2, cy, LP_ICON_W, LP_ICON_H)
//   name label:    centred horizontally, 4 px below icon art bottom.
//   focus ring:    1px ring 1px outside the icon art rect.

void QdLaunchpadElement::PaintCell(SDL_Renderer *r,
                                    const LpItem &item,
                                    size_t item_idx,
                                    s32 cx, s32 cy,
                                    bool is_focused)
{
    // Centre the icon art horizontally within the cell.
    const s32 icon_x = cx + (LP_CELL_W - LP_ICON_W) / 2;
    const s32 icon_y = cy;

    // ── 1. Background fill ────────────────────────────────────────────────────
    const u8 fill_r = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_r + 40))
        : item.bg_r;
    const u8 fill_g = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_g + 40))
        : item.bg_g;
    const u8 fill_b = is_focused
        ? static_cast<u8>(std::min(255, (int)item.bg_b + 40))
        : item.bg_b;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, fill_r, fill_g, fill_b, 0xFFu);
    SDL_Rect bg_rect { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
    SDL_RenderFillRect(r, &bg_rect);

    // ── 2. Icon texture ───────────────────────────────────────────────────────
    // Determine the cache key (same selection logic as DesktopIcons).
    const char *cache_key = nullptr;
    if (item.icon_path[0] != '\0') {
        cache_key = item.icon_path;
    } else if (item.nro_path[0] != '\0') {
        cache_key = item.nro_path;
    }

    const u8 *bgra = nullptr;
    if (cache_key && icon_cache_) {
        bgra = icon_cache_->Get(cache_key);
    }

    if (bgra != nullptr && item_idx < icon_tex_.size()) {
        // Lazily create the icon texture for this slot.
        if (!icon_loaded_[item_idx] || icon_tex_[item_idx] == nullptr) {
            if (icon_tex_[item_idx] != nullptr) {
                SDL_DestroyTexture(icon_tex_[item_idx]);
                icon_tex_[item_idx] = nullptr;
            }
            icon_tex_[item_idx] = SDL_CreateTexture(
                r, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,
                static_cast<int>(CACHE_ICON_W),
                static_cast<int>(CACHE_ICON_H));
            if (icon_tex_[item_idx] != nullptr) {
                SDL_UpdateTexture(icon_tex_[item_idx], nullptr, bgra,
                                  static_cast<int>(CACHE_ICON_W) * 4);
            }
            icon_loaded_[item_idx] = true;
        }
        if (icon_tex_[item_idx] != nullptr) {
            SDL_Rect dst { icon_x, icon_y, LP_ICON_W, LP_ICON_H };
            SDL_RenderCopy(r, icon_tex_[item_idx], nullptr, &dst);
        }
    }

    // ── 3. Glyph fallback (when no icon art) ─────────────────────────────────
    if (bgra == nullptr && item.glyph != '\0' && item_idx < glyph_tex_.size()) {
        if (glyph_tex_[item_idx] == nullptr) {
            const std::string gs(1, item.glyph);
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            glyph_tex_[item_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                gs, wh);
        }
        if (glyph_tex_[item_idx] != nullptr) {
            int gw = 0, gh = 0;
            SDL_QueryTexture(glyph_tex_[item_idx], nullptr, nullptr, &gw, &gh);
            SDL_Rect gdst {
                icon_x + (LP_ICON_W - gw) / 2,
                icon_y + (LP_ICON_H - gh) / 2,
                gw, gh
            };
            SDL_RenderCopy(r, glyph_tex_[item_idx], nullptr, &gdst);
        }
    }

    // ── 4. Name label ─────────────────────────────────────────────────────────
    if (item.name[0] != '\0' && item_idx < name_tex_.size()) {
        if (name_tex_[item_idx] == nullptr) {
            // Truncate long names with ellipsis (max 14 chars visible).
            char display[20];
            const size_t name_len = strnlen(item.name, sizeof(item.name));
            if (name_len > 14u) {
                memcpy(display, item.name, 11u);
                display[11] = '.'; display[12] = '.'; display[13] = '.';
                display[14] = '\0';
            } else {
                memcpy(display, item.name, name_len);
                display[name_len] = '\0';
            }
            const pu::ui::Color nc { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            name_tex_[item_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                std::string(display), nc,
                static_cast<u32>(LP_CELL_W));
        }
        if (name_tex_[item_idx] != nullptr) {
            int nw = 0, nh = 0;
            SDL_QueryTexture(name_tex_[item_idx], nullptr, nullptr, &nw, &nh);
            SDL_Rect ndst {
                cx + (LP_CELL_W - nw) / 2,
                icon_y + LP_ICON_H + 4,
                nw, nh
            };
            SDL_RenderCopy(r, name_tex_[item_idx], nullptr, &ndst);
        }
    }

    // ── 5. Focus ring ─────────────────────────────────────────────────────────
    if (is_focused) {
        SDL_SetRenderDrawColor(r,
            theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0xFFu);
        SDL_Rect ring { icon_x - 2, icon_y - 2, LP_ICON_W + 4, LP_ICON_H + 4 };
        SDL_RenderDrawRect(r, &ring);
        // Second ring (thicker visual) one pixel inside.
        SDL_Rect ring2 { icon_x - 1, icon_y - 1, LP_ICON_W + 2, LP_ICON_H + 2 };
        SDL_RenderDrawRect(r, &ring2);
    }
}

// ── FreeSlotTextures ──────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeSlotTextures(size_t item_idx) {
    auto free_if = [](SDL_Texture *&t) {
        if (t != nullptr) {
            SDL_DestroyTexture(t);
            t = nullptr;
        }
    };
    if (item_idx < name_tex_.size())   { free_if(name_tex_[item_idx]);  }
    if (item_idx < glyph_tex_.size())  { free_if(glyph_tex_[item_idx]); }
    if (item_idx < icon_tex_.size())   { free_if(icon_tex_[item_idx]);  }
    if (item_idx < icon_loaded_.size()) { icon_loaded_[item_idx] = false; }
}

// ── FreeAllTextures ───────────────────────────────────────────────────────────

void QdLaunchpadElement::FreeAllTextures() {
    for (size_t i = 0u; i < name_tex_.size(); ++i)  {
        if (name_tex_[i])  { SDL_DestroyTexture(name_tex_[i]);  name_tex_[i]  = nullptr; }
    }
    for (size_t i = 0u; i < glyph_tex_.size(); ++i) {
        if (glyph_tex_[i]) { SDL_DestroyTexture(glyph_tex_[i]); glyph_tex_[i] = nullptr; }
    }
    for (size_t i = 0u; i < icon_tex_.size(); ++i)  {
        if (icon_tex_[i])  { SDL_DestroyTexture(icon_tex_[i]);  icon_tex_[i]  = nullptr; }
    }
    name_tex_.clear();
    glyph_tex_.clear();
    icon_tex_.clear();
    icon_loaded_.clear();
}

// ── SectionLabel ─────────────────────────────────────────────────────────────

// static
const char *QdLaunchpadElement::SectionLabel(LpSortKind kind) {
    switch (kind) {
        case LpSortKind::Nintendo:  return "Nintendo";
        case LpSortKind::Homebrew:  return "Homebrew";
        case LpSortKind::Extras:    return "Extras";
        case LpSortKind::Builtin:   return "Built-in";
    }
    return "Other";
}

// ── PaintStatusLine ───────────────────────────────────────────────────────────
// Renders a status string at the bottom of the overlay (y ~= 1048).
// Format: "N nintendo  N homebrew  N extras  N built-in  |  B to close"

void QdLaunchpadElement::PaintStatusLine(SDL_Renderer *r,
                                          size_t total_nintendo,
                                          size_t total_homebrew,
                                          size_t total_extras,
                                          size_t total_builtins) const
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%zu nintendo  %zu homebrew  %zu extras  %zu built-in  |  B to close",
             total_nintendo, total_homebrew, total_extras, total_builtins);

    const pu::ui::Color sc { theme_.text_secondary.r, theme_.text_secondary.g,
                              theme_.text_secondary.b, 0xFFu };
    SDL_Texture *st = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        buf, sc);
    if (st) {
        int sw = 0, sh = 0;
        SDL_QueryTexture(st, nullptr, nullptr, &sw, &sh);
        // Centre horizontally; 8 px above the bottom edge.
        const s32 sx = (1920 - sw) / 2;
        const s32 sy = 1080 - sh - 8;
        SDL_Rect sd { sx, sy, sw, sh };
        SDL_RenderCopy(r, st, nullptr, &sd);
        SDL_DestroyTexture(st);
    }
}

} // namespace ul::menu::qdesktop
