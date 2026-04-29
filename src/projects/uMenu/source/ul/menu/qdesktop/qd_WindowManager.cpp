// qd_WindowManager.cpp — Window manager for Q OS uMenu v1.10.
// See qd_WindowManager.hpp for design notes.
//
// Lifecycle contract summary (cross-refs with qd_Window.cpp / qd_MinimizedDockEntry.cpp):
//
//  OpenWindow:       win→open_windows_; callbacks wired; BringToFront called.
//  CloseWindow:      win removed from open_windows_; stagger not reclaimed (simple model).
//  MinimizeWindow:   SDL_SetRenderTarget snapshot captured; dock entry pre-created and stored
//                    in pending_minimize_entry_/win_; animation started; FinalizeMinimize
//                    is called from QdWindow::AdvanceAnimation on_minimize_requested callback.
//  FinalizeMinimize: removes win from open_windows_, promotes pending entry to minimized_entries_.
//  RestoreWindow:    creates new QdWindow via QdWindow::New in Restoring state starting from
//                    dock tile position; removes entry from minimized_entries_; wires callbacks;
//                    calls OpenWindow to add to z-order.
//  RenderAll:        LayoutDockEntries(), then minimized entries (raw SDL), then all open windows.
//  PollWindowEvents: top window first (open_windows_.back()), then minimized entries.

#include <ul/menu/qdesktop/qd_WindowManager.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <algorithm>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Ctor / dtor ───────────────────────────────────────────────────────────────

QdWindowManager::QdWindowManager()
    : next_stagger_x_(104),
      next_stagger_y_(56),
      pending_minimize_win_(nullptr)
{
    // open_windows_ and minimized_entries_ are default-constructed as empty vectors.
}

QdWindowManager::~QdWindowManager() {
    // QdWindow::Ref and QdMinimizedDockEntry::Ref are shared_ptrs; destructors run
    // automatically as the vectors are cleared.  Explicit clear is defensive.
    open_windows_.clear();
    minimized_entries_.clear();
    pending_minimize_entry_.reset();
    pending_minimize_win_ = nullptr;
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

void QdWindowManager::OpenWindow(QdWindow::Ref win) {
    if (!win) {
        return;
    }

    // Check for duplicate.
    for (const auto& w : open_windows_) {
        if (w.get() == win.get()) {
            BringToFront(win.get());
            return;
        }
    }

    // Wire callbacks.  Capture raw pointer; the window is kept alive by open_windows_.
    QdWindow* raw = win.get();

    win->on_close_requested = [this, raw](QdWindow* /*w*/) {
        CloseWindow(raw);
    };

    win->on_minimize_requested = [this, raw](QdWindow* /*w*/) {
        // Called from QdWindow::AdvanceAnimation when Minimizing animation completes.
        // At this point MinimizeWindow has already captured the snapshot and stored
        // the pending entry; we just finalize the transfer.
        FinalizeMinimize(raw);
    };

    open_windows_.push_back(std::move(win));
    // The newly added window is already at the back (topmost); BringToFront is a
    // no-op since it's already last, but call it for robustness.
    BringToFront(open_windows_.back().get());
}

void QdWindowManager::CloseWindow(QdWindow* win) {
    if (!win) {
        return;
    }

    auto it = std::find_if(open_windows_.begin(), open_windows_.end(),
                           [win](const QdWindow::Ref& r) { return r.get() == win; });
    if (it != open_windows_.end()) {
        open_windows_.erase(it);
    }

    // Advance stagger position is not reclaimed in this simple v1.10 model.
    // Future versions may implement gap compaction here.
}

void QdWindowManager::TakeStaggerPos(s32 win_w, s32 win_h, s32 &out_x, s32 &out_y) {
    out_x = next_stagger_x_;
    out_y = next_stagger_y_;

    next_stagger_x_ += LAUNCH_STAGGER;
    next_stagger_y_ += LAUNCH_STAGGER;

    const s32 max_x = static_cast<s32>(SCREEN_W) - win_w - 16;
    const s32 max_y = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H) - win_h - 8;
    if (next_stagger_x_ > max_x || next_stagger_y_ > max_y) {
        next_stagger_x_ = 104;
        next_stagger_y_ = 56;
    }
}

void QdWindowManager::MinimizeWindow(QdWindow* win,
                                      pu::ui::render::Renderer::Ref& drawer)
{
    if (!win) {
        return;
    }

    // ── Snapshot capture ──────────────────────────────────────────────────────
    // Create a SNAP_W × SNAP_H render target, render the window's content into it,
    // then restore the main render target.
    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // Capture strategy: render the window into a full-screen intermediate texture,
    // then blit-scale the window's own pixel rect down to SNAP_W×SNAP_H.
    // This avoids needing to reposition the window or modify the QdWindow interface.
    const int win_px_w = win->GetW();
    const int win_px_h = win->GetH();

    // Full-screen intermediate texture (same resolution as screen for accurate capture).
    SDL_Texture* intermediate = SDL_CreateTexture(r,
                                                   SDL_PIXELFORMAT_RGBA8888,
                                                   SDL_TEXTUREACCESS_TARGET,
                                                   static_cast<int>(SCREEN_W),
                                                   static_cast<int>(SCREEN_H));

    SDL_Texture* snap = nullptr;

    if (intermediate && win_px_w > 0 && win_px_h > 0) {
        // Render window into the full-res intermediate.
        SDL_SetRenderTarget(r, intermediate);
        SDL_SetRenderDrawColor(r, 0x1A, 0x1A, 0x1A, 0xFF);
        SDL_RenderClear(r);
        win->OnRender(drawer, 0, 0);

        // Now create the SNAP_W×SNAP_H target and blit-scale from the window's
        // screen rect within the intermediate.
        snap = SDL_CreateTexture(r,
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_TARGET,
                                  static_cast<int>(SNAP_W),
                                  static_cast<int>(SNAP_H));
        if (snap) {
            SDL_SetRenderTarget(r, snap);
            SDL_SetRenderDrawColor(r, 0x1A, 0x1A, 0x1A, 0xFF);
            SDL_RenderClear(r);

            // Source: window's on-screen pixel rect within the intermediate.
            SDL_Rect src = { win->GetX(), win->GetY(), win_px_w, win_px_h };
            // Dest: full SNAP texture.
            SDL_Rect dst = { 0, 0, static_cast<int>(SNAP_W), static_cast<int>(SNAP_H) };
            SDL_RenderCopy(r, intermediate, &src, &dst);
        }

        SDL_SetRenderTarget(r, nullptr);
        pu::ui::render::DeleteTexture(intermediate);
    } else {
        if (intermediate) {
            pu::ui::render::DeleteTexture(intermediate);
        }
        SDL_SetRenderTarget(r, nullptr);
    }
    // snap may be nullptr if texture creation failed.  QdMinimizedDockEntry::Render
    // skips the SDL_RenderCopy if snapshot_ is nullptr (renders dark placeholder).

    // ── Pre-create dock entry ─────────────────────────────────────────────────
    // Store entry as pending; FinalizeMinimize will promote it when animation ends.
    pending_minimize_entry_ = QdMinimizedDockEntry::New(win->GetTitle(), snap,
                                                         win->GetProgramId());
    pending_minimize_entry_->on_restore_requested = [this](QdMinimizedDockEntry* e) {
        RestoreWindow(e);
    };
    pending_minimize_win_ = win;

    // Wire restore callback immediately so the pending entry can fire it from the
    // dock even before FinalizeMinimize is called (edge case: user taps very quickly).

    // ── Compute dock tile target for the animation ────────────────────────────
    // Tile index = future position in minimized_entries_ (will become last entry).
    const s32 tile_idx = static_cast<s32>(minimized_entries_.size()); // after finalize it will be at this index
    const s32 tile_w   = static_cast<s32>(SNAP_W) + 8;
    const s32 dock_y   = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H);
    // Tiles pack right-to-left; index 0 = rightmost.
    const s32 tile_x   = static_cast<s32>(SCREEN_W) - tile_w * (tile_idx + 1) - 8;
    const s32 tile_y   = dock_y + (static_cast<s32>(DOCK_H) - (static_cast<s32>(SNAP_H) + 8)) / 2;

    win->SetMinimizeTarget(tile_x, tile_y);

    // ── Start minimize animation ──────────────────────────────────────────────
    win->BeginMinimizeAnimation();
    // The animation will progress each frame in OnRender.  When it completes,
    // QdWindow fires on_minimize_requested → FinalizeMinimize.
}

void QdWindowManager::FinalizeMinimize(QdWindow* win) {
    if (!win || win != pending_minimize_win_) {
        // Mismatched call — ignore.
        return;
    }

    // Remove win from open_windows_.
    auto it = std::find_if(open_windows_.begin(), open_windows_.end(),
                           [win](const QdWindow::Ref& r) { return r.get() == win; });
    if (it != open_windows_.end()) {
        open_windows_.erase(it);
    }

    // Promote pending dock entry to minimized_entries_.
    if (pending_minimize_entry_) {
        minimized_entries_.push_back(std::move(pending_minimize_entry_));
    }

    pending_minimize_win_ = nullptr;
}

void QdWindowManager::RestoreWindow(QdMinimizedDockEntry* entry) {
    if (!entry) {
        return;
    }

    // Find the entry in minimized_entries_.
    auto it = std::find_if(minimized_entries_.begin(), minimized_entries_.end(),
                           [entry](const QdMinimizedDockEntry::Ref& r) {
                               return r.get() == entry;
                           });
    if (it == minimized_entries_.end()) {
        // Not found — may have been double-restored; ignore.
        return;
    }

    // Record dock tile position (the entry may be the only place this is stored).
    const s32 dock_x = entry->GetTileX();
    const s32 dock_y = entry->GetTileY();
    const std::string title = entry->GetTitle();
    const u64 program_id    = entry->GetProgramId();

    // Remove entry from minimized_entries_ first (releases snapshot texture via dtor).
    minimized_entries_.erase(it);

    // Create a new window at a staggered position, starting from the dock tile.
    // The restore animation will interpolate from (dock_x, dock_y) to the stagger pos.
    const s32 target_x = next_stagger_x_;
    const s32 target_y = next_stagger_y_;

    // Advance stagger for next window.
    next_stagger_x_ += LAUNCH_STAGGER;
    next_stagger_y_ += LAUNCH_STAGGER;

    // Wrap if the next window would exceed the usable content area.
    const s32 max_stagger_x = static_cast<s32>(SCREEN_W) - static_cast<s32>(DEFAULT_WIN_W) - 16;
    const s32 max_stagger_y = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H)
                              - static_cast<s32>(DEFAULT_WIN_H) - 16;
    if (next_stagger_x_ > max_stagger_x || next_stagger_y_ > max_stagger_y) {
        next_stagger_x_ = 104;
        next_stagger_y_ = 56;
    }

    // For restore we create a minimal window with no content element (restored windows
    // in v1.10 are visual-only; the caller (OpenVaultWindow etc.) is expected to set
    // content via SetContent after RestoreWindow if the content is not preserved.
    // In v1.10 the four opener methods always create fresh layouts, so we omit content here.
    auto win = QdWindow::New(title, nullptr,
                             target_x, target_y,
                             static_cast<s32>(DEFAULT_WIN_W),
                             static_cast<s32>(DEFAULT_WIN_H));
    win->SetProgramId(program_id);

    // Start restore animation from dock tile to stagger position.
    win->BeginRestoreAnimation(target_x, target_y,
                                static_cast<s32>(DEFAULT_WIN_W),
                                static_cast<s32>(DEFAULT_WIN_H),
                                dock_x, dock_y);

    // OpenWindow wires callbacks and adds to z-order.
    OpenWindow(std::move(win));
}

void QdWindowManager::BringToFront(QdWindow* win) {
    if (!win || open_windows_.empty()) {
        return;
    }

    auto it = std::find_if(open_windows_.begin(), open_windows_.end(),
                           [win](const QdWindow::Ref& r) { return r.get() == win; });
    if (it == open_windows_.end()) {
        return;
    }

    // Already at the back (topmost)?  Nothing to do.
    if (&*it == &open_windows_.back()) {
        return;
    }

    // Move to back: rotate so this entry ends up at open_windows_.back().
    std::rotate(it, it + 1, open_windows_.end());

    // Mark the new top window focused; all others unfocused.
    for (auto& w : open_windows_) {
        w->SetFocused(w.get() == win);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

void QdWindowManager::LayoutDockEntries() {
    // Dock band: y = SCREEN_H - DOCK_H to SCREEN_H.
    const s32 dock_band_y = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H);
    const s32 tile_w      = static_cast<s32>(SNAP_W) + 8;
    const s32 tile_h      = static_cast<s32>(SNAP_H) + 8;
    // Vertically centre tiles in the dock band.
    const s32 tile_y      = dock_band_y + (static_cast<s32>(DOCK_H) - tile_h) / 2;

    // Pack right-to-left from (SCREEN_W - tile_w - 8).
    const s32 right_edge = static_cast<s32>(SCREEN_W) - 8;

    for (size_t i = 0; i < minimized_entries_.size(); ++i) {
        const s32 tile_x = right_edge - tile_w * static_cast<s32>(i + 1);
        minimized_entries_[i]->SetTilePosition(tile_x, tile_y);
    }
}

void QdWindowManager::RenderAll(pu::ui::render::Renderer::Ref& drawer) {
    LayoutDockEntries();

    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // Render minimized dock entries (furthest back in Z=4 band).
    for (auto& entry : minimized_entries_) {
        entry->Render(r);
    }

    // Render open windows bottom-to-top (open_windows_[0] is bottom; back is topmost).
    for (auto& win : open_windows_) {
        win->OnRender(drawer, 0, 0);
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool QdWindowManager::PollWindowEvents(u64 keys_down, u64 keys_up, u64 keys_held,
                                        pu::ui::TouchPoint touch_pos)
{
    // Route to top window first (back of open_windows_).
    for (auto it = open_windows_.rbegin(); it != open_windows_.rend(); ++it) {
        if ((*it)->PollEvent(keys_down, keys_up, keys_held, touch_pos)) {
            // Bring touched window to front.
            BringToFront(it->get());
            return true;
        }
    }

    // Then minimized dock entries (left to right; any can be tapped).
    for (auto& entry : minimized_entries_) {
        if (entry->PollEvent(keys_down, keys_up, keys_held, touch_pos)) {
            return true;
        }
    }

    return false;
}

// ── Queries ───────────────────────────────────────────────────────────────────

QdWindow* QdWindowManager::FindWindowByProgramId(u64 pid) {
    if (pid == 0) {
        return nullptr;
    }
    for (auto& w : open_windows_) {
        if (w->GetProgramId() == pid) {
            return w.get();
        }
    }
    return nullptr;
}

QdMinimizedDockEntry* QdWindowManager::FindMinimizedByProgramId(u64 pid) {
    if (pid == 0) {
        return nullptr;
    }
    for (auto& e : minimized_entries_) {
        if (e->GetProgramId() == pid) {
            return e.get();
        }
    }
    return nullptr;
}

} // namespace ul::menu::qdesktop
