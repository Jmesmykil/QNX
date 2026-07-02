// qd_MinimizedDockEntry.hpp — Minimized-window snapshot tile for the dock band.
// Created by QdWindowManager::MinimizeWindow; placed in the dock band at Z=4.
// snapshot_ texture (SNAP_W×SNAP_H) is captured at minimize time via SDL_SetRenderTarget
// and freed in the destructor via pu::ui::render::DeleteTexture (B41/B42 contract).
// tile_x_/tile_y_ are assigned by QdWindowManager::LayoutDockEntries before each RenderAll.
// on_restore_requested fires when the tile is tapped or when QdWindowManager::RestoreWindow
// is called programmatically (e.g., from the task manager).
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <functional>
#include <memory>
#include <string>

#include <ul/menu/qdesktop/qd_WmConstants.hpp>

namespace ul::menu::qdesktop {

// ── QdMinimizedDockEntry ──────────────────────────────────────────────────────

class QdMinimizedDockEntry {
public:
    using Ref = std::shared_ptr<QdMinimizedDockEntry>;

    // Factory.  snapshot: SNAP_W×SNAP_H SDL_Texture* captured at minimize time.
    // Ownership transfers to this instance; freed in destructor.
    // title: window title string copied into title_ (not a pointer borrow).
    // program_id: 0 for built-in layouts; NRO title-id for v1.11.
    static Ref New(const std::string& title, SDL_Texture* snapshot, u64 program_id) {
        return std::make_shared<QdMinimizedDockEntry>(title, snapshot, program_id);
    }

    QdMinimizedDockEntry(const std::string& title, SDL_Texture* snapshot, u64 program_id);
    ~QdMinimizedDockEntry();

    // Non-copyable, non-movable (owns SDL_Texture* + callback).
    QdMinimizedDockEntry(const QdMinimizedDockEntry&)            = delete;
    QdMinimizedDockEntry& operator=(const QdMinimizedDockEntry&) = delete;
    QdMinimizedDockEntry(QdMinimizedDockEntry&&)                 = delete;
    QdMinimizedDockEntry& operator=(QdMinimizedDockEntry&&)      = delete;

    // ── Rendering ─────────────────────────────────────────────────────────────

    // Renders the dock tile at the current tile_x_/tile_y_.
    // Draws: rounded bg, snapshot, title label, focus ring when focused_.
    void Render(SDL_Renderer* r) const;

    // ── Input ────────────────────────────────────────────────────────────────

    // Z2.4 (2026-05-19): PollEvent returns a PollAction so callers can dispatch
    // both tap-restore AND ZL-open-context-menu from a single poll call.
    // Mirrors QdSuspendedAppDockEntry's interface.
    enum class PollAction {
        None,
        Restore,           // touch-tap inside the tile
        OpenContextMenu,   // ZL with software cursor over the tile
    };

    // cx / cy: current software-cursor position used to gate ZL.
    PollAction PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                         pu::ui::TouchPoint touch_pos,
                         s32 cx, s32 cy);

    // ── Geometry setters (called by LayoutDockEntries) ────────────────────────

    void SetTilePosition(s32 x, s32 y) { tile_x_ = x; tile_y_ = y; }

    // ── Accessors ────────────────────────────────────────────────────────────

    const std::string& GetTitle()     const { return title_; }
    u64                GetProgramId() const { return program_id_; }
    bool               IsFocused()    const { return focused_; }
    void               SetFocused(bool f)   { focused_ = f; }
    s32                GetTileX()     const { return tile_x_; }
    s32                GetTileY()     const { return tile_y_; }

    // ── Callbacks (wired by QdWindowManager at minimize time) ───────────────

    // Fires when the user taps this dock tile; wired by QdWindowManager::MinimizeWindow
    // to call RestoreWindow(entry).
    std::function<void(QdMinimizedDockEntry*)> on_restore_requested;

    // Fires inside RestoreWindow to re-invoke the correct opener method
    // (OpenVaultWindow, OpenSettingsWindow, etc.) rather than creating a blank window.
    // Wired by QdDesktopIconsElement (WmBridge) immediately before on_minimize_begin_ fires.
    // This ensures restored windows have proper content, on_tick wiring, and on_minimize_begin_.
    std::function<void()> on_reopen;

private:
    std::string   title_;
    SDL_Texture*  snapshot_;    // SNAP_W×SNAP_H; owned; freed by DeleteTexture in dtor
    u64           program_id_;
    s32           tile_x_ = 0;
    s32           tile_y_ = 0;
    bool          focused_ = false;

    // ── Label texture cache (per uMenu optimization audit F2.2) ──────────────
    //
    // Previously Render() called pu::ui::render::RenderText() every frame —
    // 60 Hz × N entries = ~360 SDL_Texture allocs/sec of pure font-cache
    // churn.  Now: build on first Render (or when title_ would change, but
    // title_ is fixed at construction so cache is build-once).  Freed in
    // dtor alongside snapshot_.  Mutable so the const-correct Render() can
    // lazy-build them — caching is an implementation detail.
    mutable SDL_Texture*  label_tex_     = nullptr;
    mutable int           label_w_       = 0;
    mutable int           label_h_       = 0;

    // Build label_tex_ if absent.  No-op if already built.
    void EnsureLabelTexture() const;

    // ── Drawing helpers ───────────────────────────────────────────────────────

    // Draws a filled rounded-rect with corner radius 4 px.
    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col);
};

} // namespace ul::menu::qdesktop
