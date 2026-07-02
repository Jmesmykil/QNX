// qd_SuspendedAppDockEntry.hpp — Dock-band tile for a Switch Application
// that the user backgrounded via HOME (g_GlobalSettings.system_status.
// suspended_app_id != 0).
//
// Mirrors QdMinimizedDockEntry's tile geometry (SNAP_W × SNAP_H + 4-px padding)
// so the two entry kinds line up cleanly in the dock band when both are
// present.  Differences from QdMinimizedDockEntry:
//
//   • Visual: shows the app's NACP JPEG icon (via QdNsIconCache) instead of a
//     captured SDL_Texture snapshot.  HOS doesn't let us capture the
//     suspended app's framebuffer — IndirectLayer is gated (proven 2026-05-19).
//   • Action: tap = `smi::ResumeApplication` → uMenu Finalize/FadeOut → HOS
//     resumes the app.  Mirrors the resume path in QdTaskManagerLayout.
//   • Context menu (ZL): [Resume, Terminate, Cancel].  Resume = same as tap;
//     Terminate = `smi::TerminateApplication`.  Cancel = no-op.
//
// Lifetime: created by QdWindowManager when suspended_app_id flips from 0 to
// non-zero; destroyed when it flips back to 0 (app terminated/resumed).
// QdWindowManager owns the Ref; the dock band renders / dispatches.
//
// Why dedicated entry vs extending QdMinimizedDockEntry: snapshot ownership
// model differs (we don't own a snapshot, we own a borrowed icon cache key),
// action dispatch differs (am IPC vs in-uMenu window restore), context menu
// is new.  Separate class keeps each entry's contract simple.

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

class QdSuspendedAppDockEntry {
public:
    using Ref = std::shared_ptr<QdSuspendedAppDockEntry>;

    // Factory.  program_id = the Switch Application's NCM TitleId.  title =
    // human-readable name (already resolved from NACP).  icon_tex = borrowed
    // pointer to the JPEG-decoded icon SDL_Texture owned by QdNsIconCache;
    // QdSuspendedAppDockEntry does NOT free it — the cache outlives this entry.
    static Ref New(u64 program_id, const std::string& title, SDL_Texture* icon_tex) {
        return std::make_shared<QdSuspendedAppDockEntry>(program_id, title, icon_tex);
    }

    QdSuspendedAppDockEntry(u64 program_id, const std::string& title, SDL_Texture* icon_tex);
    ~QdSuspendedAppDockEntry();

    QdSuspendedAppDockEntry(const QdSuspendedAppDockEntry&)            = delete;
    QdSuspendedAppDockEntry& operator=(const QdSuspendedAppDockEntry&) = delete;
    QdSuspendedAppDockEntry(QdSuspendedAppDockEntry&&)                 = delete;
    QdSuspendedAppDockEntry& operator=(QdSuspendedAppDockEntry&&)      = delete;

    // ── Rendering ─────────────────────────────────────────────────────────────

    // Renders the dock tile at the current tile_x_/tile_y_.
    // Layout: rounded bg, icon, title label (cached), focus ring when focused_.
    void Render(SDL_Renderer* r) const;

    // ── Input ────────────────────────────────────────────────────────────────

    // Returns the action the dock entry resolved on this poll.  None = no
    // interaction.  Resume = tap inside the tile.  OpenContextMenu = ZL
    // press while the software cursor is over the tile (cx, cy parameters)
    // — note we use cursor-over-tile NOT focused_ because focus tracking
    // for suspended entries isn't wired and there's only ever one suspended
    // app at a time (HOS invariant).
    enum class PollAction {
        None,
        Resume,
        OpenContextMenu,
    };
    PollAction PollEvent(u64 keys_down, u64 keys_up, u64 keys_held,
                         pu::ui::TouchPoint touch_pos,
                         s32 cx, s32 cy);

    // ── Geometry setters (called by LayoutDockEntries) ────────────────────────

    void SetTilePosition(s32 x, s32 y) { tile_x_ = x; tile_y_ = y; }

    // ── Accessors ────────────────────────────────────────────────────────────

    u64                 GetProgramId() const { return program_id_; }
    const std::string&  GetTitle()     const { return title_; }
    bool                IsFocused()    const { return focused_; }
    void                SetFocused(bool f)   { focused_ = f; }
    s32                 GetTileX()     const { return tile_x_; }
    s32                 GetTileY()     const { return tile_y_; }

private:
    u64           program_id_;
    std::string   title_;
    SDL_Texture*  icon_tex_;       // BORROWED — owned by QdNsIconCache
    s32           tile_x_ = 0;
    s32           tile_y_ = 0;
    bool          focused_ = false;

    // Cached label texture — title doesn't change at runtime so build once.
    // Mirrors the F2.2 caching pattern in QdMinimizedDockEntry.
    mutable SDL_Texture*  label_tex_ = nullptr;
    mutable int           label_w_   = 0;
    mutable int           label_h_   = 0;
    void EnsureLabelTexture() const;

    // ── Drawing helpers ───────────────────────────────────────────────────────

    static void DrawRoundedRect(SDL_Renderer* r, int x, int y, int w, int h,
                                pu::ui::Color col);
};

}  // namespace ul::menu::qdesktop
