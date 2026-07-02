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
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // STAGGER_ORIGIN_*, DOCK_RIGHT_PAD, DOCK_TILE_GAP
#include <ul/menu/qdesktop/qd_NsIconCache.hpp>     // suspended-app icon lookup
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/menu/ui/ui_Common.hpp>                // g_GlobalSettings type
#include <pu/ui/render/render_SDL2.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// g_GlobalSettings is defined in main.cpp at file/global scope as
// `ul::menu::ui::GlobalSettings g_GlobalSettings;`.  Declare the extern at
// file scope (outside any namespace) so the linker matches the same symbol
// rather than treating it as `ul::menu::qdesktop::g_GlobalSettings`.
extern ::ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── Ctor / dtor ───────────────────────────────────────────────────────────────

QdWindowManager::QdWindowManager()
    : next_stagger_x_(STAGGER_ORIGIN_X),  // 104 = TOPBAR_H*2+8 — D3 fix
      next_stagger_y_(STAGGER_ORIGIN_Y),  //  56 = TOPBAR_H+8
      pending_minimize_win_(nullptr)
{
    // open_windows_ and minimized_entries_ are default-constructed as empty vectors.
    // A2-OPT-2: pre-allocate scratch vectors so per-frame copies do not heap-allocate.
    scratch_windows_.reserve(128);
    scratch_entries_.reserve(128);
    // A2-OPT-6: focused_win_ starts null (no windows yet).
    focused_win_ = nullptr;
}

QdWindowManager::~QdWindowManager() {
    // Join any in-flight NACP background thread before tearing down state.
    // nacp_result_ready_ is not checked here — we just need the thread gone.
    if (nacp_bg_thread_.joinable()) {
        nacp_bg_thread_.join();
    }

    // W15-C ORPHAN-1 FIX: untrack every ledger handle BEFORE clearing the
    // owning containers.  Previously the dtor cleared open_windows_ /
    // minimized_entries_ without iterating the parallel ledger-handle maps,
    // leaving phantom entries in QdResourceLedger that made Monitor's
    // Resources view + perf-log win/snap counts inaccurate after WM teardown.
    for (const auto &kv : open_window_ledger_handles_) {
        UL_LEDGER_UNTRACK(kv.second);
    }
    open_window_ledger_handles_.clear();
    for (const auto &kv : minimized_ledger_handles_) {
        UL_LEDGER_UNTRACK(kv.second);
    }
    minimized_ledger_handles_.clear();

    // QdWindow::Ref and QdMinimizedDockEntry::Ref are shared_ptrs; destructors run
    // automatically as the vectors are cleared.  Explicit clear is defensive.
    open_windows_.clear();
    minimized_entries_.clear();
    suspended_app_entries_.clear();
    pending_minimize_entry_.reset();
    pending_minimize_win_ = nullptr;
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

// WIN-SCALE-FIX-3: hard cap on simultaneously-open (non-minimized) windows.
// At ~12 MB/window baked (content+shadow+ring), 64 baked windows = ~768 MB which
// would exhaust VRAM on Switch Erista (~4 GB total, ~1–1.5 GB available to applets
// including all other textures).  The eviction logic (kMaxBakedWindows=8) means
// bake VRAM is capped at ~96 MB regardless of this limit, BUT the open_windows_
// vector, scratch vectors, occlusion pass, and sdl renderer command buffers all
// scale with window count.  Cap at 64 so the WM never falls over structurally.
// The user sees a non-fatal log warning; the new window is silently dropped
// (same as attempting to open a duplicate).
static constexpr size_t kMaxOpenWindows = 128;  // raised from 64 to probe the real 100-window ceiling; VRAM bounded by kMaxBakedWindows eviction. If HW shows structural failure (NVN cmd-buffer) below 100, lower to the proven-stable value.

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

    // WIN-SCALE-FIX-3: graceful cap — refuse to open a new window if we have
    // already reached kMaxOpenWindows.  Log the refusal and return cleanly so
    // the caller doesn't crash; the refused window's shared_ptr is dropped here.
    if (open_windows_.size() >= kMaxOpenWindows) {
        UL_LOG_WARN("qdesktop: OpenWindow refused — window cap (%zu) reached. "
                    "Close some windows first.", kMaxOpenWindows);
        return;
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

    // Record the stagger slot assigned to this window for LIFO reclaim on close.
    open_stagger_positions_[raw] = std::make_pair(
        next_stagger_x_ - LAUNCH_STAGGER,
        next_stagger_y_ - LAUNCH_STAGGER
    );

    open_windows_.push_back(std::move(win));
    // W6-LEDGER: track this new open window.
    {
        QdWindow* back_raw = open_windows_.back().get();
        const std::string& title = back_raw->GetTitle();
        open_window_ledger_handles_[back_raw] = UL_LEDGER_TRACK(
            QdResKind::Window, title.c_str(), 0);
    }
    // A2-OPT-5: cursor hover must be re-evaluated (new window may be under cursor).
    hover_dirty_ = true;
    // The newly added window is already at the back (topmost); BringToFront is a
    // no-op since it's already last, but call it for robustness.
    BringToFront(open_windows_.back().get());
}

void QdWindowManager::CloseWindow(QdWindow* win) {
    if (!win) {
        return;
    }

    // v2.9.11 — clear any in-flight interaction state on the closing window
    // before it gets evicted.  Belt-and-suspenders alongside the per-window
    // watchdog: prevents a stuck flag from outliving the window via shared
    // PollEvent paths (snapshot iteration, BringToFront, etc.).
    win->ResetInteractionState();

    // W6-LEDGER: untrack before removing from the vector.
    {
        auto lit = open_window_ledger_handles_.find(win);
        if (lit != open_window_ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(lit->second);
            open_window_ledger_handles_.erase(lit);
        }
    }
    auto it = std::find_if(open_windows_.begin(), open_windows_.end(),
                           [win](const QdWindow::Ref& r) { return r.get() == win; });
    if (it != open_windows_.end()) {
        open_windows_.erase(it);
    }

    // v1.10.3.6 LIFO stagger reclaim: if the closing window was the last one opened
    // (i.e., its stagger slot is one step before the current next_stagger_*), roll
    // back the counter so the slot is reused by the next OpenWindow call.
    auto sit = open_stagger_positions_.find(win);
    if (sit != open_stagger_positions_.end()) {
        const s32 assigned_x = sit->second.first;
        const s32 assigned_y = sit->second.second;
        if (assigned_x == next_stagger_x_ - LAUNCH_STAGGER &&
            assigned_y == next_stagger_y_ - LAUNCH_STAGGER) {
            next_stagger_x_ = assigned_x;
            next_stagger_y_ = assigned_y;
        }
        open_stagger_positions_.erase(sit);
    }

    // v3.0.2 (FIX-3 from cumulative-tech-debt-audit.md): clear any pending
    // reopen functor keyed on this window.  Without this, every window
    // closed via the × button (not minimized) leaves a std::function in
    // pending_reopen_map_ that captures the desktop element's `this`.
    // Pointer key is now dangling; map grows unbounded across a session.
    // Not a use-after-free (the captured `this` is valid for uMenu's
    // lifetime), just a real memory leak.  Erase on close to bound the
    // map to currently-minimized windows only.
    pending_reopen_map_.erase(win);

    // A2-OPT-6: if the closed window was the focused one, null the fast-track ptr.
    if (focused_win_ == win) focused_win_ = nullptr;
    // A2-OPT-5: window set changed; cursor hover needs re-scan next frame.
    hover_dirty_ = true;
    // A3-OPT-5: dock layout unaffected by open-window close, but mark dirty for safety.
    dock_layout_dirty_ = true;
}

void QdWindowManager::SetPendingReopen(QdWindow* key, std::function<void()> fn) {
    pending_reopen_map_[key] = std::move(fn);
}

void QdWindowManager::TakeStaggerPos(s32 win_w, s32 win_h, s32 &out_x, s32 &out_y) {
    out_x = next_stagger_x_;
    out_y = next_stagger_y_;

    next_stagger_x_ += LAUNCH_STAGGER;
    next_stagger_y_ += LAUNCH_STAGGER;

    // D4 fix: cascade bounds — 16px right margin, 8px bottom clearance above dock.
    // Named constants from qd_LayoutConstants.hpp.
    const s32 max_x = static_cast<s32>(SCREEN_W) - win_w - DOCK_RIGHT_PAD * 2;  // 16px
    const s32 max_y = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H) - win_h - DOCK_TILE_GAP;  // 8px
    if (next_stagger_x_ > max_x || next_stagger_y_ > max_y) {
        // D3 fix: reset stagger to named origin instead of bare 104/56.
        next_stagger_x_ = STAGGER_ORIGIN_X;
        next_stagger_y_ = STAGGER_ORIGIN_Y;
    }
}

void QdWindowManager::MinimizeWindow(QdWindow* win,
                                      pu::ui::render::Renderer::Ref& drawer)
{
    if (!win) {
        return;
    }

    // v2.9.11 — clear stale drag/resize flags BEFORE capturing the snapshot
    // and starting the minimize animation.  If the user tapped minimize
    // mid-resize (rare but possible), the resize_drag_active_ flag would
    // otherwise survive into the minimized state and propagate back on
    // restore, swallowing every future input.
    win->ResetInteractionState();

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
        // WIN-SCALE-FIX-1: guard SetRenderTarget for the intermediate capture.
        // On VRAM exhaustion the texture was created but the target redirect can
        // still fail; treat exactly like creation failure (snap stays nullptr,
        // dock entry renders a dark placeholder).
        if (SDL_SetRenderTarget(r, intermediate) != 0) {
            UL_LOG_WARN("qdesktop: MinimizeWindow intermediate SetRenderTarget failed (%s) "
                        "— snap will be nullptr (dark placeholder)", SDL_GetError());
            pu::ui::render::DeleteTexture(intermediate);
            SDL_SetRenderTarget(r, nullptr);
        } else {
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
                // WIN-SCALE-FIX-1: guard snap SetRenderTarget.
                if (SDL_SetRenderTarget(r, snap) != 0) {
                    UL_LOG_WARN("qdesktop: MinimizeWindow snap SetRenderTarget failed (%s) "
                                "— snap discarded", SDL_GetError());
                    SDL_DestroyTexture(snap);
                    snap = nullptr;
                } else {
                    SDL_SetRenderDrawColor(r, 0x1A, 0x1A, 0x1A, 0xFF);
                    SDL_RenderClear(r);

                    // Source: window's on-screen pixel rect within the intermediate.
                    SDL_Rect src = { win->GetX(), win->GetY(), win_px_w, win_px_h };
                    // Dest: full SNAP texture.
                    SDL_Rect dst = { 0, 0, static_cast<int>(SNAP_W), static_cast<int>(SNAP_H) };
                    SDL_RenderCopy(r, intermediate, &src, &dst);
                }
            }

            SDL_SetRenderTarget(r, nullptr);
            pu::ui::render::DeleteTexture(intermediate);
        }
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

    // Transfer the pending reopen functor onto the dock entry.  This was registered
    // by the WmBridge opener via SetPendingReopen(win, fn) inside on_minimize_begin_
    // before this call.  v1.10.3.6: keyed by win pointer so concurrent minimizes
    // don't overwrite each other.
    {
        auto it = pending_reopen_map_.find(win);
        if (it != pending_reopen_map_.end()) {
            pending_minimize_entry_->on_reopen = std::move(it->second);
            pending_reopen_map_.erase(it);
        }
    }

    pending_minimize_win_ = win;

    // Wire restore callback immediately so the pending entry can fire it from the
    // dock even before FinalizeMinimize is called (edge case: user taps very quickly).

    // ── Compute dock tile target for the animation ────────────────────────────
    // Tile index = future position in minimized_entries_ (will become last entry).
    // W5-TRANSITIONS #3: include any pending slot so concurrent minimize calls
    // do not both resolve to the same dock tile position.
    const s32 tile_idx = static_cast<s32>(minimized_entries_.size())
                       + (pending_minimize_entry_ != nullptr ? 1 : 0);
    // D5 fix: DOCK_TILE_GAP=8 replaces all magic `8` dock-tile padding literals.
    const s32 tile_w   = static_cast<s32>(SNAP_W) + DOCK_TILE_GAP;
    const s32 dock_y   = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H);
    // Tiles pack right-to-left; index 0 = rightmost.
    const s32 tile_x   = static_cast<s32>(SCREEN_W) - tile_w * (tile_idx + 1) - DOCK_RIGHT_PAD;
    const s32 tile_y   = dock_y + (static_cast<s32>(DOCK_H) - (static_cast<s32>(SNAP_H) + DOCK_TILE_GAP)) / 2;

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
    {
        // W6-LEDGER: untrack the open-window entry (it's being turned into a snap).
        auto lit = open_window_ledger_handles_.find(win);
        if (lit != open_window_ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(lit->second);
            open_window_ledger_handles_.erase(lit);
        }
    }
    auto it = std::find_if(open_windows_.begin(), open_windows_.end(),
                           [win](const QdWindow::Ref& r) { return r.get() == win; });
    if (it != open_windows_.end()) {
        open_windows_.erase(it);
    }

    // Erase from stagger map — minimized windows don't hold a stagger slot.
    open_stagger_positions_.erase(win);

    // Promote pending dock entry to minimized_entries_.
    if (pending_minimize_entry_) {
        // v3.2.1 (W4-LEAKS P1 #4): cap minimized window count.  Each entry
        // holds a full-screen RGBA snapshot (~8 MiB at 1280×720×4).  A user
        // who minimizes ~10 windows without closing them consumes ~80 MiB
        // of GPU memory.  Evict the OLDEST minimized window (front of the
        // vector) before pushing the new one.  Evicted window can be
        // reopened from the launchpad — its snapshot is gone but the app
        // state is recovered from its source.
        constexpr size_t kMaxMinimized = 8;
        while (minimized_entries_.size() >= kMaxMinimized) {
            // W6-LEDGER: untrack evicted snap before destroying it.
            const QdMinimizedDockEntry* evict_raw = minimized_entries_.front().get();
            auto elit = minimized_ledger_handles_.find(evict_raw);
            if (elit != minimized_ledger_handles_.end()) {
                UL_LEDGER_UNTRACK(elit->second);
                minimized_ledger_handles_.erase(elit);
            }
            // ~Ref destructor releases the SDL_Texture snapshot.
            minimized_entries_.erase(minimized_entries_.begin());
        }
        // W6-LEDGER: track the new minimized snapshot (SNAP_W × SNAP_H × 4 bytes).
        {
            const QdMinimizedDockEntry* snap_raw = pending_minimize_entry_.get();
            const std::string& snap_title = win->GetTitle();
            const size_t snap_bytes = static_cast<size_t>(SNAP_W)
                                    * static_cast<size_t>(SNAP_H) * 4u;
            minimized_ledger_handles_[snap_raw] = UL_LEDGER_TRACK(
                QdResKind::MinimizedSnap, snap_title.c_str(), snap_bytes);
        }
        minimized_entries_.push_back(std::move(pending_minimize_entry_));
    }

    pending_minimize_win_ = nullptr;

    // A2-OPT-6: the minimized window is no longer in open_windows_, so the
    // focused_win_ ptr would be dangling if it pointed at it.
    if (focused_win_ == win) focused_win_ = nullptr;
    // A3-OPT-5: dock entry list changed; layout must be recomputed.
    dock_layout_dirty_ = true;
    // A2-OPT-5: window set changed; hover must be re-evaluated.
    hover_dirty_ = true;
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

    // Capture the reopen functor before erasing the entry (erase destroys it).
    // on_reopen is set by WmBridge via SetPendingReopen → MinimizeWindow → dock entry.
    std::function<void()> reopen_fn = entry->on_reopen;

    // W6-LEDGER: untrack before removing.
    {
        auto lit = minimized_ledger_handles_.find(entry);
        if (lit != minimized_ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(lit->second);
            minimized_ledger_handles_.erase(lit);
        }
    }
    // Remove entry from minimized_entries_ (releases snapshot texture via dtor).
    minimized_entries_.erase(it);
    // A3-OPT-5: dock entry list changed; layout must be recomputed next frame.
    dock_layout_dirty_ = true;

    if (!reopen_fn) {
        // No reopen functor — entry was created before v1.10.3.4 or was wired
        // without on_reopen.  Nothing to restore; the window is gone.
        return;
    }

    // Point the stagger position to the desired restore target.
    // The WmBridge opener calls TakeStaggerPos() which returns next_stagger_x_/y_
    // and advances them.  We pre-set them here so the opener lands the restored
    // window at a fresh stagger slot rather than wherever the counter currently sits.
    // (next_stagger_x_/y_ are already at the correct next-slot value; no override
    // needed — just call the opener and let TakeStaggerPos advance normally.)

    // Re-invoke the correct opener.  This:
    //  1. Creates a fresh layout element with real content.
    //  2. Wires on_tick (for Settings/About live refresh).
    //  3. Wires on_minimize_begin_ (required for the window to be minimizable again).
    //  4. Calls wm_.OpenWindow() which wires on_close_requested and on_minimize_requested.
    // All of Fixes 1, 2, 6 resolve from this single call.
    reopen_fn();
}

// Z2.4 — Close without restore.  Drops the minimized entry from the list
// (dtor frees its snapshot SDL_Texture).  Safe to call with stale ptr.
void QdWindowManager::CloseMinimizedEntry(QdMinimizedDockEntry* entry) {
    if (!entry) return;
    auto it = std::find_if(minimized_entries_.begin(), minimized_entries_.end(),
                           [entry](const QdMinimizedDockEntry::Ref& r) {
                               return r.get() == entry;
                           });
    if (it == minimized_entries_.end()) {
        return;  // already gone
    }
    UL_LOG_INFO("wm: CloseMinimizedEntry: dropping minimized tile (no restore)");
    // W6-LEDGER: untrack before destroying.
    {
        auto lit = minimized_ledger_handles_.find(entry);
        if (lit != minimized_ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(lit->second);
            minimized_ledger_handles_.erase(lit);
        }
    }
    minimized_entries_.erase(it);
    // A3-OPT-5: dock entry list changed; layout must be recomputed next frame.
    dock_layout_dirty_ = true;
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
        // Still mark it focused if focused_win_ disagrees (e.g. first OpenWindow call).
        if (focused_win_ != win) {
            if (focused_win_ != nullptr) focused_win_->SetFocused(false);
            win->SetFocused(true);
            focused_win_ = win;
        }
        return;
    }

    // Move to back: rotate so this entry ends up at open_windows_.back().
    std::rotate(it, it + 1, open_windows_.end());

    // A2-OPT-6: O(1) focus update — unfocus previous, focus new.
    // Replaces the O(N) SetFocused loop.
    if (focused_win_ != nullptr && focused_win_ != win) {
        focused_win_->SetFocused(false);
    }
    win->SetFocused(true);
    focused_win_ = win;

    // A2-OPT-5: z-order changed; hover highlight on new topmost needs recalc.
    hover_dirty_ = true;
}

// ── Render ────────────────────────────────────────────────────────────────────

// ── Suspended-app sync ───────────────────────────────────────────────────────

// RefreshSuspendedApps — RESIDUAL OPEN-HITCH FIX
//
// Problem: nsGetApplicationControlData blocks on NAND/gamecard IPC (~0.8 s) AND
// the original code allocated a ~147 KB NsApplicationControlData on the render-thread
// stack — both producing a visible frame freeze on every new suspended-app event.
//
// Fix (two-phase):
//   Phase A (render thread): detect a new suspended_app_id; if no bg thread is
//     already running for this id, join any finished thread, then launch a new one
//     to do the NACP IPC call on a background std::thread.  Return immediately —
//     the render thread is never blocked.
//   Phase B (render thread): on subsequent ticks, check nacp_result_ready_.  When
//     set, consume the resolved title + do icon lookup (GetSharedNsIconCache().Get
//     must stay on the render thread — A4-RF-03: SDL_DestroyTexture is render-only),
//     create the dock entry, and clear the ready flag.
//
// Threading contract:
//   • nacp_result_mutex_ protects nacp_result_ (program_id + title).
//   • nacp_result_ready_ is atomic; bg thread sets it, render thread clears it.
//   • nacp_inflight_id_ tracks the in-flight program_id so we can discard stale
//     results if suspended_app_id changes between launch and consume.
//   • nacp_bg_thread_ is always joined (never detached) — either here (if joinable
//     and not running) or in the destructor.  A single suspended app at a time
//     (HOS invariant) means there is at most one bg thread at any moment.
void QdWindowManager::RefreshSuspendedApps() {
    const u64 cur_id = ::g_GlobalSettings.system_status.suspended_app_id;

    // ── Fast-path A: id unchanged AND no pending result ──────────────────────
    if (cur_id == cached_suspended_id_ && !nacp_result_ready_.load(std::memory_order_acquire)) {
        return;
    }

    // ── Handle clear (app resumed / terminated) ───────────────────────────────
    if (cur_id == 0) {
        if (!suspended_app_entries_.empty()) {
            UL_LOG_INFO("wm: suspended_app_id cleared — dropping %zu dock entries",
                        suspended_app_entries_.size());
            suspended_app_entries_.clear();
            dock_layout_dirty_ = true;  // A3-OPT-5
        }
        // Cancel any in-flight request — the result will be discarded when it
        // arrives because nacp_inflight_id_ != cur_id (0).  Join the thread now
        // if it has already finished to free OS resources; do NOT block if still
        // running (we return immediately; dtor will join).
        cached_suspended_id_ = 0;
        cached_suspended_title_.clear();
        nacp_result_ready_.store(false, std::memory_order_release);
        return;
    }

    // ── Phase B: consume a completed NACP result ─────────────────────────────
    if (nacp_result_ready_.load(std::memory_order_acquire)) {
        // Grab the resolved title under the mutex.
        NacpResult local_result;
        {
            std::lock_guard<std::mutex> lk(nacp_result_mutex_);
            local_result = nacp_result_;
        }
        nacp_result_ready_.store(false, std::memory_order_release);

        // Discard stale results (suspended_app_id changed while thread ran).
        if (local_result.program_id != cur_id) {
            UL_LOG_WARN("wm: NACP result for 0x%016lX discarded (cur=0x%016lX)",
                        static_cast<unsigned long>(local_result.program_id),
                        static_cast<unsigned long>(cur_id));
            // Fall through — will fire a new bg thread below for cur_id.
        } else {
            // Icon lookup on the render thread (SDL_DestroyTexture safety — A4-RF-03).
            SDL_Renderer *renderer = pu::ui::render::GetMainRenderer();
            SDL_Texture *icon_tex = nullptr;
            if (renderer != nullptr) {
                icon_tex = GetSharedNsIconCache().Get(cur_id, renderer);
            }

            suspended_app_entries_.clear();
            suspended_app_entries_.push_back(
                QdSuspendedAppDockEntry::New(cur_id, local_result.title, icon_tex));

            UL_LOG_INFO("wm: suspended-app dock entry added program_id=0x%016lX title='%s' icon=%s",
                        static_cast<unsigned long>(cur_id), local_result.title.c_str(),
                        icon_tex ? "ok" : "MISSING");

            cached_suspended_id_    = cur_id;
            cached_suspended_title_ = local_result.title;
            dock_layout_dirty_ = true;  // A3-OPT-5
            return;
        }
    }

    // ── Phase A: fire bg thread for new/changed suspended_app_id ─────────────
    // Skip if a thread is already in flight for this exact id.
    if (nacp_inflight_id_ == cur_id) {
        return;  // bg thread is running — wait for Phase B on the next tick
    }

    // Join previous thread if it finished (non-blocking: joinable() is true only
    // when the thread object holds a valid thread; we can probe readiness via the
    // ready flag which the thread sets last).  If not yet ready, we simply don't
    // join here — the destructor will.
    if (nacp_bg_thread_.joinable() && !nacp_result_ready_.load(std::memory_order_acquire)) {
        // Thread still running (for a different id) — join to avoid resource leak.
        // This is a rare edge: suspended_app_id changed AGAIN before the previous
        // nsGetApplicationControlData completed.  The HOS single-app invariant makes
        // this extremely unlikely.  Block here briefly to reclaim the thread.
        nacp_bg_thread_.join();
    } else if (nacp_bg_thread_.joinable()) {
        nacp_bg_thread_.join();  // thread finished; collect it before replacing
    }

    nacp_inflight_id_  = cur_id;
    nacp_result_ready_.store(false, std::memory_order_release);

    // Launch background thread: heap-allocate the 147 KB NACP struct, run the
    // IPC call, resolve the title, write to nacp_result_ under the mutex, and
    // set nacp_result_ready_ so the render thread picks it up next tick.
    nacp_bg_thread_ = std::thread([this, cur_id]() {
        // Heap-allocate — 147 KB is far too large for the Switch stack.
        NsApplicationControlData *ctrl = static_cast<NsApplicationControlData *>(
            std::malloc(sizeof(NsApplicationControlData)));

        std::string title;
        if (ctrl != nullptr) {
            u64 actual_size = 0;
            Result rc = nsGetApplicationControlData(
                NsApplicationControlSource_Storage,
                cur_id,
                ctrl,
                sizeof(NsApplicationControlData),
                &actual_size);
            if (R_FAILED(rc)) {
                rc = nsGetApplicationControlData(
                    NsApplicationControlSource_CacheOnly,
                    cur_id,
                    ctrl,
                    sizeof(NsApplicationControlData),
                    &actual_size);
            }
            if (R_SUCCEEDED(rc) && actual_size >= sizeof(NacpStruct)) {
                NacpLanguageEntry *lang_entry = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&ctrl->nacp, &lang_entry))
                        && lang_entry != nullptr) {
                    title = lang_entry->name;
                }
            } else {
                UL_LOG_WARN("wm: bg: nsGetApplicationControlData failed for 0x%016lX rc=0x%08X",
                            static_cast<unsigned long>(cur_id), rc);
            }
            std::free(ctrl);
        }

        if (title.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%016lX", static_cast<unsigned long>(cur_id));
            title = buf;
        }

        // Post the result — render thread picks it up in Phase B on the next tick.
        {
            std::lock_guard<std::mutex> lk(nacp_result_mutex_);
            nacp_result_.program_id = cur_id;
            nacp_result_.title      = std::move(title);
        }
        nacp_result_ready_.store(true, std::memory_order_release);
    });
}

void QdWindowManager::LayoutDockEntries() {
    // A3-OPT-5: skip recompute when the dock entry list hasn't changed.
    if (!dock_layout_dirty_) return;
    dock_layout_dirty_ = false;

    // Dock band: y = SCREEN_H - DOCK_H to SCREEN_H.
    const s32 dock_band_y = static_cast<s32>(SCREEN_H) - static_cast<s32>(DOCK_H);
    // D6 fix: DOCK_TILE_GAP=8 and DOCK_RIGHT_PAD=8 replace all magic `8` literals here.
    const s32 tile_w      = static_cast<s32>(SNAP_W) + DOCK_TILE_GAP;
    const s32 tile_h      = static_cast<s32>(SNAP_H) + DOCK_TILE_GAP;
    // Vertically centre tiles in the dock band.
    const s32 tile_y      = dock_band_y + (static_cast<s32>(DOCK_H) - tile_h) / 2;

    // Pack right-to-left from (SCREEN_W - DOCK_RIGHT_PAD).
    const s32 right_edge = static_cast<s32>(SCREEN_W) - DOCK_RIGHT_PAD;

    // Layout order, right-to-left:
    //   slot 0 (right-most): suspended_app_entries_ (Switch HOS apps the user
    //     pressed HOME on — the canonical "running thing")
    //   slot 1+: minimized_entries_ (in-uMenu windows the user minimized)
    s32 slot = 0;
    for (size_t i = 0; i < suspended_app_entries_.size(); ++i, ++slot) {
        const s32 tile_x = right_edge - tile_w * (slot + 1);
        suspended_app_entries_[i]->SetTilePosition(tile_x, tile_y);
    }
    for (size_t i = 0; i < minimized_entries_.size(); ++i, ++slot) {
        const s32 tile_x = right_edge - tile_w * (slot + 1);
        minimized_entries_[i]->SetTilePosition(tile_x, tile_y);
    }
}

void QdWindowManager::RenderAll(pu::ui::render::Renderer::Ref& drawer) {
    // NOTE: RefreshSuspendedApps() is NOT called here.  It must be called
    // by the owner (QdDesktopIconsElement::OnRender) BEFORE the
    // GetTotalWindowCount() gate so we don't get a chicken-and-egg where
    // the gate is 0 because the vector is empty because the gate was 0.
    LayoutDockEntries();

    SDL_Renderer* r = pu::ui::render::GetMainRenderer();

    // v1.10.3.10.5: snapshot the underlying vectors before iterating.  When
    // a minimize animation finishes inside QdWindow::AdvanceAnimation (called
    // from OnRender), QdWindow fires on_minimize_requested → FinalizeMinimize
    // → open_windows_.erase(it).  Without a snapshot, the range-for's iterator
    // would be invalidated mid-loop and subsequent iterations dereference a
    // stale shared_ptr block, faulting in tick_counter_++ (NULL + 0x1e0).
    //
    // Repro before the fix: open ≥ 2 windows, BL-tap one to minimize, the
    // animation completes during RenderAll's loop and the next window in the
    // loop calls OnRender on memory that was just shifted by vector::erase.
    // Crash log reference: addr2line resolves PC=uMenu+0x79d0c to
    // qd_Window.cpp:199 (`tick_counter_++`), LR points to the range-for at
    // qd_WindowManager.cpp:361 — see DIAGNOSIS.md in
    // staging/v1.10.3.10.5-INCIDENT-2026-04-29/.
    //
    // Snapshots are vectors of shared_ptrs, so the copy just bumps refcounts
    // (cheap) and keeps the windows alive even if the underlying open_windows_
    // shrinks mid-loop.  Same protection for minimized_entries_ since
    // RestoreWindow erases from it during touch dispatch.
    //
    // A2-OPT-2 (CONVERGENT): reuse pre-allocated member scratch vectors instead
    // of allocating new local vectors each frame.  The crash-safety snapshot
    // mechanism is PRESERVED — we just assign into the scratch vectors (which
    // already have capacity reserved from the ctor) rather than constructing
    // fresh locals.  shared_ptr ref-bumps happen identically.
    scratch_entries_  = minimized_entries_;
    auto& entries_snapshot           = scratch_entries_;
    // suspended_app_entries_ is a small (≤1) vector; plain local copy is fine here.
    auto suspended_entries_snapshot  = suspended_app_entries_;
    scratch_windows_ = open_windows_;
    auto& windows_snapshot           = scratch_windows_;

    // Render suspended-app dock entries first (they're on the right side
    // of the dock; rendering order doesn't matter visually since tiles
    // don't overlap, but doing them first keeps the Z=4 band ordering
    // consistent: suspended apps at the back of the band's z-list).
    for (auto& entry : suspended_entries_snapshot) {
        if (entry) entry->Render(r);
    }
    // Render minimized dock entries (furthest back in Z=4 band).
    for (auto& entry : entries_snapshot) {
        if (entry) entry->Render(r);
    }

    // Render open windows bottom-to-top (open_windows_[0] is bottom; back is topmost).
    //
    // WIN-5 UNION-OCCLUSION CULL + VISIBLE-REGION SCISSOR CLIP:
    //
    // Two-pass algorithm:
    //
    // PASS 1 (top-to-bottom): for each window, compute whether it is culled and
    // what visible-region scissor clip to use.  Results stored in per-window
    // local arrays (cull_flags[] and scissor_clips[]).
    //
    //   union_rects accumulates the opaque bodies (DEFLATED by kBodyDeflate px)
    //   of windows already processed (i.e. higher z-order windows) as we walk
    //   downward.  A window's opaque body is added regardless of whether it is
    //   itself culled (transitive occlusion: a culled intermediate window still
    //   blocks the windows beneath it).
    //
    //   Cull test: window's 12-px-inflated envelope fully covered by union → skip.
    //
    //   Scissor clip (Rank 2): for non-culled, partially-covered windows, clip
    //   rendering to the bounding box of (envelope ∖ union), inflated +12 px and
    //   capped to the envelope.  Implemented as: start with the full envelope,
    //   then for each side (left/top/right/bottom) find the furthest coverage
    //   from the union and shrink the clip boundary to just outside it.
    //   Approximation: we tighten only the axis-aligned sides, not arbitrary
    //   polygons — pixel-exact for the common case of windows stacked in a
    //   uniform-direction cascade.
    //
    //   Fully-visible windows (union does not touch their envelope) get nullptr
    //   (no extra clip = paint the full window, same as before).
    //
    // PASS 2 (bottom-to-top): render non-culled windows with their scissor clip.
    //
    // Constants (matching chrome tokens):
    //   kEnvInflate  = 12 px  (focus halo 6 + drop shadow 6)
    //   kBodyDeflate = 14 px  (kBodyRadius = radius/md token = 12 px + 2 safety)
    //
    // Safety:
    //   • Iterates snapshot (not live vector) — crash-safety intact.
    //   • Pure state reads only (GetX/GetY/GetW/GetH/GetState/IsFocused).
    //   • scratch_occ_union_ is pre-allocated member vector (no heap churn).
    //   • Under-estimate: kBodyDeflate strips corners → never culls a visible px.
    //   • +12 inflation on scissor → shadow/halo on exposed edges is always kept.
    //   • Topmost and focused windows always rendered without cull or extra clip.
    static constexpr s32 kEnvInflate  = 12;
    static constexpr s32 kBodyDeflate = 14;

    const size_t win_count = windows_snapshot.size();

    // Per-window results from Pass 1.  Stack-allocate up to 32 entries (N ≤ 32).
    // Using fixed arrays to avoid heap allocation in the hot path.
    static constexpr size_t kMaxWins = 32;
    bool      cull_flags[kMaxWins]  = {};
    SDL_Rect  scissor_rects[kMaxWins] = {};  // only valid when has_scissor[i] is true
    bool      has_scissor[kMaxWins] = {};

    // ── PASS 1: top-to-bottom, build occlusion union, determine cull + scissor ─

    scratch_occ_union_.clear();

    const size_t pass1_count = (win_count <= kMaxWins) ? win_count : kMaxWins;
    for (size_t ri = 0; ri < pass1_count; ++ri) {
        // Walk top-to-bottom: index j = (win_count - 1 - ri) is the z-order index.
        const size_t j = (win_count - 1) - ri;
        const auto& win = windows_snapshot[j];
        if (!win) continue;

        const bool is_top    = (j == win_count - 1);
        const bool animating = (win->GetState() != WindowState::Normal);

        const SDL_Rect envelope = RectInflate(
            win->GetX(), win->GetY(), win->GetW(), win->GetH(), kEnvInflate);

        // ── Cull check ───────────────────────────────────────────────────────
        if (!is_top && !animating && !win->IsFocused()) {
            if (RectFullyCoveredByUnion(envelope, scratch_occ_union_)) {
                cull_flags[j] = true;
                // Still add body to union for transitive occlusion of lower windows.
                const SDL_Rect body = RectDeflate(
                    win->GetX(), win->GetY(), win->GetW(), win->GetH(), kBodyDeflate);
                if (body.w > 0 && body.h > 0) {
                    scratch_occ_union_.push_back(
                        { body.x, body.y, body.x + body.w, body.y + body.h });
                }
                continue;
            }
        }

        // ── Scissor clip (Rank 2) ─────────────────────────────────────────────
        // For the topmost and focused windows: no scissor (nullptr = full render).
        // For others: compute the tightest axis-aligned clip that keeps all
        // unoccluded pixels and their +12 px shadow/halo border.
        if (!is_top && !animating && !win->IsFocused() && !scratch_occ_union_.empty()) {
            // Start with the full envelope as the clip.
            s32 clip_x0 = envelope.x;
            s32 clip_y0 = envelope.y;
            s32 clip_x1 = envelope.x + envelope.w;
            s32 clip_y1 = envelope.y + envelope.h;

            // For each side, tighten the clip by finding the union rects that
            // cover that side and extend furthest inward.
            for (const auto& occ : scratch_occ_union_) {
                // A union rect occludes the LEFT side of this window when it
                // covers the window's full height and overlaps from the left.
                if (occ.y0 <= clip_y0 && occ.y1 >= clip_y1 && occ.x0 <= clip_x0) {
                    // The occluder covers from x0=occ.x0 to occ.x1.  We can
                    // tighten our left clip to occ.x1 (the first uncovered pixel
                    // to the right).  Re-inflate by kEnvInflate to keep shadow.
                    const s32 new_left = occ.x1 - kEnvInflate;
                    if (new_left > clip_x0) clip_x0 = new_left;
                }
                // RIGHT side.
                if (occ.y0 <= clip_y0 && occ.y1 >= clip_y1 && occ.x1 >= clip_x1) {
                    const s32 new_right = occ.x0 + kEnvInflate;
                    if (new_right < clip_x1) clip_x1 = new_right;
                }
                // TOP side.
                if (occ.x0 <= clip_x0 && occ.x1 >= clip_x1 && occ.y0 <= clip_y0) {
                    const s32 new_top = occ.y1 - kEnvInflate;
                    if (new_top > clip_y0) clip_y0 = new_top;
                }
                // BOTTOM side.
                if (occ.x0 <= clip_x0 && occ.x1 >= clip_x1 && occ.y1 >= clip_y1) {
                    const s32 new_bottom = occ.y0 + kEnvInflate;
                    if (new_bottom < clip_y1) clip_y1 = new_bottom;
                }
            }

            // Cap to the full envelope (inflation already applied; don't expand).
            clip_x0 = std::max(clip_x0, envelope.x);
            clip_y0 = std::max(clip_y0, envelope.y);
            clip_x1 = std::min(clip_x1, envelope.x + envelope.w);
            clip_y1 = std::min(clip_y1, envelope.y + envelope.h);

            // WIN-4b TIGHTENING THRESHOLD: only activate the scissor when the
            // visible bbox is MEANINGFULLY smaller than the full envelope — i.e.,
            // its area is ≤ 70% of the envelope area.  For diagonal cascades the
            // occluded region is an L-shape whose bounding box ≈ the full window;
            // the clip barely tightens but each SDL_RenderSetClipRect still flushes
            // the WIN-1 render batch (38ms→45ms in measured v3.7.30 ft_max with ZERO
            // fps gain).  Skipping the scissor in that case (clip area > 70% of
            // envelope) restores pre-scissor v3.7.29 clip lifecycle for diagonal
            // cascades while keeping the optimisation for axis-aligned occlusion
            // (e.g. a window fully behind one window above it) where the clip is
            // genuinely tight.
            const bool narrowed = (clip_x0 > envelope.x || clip_y0 > envelope.y ||
                                   clip_x1 < envelope.x + envelope.w ||
                                   clip_y1 < envelope.y + envelope.h);
            if (narrowed && clip_x1 > clip_x0 && clip_y1 > clip_y0) {
                const s32 clip_area     = (clip_x1 - clip_x0) * (clip_y1 - clip_y0);
                const s32 envelope_area = envelope.w * envelope.h;
                // Only store scissor when it cuts at least 30% of the envelope area
                // (clip_area ≤ 70% of envelope_area).  Guard against zero-area envelope.
                const bool meaningful = (envelope_area > 0) &&
                    (clip_area * 10 <= envelope_area * 7);
                if (meaningful) {
                    has_scissor[j]   = true;
                    scissor_rects[j] = { clip_x0, clip_y0,
                                         clip_x1 - clip_x0, clip_y1 - clip_y0 };
                }
            }
        }

        // ── Accumulate this window's body for lower windows ───────────────────
        if (!animating) {
            const SDL_Rect body = RectDeflate(
                win->GetX(), win->GetY(), win->GetW(), win->GetH(), kBodyDeflate);
            if (body.w > 0 && body.h > 0) {
                scratch_occ_union_.push_back(
                    { body.x, body.y, body.x + body.w, body.y + body.h });
            }
        }
    }

    // ── WIN-SCALE-FIX-2: Non-visible texture eviction ────────────────────────
    // After Pass 1 we know which windows are culled (fully occluded).  Evict the
    // large per-window SDL_Textures (content bake ~3.7 MB, shadow ~4.2 MB, ring
    // ~4.2 MB = ~12 MB/window) for windows that:
    //   a) are culled (fully hidden behind higher z-order windows), OR
    //   b) are in Minimized state (not rendered at all this frame), OR
    //   c) are below the pass1 range (z-index < win_count - kMaxWins) — these
    //      are always fully occluded when there are more than kMaxWins open windows.
    // Textures are re-baked lazily when the window becomes visible again.
    // This caps total bake VRAM to kMaxBakedWindows × ~12 MB regardless of how
    // many windows are open, allowing 50-100 open windows without exhausting VRAM.
    //
    // We only evict when the window currently HAS bake textures (HasBakeTextures)
    // to avoid the log noise of evicting already-evicted windows every frame.
    static constexpr size_t kMaxBakedWindows = 8;  // keep textures for top-8 visible windows only

    {
        size_t visible_count = 0;
        // Walk ALL windows top-to-bottom.  Windows outside pass1 range are always
        // considered culled (they were never evaluated — too deep in z-order).
        for (size_t ri = 0; ri < win_count; ++ri) {
            const size_t j = (win_count - 1) - ri;
            const auto& win = windows_snapshot[j];
            if (!win) continue;
            const bool minimized    = (win->GetState() == WindowState::Minimized);
            const bool below_pass1  = (ri >= pass1_count);  // not evaluated → always culled
            const bool culled       = below_pass1 || (j < kMaxWins && cull_flags[j]);
            if (minimized || culled) {
                // Window is not visible: evict its bake textures if present.
                if (win->HasBakeTextures()) {
                    win->EvictBakeTextures();
                }
            } else {
                // Window is visible.
                visible_count++;
                if (visible_count > kMaxBakedWindows) {
                    // More visible windows than our budget allows baked simultaneously:
                    // evict this one (it's lower in z-order, partially covered).
                    if (win->HasBakeTextures()) {
                        win->EvictBakeTextures();
                    }
                }
            }
        }
    }

    // ── PASS 2: bottom-to-top render with cull + scissor results ─────────────

    for (size_t i = 0; i < win_count; ++i) {
        const auto& win = windows_snapshot[i];
        if (!win) continue;

        // Skip culled windows.
        if (i < kMaxWins && cull_flags[i]) continue;

        // Render with scissor clip (nullptr = full render for top/focused/unclipped).
        const SDL_Rect* clip = (i < kMaxWins && has_scissor[i])
            ? &scissor_rects[i]
            : nullptr;
        win->OnRender(drawer, 0, 0, clip);
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

bool QdWindowManager::PollWindowEvents(u64 keys_down, u64 keys_up, u64 keys_held,
                                        pu::ui::TouchPoint touch_pos,
                                        s32 cx, s32 cy)
{
    // v1.10.3.10.5: snapshot open_windows_ + minimized_entries_ before any
    // iteration that can dispatch into a method whose callback graph reaches
    // OpenWindow / CloseWindow / FinalizeMinimize / RestoreWindow.  Same root
    // cause as RenderAll: input dispatch on a dock tile fires on_restore →
    // RestoreWindow → minimized_entries_.erase + open_windows_.push_back,
    // which mutates the very vector we are iterating.  Snapshot keeps the
    // loop's view stable; underlying vectors mutate independently.
    //
    // A2-OPT-2 (CONVERGENT): reuse pre-allocated scratch vectors (see RenderAll).
    scratch_windows_ = open_windows_;
    auto& windows_snapshot = scratch_windows_;
    scratch_entries_ = minimized_entries_;
    auto& entries_snapshot = scratch_entries_;

    // ── Hover update (every frame, all windows) ───────────────────────────────
    // A2-OPT-5: only run the O(N) scan when the cursor moved or the window
    // set changed (hover_dirty_).  Set dirty in Open/Close/BringToFront.
    // Walk all open windows so hover highlights track the cursor even on
    // non-topmost windows (macOS-style).
    if (hover_dirty_ || cx != hover_last_cx_ || cy != hover_last_cy_) {
        for (auto& w : windows_snapshot) {
            if (w) w->UpdateHoverForCursor(cx, cy);
        }
        hover_last_cx_ = cx;
        hover_last_cy_ = cy;
        hover_dirty_   = false;
    }

    // ── ZR cursor drag — update / end ─────────────────────────────────────────
    // Check the topmost cursor-drag-active window first.
    {
        QdWindow* dragging = nullptr;
        for (auto it = windows_snapshot.rbegin(); it != windows_snapshot.rend(); ++it) {
            if (*it && (*it)->IsCursorDragging()) {
                dragging = it->get();
                break;
            }
        }

        if (dragging) {
            const bool zr_held = (keys_held & HidNpadButton_ZR) != 0;
            const bool zr_up   = (keys_up   & HidNpadButton_ZR) != 0;

            if (zr_up || !zr_held) {
                dragging->EndCursorDrag();
                // Fall through — do not consume so ZR-up can also trigger other actions.
            } else {
                // ZR still held: move window.
                dragging->UpdateCursorDrag(cx, cy);
                return true;  // cursor drag consumes event while in flight
            }
        }
    }

    // ── ZR resize drag — update / end ────────────────────────────────────────
    // Parallel to cursor drag above; handles BR-corner resize drag.
    {
        QdWindow* resizing = nullptr;
        for (auto it = windows_snapshot.rbegin(); it != windows_snapshot.rend(); ++it) {
            if (*it && (*it)->IsResizeDragging()) {
                resizing = it->get();
                break;
            }
        }

        if (resizing) {
            const bool zr_held = (keys_held & HidNpadButton_ZR) != 0;
            const bool zr_up   = (keys_up   & HidNpadButton_ZR) != 0;

            if (zr_up) {
                // ZR explicitly released: end the resize drag.
                // Fall through — do not consume so ZR-up can trigger snap commit.
                resizing->EndResizeDrag();
            } else if (zr_held) {
                // ZR still held: update resize via cursor position.
                resizing->UpdateResizeDrag(cx, cy);
                return true;  // resize drag consumes event while in flight
            }
            // Neither zr_up nor zr_held: touch-initiated resize is in flight.
            // PollEvent handles UpdateResizeDrag/EndResizeDrag via touch coords.
        }
    }

    // ── ZR press — activate corner button or begin cursor drag ───────────────
    if (keys_down & HidNpadButton_ZR) {
        // Topmost window gets ZR first.
        for (auto it = windows_snapshot.rbegin(); it != windows_snapshot.rend(); ++it) {
            if (!*it) continue;
            QdWindow* w = it->get();

            // Try corner-button activation at cursor pos.
            if (w->TryActivateAtCursor(cx, cy)) {
                BringToFront(w);
                return true;
            }

            // If cursor is inside the window but not on a corner button, start drag.
            if (w->ContainsCursor(cx, cy)) {
                w->BeginCursorDrag(cx, cy);
                BringToFront(w);
                return true;
            }
        }
        // ZR over empty desktop — not consumed here; fall through so caller handles.
    }

    // ── Touch and controller input — route to top window first ───────────────
    for (auto it = windows_snapshot.rbegin(); it != windows_snapshot.rend(); ++it) {
        if (!*it) continue;
        if ((*it)->PollEvent(keys_down, keys_up, keys_held, touch_pos)) {
            BringToFront(it->get());
            return true;
        }
    }

    // Z2.4 — Drain the minimized-tile context menu first when it's open.
    // Same pattern as suspended_ctx_menu_ below.
    if (minimized_ctx_menu_.IsOpen()) {
        const s32 touch_x_eff = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.x);
        const s32 touch_y_eff = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.y);
        minimized_ctx_menu_.HandleInput(keys_down, keys_up, cx, cy, touch_x_eff, touch_y_eff);
        if (!minimized_ctx_menu_.IsOpen()) {
            const int sel = minimized_ctx_menu_.GetSelectedIndex();
            QdMinimizedDockEntry* target = ctx_target_minimized_entry_;
            ctx_target_minimized_entry_  = nullptr;
            // Defensive: re-verify the target is still in the list before dispatch.
            const bool target_alive = (target != nullptr) &&
                std::any_of(minimized_entries_.begin(), minimized_entries_.end(),
                            [target](const QdMinimizedDockEntry::Ref& r) {
                                return r.get() == target;
                            });
            if (target_alive) {
                switch (sel) {
                    case 0:  // Restore
                        UL_LOG_INFO("wm: minimized ctx -> Restore");
                        RestoreWindow(target);
                        break;
                    case 1:  // Close Window
                        UL_LOG_INFO("wm: minimized ctx -> Close Window");
                        CloseMinimizedEntry(target);
                        break;
                    default:
                        UL_LOG_INFO("wm: minimized ctx dismissed (sel=%d)", sel);
                        break;
                }
            } else {
                UL_LOG_INFO("wm: minimized ctx dispatch skipped (target gone)");
            }
        }
        return true;  // consume input while menu is open / just closed
    }

    // Then minimized dock entries (left to right; any can be tapped).
    // Z2.4: PollEvent now returns PollAction so we can dispatch both tap-restore
    // and ZL-open-context-menu from this loop.
    for (auto& entry : entries_snapshot) {
        if (!entry) continue;
        const auto action = entry->PollEvent(keys_down, keys_up, keys_held,
                                              touch_pos, cx, cy);
        if (action == QdMinimizedDockEntry::PollAction::Restore) {
            // PollEvent already fired on_restore_requested (which calls
            // RestoreWindow), so just consume the event.
            return true;
        }
        if (action == QdMinimizedDockEntry::PollAction::OpenContextMenu) {
            SDL_Renderer *r = pu::ui::render::GetMainRenderer();
            if (r != nullptr) {
                const s32 anchor_x = entry->GetTileX();
                const s32 anchor_y = entry->GetTileY();
                ctx_target_minimized_entry_ = entry.get();
                const std::vector<std::string> items = { "Restore", "Close Window", "Cancel" };
                minimized_ctx_menu_.Open(r, items, anchor_x, anchor_y);
                UL_LOG_INFO("wm: minimized context menu opened for tile '%s'",
                            entry->GetTitle().c_str());
            }
            return true;
        }
    }

    // Drain the suspended-app context menu FIRST when it's open.  This
    // intercepts D-pad / A / B / touch input before the entries themselves
    // see it, mirrors the pattern QdTaskManagerElement uses for its row
    // context menu.
    if (suspended_ctx_menu_.IsOpen()) {
        const s32 touch_x_eff = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.x);
        const s32 touch_y_eff = touch_pos.IsEmpty() ? -1 : static_cast<s32>(touch_pos.y);
        suspended_ctx_menu_.HandleInput(keys_down, keys_up, cx, cy, touch_x_eff, touch_y_eff);
        if (!suspended_ctx_menu_.IsOpen()) {
            // Menu just closed — dispatch to the appropriate callback.
            const int sel = suspended_ctx_menu_.GetSelectedIndex();
            const u64 pid = ctx_target_program_id_;
            ctx_target_program_id_ = 0;
            switch (sel) {
                case 0:  // Resume
                    if (on_resume_suspended_requested) {
                        on_resume_suspended_requested(pid);
                    }
                    break;
                case 1:  // Terminate
                    if (on_terminate_suspended_requested) {
                        on_terminate_suspended_requested(pid);
                    }
                    break;
                default:
                    // 2 = Cancel, -1 = dismiss (B / outside touch)
                    UL_LOG_INFO("wm: suspended-app ctx menu dismissed (sel=%d)", sel);
                    break;
            }
        }
        return true;  // consume input while menu is open / just closed
    }

    // Suspended-app dock entries (one per HOS-suspended Switch app).  Tap
    // → on_resume_suspended_requested callback.  ZL → open context menu.
    auto suspended_snapshot = suspended_app_entries_;
    for (auto& entry : suspended_snapshot) {
        if (!entry) continue;
        const auto action = entry->PollEvent(keys_down, keys_up, keys_held, touch_pos, cx, cy);
        if (action == QdSuspendedAppDockEntry::PollAction::Resume) {
            if (on_resume_suspended_requested) {
                on_resume_suspended_requested(entry->GetProgramId());
            }
            return true;
        }
        if (action == QdSuspendedAppDockEntry::PollAction::OpenContextMenu) {
            SDL_Renderer *r = pu::ui::render::GetMainRenderer();
            if (r != nullptr) {
                // Anchor the menu at the top-right corner of the dock tile
                // so it pops up ABOVE the tile (QdContextMenu clamps to
                // SCREEN_W × SCREEN_H so it'll auto-fit).
                const s32 anchor_x = entry->GetTileX();
                const s32 anchor_y = entry->GetTileY();
                ctx_target_program_id_ = entry->GetProgramId();
                const std::vector<std::string> items = { "Resume", "Terminate", "Cancel" };
                suspended_ctx_menu_.Open(r, items, anchor_x, anchor_y);
                UL_LOG_INFO("wm: suspended-app context menu opened program_id=0x%016lX",
                            static_cast<unsigned long>(ctx_target_program_id_));
            }
            return true;
        }
    }

    return false;
}

// ── Queries ───────────────────────────────────────────────────────────────────

// W9-FIX Bug 1 — singleton window lookup by title.
std::pair<QdWindow*, QdMinimizedDockEntry*>
QdWindowManager::FindWindowByTitle(const std::string& title) const {
    // Scan open windows first (back = topmost; iterate back-to-front for the
    // most-recently-focused match — in practice there should be at most one).
    for (auto it = open_windows_.rbegin(); it != open_windows_.rend(); ++it) {
        if (*it && (*it)->GetTitle() == title) {
            return { it->get(), nullptr };
        }
    }
    // Then scan minimized dock entries.
    for (const auto& e : minimized_entries_) {
        if (e && e->GetTitle() == title) {
            return { nullptr, e.get() };
        }
    }
    return { nullptr, nullptr };
}

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
