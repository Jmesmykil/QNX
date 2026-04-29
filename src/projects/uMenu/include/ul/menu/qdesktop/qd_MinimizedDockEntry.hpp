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

    // Returns true if this tile consumed the event (touch-tap within tile bounds).
    // Fires on_restore_requested on tap.
    bool PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                   pu::ui::TouchPoint touch_pos);

    // ── Geometry setters (called by LayoutDockEntries) ────────────────────────

    void SetTilePosition(s32 x, s32 y) { tile_x_ = x; tile_y_ = y; }

    // ── Accessors ────────────────────────────────────────────────────────────

    const std::string& GetTitle()     const { return title_; }
    u64                GetProgramId() const { return program_id_; }
    bool               IsFocused()    const { return focused_; }
    void               SetFocused(bool f)   { focused_ = f; }
    s32                GetTileX()     const { return tile_x_; }
    s32                GetTileY()     const { return tile_y_; }

    // ── Callback (wired by QdWindowManager at minimize time) ─────────────────

    std::function<void(QdMinimizedDockEntry*)> on_restore_requested;

private:
    std::string   title_;
    SDL_Texture*  snapshot_;    // SNAP_W×SNAP_H; owned; freed by DeleteTexture in dtor
    u64           program_id_;
    s32           tile_x_ = 0;
    s32           tile_y_ = 0;
    bool          focused_ = false;

    // ── Drawing helpers ───────────────────────────────────────────────────────

    // Draws a filled rounded-rect with corner radius 4 px.
    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col);
};

} // namespace ul::menu::qdesktop
