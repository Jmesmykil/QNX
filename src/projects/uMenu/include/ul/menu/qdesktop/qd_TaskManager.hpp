// qd_TaskManager.hpp — Full-screen process/task manager modal for uMenu v1.9.
//
// Triggered by a 30-frame (0.5 s) hold on dock tile 0.
// Owned by QdDesktopIconsElement; wire-in is in qd_DesktopIcons.cpp.
//
// Lifecycle:
//   Open(renderer)  — opens pm:dmnt / pm:info / pm:shell / ns sessions;
//                     enumerates running processes; resolves display names;
//                     pre-renders all row textures; sets open_=true.
//   Render(renderer) — blits background + row tiles; auto-refreshes every
//                      kAutoRefreshFrames (~3 s at 60 fps); no RenderText per frame.
//   HandleInput(keys_down, keys_held) — D-pad navigation, A=action menu,
//                      B=close/collapse, X=manual refresh; returns true if consumed.
//   Close()          — frees all textures + closes service sessions; sets open_=false.
//   ~QdTaskManager() — calls Close().
//
// Wire-in spec (qd_DesktopIcons.cpp / qd_DesktopIcons.hpp):
//   hpp: add private members tile0_hold_frames_ (int) and task_mgr_ (QdTaskManager).
//   cpp OnInput (before all other handlers):
//     if tile0 ZL/A is held for 30 frames → task_mgr_.Open(renderer); return;
//     if task_mgr_.IsOpen() → if (task_mgr_.HandleInput(keys_down, keys_held)) return;
//   cpp OnRender (after RenderFirstBootWelcomeIfOpen — absolute last call):
//     if (task_mgr_.IsOpen()) task_mgr_.Render(renderer);
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>
#include <array>
#include <string>

namespace ul::menu::qdesktop {

// Per-process side-table entry. Allocated as a fixed array inside QdTaskManager;
// never embedded in NroEntry (which is pinned at 1632 bytes).
struct TmEntry {
    u64 pid        = 0;
    u64 program_id = 0;
    u64 mem_used   = 0;   // bytes; 0 = unavailable
    u64 mem_total  = 0;   // bytes; 0 = unavailable
    int proc_state = -1;  // ProcessState value; -1 = unavailable
    bool is_self   = false;  // true if this row is uMenu itself
    bool is_app    = false;  // true if pmdmntGetApplicationProcessId matched
    bool protected_title = false;  // true if Close must be greyed out
    char name[64]  = {};  // display name (UTF-8, truncated to 63 chars + NUL)

    // Pre-rendered row tile texture (freed by FreeAllTiles / Close).
    SDL_Texture *tex_row = nullptr;
    int          row_w   = 0, row_h = 0;
};

// ── QdTaskManager ────────────────────────────────────────────────────────────
//
// Full-screen modal overlay.  Non-copyable, non-movable (owns SDL textures and
// service session handles).
//
// Memory budget: kMaxEntries (32) SDL_Texture* row tiles + 4 static textures.
// Row tiles are allocated on Open() and freed on Close().  No per-frame
// RenderText calls — Render() only blits from the pre-rendered tile cache.
class QdTaskManager {
public:
    static constexpr int kMaxEntries        = 32;
    static constexpr int kRowH              = 72;   // px per row tile
    static constexpr int kPadX              = 60;   // left/right margin
    static constexpr int kPadY              = 90;   // top margin (below title)
    static constexpr int kAutoRefreshFrames = 180;  // ~3 s at 60 fps

    QdTaskManager();
    ~QdTaskManager();

    QdTaskManager(const QdTaskManager&)            = delete;
    QdTaskManager& operator=(const QdTaskManager&) = delete;
    QdTaskManager(QdTaskManager&&)                 = delete;
    QdTaskManager& operator=(QdTaskManager&&)      = delete;

    bool IsOpen() const { return open_; }

    // Enumerates live processes, resolves names, pre-renders all row textures.
    // Opens pm:dmnt, pm:info, pm:shell, ns sessions; closes them in Close().
    // Safe to call when already open — re-enumerates (live refresh).
    void Open(SDL_Renderer *r);

    // Frees all row textures + closes service sessions. Sets open_=false.
    // Safe to call when already closed.
    void Close();

    // Blits background + row tiles + action menu (when expanded).
    // Auto-refreshes every kAutoRefreshFrames. No-op if !open_.
    void Render(SDL_Renderer *r);

    // Handles D-pad navigation (Up/Down), A to expand/confirm action,
    // B to collapse action menu or close overlay, X to refresh process list.
    // Returns true if input was consumed.
    bool HandleInput(u64 keys_down, u64 keys_held);

private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    // Renders a string with the given font size and color into *out_tex;
    // stores pixel dimensions in *out_w / *out_h.
    // If rendering fails, *out_tex is left nullptr and dimensions are zeroed.
    static void MakeText(SDL_Renderer *r,
                         pu::ui::DefaultFontSize font_size,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);

    // Frees a single texture via pu::ui::render::DeleteTexture; nulls the pointer.
    // Safe if *tex is already nullptr.
    static void FreeTexture(SDL_Texture **tex);

    // Blits tex at (x, y) on r. No-op if tex is nullptr.
    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // Enumerates PIDs via pmdmntGetJitDebugProcessIdList; fills entries_[].
    // Resolves program_id via pmdmntGetProgramId.
    // Reads memory for self via svcGetInfo(CUR_PROCESS_HANDLE); for others
    // tries svcOpenProcess — falls back to mem_used=0/mem_total=0 on failure.
    // Reads process state via svcGetProcessInfo.
    // Sets is_self, is_app, protected_title flags.
    // Calls ResolveName for each entry. Returns count of valid entries populated.
    int EnumerateProcesses();

    // Looks up display name for program_id. Checks hardcoded table first
    // (uMenu 0x0100000000001000, uSystem 0x0100000000001010,
    // hbloader 0x010000000000100D, overlayDisp 0x010000000000100C,
    // starter 0x0100000000001012). Falls back to nsGetApplicationControlData.
    // Writes up to 63 chars into out_name[64]; always NUL-terminates.
    void ResolveName(u64 program_id, char out_name[64]);

    // Pre-renders a single row tile into entry.tex_row / row_w / row_h.
    // Tile: name (left), mem_used/mem_total (centre), state badge (right).
    // selected=true renders with a lighter background tint baked in.
    void RenderEntryTile(SDL_Renderer *r, TmEntry &entry, bool selected);

    // Frees all row textures across entries_[0..count_-1].
    void FreeAllTiles();

    // Renders the action menu popup over the selected row.
    // Items: "Switch To", "Close" (greyed if protected_title), "Details".
    void RenderActionMenu(SDL_Renderer *r);

    // Executes the currently highlighted action menu item:
    //   0 = Switch To  → appletRequestToGetForeground()
    //   1 = Close      → pmshellTerminateProcess(entries_[selected_].pid); refresh
    //   2 = Details    → expands a detail sub-panel in place
    void ExecuteAction();

    // ── Service session flags ─────────────────────────────────────────────────
    bool pmdmnt_open_  = false;
    bool pminfo_open_  = false;
    bool pmshell_open_ = false;
    bool ns_open_      = false;

    // ── Runtime state ─────────────────────────────────────────────────────────
    bool open_          = false;
    bool action_menu_   = false;  // action popup visible
    bool detail_panel_  = false;  // detail sub-panel visible
    int  selected_      = 0;      // highlighted row index
    int  action_item_   = 0;      // 0=Switch To, 1=Close, 2=Details
    int  scroll_offset_ = 0;      // first visible row
    int  count_         = 0;      // valid entries in entries_[]
    int  refresh_frame_counter_ = 0;  // auto-refresh tick
    u64  own_pid_       = 0;      // PID of uMenu itself (set on first Open)

    std::array<TmEntry, kMaxEntries> entries_ = {};

    // ── Static display textures (pre-rendered on Open; freed on Close) ────────
    SDL_Texture *tex_title_   = nullptr; int title_w_   = 0, title_h_   = 0;
    SDL_Texture *tex_col_hdr_ = nullptr; int col_hdr_w_ = 0, col_hdr_h_ = 0;
    SDL_Texture *tex_footer_  = nullptr; int footer_w_  = 0, footer_h_  = 0;
    SDL_Texture *tex_empty_   = nullptr; int empty_w_   = 0, empty_h_   = 0;

    // Action menu item textures (3 items; allocated on Open, freed on Close).
    SDL_Texture *tex_act_[3] = {nullptr, nullptr, nullptr};
    int          act_w_[3]   = {0, 0, 0};
    int          act_h_[3]   = {0, 0, 0};
};

}  // namespace ul::menu::qdesktop
