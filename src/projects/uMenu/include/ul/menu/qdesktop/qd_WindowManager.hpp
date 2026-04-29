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
#include <memory>
#include <vector>

#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_MinimizedDockEntry.hpp>
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
    bool PollWindowEvents(u64 keys_down, u64 keys_up, u64 keys_held,
                          pu::ui::TouchPoint touch_pos);

    // ── Queries ───────────────────────────────────────────────────────────────

    // Returns the next stagger position for a new window and advances the internal
    // stagger counter (wrapping when the window would overflow the usable area).
    // win_w / win_h: the size of the window that will be opened at this position.
    void TakeStaggerPos(s32 win_w, s32 win_h, s32 &out_x, s32 &out_y);

    // Returns total count of open + minimized entries (0 = nothing to render).
    u32 GetTotalWindowCount() const {
        return static_cast<u32>(open_windows_.size() + minimized_entries_.size());
    }

    // Returns nullptr if no window has the given program_id.
    QdWindow* FindWindowByProgramId(u64 pid);

    // Returns nullptr if no minimized entry has the given program_id.
    QdMinimizedDockEntry* FindMinimizedByProgramId(u64 pid);

private:
    // ── State ─────────────────────────────────────────────────────────────────

    // open_windows_[0] = bottom; open_windows_.back() = topmost (rendered last).
    std::vector<QdWindow::Ref>             open_windows_;

    // minimized_entries_: left-to-right dock order.
    std::vector<QdMinimizedDockEntry::Ref> minimized_entries_;

    // Stagger position for next OpenWindow call.
    s32 next_stagger_x_;  // init 104
    s32 next_stagger_y_;  // init 56

    // Pending minimize: set by MinimizeWindow, consumed by FinalizeMinimize.
    // Stores the dock entry pre-created in MinimizeWindow so FinalizeMinimize
    // can register it without needing the renderer again.
    QdMinimizedDockEntry::Ref pending_minimize_entry_;
    QdWindow*                 pending_minimize_win_ = nullptr;
};

} // namespace ul::menu::qdesktop
