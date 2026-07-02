// qd_TaskManagerLayout.hpp — Task Manager window content for Q OS qdesktop v1.10.3.11.
// Inherits QdContentElement (SP3 passive-content contract — see qd_ContentElement.hpp).
//
// Lists all open windows and minimized dock entries managed by QdWindowManager.
// Optionally enriches with a live process PID from pm:dmnt (pmdmntGetApplicationProcessId).
//
// Layout (natural canvas 780×480):
//   y=0..48   — header bar "Task Manager" + subtitle
//   y=48..420 — scrollable row list (each row kRowH=54 px tall)
//   y=420..480 — hint bar (B = Close, action buttons guidance)
//
// Each row shows:
//   • Window title (left, ~300 px wide)
//   • State badge (Normal / Minimized / Restoring / etc.) centred
//   • Program ID hex string (right-aligned, if != 0)
//   • Action buttons: [Focus] [Minimize] [Close]
//     — Focus fires wm_.BringToFront; Minimize is deferred to OnRender (needs drawer);
//       Close fires wm_.CloseWindow.
//
// Refresh():
//   Called by QdWindow's on_tick every kTickRefreshHz (~60) frames.
//   Internally throttled to kRefreshEvery=30 frames — halves service call rate.
//   Re-snapshots wm_.GetOpenWindows() + wm_.GetMinimizedEntries() into rows_.
//
// Must NOT regress the QdContentElement contract:
//   OnRender and OnInput must not call SDL_RenderSetScale (QdWindow owns scale).
//   touch_pos in OnInput is already in content-local natural coordinates.
#pragma once

#include <pu/Plutonium>
#include <switch.h>
#include <string>
#include <vector>

#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_WindowManager.hpp>
#include <ul/menu/qdesktop/qd_ContextMenu.hpp>  // v2.0.4.2: ZL row-action menu

namespace ul::menu::qdesktop {

// ── Layout constants ──────────────────────────────────────────────────────────

static constexpr s32 QD_TM_NATURAL_W     = 780;
static constexpr s32 QD_TM_NATURAL_H     = 480;
static constexpr s32 QD_TM_HEADER_H      =  48;
static constexpr s32 QD_TM_HINT_H        =  60;
static constexpr s32 QD_TM_LIST_TOP      = QD_TM_HEADER_H;
static constexpr s32 QD_TM_LIST_BOTTOM   = QD_TM_NATURAL_H - QD_TM_HINT_H;
static constexpr s32 QD_TM_LIST_H        = QD_TM_LIST_BOTTOM - QD_TM_LIST_TOP; // 372
static constexpr s32 QD_TM_ROW_H         =  54;
static constexpr s32 QD_TM_ROW_PAD_X     =  12;
static constexpr s32 QD_TM_BTN_W         =  72;
static constexpr s32 QD_TM_BTN_H         =  32;
static constexpr s32 QD_TM_BTN_GAP       =   6;

/// Throttle: Refresh() actually re-queries only every N on_tick calls.
/// v2.0.4.3: dropped 30 → 0 (refresh every on_tick = every 1 second).
/// At 30 the task manager appeared frozen for 30 seconds after opening or
/// closing any other window — the user reported "not populating live".
/// pm:dmnt + WM enumeration are cheap (dozens of µs); refreshing every
/// second is well below the perceptible-staleness threshold.
static constexpr int QD_TM_REFRESH_EVERY = 0;

// ── QdTaskManagerElement ──────────────────────────────────────────────────────

/// Task Manager content element — lists open and minimized windows with
/// per-row Focus / Minimize / Close actions.
///
/// Constructor takes a non-owning reference to the QdWindowManager so it can
/// call BringToFront / CloseWindow.  MinimizeWindow is deferred to OnRender
/// where Renderer::Ref is live.
class QdTaskManagerElement : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdTaskManagerElement>;

    static Ref New(const QdTheme& theme, QdWindowManager& wm) {
        return std::make_shared<QdTaskManagerElement>(theme, wm);
    }

    QdTaskManagerElement(const QdTheme& theme, QdWindowManager& wm);
    ~QdTaskManagerElement() override;

    // ── QdContentElement interface ────────────────────────────────────────────

    s32 GetNaturalW() const override { return QD_TM_NATURAL_W; }
    s32 GetNaturalH() const override { return QD_TM_NATURAL_H; }
    // v2.9.10 — opt in to width-bound scale so task rows fill the window
    // horizontally and overflow scrolls vertically.
    bool PrefersWidthBoundScale() const override { return true; }

    void OnRender(pu::ui::render::Renderer::Ref& drawer, s32 x, s32 y) override;

    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    /// Re-snapshot window list from WM + pm:dmnt.  Called via on_tick (gated by
    /// WindowState::Normal) so service calls never fire during minimize snapshots.
    void Refresh() override;

private:
    // ── Row data ──────────────────────────────────────────────────────────────

    /// Source of a TaskRow.
    /// v2.0.4.1: SuspendedApp added — surfaces a Switch retail Application that
    /// entered background via HOME (system_status.suspended_app_id non-zero) so
    /// the user sees and can resume/terminate it from the Task Manager.
    enum class RowKind { OpenWindow, MinimizedEntry, SuspendedApp };

    struct TaskRow {
        RowKind      kind;
        std::string  title;
        std::string  state_label;   // "Normal", "Minimized", "Restoring", etc.
        u64          program_id;    // 0 if not set
        std::string  pid_hex;       // "0x%016lX" if program_id != 0, else ""
        // Back-pointer for actions — valid only while WM is alive (guaranteed:
        // QdTaskManagerElement lifetime is scoped to its QdWindow, WM outlives it).
        QdWindow*              open_win   = nullptr;  // kind==OpenWindow
        QdMinimizedDockEntry*  min_entry  = nullptr;  // kind==MinimizedEntry
    };

    // ── Action button hit boxes (relative to row top-left in natural coords) ──

    struct BtnRect { s32 x, y, w, h; };

    // Returns the natural-coord rect for the Focus/Minimize/Close buttons
    // within a row whose top-left is at (row_x, row_y).
    static BtnRect FocusRect(s32 row_x, s32 row_y);
    static BtnRect MinimizeRect(s32 row_x, s32 row_y);
    static BtnRect CloseRect(s32 row_x, s32 row_y);

    // Returns true if natural-coord point (px, py) falls inside r.
    static bool HitTest(const BtnRect& r, s32 px, s32 py);

    // ── Rendering helpers ─────────────────────────────────────────────────────

    // Fills a rounded-rect entirely in col.  radius=4 px.
    static void FillRounded(SDL_Renderer* r, s32 x, s32 y, s32 w, s32 h,
                            pu::ui::Color col);

    // Blits tex at (x, y).  Null-safe (no-op if tex==nullptr).
    static void BlitTex(SDL_Renderer* r, SDL_Texture* tex, s32 x, s32 y);

    // Builds (or re-builds) a text texture.  Frees old_tex first if non-null.
    // Returns the new texture (caller owns it).
    static SDL_Texture* BuildText(pu::ui::render::Renderer::Ref& drawer,
                                  const std::string& text,
                                  pu::ui::Color col,
                                  s32 font_size,
                                  SDL_Texture* old_tex);

    // Renders one row at natural coords (rx, ry).
    void RenderRow(pu::ui::render::Renderer::Ref& drawer,
                   SDL_Renderer* r,
                   const TaskRow& row,
                   s32 rx, s32 ry,
                   bool hovered) const;

    // Renders a small action button (Focus/Minimize/Close) at (bx, by).
    static void RenderBtn(SDL_Renderer* r, s32 bx, s32 by, s32 bw, s32 bh,
                          const char* label_cstr,
                          pu::ui::Color bg_col, pu::ui::Color text_col);

    // ── Row top-y in list coordinates (relative to list area top = QD_TM_LIST_TOP).
    static s32 RowTopInList(s32 row_idx) { return row_idx * QD_TM_ROW_H; }

    // ── Row natural-canvas y for row row_idx given current scroll_y_.
    s32 RowNaturalY(s32 row_idx) const {
        return QD_TM_LIST_TOP + RowTopInList(row_idx) - scroll_y_;
    }

    // ── State ─────────────────────────────────────────────────────────────────

    QdTheme          theme_;
    QdWindowManager& wm_;           // non-owning reference

    std::vector<TaskRow> rows_;     // rebuilt every Refresh()

    s32  scroll_y_       = 0;       // vertical scroll offset (natural coords)
    int  hovered_row_    = -1;      // row under the software cursor (-1 = none)
    int  refresh_ctr_    = 0;       // frames since last actual data refresh

    // pm:dmnt state
    bool pm_init_attempted_ = false;   // v3.0.2 FIX-1: lazy pm:dmnt init flag
    bool pm_ok_             = false;   // true if pmdmntInitialize succeeded
    u64  pm_app_pid_        = 0;       // last result from pmdmntGetApplicationProcessId

    // Deferred minimize: set in OnInput (no drawer there), executed in OnRender.
    QdWindow* pending_minimize_win_ = nullptr;

    // ── v2.0.4.2: ZL context menu ──────────────────────────────────────────────
    // ZL on a row opens QdContextMenu with actions appropriate to row.kind:
    //   OpenWindow:     [Bring to Front, Minimize, Close, Cancel]
    //   MinimizedEntry: [Restore, Close, Cancel]
    //   SuspendedApp:   [Resume, Terminate, Cancel]
    // The menu is drained in OnRender (after lp render, before exit).  Saved
    // ctx_target_idx_ lets us look up the row again on confirm — rows_ may
    // have re-Refresh'd between menu-open and menu-close.
    QdContextMenu ctx_menu_;
    int           ctx_target_row_idx_ = -1;
    RowKind       ctx_target_kind_    = RowKind::OpenWindow;
    QdWindow*     ctx_target_win_     = nullptr;
    QdMinimizedDockEntry* ctx_target_min_entry_ = nullptr;
    u64           ctx_target_program_id_ = 0;

    // Cached rendered textures (freed in dtor + on re-Refresh).
    SDL_Texture* header_tex_ = nullptr;  // "Task Manager" title
    SDL_Texture* hint_tex_   = nullptr;  // hint bar text
    // Per uMenu optimization audit F2.3/F2.4: count-subtitle + empty-state
    // textures used to be allocated every OnRender frame.  Now cached:
    //   count_tex_   rebuilt only when (open, minimized) counts change
    //   empty_tex_   built once on first need, reused forever
    SDL_Texture* count_tex_  = nullptr;
    size_t       count_cached_open_      = SIZE_MAX;  // sentinel = uninitialized
    size_t       count_cached_minimized_ = SIZE_MAX;
    SDL_Texture* empty_tex_ = nullptr;

    // Per-row title textures: rebuilt when rows_ changes.
    // Parallel to rows_; index matches.
    std::vector<SDL_Texture*> row_title_textures_;

    // Flag: rows_ has changed since last OnRender — rebuild row title textures.
    bool rows_dirty_ = true;

    // Free all per-row title textures.
    void FreeRowTextures();

    // Rebuild row_title_textures_ from current rows_.
    void RebuildRowTextures(pu::ui::render::Renderer::Ref& drawer);

    // Build header and hint textures if not yet built.
    void EnsureStaticTextures(pu::ui::render::Renderer::Ref& drawer);
};

} // namespace ul::menu::qdesktop
