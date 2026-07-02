// qd_WindowManager.hpp — Window manager for Q OS uMenu v1.10.
// Owns all QdWindow and QdMinimizedDockEntry instances.
// Non-copyable; stored as a value member of QdDesktopIconsElement (same pattern as
// QdTaskManager task_mgr_ and QdHotCornerDropdown dropdown_ at hpp:721/727).
//
// Z-order: windows render at Z=4 (above desktop icons/favorites, below hot-corner widget).
// Minimized entries share Z=4 and are rendered before open windows (furthest back).
//
// Window stagger starts at (104, 56) to clear the hot-corner widget (96×72) and
// the top-bar (y=0..48).  Each subsequent window is offset by LAUNCH_STAGGER=36 in
// both axes, wrapping when the window would exceed the content area.
#pragma once

#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_MinimizedDockEntry.hpp>
#include <ul/menu/qdesktop/qd_SuspendedAppDockEntry.hpp>
#include <ul/menu/qdesktop/qd_ContextMenu.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>

namespace ul::menu::qdesktop {

// ── QdWindowManager ───────────────────────────────────────────────────────────

class QdWindowManager {
public:
    // Default-constructible: initialises empty open_windows_ and minimized_entries_.
    QdWindowManager();
    ~QdWindowManager();

    // Non-copyable, non-movable (owns SDL state via QdWindow / QdMinimizedDockEntry).
    QdWindowManager(const QdWindowManager&)            = delete;
    QdWindowManager& operator=(const QdWindowManager&) = delete;
    QdWindowManager(QdWindowManager&&)                 = delete;
    QdWindowManager& operator=(QdWindowManager&&)      = delete;

    // ── Window lifecycle ──────────────────────────────────────────────────────

    // Add win to top of z-order and wire its callbacks.
    // win->on_close_requested   → CloseWindow(win)
    // win->on_minimize_requested → MinimizeWindow(win, ...)
    // No-op if win is already tracked.
    void OpenWindow(QdWindow::Ref win);

    // Remove win from open_windows_, clean up stagger state.
    // Called from on_close_requested callback or externally.
    void CloseWindow(QdWindow* win);

    // Capture a SNAP_W×SNAP_H snapshot from win's current render output,
    // create a QdMinimizedDockEntry, and move win to WindowState::Minimized.
    // drawer provides the SDL renderer for the capture.
    // NOTE: capture via SDL_SetRenderTarget; FBO is reset after capture.
    void MinimizeWindow(QdWindow* win, pu::ui::render::Renderer::Ref& drawer);

    // Restore a minimized entry: remove from minimized_entries_, create a new
    // QdWindow at the dock-tile position in WindowState::Restoring, wire callbacks.
    // Snapshot in the entry is freed (DeleteTexture) when the entry is destroyed.
    void RestoreWindow(QdMinimizedDockEntry* entry);

    // Z2.4 — Close a minimized entry WITHOUT restoring it.  Drops the entry
    // from minimized_entries_ (dtor frees the snapshot texture).  No-op if
    // entry is not in the list (defensive — entry might have been restored
    // by a parallel code path).
    void CloseMinimizedEntry(QdMinimizedDockEntry* entry);

    // Move win to back-of-stack (highest z-order / rendered last = topmost).
    void BringToFront(QdWindow* win);

    // Called by QdWindow::AdvanceAnimation when the Minimizing animation completes.
    // Removes win from open_windows_ and registers the dock entry that was pre-created
    // in MinimizeWindow.  Must only be called from within AdvanceAnimation callback.
    void FinalizeMinimize(QdWindow* win);

    // ── Render ───────────────────────────────────────────────────────────────

    // Render all minimized dock entries then all open windows, bottom to top.
    // Calls LayoutDockEntries first to assign current tile positions.
    void RenderAll(pu::ui::render::Renderer::Ref& drawer);

    // Assign tile_x_/tile_y_ to every QdMinimizedDockEntry based on position
    // in minimized_entries_ and SNAP_W/SNAP_H geometry in the dock band.
    // Dock band: y = SCREEN_H - DOCK_H (932) to SCREEN_H (1080).
    // Tiles are packed right-to-left starting from SCREEN_W - SNAP_W - 8.
    void LayoutDockEntries();

    // ── Input ────────────────────────────────────────────────────────────────

    // Route input to windows and dock entries.
    // Order: top window first (last in open_windows_), then minimized entries.
    // Returns true if any window or dock entry consumed the event (caller should return).
    // cx / cy: current software cursor position for hover update and ZR dispatch.
    bool PollWindowEvents(u64 keys_down, u64 keys_up, u64 keys_held,
                          pu::ui::TouchPoint touch_pos, s32 cx, s32 cy);

    // ── Queries ───────────────────────────────────────────────────────────────

    // Returns the next stagger position for a new window and advances the internal
    // stagger counter (wrapping when the window would overflow the usable area).
    // win_w / win_h: the size of the window that will be opened at this position.
    void TakeStaggerPos(s32 win_w, s32 win_h, s32 &out_x, s32 &out_y);

    // Register the reopen functor keyed by QdWindow pointer.
    // Called by WmBridge opener methods inside on_minimize_begin_ before
    // MinimizeWindow runs.  MinimizeWindow picks this up (keyed by win) and stores
    // it on the dock entry's on_reopen field so RestoreWindow re-invokes the
    // correct opener.  Replacing the single pending_reopen_ slot with a per-window
    // map means two windows minimizing concurrently do not overwrite each other.
    void SetPendingReopen(QdWindow* key, std::function<void()> fn);

    // Returns total count of open + minimized + suspended-app entries
    // (0 = nothing to render).  Suspended-app entries count even when
    // there are no in-uMenu windows so callers gate RenderAll/PollEvents
    // correctly when the only dock entry is a HOS-suspended Application.
    u32 GetTotalWindowCount() const {
        return static_cast<u32>(open_windows_.size()
                              + minimized_entries_.size()
                              + suspended_app_entries_.size());
    }

    // W9-FIX Bug 2: count of currently-open (non-minimized) windows.
    // Used to gate desktop-icon touch launches: when any window is open,
    // clicks on the NRO/folder grid behind the windows are suppressed.
    u32 GetOpenWindowCount() const {
        return static_cast<u32>(open_windows_.size());
    }

    // W9-FIX Bug 1: scan both open_windows_ and minimized_entries_ for a
    // window / dock entry whose title matches the given string.
    // Returns a pair { open_win, minimized_entry } — at most one is non-null.
    //   open_win       != nullptr → window is currently open; call BringToFront.
    //   minimized_entry != nullptr → window is minimized; call RestoreWindow.
    //   both nullptr               → no matching window exists; create a new one.
    // Comparison is exact (no case folding) since window titles are compiled-in
    // constants ("Monitor", "Files", "Settings", etc.).
    std::pair<QdWindow*, QdMinimizedDockEntry*>
    FindWindowByTitle(const std::string& title) const;

    // Read-only access to the open window list; used by DesktopIcons to render snap preview
    // overlays and to query whether the cursor is over any open window.
    const std::vector<QdWindow::Ref>& GetOpenWindows() const { return open_windows_; }

    // Read-only access to the minimized dock entry list; used by QdTaskManagerElement
    // to list minimized entries alongside open windows.
    const std::vector<QdMinimizedDockEntry::Ref>& GetMinimizedEntries() const {
        return minimized_entries_;
    }

    // ── Suspended-app dock entries (v3.1+) ────────────────────────────────────
    //
    // When the user presses HOME on a running Switch retail Application,
    // uSystem captures the program_id into g_GlobalSettings.system_status.
    // suspended_app_id (single-app HOS invariant: at most one suspended
    // app at a time).  uMenu's window manager syncs a dedicated dock entry
    // for that program — distinct from minimized in-uMenu windows because
    // the action surface differs (HOS am-IPC resume/terminate vs in-uMenu
    // window restore).
    //
    // RefreshSuspendedApps() is the sync function — call once per uMenu
    // tick (cheap when suspended_app_id is unchanged).
    void RefreshSuspendedApps();

    // Resume callback: fires when the user taps the suspended-app entry.
    // Owner (QdDesktopIconsElement) wires this to smi::ResumeApplication
    // + FadeOutToNonLibraryApplet + Finalize (mirrors the resume path in
    // QdTaskManagerLayout for SuspendedApp rows).
    std::function<void(u64 program_id)> on_resume_suspended_requested;

    // Terminate callback: fires from the ZL context menu's "Terminate"
    // option.  Owner wires to smi::TerminateApplication.
    std::function<void(u64 program_id)> on_terminate_suspended_requested;

    // Render the suspended-app dock-entry context menu overlay if open.
    // Owner (QdDesktopIconsElement) MUST call this AFTER RenderAll so the
    // menu sits on top of the dock tiles.  No-op when the menu is closed.
    void RenderSuspendedContextMenu(SDL_Renderer *r) const {
        suspended_ctx_menu_.Render(r);
    }

    // Z2.4 — Render the minimized-tile context menu overlay if open.
    // Same z-order contract as RenderSuspendedContextMenu.
    void RenderMinimizedContextMenu(SDL_Renderer *r) const {
        minimized_ctx_menu_.Render(r);
    }

    // Returns nullptr if no window has the given program_id.
    QdWindow* FindWindowByProgramId(u64 pid);

    // Returns nullptr if no minimized entry has the given program_id.
    QdMinimizedDockEntry* FindMinimizedByProgramId(u64 pid);

    // W8-FIX Bug 2/4: returns true if any open window is currently in a
    // titlebar-drag (finger held and moving the window).  Used by DesktopIcons
    // to suppress hot-corner activation and dismiss open context menus while
    // a window is being repositioned.
    bool IsAnyTitlebarDragging() const {
        for (const auto &win : open_windows_) {
            if (win && win->IsTitlebarDragging()) return true;
        }
        return false;
    }

private:
    // ── State ─────────────────────────────────────────────────────────────────

    // open_windows_[0] = bottom; open_windows_.back() = topmost (rendered last).
    std::vector<QdWindow::Ref>             open_windows_;

    // minimized_entries_: left-to-right dock order.
    std::vector<QdMinimizedDockEntry::Ref> minimized_entries_;

    // suspended_app_entries_: dock entries for HOS-suspended Switch apps.
    // Single-entry vector in practice (HOS allows one suspended app at a
    // time) but kept as vector for symmetry with minimized_entries_ and
    // future-proofing if HOS ever lifts the limit.
    std::vector<QdSuspendedAppDockEntry::Ref> suspended_app_entries_;

    // Last-known suspended_app_id — used by RefreshSuspendedApps to avoid
    // re-creating the entry every tick.  0 = no app suspended.
    u64 cached_suspended_id_ = 0;
    // Last-known resolved title for cached_suspended_id_ — used when re-
    // creating the entry across uMenu instances without re-querying NACP.
    std::string cached_suspended_title_;

    // Context menu state — opened on ZL press over a suspended-app dock tile.
    // mutable so the public Render() can be const-correct from owner side.
    mutable QdContextMenu suspended_ctx_menu_;
    // Target program_id for the menu's current open instance.  Read on close
    // to dispatch the right callback (Resume / Terminate / Cancel).
    u64 ctx_target_program_id_ = 0;

    // Z2.4 — Minimized-tile context menu state.  Single instance shared
    // across all minimized tiles since at most one menu is open at a time.
    // Target tile pointer is validated against minimized_entries_ before
    // dispatch (defensive — entry could be destroyed between open and confirm).
    mutable QdContextMenu       minimized_ctx_menu_;
    QdMinimizedDockEntry*       ctx_target_minimized_entry_ = nullptr;

    // A2-OPT-2 (CONVERGENT): pre-allocated scratch vectors for per-frame snapshots
    // in RenderAll and PollWindowEvents.  Reused each frame; eliminates the 2
    // heap allocs/frame that triggered the per-frame crash-safety copies.
    // IMPORTANT: the snapshot mechanism itself is preserved (crash-safety against
    // FinalizeMinimize mid-loop UAF); only the allocation is removed.
    std::vector<QdWindow::Ref>                  scratch_windows_;
    std::vector<QdMinimizedDockEntry::Ref>      scratch_entries_;

    // WIN-5 UNION-OCCLUSION CULL: per-frame scratch storage for the union
    // accumulator in RenderAll.  Pre-allocated to avoid heap churn; N ≤ 32.
    // Each entry is { x0, y0, x1, y1 } (right-exclusive pixel coords).
    struct OccRect { s32 x0, y0, x1, y1; };
    std::vector<OccRect> scratch_occ_union_;

    // ── Static rect helpers (used by RenderAll union-cull + scissor-clip) ──────

    // Inflate rect by `d` on every side (returns new rect; may produce inverted
    // rect if d is negative and the rect is smaller than |2d|).
    static SDL_Rect RectInflate(s32 rx, s32 ry, s32 rw, s32 rh, s32 d) {
        return SDL_Rect{ rx - d, ry - d, rw + 2 * d, rh + 2 * d };
    }

    // Deflate rect by `d` on every side (same as Inflate(-d); may collapse to
    // zero/negative — callers must check w > 0 && h > 0 before using).
    static SDL_Rect RectDeflate(s32 rx, s32 ry, s32 rw, s32 rh, s32 d) {
        return SDL_Rect{ rx + d, ry + d, rw - 2 * d, rh - 2 * d };
    }

    // Returns true iff the SDL_Rect `target` is fully covered by the union of
    // axis-aligned rectangles in `union_rects`.  The union is maintained as a
    // list of non-overlapping opaque bodies; a target is covered iff for every
    // pixel column of target there exists at least one rect in the union that
    // spans that column vertically across the full height of target.
    //
    // Implementation: scan the horizontal extent of `target`; at each step
    // find the rect in the union whose left edge is ≤ current x and whose
    // right edge is furthest right, advancing x to that right edge.  If we
    // can reach target.x + target.w this way, the column is fully covered.
    // We do the same check banded by the target's vertical extent.
    //
    // O(N²) worst case, N ≤ 32 → < 1 µs per call.
    static bool RectFullyCoveredByUnion(const SDL_Rect& target,
                                         const std::vector<OccRect>& union_rects) {
        if (target.w <= 0 || target.h <= 0) return true;  // degenerate = trivially covered
        const s32 tx0 = target.x;
        const s32 ty0 = target.y;
        const s32 tx1 = target.x + target.w;
        const s32 ty1 = target.y + target.h;

        // Walk the horizontal span of target, greedily extending rightward.
        s32 x = tx0;
        while (x < tx1) {
            s32 best_x1 = x;  // furthest right edge we can reach from x
            for (const auto& occ : union_rects) {
                // The occluder must cover the full vertical band [ty0, ty1].
                if (occ.y0 > ty0 || occ.y1 < ty1) continue;
                // The occluder must overlap with the current x position.
                if (occ.x0 > x || occ.x1 <= x) continue;
                if (occ.x1 > best_x1) best_x1 = occ.x1;
            }
            if (best_x1 <= x) return false;  // no progress → gap found
            x = best_x1;
        }
        return true;
    }

    // A2-OPT-5: hover-scan dirty flag.  Set when the cursor moves or when the
    // window set changes (Open/Close/BringToFront).  Cleared after the scan.
    s32  hover_last_cx_  = -1;
    s32  hover_last_cy_  = -1;
    bool hover_dirty_    = true;

    // A2-OPT-6: O(1) focus tracking.  Maintained by BringToFront / CloseWindow /
    // FinalizeMinimize.  Raw pointer — always cross-checked against open_windows_.
    QdWindow* focused_win_ = nullptr;

    // A3-OPT-5: dock-layout dirty flag.  Set on minimize/restore/close/add; cleared
    // after LayoutDockEntries runs so re-layout is skipped on unchanged frames.
    bool dock_layout_dirty_ = true;

    // Stagger position for next OpenWindow call.
    s32 next_stagger_x_;  // init 104
    s32 next_stagger_y_;  // init 56

    // Pending minimize: set by MinimizeWindow, consumed by FinalizeMinimize.
    // Stores the dock entry pre-created in MinimizeWindow so FinalizeMinimize
    // can register it without needing the renderer again.
    QdMinimizedDockEntry::Ref pending_minimize_entry_;
    QdWindow*                 pending_minimize_win_ = nullptr;

    // Pending reopen: set by WmBridge via SetPendingReopen() inside on_minimize_begin_,
    // consumed by MinimizeWindow which copies it to pending_minimize_entry_->on_reopen.
    // v1.10.3.6: keyed by QdWindow* so two concurrent on_minimize_begin_ callbacks
    // don't overwrite each other (replaces the single pending_reopen_ slot).
    std::unordered_map<QdWindow*, std::function<void()>> pending_reopen_map_;

    // Stagger position each open window was assigned (key=QdWindow*, value=(x,y)).
    // Used by CloseWindow for LIFO stagger reclaim (v1.10.3.6).
    std::map<QdWindow*, std::pair<s32,s32>> open_stagger_positions_;

    // W6-LEDGER: ledger handles for open windows (kind=Window) and
    // minimized snapshots (kind=MinimizedSnap).  Key is raw pointer.
    std::unordered_map<QdWindow*, uint64_t> open_window_ledger_handles_;
    std::unordered_map<const QdMinimizedDockEntry*, uint64_t> minimized_ledger_handles_;

    // ── NACP async resolution (RESIDUAL OPEN-HITCH fix) ──────────────────────
    //
    // nsGetApplicationControlData is a blocking NAND/gamecard IPC call (~0.8 s).
    // Running it on the render thread causes a visible frame freeze on window-open.
    //
    // Fix: a single background thread races the IPC call.  The render thread fires
    // it when a new suspended_app_id appears, then picks up the resolved title on
    // the next RefreshSuspendedApps() tick where nacp_result_ready_ is set.
    //
    // Threading contract:
    //   • nacp_bg_thread_ is joined (or detached) before a new one is started.
    //   • nacp_result_mutex_ protects nacp_result_title_ and nacp_result_id_.
    //   • nacp_result_ready_ is an atomic flag: the bg thread sets it after writing
    //     the protected fields; the render thread clears it after consuming them.
    //   • SDL calls (GetSharedNsIconCache().Get) happen ONLY on the render thread
    //     when consuming the result — satisfying A4-RF-03.
    //   • nacp_inflight_id_ tracks which program_id the bg thread was launched for;
    //     used to discard stale results if suspended_app_id changes again before
    //     the thread finishes.
    struct NacpResult {
        u64         program_id  = 0;
        std::string title;
    };

    std::thread              nacp_bg_thread_;
    std::mutex               nacp_result_mutex_;
    NacpResult               nacp_result_;
    std::atomic<bool>        nacp_result_ready_{false};
    u64                      nacp_inflight_id_ = 0;  // id currently being resolved
};

} // namespace ul::menu::qdesktop
