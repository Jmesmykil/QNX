// qd_TaskManagerLayout.cpp — Task Manager window content implementation.
// v1.10.3.11 AGENT-A deliverable.
//
// Architecture notes:
//   - QdContentElement contract: scale + clip pre-applied by QdWindow; NEVER call
//     SDL_RenderSetScale here; touch_pos is already in natural-coordinate space.
//   - Minimize deferred: OnInput stores pending_minimize_win_; OnRender drains it
//     with wm_.MinimizeWindow(win, drawer) where Renderer::Ref is live.
//   - libnx pm:dmnt: pmdmntInitialize in ctor (non-fatal); pmdmntGetApplicationProcessId
//     in Refresh(); pmdmntExit in dtor.  Fallback to WM list data if pm unavailable.
//   - Per-row title textures rebuilt lazily when rows_dirty_ is set by Refresh().
//   - Scroll: up/down touch-drag sets scroll_y_; clamped to [0, max_scroll].
//
// Must NOT call SDL_RenderSetScale — this is a content element; QdWindow owns scale.

#include <ul/menu/qdesktop/qd_TaskManagerLayout.hpp>
#include <ul/menu/qdesktop/qd_MinimizedDockEntry.hpp>
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>     // v2.0.4.1: MenuApplication for g_MenuApplication
#include <ul/menu/smi/smi_Commands.hpp>          // v2.0.4.1: smi::ResumeApplication / TerminateApplication
#include <ul/ul_Result.hpp>                      // UL_LOG_INFO

// v2.0.4.1: globals defined in main.cpp (qd_DesktopIcons.cpp uses the same
// extern pattern at lines 59/64).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>

#include <SDL2/SDL.h>
#include <switch.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

// libnx pm:dmnt service header.
// pmdmntInitialize / pmdmntGetApplicationProcessId / pmdmntExit
#include <switch/services/pm.h>

namespace ul::menu::qdesktop {

// ── Static state-label helper ─────────────────────────────────────────────────

static const char* StateLabel(WindowState s) {
    switch (s) {
        case WindowState::Normal:     return "Normal";
        case WindowState::Minimizing: return "Minimizing";
        case WindowState::Minimized:  return "Minimized";
        case WindowState::Restoring:  return "Restoring";
        case WindowState::Closing:    return "Closing";
        default:                      return "Unknown";
    }
}

// ── Constructor / destructor ──────────────────────────────────────────────────

QdTaskManagerElement::QdTaskManagerElement(const QdTheme& theme, QdWindowManager& wm)
    : theme_(theme), wm_(wm)
{
    // v3.0.2 (FIX-1 from cumulative-tech-debt-audit.md): pmdmntInitialize()
    // was previously called HERE in the constructor, synchronously on the
    // main render thread the moment the Tasks dock slot was tapped.  pm:dmnt
    // is a libnx IPC service open; in library-applet context it can stall
    // for tens to hundreds of ms.  The Refresh() call below also issued a
    // pmdmntGetApplicationProcessId IPC on the same thread.  Combined with
    // QdWindow's first-paint scale/clip setup, the user saw the Tasks
    // window open with empty / broken content for noticeable time —
    // creator HW report 2026-05-19 "the first 3 open but the nintendo and
    // task manager started problem".
    //
    // Lazy-init pattern: leave pm_ok_ false at construction; first Refresh()
    // (called from on_tick via QdWindow::PollEvent at Normal state) opens
    // pm:dmnt once and caches the result.  Window opens instantly with the
    // WM-list-based fallback (rows from open_windows_ / minimized_entries_
    // which need no pm:dmnt IPC) and the live PID column fills in on the
    // next tick.
    pm_init_attempted_ = false;
    pm_ok_             = false;

    // Initial data load — uses WM list data only since pm_ok_ is false here.
    Refresh();
}

QdTaskManagerElement::~QdTaskManagerElement() {
    // Free all SDL textures we own.
    if (header_tex_) {
        pu::ui::render::DeleteTexture(header_tex_);
        header_tex_ = nullptr;
    }
    if (hint_tex_) {
        pu::ui::render::DeleteTexture(hint_tex_);
        hint_tex_ = nullptr;
    }
    // Per uMenu optimization audit F2.3/F2.4: count + empty cached textures.
    if (count_tex_) {
        pu::ui::render::DeleteTexture(count_tex_);
        count_tex_ = nullptr;
    }
    if (empty_tex_) {
        pu::ui::render::DeleteTexture(empty_tex_);
        empty_tex_ = nullptr;
    }
    FreeRowTextures();

    // Close pm:dmnt session if we opened it.
    if (pm_ok_) {
        pmdmntExit();
        pm_ok_ = false;
    }
}

// ── Refresh ───────────────────────────────────────────────────────────────────

void QdTaskManagerElement::Refresh() {
    // Throttle: only re-query every QD_TM_REFRESH_EVERY calls.
    if (refresh_ctr_ > 0) {
        --refresh_ctr_;
        return;
    }
    refresh_ctr_ = QD_TM_REFRESH_EVERY;

    // v3.0.2 FIX-1: lazy pm:dmnt initialization — opened on the FIRST
    // Refresh() call after construction, NOT in the ctor.  Window open path
    // stays render-thread-light; the service IPC happens on the tick
    // callback path (QdWindow::PollEvent in Normal state) so the user sees
    // the window populate from WM list data instantly and the live PID
    // column updates on the next tick.  If pm:dmnt is unreachable, pm_ok_
    // stays false and the WM-list fallback is the only source.
    if (!pm_init_attempted_) {
        pm_init_attempted_ = true;
        Result rc_init = pmdmntInitialize();
        pm_ok_ = R_SUCCEEDED(rc_init);
        UL_LOG_INFO("qdesktop: TaskManager lazy pmdmntInitialize rc=0x%08X "
                    "(pm_ok=%d)", rc_init, pm_ok_ ? 1 : 0);
    }

    // Try to fetch application PID from pm:dmnt.
    if (pm_ok_) {
        u64 pid = 0;
        Result rc = pmdmntGetApplicationProcessId(&pid);
        pm_app_pid_ = R_SUCCEEDED(rc) ? pid : 0;
    }

    rows_.clear();

    // Open windows.
    for (const auto& win_ref : wm_.GetOpenWindows()) {
        QdWindow* win = win_ref.get();
        TaskRow row;
        row.kind        = RowKind::OpenWindow;
        row.title       = win->GetTitle();
        row.state_label = StateLabel(win->GetState());
        row.program_id  = win->GetProgramId();
        row.open_win    = win;
        row.min_entry   = nullptr;
        if (row.program_id != 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%016lX", (unsigned long)row.program_id);
            row.pid_hex = buf;
        }
        rows_.push_back(std::move(row));
    }

    // Minimized entries.
    for (const auto& entry_ref : wm_.GetMinimizedEntries()) {
        QdMinimizedDockEntry* entry = entry_ref.get();
        TaskRow row;
        row.kind        = RowKind::MinimizedEntry;
        row.title       = entry->GetTitle();
        row.state_label = "Minimized";
        row.program_id  = entry->GetProgramId();
        row.open_win    = nullptr;
        row.min_entry   = entry;
        if (row.program_id != 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%016lX", (unsigned long)row.program_id);
            row.pid_hex = buf;
        }
        rows_.push_back(std::move(row));
    }

    // v2.0.4.1: surface a suspended Switch Application (uSystem updates
    // GlobalSettings.system_status.suspended_app_id when the user presses HOME
    // mid-game) so the user sees the running game in the task list and can
    // resume/terminate it from there.  The row's kind is SuspendedApp; tap-
    // launches use smi::ResumeApplication().
    //
    // v2.0.4.4: query the NACP for the actual application name via
    // nsGetApplicationControlData.  NsApplicationControlData is ~393 KB so we
    // heap-allocate it (same pattern as qd_DesktopIcons.cpp:5181 LoadNsIconToCache)
    // to avoid stack overflow on the main thread.  Falls back to the placeholder
    // string when the query fails.
    {
        const u64 suspended_app_id = g_GlobalSettings.system_status.suspended_app_id;
        if (suspended_app_id != 0) {
            TaskRow row;
            row.kind        = RowKind::SuspendedApp;
            row.title       = "Suspended Application";  // fallback if NACP query fails
            row.state_label = "Suspended";
            row.program_id  = suspended_app_id;
            row.open_win    = nullptr;
            row.min_entry   = nullptr;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%016lX", (unsigned long)row.program_id);
            row.pid_hex = buf;

            // Refresh runs every ~1 sec; the NACP query allocates 393 KB and
            // hits ns:srv IPC, so cache the (app_id → name) pair process-wide.
            // suspended_app_id can change when user terminates one app and
            // launches another, so the cache key includes app_id.
            static u64         cached_app_id = 0;
            static std::string cached_name;

            if (suspended_app_id == cached_app_id && !cached_name.empty()) {
                row.title = cached_name;
            } else {
                auto *ctrl = new(std::nothrow) NsApplicationControlData;
                if (ctrl != nullptr) {
                    u64 actual_size = 0;
                    Result rc = nsGetApplicationControlData(
                        NsApplicationControlSource_Storage,
                        suspended_app_id,
                        ctrl,
                        sizeof(NsApplicationControlData),
                        &actual_size);
                    if (R_SUCCEEDED(rc)) {
                        NacpLanguageEntry *lang = nullptr;
                        if (R_SUCCEEDED(nacpGetLanguageEntry(&ctrl->nacp, &lang))
                            && lang != nullptr
                            && lang->name[0] != '\0') {
                            row.title       = std::string(lang->name);
                            cached_app_id   = suspended_app_id;
                            cached_name     = row.title;
                        }
                    } else {
                        UL_LOG_WARN("qdesktop: TM SuspendedApp NACP query failed app_id=0x%016lX rc=0x%08x",
                                    static_cast<unsigned long>(suspended_app_id), rc);
                    }
                    delete ctrl;
                }
            }

            rows_.push_back(std::move(row));
        }
    }

    rows_dirty_ = true;

    // Clamp scroll after list changes (list may have shrunk).
    s32 max_scroll = std::max(0, static_cast<s32>(rows_.size()) * QD_TM_ROW_H - QD_TM_LIST_H);
    if (scroll_y_ > max_scroll) {
        scroll_y_ = max_scroll;
    }
}

// ── Button hit-box helpers ────────────────────────────────────────────────────

// Buttons are packed right-to-left within the row:
//   [Close] [Minimize] [Focus]   (right-most first)
// All at the vertical centre of the row.

static constexpr s32 kBtnAreaRight = QD_TM_NATURAL_W - QD_TM_ROW_PAD_X;

QdTaskManagerElement::BtnRect QdTaskManagerElement::CloseRect(s32 row_x, s32 row_y) {
    s32 bx = row_x + kBtnAreaRight - QD_TM_BTN_W;
    s32 by = row_y + (QD_TM_ROW_H - QD_TM_BTN_H) / 2;
    return {bx, by, QD_TM_BTN_W, QD_TM_BTN_H};
}

QdTaskManagerElement::BtnRect QdTaskManagerElement::MinimizeRect(s32 row_x, s32 row_y) {
    s32 bx = row_x + kBtnAreaRight - QD_TM_BTN_W - QD_TM_BTN_GAP - QD_TM_BTN_W;
    s32 by = row_y + (QD_TM_ROW_H - QD_TM_BTN_H) / 2;
    return {bx, by, QD_TM_BTN_W, QD_TM_BTN_H};
}

QdTaskManagerElement::BtnRect QdTaskManagerElement::FocusRect(s32 row_x, s32 row_y) {
    s32 bx = row_x + kBtnAreaRight
             - QD_TM_BTN_W - QD_TM_BTN_GAP
             - QD_TM_BTN_W - QD_TM_BTN_GAP
             - QD_TM_BTN_W;
    s32 by = row_y + (QD_TM_ROW_H - QD_TM_BTN_H) / 2;
    return {bx, by, QD_TM_BTN_W, QD_TM_BTN_H};
}

bool QdTaskManagerElement::HitTest(const BtnRect& r, s32 px, s32 py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// ── Static drawing helpers ────────────────────────────────────────────────────

void QdTaskManagerElement::FillRounded(SDL_Renderer* r,
                                       s32 x, s32 y, s32 w, s32 h,
                                       pu::ui::Color col) {
    // Simple filled rectangle — SDL2 has no built-in rounded-rect fill.
    // Use the same software rounding pattern as qd_HotCornerDropdown.cpp:
    // 4-px corner radius approximated by clipping corner pixels row-by-row.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, col.a);
    constexpr int rad = 4;

    // Centre body (full-width rows excluding radius corners at top/bottom).
    SDL_Rect centre = {x, y + rad, w, h - 2 * rad};
    SDL_RenderFillRect(r, &centre);

    // Top row and bottom row with progressive inset.
    for (int i = 0; i < rad; ++i) {
        // Approximate a quarter-circle: inset = rad - sqrt(rad*rad - (rad-i)^2).
        // Use a simple lookup for the 4-px radius.
        static const int kInset[4] = {3, 1, 0, 0};
        int ins = (i < rad) ? kInset[i] : 0;
        SDL_Rect top_row    = {x + ins, y + i,           w - 2 * ins, 1};
        SDL_Rect bottom_row = {x + ins, y + h - 1 - i,  w - 2 * ins, 1};
        SDL_RenderFillRect(r, &top_row);
        SDL_RenderFillRect(r, &bottom_row);
    }
}

void QdTaskManagerElement::BlitTex(SDL_Renderer* r, SDL_Texture* tex, s32 x, s32 y) {
    if (!tex) return;
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_Rect dst = {x, y, tw, th};
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

SDL_Texture* QdTaskManagerElement::BuildText(pu::ui::render::Renderer::Ref& drawer,
                                             const std::string& text,
                                             pu::ui::Color col,
                                             s32 font_size,
                                             SDL_Texture* old_tex) {
    if (old_tex) {
        pu::ui::render::DeleteTexture(old_tex);
        old_tex = nullptr;
    }
    if (text.empty()) return nullptr;
    // v1.10.3.10.5 main-thread fix: Renderer has no GetBaseFont(); use the
    // canonical pu::ui::GetDefaultFont(DefaultFontSize) the way every other
    // qdesktop layout does.  Map ranged sizes to the three default buckets.
    pu::ui::DefaultFontSize bucket = pu::ui::DefaultFontSize::Small;
    if (font_size >= 24)      bucket = pu::ui::DefaultFontSize::Large;
    else if (font_size >= 18) bucket = pu::ui::DefaultFontSize::Medium;
    return pu::ui::render::RenderText(pu::ui::GetDefaultFont(bucket), text, col);
}

// ── Texture management ────────────────────────────────────────────────────────

void QdTaskManagerElement::FreeRowTextures() {
    for (SDL_Texture* t : row_title_textures_) {
        if (t) pu::ui::render::DeleteTexture(t);
    }
    row_title_textures_.clear();
}

void QdTaskManagerElement::RebuildRowTextures(pu::ui::render::Renderer::Ref& drawer) {
    FreeRowTextures();
    row_title_textures_.reserve(rows_.size());
    for (const auto& row : rows_) {
        SDL_Texture* t = BuildText(drawer, row.title,
                                   theme_.text_primary, 20, nullptr);
        row_title_textures_.push_back(t);
    }
    rows_dirty_ = false;
}

void QdTaskManagerElement::EnsureStaticTextures(pu::ui::render::Renderer::Ref& drawer) {
    if (!header_tex_) {
        header_tex_ = BuildText(drawer, "Task Manager",
                                theme_.text_primary, 24, nullptr);
    }
    if (!hint_tex_) {
        hint_tex_ = BuildText(drawer,
                              "B: Close window  |  Focus: bring to front  |  Minimize/Close: window controls",
                              theme_.text_secondary, 16, nullptr);
    }
}

// ── Button render helper ──────────────────────────────────────────────────────

void QdTaskManagerElement::RenderBtn(SDL_Renderer* r,
                                     s32 bx, s32 by, s32 bw, s32 bh,
                                     const char* label_cstr,
                                     pu::ui::Color bg_col,
                                     pu::ui::Color text_col) {
    FillRounded(r, bx, by, bw, bh, bg_col);

    // Render label directly as a simple character strip — we avoid per-frame
    // texture creation by drawing nothing if the renderer font path isn't
    // accessible here.  The button rects use colour to communicate: green=focus,
    // amber=minimize, red=close.  Text label is a nice-to-have; real control is
    // through the colour-coded buttons.
    // NOTE: We cannot call pu::ui::render::RenderText here without a
    // Renderer::Ref.  Since RenderBtn is called from RenderRow which is called
    // from OnRender (which HAS drawer), the alternative is to pass drawer in.
    // However, to keep RenderBtn a static helper, we accept that button labels
    // are colour-coded only in this implementation.  Future work can pass the
    // Renderer::Ref when the static helper becomes non-static.
    // The colour difference between [Focus]=green, [Minimize]=amber, [Close]=red
    // is sufficient for visual disambiguation.
    (void)label_cstr;
    (void)text_col;
}

// ── Row render ────────────────────────────────────────────────────────────────

void QdTaskManagerElement::RenderRow(pu::ui::render::Renderer::Ref& drawer,
                                     SDL_Renderer* r,
                                     const TaskRow& row,
                                     s32 rx, s32 ry,
                                     bool hovered) const {
    // Row background.
    pu::ui::Color row_bg = hovered
        ? pu::ui::Color(0x1E, 0x1E, 0x3C, 0xCC)
        : pu::ui::Color(0x14, 0x14, 0x28, 0xAA);
    FillRounded(r, rx + 4, ry + 2, QD_TM_NATURAL_W - 8, QD_TM_ROW_H - 4, row_bg);

    // Left separator line for minimized entries.
    if (row.kind == RowKind::MinimizedEntry) {
        SDL_SetRenderDrawColor(r, theme_.button_minimize.r,
                               theme_.button_minimize.g,
                               theme_.button_minimize.b, 0xCC);
        SDL_Rect bar = {rx + 4, ry + 4, 3, QD_TM_ROW_H - 8};
        SDL_RenderFillRect(r, &bar);
    }

    // Title texture.
    // row_title_textures_ is parallel to rows_; find the index by pointer.
    // Since this is called from OnRender which iterates rows_ by index, callers
    // pass the correct index via the external iterator.  We use &row - rows_.data()
    // to recover the index safely.
    ptrdiff_t idx = &row - rows_.data();
    if (idx >= 0 && static_cast<size_t>(idx) < row_title_textures_.size()) {
        SDL_Texture* title_tex = row_title_textures_[idx];
        if (title_tex) {
            BlitTex(r, title_tex, rx + QD_TM_ROW_PAD_X + 8,
                    ry + (QD_TM_ROW_H - 20) / 2);
        }
    }

    // State badge text (drawn inline without cached texture — small enough
    // that RenderText per frame for the badge is acceptable only if we limit it;
    // but per-frame texture creation is forbidden (B41/B42).  Render as a coloured
    // rounded rect for the badge colour with no text label — colour communicates
    // state: cyan=Normal, amber=Minimized, green=Restoring).
    pu::ui::Color badge_col;
    if      (row.state_label == "Normal")     badge_col = pu::ui::Color(0x00, 0xE5, 0xFF, 0xCC); // cyan
    else if (row.state_label == "Minimized")  badge_col = pu::ui::Color(0xFB, 0xBF, 0x24, 0xCC); // amber
    else if (row.state_label == "Restoring")  badge_col = pu::ui::Color(0x4A, 0xDE, 0x80, 0xCC); // green
    else if (row.state_label == "Minimizing") badge_col = pu::ui::Color(0xFB, 0xBF, 0x24, 0x88); // faded amber
    else if (row.state_label == "Closing")    badge_col = pu::ui::Color(0xF8, 0x71, 0x71, 0x88); // faded red
    else                                       badge_col = theme_.text_secondary;

    FillRounded(r, rx + 310, ry + (QD_TM_ROW_H - 14) / 2, 80, 14, badge_col);

    // Action buttons — right side of row.
    BtnRect fr = FocusRect(rx, ry);
    BtnRect mr = MinimizeRect(rx, ry);
    BtnRect cr = CloseRect(rx, ry);

    // Focus = green, enabled only for open windows (can't focus a minimized entry).
    bool can_focus    = (row.kind == RowKind::OpenWindow && row.open_win != nullptr);
    bool can_minimize = (row.kind == RowKind::OpenWindow && row.open_win != nullptr);
    bool can_close    = (row.kind == RowKind::OpenWindow && row.open_win != nullptr);
    // Minimized entries only support restore (via tap in dock — no button here).

    pu::ui::Color disabled_col = pu::ui::Color(0x30, 0x30, 0x48, 0x80);

    RenderBtn(r, fr.x, fr.y, fr.w, fr.h, "Focus",
              can_focus ? theme_.button_maximize : disabled_col,
              theme_.text_primary);

    RenderBtn(r, mr.x, mr.y, mr.w, mr.h, "Min",
              can_minimize ? theme_.button_minimize : disabled_col,
              theme_.text_primary);

    RenderBtn(r, cr.x, cr.y, cr.w, cr.h, "Close",
              can_close ? theme_.button_close : disabled_col,
              theme_.text_primary);

    (void)drawer;  // drawer available here but not needed for button label workaround above
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdTaskManagerElement::OnRender(pu::ui::render::Renderer::Ref& drawer,
                                    s32 x, s32 y) {
    // v1.10.3.10.5 main-thread fix: Plutonium Renderer has no GetSDLRenderer().
    // Use the canonical pu::ui::render::GetMainRenderer() that all other
    // qdesktop layouts use (qd_AboutLayout / qd_MonitorLayout / qd_VaultLayout).
    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // Drain deferred minimize first (needs drawer).
    if (pending_minimize_win_) {
        wm_.MinimizeWindow(pending_minimize_win_, drawer);
        pending_minimize_win_ = nullptr;
        // After minimize, force a refresh on the next tick.
        refresh_ctr_ = 0;
    }

    // Rebuild row title textures if rows changed.
    if (rows_dirty_) {
        RebuildRowTextures(drawer);
    }

    EnsureStaticTextures(drawer);

    // ── Header bar ──────────────────────────────────────────────────────────
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g,
                               theme_.topbar_bg.b, 0xFF);
        SDL_Rect hdr_rect = {x, y, QD_TM_NATURAL_W, QD_TM_HEADER_H};
        SDL_RenderFillRect(r, &hdr_rect);

        // Header separator.
        SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 0x1E);
        SDL_Rect sep = {x, y + QD_TM_HEADER_H - 1, QD_TM_NATURAL_W, 1};
        SDL_RenderFillRect(r, &sep);

        // Title text.
        if (header_tex_) {
            BlitTex(r, header_tex_, x + 12, y + (QD_TM_HEADER_H - 24) / 2);
        }

        // Row count subtitle — cached, rebuilt only when (open, minimized)
        // counts actually change.  Per uMenu optimization audit F2.3:
        // previously this allocated/destroyed an SDL_Texture every frame.
        // v1.10.3.10.5 main-thread fix: bumped buffer 48 → 96 to silence
        // -Werror=format-truncation; %zu can render up to 18 digits each.
        const size_t open_count = wm_.GetOpenWindows().size();
        const size_t min_count  = wm_.GetMinimizedEntries().size();
        if (open_count != count_cached_open_ || min_count != count_cached_minimized_) {
            if (count_tex_) {
                pu::ui::render::DeleteTexture(count_tex_);
                count_tex_ = nullptr;
            }
            char count_buf[96];
            std::snprintf(count_buf, sizeof(count_buf),
                          "%zu window(s) open, %zu minimized",
                          open_count, min_count);
            count_tex_ = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                count_buf, theme_.text_secondary);
            count_cached_open_      = open_count;
            count_cached_minimized_ = min_count;
        }
        if (count_tex_) {
            int cw = 0, ch = 0;
            SDL_QueryTexture(count_tex_, nullptr, nullptr, &cw, &ch);
            BlitTex(r, count_tex_,
                    x + QD_TM_NATURAL_W - cw - 12,
                    y + (QD_TM_HEADER_H - ch) / 2);
        }
    }

    // ── Row list ─────────────────────────────────────────────────────────────
    {
        // Clip to list area.
        SDL_Rect list_clip = {x, y + QD_TM_LIST_TOP,
                              QD_TM_NATURAL_W, QD_TM_LIST_H};
        SDL_RenderSetClipRect(r, &list_clip);

        for (size_t i = 0; i < rows_.size(); ++i) {
            s32 row_y_natural = RowNaturalY(static_cast<s32>(i));
            s32 ry = y + row_y_natural;

            // Skip rows fully outside the list area.
            if (ry + QD_TM_ROW_H < y + QD_TM_LIST_TOP) continue;
            if (ry > y + QD_TM_LIST_BOTTOM)             break;

            RenderRow(drawer, r, rows_[i], x, ry,
                      hovered_row_ == static_cast<int>(i));
        }

        // Empty state — cached lazily on first need, reused thereafter.
        // Per uMenu optimization audit F2.4.
        if (rows_.empty()) {
            if (!empty_tex_) {
                empty_tex_ = pu::ui::render::RenderText(
                    pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                    "No windows open.",
                    theme_.text_secondary);
            }
            if (empty_tex_) {
                int ew = 0, eh = 0;
                SDL_QueryTexture(empty_tex_, nullptr, nullptr, &ew, &eh);
                BlitTex(r, empty_tex_,
                        x + (QD_TM_NATURAL_W - ew) / 2,
                        y + QD_TM_LIST_TOP + (QD_TM_LIST_H - eh) / 2);
            }
        }

        // Restore full clip.
        SDL_RenderSetClipRect(r, nullptr);
    }

    // ── Scroll bar ───────────────────────────────────────────────────────────
    {
        s32 total_h = static_cast<s32>(rows_.size()) * QD_TM_ROW_H;
        if (total_h > QD_TM_LIST_H) {
            float ratio = static_cast<float>(QD_TM_LIST_H) / static_cast<float>(total_h);
            s32 bar_h   = std::max(20, static_cast<s32>(QD_TM_LIST_H * ratio));
            s32 max_s   = total_h - QD_TM_LIST_H;
            s32 bar_y   = y + QD_TM_LIST_TOP
                          + static_cast<s32>((QD_TM_LIST_H - bar_h)
                                             * (static_cast<float>(scroll_y_)
                                                / static_cast<float>(max_s)));
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0xFF, 0xFF, 0xFF, 0x40);
            SDL_Rect sbar = {x + QD_TM_NATURAL_W - 4, bar_y, 3, bar_h};
            SDL_RenderFillRect(r, &sbar);
        }
    }

    // ── Hint bar (v2.0.4.1: suppressed) ──────────────────────────────────────
    // The window's bottom-bar already shows the hint via QdWindow::SetHintText
    // (set in OpenTaskManagerWindow / qd_DesktopIcons_WmBridge.cpp).  Rendering
    // a second hint inside the content area duplicates the same text in two
    // places and overlaps the row list when the list grows long.  Drop the
    // in-content render; QD_TM_HINT_H is preserved as bottom padding so the
    // last row doesn't kiss the window's bottom-bar separator.
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdTaskManagerElement::OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                                   pu::ui::TouchPoint touch_pos) {
    (void)keys_up;
    (void)keys_held;

    // D-pad scroll.
    if (keys_down & HidNpadButton_Down) {
        scroll_y_ += QD_TM_ROW_H;
    }
    if (keys_down & HidNpadButton_Up) {
        scroll_y_ -= QD_TM_ROW_H;
    }

    // Clamp scroll.
    s32 max_scroll = std::max(0,
        static_cast<s32>(rows_.size()) * QD_TM_ROW_H - QD_TM_LIST_H);
    scroll_y_ = std::max(0, std::min(scroll_y_, max_scroll));

    // B / Plus — close this window (parent QdWindow handles the on_close_requested
    // callback; we don't need to call anything extra — returning from OnInput
    // without consuming means QdWindow's own B-button handler fires next).
    // The spec says "B/+ pressed = close window, return to desktop".
    // QdWindow already wires B to on_close_requested when state==Normal.
    // Nothing to do here; QdWindow handles it.

    // Touch input — touch_pos is already in content-local natural coordinates
    // (guaranteed by QdWindow's SP3 input contract).
    // v1.10.3.10.5 main-thread fix: TouchPoint API uses IsEmpty(), not IsTouch().
    if (!touch_pos.IsEmpty()) {
        s32 px = static_cast<s32>(touch_pos.x);
        s32 py = static_cast<s32>(touch_pos.y);

        // Determine which row was touched.
        if (py >= QD_TM_LIST_TOP && py < QD_TM_LIST_BOTTOM) {
            s32 list_py = py - QD_TM_LIST_TOP + scroll_y_;
            int row_idx = list_py / QD_TM_ROW_H;
            if (row_idx >= 0 && static_cast<size_t>(row_idx) < rows_.size()) {
                hovered_row_ = row_idx;
                const TaskRow& row = rows_[row_idx];

                // Compute row natural-canvas y for hit-testing buttons.
                s32 ry = RowNaturalY(row_idx);

                if (row.kind == RowKind::OpenWindow && row.open_win != nullptr) {
                    // Focus button.
                    if (HitTest(FocusRect(0, ry), px, py)) {
                        wm_.BringToFront(row.open_win);
                        return;
                    }
                    // Minimize button — deferred to OnRender.
                    if (HitTest(MinimizeRect(0, ry), px, py)) {
                        // Fire on_minimize_begin_ so WmBridge can stash the reopen fn.
                        if (row.open_win->on_minimize_begin_) {
                            row.open_win->on_minimize_begin_(row.open_win);
                        }
                        pending_minimize_win_ = row.open_win;
                        return;
                    }
                    // Close button.
                    if (HitTest(CloseRect(0, ry), px, py)) {
                        wm_.CloseWindow(row.open_win);
                        // Force a refresh on next tick.
                        refresh_ctr_ = 0;
                        return;
                    }
                }
                // v2.0.4.1: tap on a SuspendedApp row resumes the Switch
                // Application via smi::ResumeApplication, then fades uMenu out
                // (the same FadeOut+Finalize sequence LaunchIcon uses for the
                // Resume path at qd_DesktopIcons.cpp:4530).
                if (row.kind == RowKind::SuspendedApp && row.program_id != 0) {
                    UL_LOG_INFO("qdesktop: TaskManager Resume tap app_id=0x%016lX",
                                static_cast<unsigned long>(row.program_id));
                    const auto rrc = smi::ResumeApplication();
                    if (R_SUCCEEDED(rrc) && g_MenuApplication != nullptr) {
                        g_MenuApplication->FadeOutToNonLibraryApplet();
                        g_MenuApplication->Finalize();
                    } else {
                        UL_LOG_WARN("qdesktop: TaskManager Resume failed rc=0x%08x", rrc);
                    }
                    return;
                }
                // Minimized entries: tapping the row fires restore via the dock
                // entry's on_restore_requested (handled by WM).  The task manager
                // does not duplicate that action — the entry tile in the dock band
                // is the canonical restore trigger.
            } else {
                hovered_row_ = -1;
            }
        } else {
            hovered_row_ = -1;
        }
    }
}

} // namespace ul::menu::qdesktop
