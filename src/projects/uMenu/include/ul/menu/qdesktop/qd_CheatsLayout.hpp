// qd_CheatsLayout.hpp — Atmosphère cheat browser UI element for QdWindow.
//
// W12-CHEATS (v3.4).
//
// QdCheatsLayout : QdContentElement (matches SaveEditor pattern).
// Natural canvas: 960×600 (same as Save Editor).
//
// Two navigation modes:
//   TitleList — all titles that have at least one cheat file installed.
//                A → enter CheatList for that title.
//                B → close window.
//   CheatList — cheats for a single title.
//                A → toggle enabled/disabled (writes sidecar TOML).
//                Y → show hex-code popup for the focused cheat.
//                B → back to TitleList.
//
// Master Code is always the first row; shown with "[master]" badge; cannot
// be disabled.
//
// The "View Cheats" for a specific app_id is supported via OpenForTitle():
//   called by OpenCheatsWindow(app_id) — skips TitleList and goes directly
//   into CheatList mode for the matching title.
#pragma once

#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // PANEL_HEADER_H, PANEL_ROW_H etc (D13 fix)
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_CheatsManager.hpp>
#include <ul/menu/qdesktop/qd_CheatsInstaller.hpp>
#include <SDL2/SDL.h>
#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── QdCheatsLayout ────────────────────────────────────────────────────────────

class QdCheatsLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdCheatsLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdCheatsLayout>(theme);
    }

    explicit QdCheatsLayout(const QdTheme &theme);
    ~QdCheatsLayout() override;

    // ── QdContentElement contract ──────────────────────────────────────────

    s32 GetNaturalW() const override { return 960; }
    s32 GetNaturalH() const override { return 600; }

    bool PrefersWidthBoundScale() const override { return false; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 ox, s32 oy) override;

    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Return context-appropriate bottom-bar hint string for QdWindow chrome.
    std::string GetBottomHint() const;

    /// Hierarchical Back (B): pop one level (CodePopup/Installing/CheatList ->
    /// TitleList), then return false at TitleList so QdWindow closes.  Consulted
    /// by the chrome before it closes on B — aligns B across all windows.
    bool OnBackRequested() override;

    std::string GetDebugState() const override;

    /// Skip TitleList and open directly into CheatList for the title whose
    /// tid matches the given app_id.  If no matching title is found, falls back
    /// to TitleList mode.  Call after construction, before first render.
    void OpenForTitle(u64 app_id);

    /// Refresh the cheat file scan (e.g. after inserting a new cheat file).
    void Rescan();

private:
    // ── Navigation mode ────────────────────────────────────────────────────

    enum class Mode : uint8_t {
        TitleList,  ///< Browse all titles with installed cheats.
        CheatList,  ///< Browse cheats for a single title.
        CodePopup,  ///< Read-only hex-code popup for the focused cheat.
        Installing, ///< W14: HTTPS installer running; show progress bar.
    };

    Mode   mode_          = Mode::TitleList;
    int    title_focus_   = 0;  ///< Focused row in TitleList.
    int    cheat_focus_   = 0;  ///< Focused row in CheatList.

    // ── Title-list data ────────────────────────────────────────────────────

    std::vector<CheatFile>  titles_;     ///< All discovered cheat files.
    bool                    scanned_    = false;

    // v3.6 absorb wave 1 — async NACP title resolution.
    //
    // resolve_kicked_:  has the bg resolver been started for this session?
    //                   (single-shot per Rescan; subsequent OnRender ticks
    //                   poll resolve_done_ instead of restarting.)
    // resolve_done_:    set true by QdCheatTitleResolver's on-complete
    //                   callback when the bg thread finishes.  Polled on
    //                   the UI thread to trigger a label refresh.
    // labels_refreshed_: prevents the label refresh from re-running every
    //                   frame after resolve_done_ flips true.
    bool                    resolve_kicked_     = false;
    std::atomic<bool>       resolve_done_       { false };
    bool                    labels_refreshed_   = false;

    // ── Cheat-list data ────────────────────────────────────────────────────

    CheatList                cheats_;    ///< Cheats for the active title.
    std::set<std::string>    enabled_;   ///< Enabled cheat names from sidecar.
    int                      active_title_idx_ = -1;  ///< Index into titles_.

    // ── Layout constants ──────────────────────────────────────────────────
    // D13 fix: kTopbarH was 36 — a private divergence from the global TOPBAR_H=48.
    // The cheats panel is windowed (not full-screen), so its "topbar" is its own
    // content header strip, not the OS topbar.  PANEL_HEADER_H=36 names this
    // intent from qd_LayoutConstants.hpp without aliasing the OS-topbar constant.

    static constexpr s32 kTopbarH     = PANEL_HEADER_H;   ///< Header bar height (panel, not OS bar).
    static constexpr s32 kRowH        = PANEL_ROW_H;      ///< Each list row height.
    static constexpr s32 kRowGap      = PANEL_ROW_GAP;    ///< Gap between rows.
    static constexpr s32 kHintBarH    = PANEL_HINT_BAR_H; ///< Bottom hint strip height.
    static constexpr s32 kMargin      = PANEL_MARGIN;     ///< Left/right margin.
    static constexpr s32 kDetailPaneX = PANEL_DETAIL_X;   ///< Right detail pane X offset.

    // ── SDL texture cache ──────────────────────────────────────────────────

    SDL_Texture *tex_no_cheats_     = nullptr;  ///< "No cheat files found" msg.
    SDL_Texture *tex_scanning_      = nullptr;  ///< "Scanning…" placeholder.
    SDL_Texture *tex_master_badge_  = nullptr;  ///< "[master]" badge text.
    SDL_Texture *tex_enabled_       = nullptr;  ///< "Enabled" status text.
    SDL_Texture *tex_disabled_      = nullptr;  ///< "Disabled" status text.
    SDL_Texture *tex_detail_hint_   = nullptr;  ///< Detail-pane instruction text.

    // W15-B fix: per-focus detail-pane cache.  Previously RenderCheatList
    // rebuilt the "N instructions" + "Enabled / Disabled / Master Code" lines
    // every frame (~120 alloc+destroy/sec).  Now we shadow-compare the focus
    // index AND the enabled state, only rebuilding when either changes.
    SDL_Texture *detail_info_tex_   = nullptr;  ///< "N instructions" line.
    SDL_Texture *detail_status_tex_ = nullptr;  ///< "Enabled" / "Disabled" / "Master".
    int          detail_last_focus_ = -1;       ///< cheats_ index last rebuilt for.
    bool         detail_last_enabled_ = false;  ///< Enabled state at last rebuild.

    bool         textures_built_    = false;

    // Per-title name textures (lazy, parallel to titles_).
    std::vector<SDL_Texture *> title_textures_;
    // Per-cheat name textures (rebuilt when entering a title).
    std::vector<SDL_Texture *> cheat_textures_;

    // Popup: per-line textures for the code-view popup.
    std::vector<SDL_Texture *> popup_line_textures_;

    // ── W14: HTTPS installer ───────────────────────────────────────────────

    std::unique_ptr<QdCheatsInstaller> installer_;

    // Per-frame installer progress snapshot (updated each frame from worker).
    InstallerProgress install_progress_;

    // Cached textures for the installer progress view (rebuilt when phase
    // changes or percent changes by >= 5%).
    SDL_Texture *tex_install_phase_   = nullptr;  ///< Phase label.
    SDL_Texture *tex_install_counts_  = nullptr;  ///< "N TIDs / M files" line.
    SDL_Texture *tex_install_error_   = nullptr;  ///< Error message (if Failed).
    InstallerProgress::Phase install_last_phase_ = InstallerProgress::Phase::Idle;
    int          install_last_pct_    = -1;

    // ── Theme ──────────────────────────────────────────────────────────────

    QdTheme theme_;

    // ── Private helpers ────────────────────────────────────────────────────

    /// Build base textures on first render.
    void BuildTextures(SDL_Renderer *r);

    /// Free all per-title + per-cheat textures.
    void FreeDynamicTextures();

    /// Rebuild per-title name textures after a scan.
    void BuildTitleTextures(SDL_Renderer *r);

    /// Rebuild per-cheat name textures after entering a title.
    void BuildCheatTextures(SDL_Renderer *r);

    /// Build popup line textures for the focused cheat.
    void BuildPopupTextures(SDL_Renderer *r);

    /// Free popup line textures.
    void FreePopupTextures();

    /// Render the TitleList panel.
    void RenderTitleList(SDL_Renderer *r, s32 ox, s32 oy);

    /// Render the CheatList panel.
    void RenderCheatList(SDL_Renderer *r, s32 ox, s32 oy);

    /// Render the CodePopup overlay.
    void RenderCodePopup(SDL_Renderer *r, s32 ox, s32 oy);

    /// Toggle enabled state for the focused cheat and write sidecar.
    void ToggleFocusedCheat();

    /// Enter the cheat list for title at titles_[idx].
    void EnterTitle(int idx, SDL_Renderer *r);

    /// Blit a texture at (x, y); no-op if tex is null.
    static void BlitTex(SDL_Renderer *r, SDL_Texture *tex, s32 x, s32 y);

    // ── W14: installer progress view ──────────────────────────────────────

    /// Free installer progress textures.
    void FreeInstallTextures();

    /// Render the installer progress view (replaces TitleList during install).
    void RenderInstalling(SDL_Renderer *r, s32 ox, s32 oy);
};

} // namespace ul::menu::qdesktop
