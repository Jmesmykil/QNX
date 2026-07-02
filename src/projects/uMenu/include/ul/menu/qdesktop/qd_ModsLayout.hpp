// qd_ModsLayout.hpp — Atmosphère LayeredFS mod browser UI element for QdWindow.
//
// B3.1 — launcher-side mod manager (mirrors qd_CheatsLayout pattern exactly).
//
// QdModsLayout : QdContentElement.
// Natural canvas: 960×600 (identical to Save Editor and Cheats UI).
//
// Two navigation modes:
//   TitleList — all titles that have at least one mod slot installed.
//                A → enter ModSlotList for that title.
//                B → close window (via OnBackRequested / LoadMenu Main).
//   SlotList  — individual slots (romfs, exefs, patches) for a single title.
//                A → toggle enabled/disabled (applies rename + sidecar write).
//                B → back to TitleList.
//
// Mod changes apply NEXT LAUNCH — a mounted romfs cannot be hot-swapped.
// The detail pane explicitly states "Changes take effect on next launch."
//
// OpenForTitle(app_id): skip TitleList and open directly into SlotList for
//   the matching title — called by OpenModsWindow(app_id) in WmBridge.
//
// Rescan(): re-enumerate the contents tree (e.g. after user copies files).

#pragma once

#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_LayoutConstants.hpp>  // PANEL_HEADER_H, PANEL_ROW_H etc
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_ModsManager.hpp>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── QdModsLayout ──────────────────────────────────────────────────────────────

class QdModsLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdModsLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdModsLayout>(theme);
    }

    explicit QdModsLayout(const QdTheme &theme);
    ~QdModsLayout() override;

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

    /// Hierarchical Back (B): pop one level (SlotList → TitleList), then
    /// return false at TitleList so QdWindow closes.  Mirrors CheatsLayout.
    bool OnBackRequested() override;

    std::string GetDebugState() const override;

    /// Skip TitleList and open directly into SlotList for the title whose
    /// tid matches the given app_id.  Falls back to TitleList if no match.
    /// Call after construction, before first render.
    void OpenForTitle(u64 app_id);

    /// Re-enumerate sdmc:/atmosphere/contents/ (e.g. after SD card write).
    void Rescan();

private:
    // ── Navigation mode ────────────────────────────────────────────────────

    enum class Mode : uint8_t {
        TitleList,  ///< Browse all titles with installed mods.
        SlotList,   ///< Browse mod slots for a single title.
    };

    Mode  mode_         = Mode::TitleList;
    int   title_focus_  = 0;   ///< Focused row in TitleList.
    int   slot_focus_   = 0;   ///< Focused row in SlotList.

    // ── Title-list data ────────────────────────────────────────────────────

    std::vector<ModSet>  titles_;         ///< All discovered mod sets.
    bool                 scanned_ = false;
    int                  active_title_idx_ = -1;  ///< Index into titles_.

    // Async NACP title resolution — same pattern as QdCheatsLayout.
    bool              resolve_kicked_   = false;
    std::atomic<bool> resolve_done_     { false };
    bool              labels_refreshed_ = false;

    // ── Layout constants ──────────────────────────────────────────────────
    // Unified with QdCheatsLayout via qd_LayoutConstants.hpp (same fix).

    static constexpr s32 kTopbarH     = PANEL_HEADER_H;   ///< Panel header bar height (36px).
    static constexpr s32 kRowH        = PANEL_ROW_H;      ///< Each list row height.
    static constexpr s32 kRowGap      = PANEL_ROW_GAP;    ///< Gap between rows.
    static constexpr s32 kHintBarH    = PANEL_HINT_BAR_H; ///< Bottom hint strip height.
    static constexpr s32 kMargin      = PANEL_MARGIN;     ///< Left/right margin.
    static constexpr s32 kDetailPaneX = PANEL_DETAIL_X;   ///< Right detail pane X offset.

    // ── SDL texture cache ──────────────────────────────────────────────────

    SDL_Texture *tex_no_mods_       = nullptr;  ///< "No mods found" message.
    SDL_Texture *tex_scanning_      = nullptr;  ///< "Scanning…" placeholder.
    SDL_Texture *tex_enabled_       = nullptr;  ///< "◉ On" status.
    SDL_Texture *tex_disabled_      = nullptr;  ///< "○ Off" status.
    SDL_Texture *tex_detail_hint_   = nullptr;  ///< "Changes take effect on next launch."

    // Per-focus detail-pane shadow cache (avoid per-frame RenderText).
    SDL_Texture *detail_type_tex_   = nullptr;  ///< Slot type line (e.g. "romfs — file replacements").
    SDL_Texture *detail_status_tex_ = nullptr;  ///< "Enabled" / "Disabled".
    int          detail_last_focus_ = -1;
    bool         detail_last_enabled_ = false;

    bool textures_built_ = false;

    std::vector<SDL_Texture *> title_textures_;  ///< Per-title name textures.
    std::vector<SDL_Texture *> slot_textures_;   ///< Per-slot name textures.

    // ── Theme ──────────────────────────────────────────────────────────────

    QdTheme theme_;

    // ── Private helpers ────────────────────────────────────────────────────

    void BuildTextures(SDL_Renderer *r);
    void FreeDynamicTextures();
    void BuildTitleTextures(SDL_Renderer *r);
    void BuildSlotTextures(SDL_Renderer *r);

    void RenderTitleList(SDL_Renderer *r, s32 ox, s32 oy);
    void RenderSlotList(SDL_Renderer *r, s32 ox, s32 oy);

    void ToggleFocusedSlot();
    void EnterTitle(int idx, SDL_Renderer *r);

    static void BlitTex(SDL_Renderer *r, SDL_Texture *tex, s32 x, s32 y);

    /// Return a human-readable type description for the given slot name.
    /// e.g. "romfs" → "File replacements (romfs)"
    static std::string SlotTypeLabel(const std::string &name, bool is_dir);
};

} // namespace ul::menu::qdesktop
