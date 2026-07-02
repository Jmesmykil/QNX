// qd_SaveEditorLayout.hpp — Pokemon save-editor skeleton for uMenu (PR-skeleton).
//
// Architecture:
//   QdSaveEditorLayout is a QdContentElement that QdWindow can host.
//   The follow-up PR wires it into the dock / Vault context menu and adds the
//   per-game save parsers sourced from pkHouse (GPL-2.0, license-compatible).
//
// Navigation state machine:
//   TitlePicker  → shows supported game list; A to enter.
//   PartyBox     → placeholder panel (PKM grid in follow-up PR).
//   Inventory    → placeholder panel.
//   Trainer      → placeholder panel.
//   L/R or D-pad left/right switch tabs in non-TitlePicker modes.
//   B            → back (TitlePicker → dismiss, non-TitlePicker → TitlePicker).
//
// Compile guard: nothing in this file touches libnx save-data APIs;
//   the save open / commit calls are deferred to qd_SwishCrypto.cpp
//   and the per-game parser layer added in the follow-up PR.
#pragma once

#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_SaveAutoscan.hpp>  // W12-SAVE-DISCO: autoscan integration
#include <ul/menu/qdesktop/qd_SwShSaveParser.hpp> // W13-SAVE-PARSER: PK8 decode
#include <ul/menu/qdesktop/qd_BDSPSaveParser.hpp> // BoxSlotLite, kBoxCount — box view
#include <ul/menu/ui/ui_IMenuLayout.hpp>   // QdSaveEditorHostLayout : IMenuLayout
#include <SDL2/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ul::menu::qdesktop {

// ── QdSaveEditorLayout ────────────────────────────────────────────────────────

/// Pokemon save editor hosted inside a QdWindow.
///
/// PR-skeleton: renders a title picker and named placeholder panels only.
/// Per-game parsers, SwishCrypto wiring, and save I/O are deferred to the
/// follow-up PR once this integration model is reviewed.
class QdSaveEditorLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdSaveEditorLayout>;

    // Factory — mirrors the QdAboutElement::New pattern used across qdesktop.
    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdSaveEditorLayout>(theme);
    }

    explicit QdSaveEditorLayout(const QdTheme &theme);
    ~QdSaveEditorLayout() override;

    // ── QdContentElement contract ──────────────────────────────────────────

    // Natural canvas = full Switch display in portrait layout spec coordinates.
    s32 GetNaturalW() const override { return 1280; }
    s32 GetNaturalH() const override { return  720; }

    // Content is fixed-extent (not a growing list) → uniform scale.
    bool PrefersWidthBoundScale() const override { return false; }

    // IsNaturalSizeDirty / ClearNaturalSizeDirty are inherited from
    // QdContentElement; natural size never changes for this panel.
    bool IsNaturalSizeDirty() const { return false; }

    // Paint the active mode onto the renderer at natural (unscaled) coords.
    // SDL scale and clip are pre-applied by the hosting QdWindow.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 ox, s32 oy) override;

    // Handle controller input forwarded from QdWindow.
    // touch_pos is pre-translated to content-local natural coordinates.
    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    // W11-SAVE Part 5: return a context-appropriate bottom-bar hint string.
    // TitlePicker → "A  Open game · B  Close"
    // Any panel  → "L/R  Tab · B  Back · A  Edit (coming soon)"
    // Called by on_tick in OpenSaveEditorWindow to keep the QdWindow hint bar
    // in sync with the active navigation state.
    std::string GetBottomHint() const;

    // Hierarchical Back (B): pop one nav level (detail -> box-expanded -> box-list
    // -> panel -> TitlePicker), then return false at the top so QdWindow closes.
    // Consulted by the chrome before it closes on B — aligns B across all windows.
    bool OnBackRequested() override;

    std::string GetDebugState() const override;

    // W12-SAVE-DISCO Part 3: force a save rescan (e.g. "Rescan" button).
    void RequestRescan();

    // v3.6 absorb wave 1: JKSV-style backup hooks on the TitlePicker.
    // Iterate every TID mapped to focused game_index in kBackupTidMap and
    // try fsdevMountSaveData against each.  Toast on result.
    void DoBackupFocusedGame();
    void DoRestoreLatestFocusedGame();

private:
    // ── Navigation state ───────────────────────────────────────────────────

    /// Top-level UI mode for the save editor.
    enum class Mode : uint8_t {
        TitlePicker,  ///< List of supported games — entry point.
        PartyBox,     ///< Party / Box viewer (placeholder in this PR).
        Inventory,    ///< Item bag editor (placeholder in this PR).
        Trainer,      ///< Trainer card editor (placeholder in this PR).
    };

    Mode mode_        = Mode::TitlePicker;
    int  title_focus_ = 0;  ///< Index of the highlighted game in TitlePicker.
    int  panel_focus_ = 0;  ///< Tab focus index within non-TitlePicker modes.

    // ── Supported games ────────────────────────────────────────────────────

    /// Number of supported game entries shown in TitlePicker.
    /// Slot 5 (index == QdSaveAutoscan::kOtherGameIndex) is the "Other" row
    /// shown only when unattributed saves are found in sdmc:/saves/.
    static constexpr int kGameCount = 6;

    /// Display names in tab order (pkHouse canonical order).
    /// W12-SAVE-DISCO: kTidMap in qd_SaveAutoscan.cpp is keyed to these indices.
    ///   0: Let's Go (Pikachu + Eevee)
    ///   1: Sword / Shield
    ///   2: Brilliant Diamond / Shining Pearl
    ///   3: Legends: Arceus
    ///   4: Scarlet / Violet
    ///   5: Other (generic saves found in sdmc:/saves/ — kOtherGameIndex)
    static constexpr const char *kGameNames[kGameCount] = {
        "Pokemon: Let's Go",
        "Sword / Shield",
        "Brilliant Diamond / Shining Pearl",
        "Legends: Arceus",
        "Scarlet / Violet",
        "Other (generic saves)",
    };

    // ── Tab labels for non-TitlePicker modes ──────────────────────────────

    static constexpr int kTabCount = 4;

    /// Tab labels; index 3 is a pseudo-tab ("Back") that pops to TitlePicker.
    static constexpr const char *kTabLabels[kTabCount] = {
        "Party/Box",
        "Items",
        "Trainer",
        "Back",
    };

    // ── W12-SAVE-DISCO: autoscan ───────────────────────────────────────────

    /// Autoscanner instance; cached between Open() calls until Rescan() resets it.
    QdSaveAutoscan autoscan_;

    /// Per-game save-count snapshot from the last scan (-1 = not scanned yet).
    int save_counts_[kGameCount] = { -1, -1, -1, -1, -1, -1 };

    /// Whether the autoscan result has been applied to save_counts_[].
    bool scan_applied_ = false;

    // ── SDL texture cache ──────────────────────────────────────────────────

    /// Cached title textures for TitlePicker rows (index == kGameNames index).
    SDL_Texture *title_textures_[kGameCount] = {};

    /// Cached "N saves found" / "no saves" suffix textures (W12-SAVE-DISCO).
    /// Built or rebuilt when scan results change (RebuildSaveCountTextures).
    SDL_Texture *save_count_textures_[kGameCount] = {};

    /// "Rescanning…" status texture shown while scan is running.
    SDL_Texture *rescan_tex_ = nullptr;

    /// W12B-AUTOSCAN: diagnostic line shown at the bottom of TitlePicker,
    /// e.g. "Scanned: 32 paths  Skipped: 28 (not found)".
    SDL_Texture *diag_tex_ = nullptr;

    /// Frame budget for transient toasts (counts down at 60 fps).  Currently
    /// only the PartyBox detail-defer toast (detail_toast_frames_) uses it; the
    /// old TitlePicker "no saves" toast was removed (it had no reachable setter
    /// and could never display — see audits/2026-06-12-audit.md item).
    static constexpr int kToastFrames = 180;  ///< ~3 s at 60 fps.

    /// Cached tab label textures.
    SDL_Texture *tab_textures_[kTabCount] = {};

    /// "Coming soon" body text, rendered once (Inventory / Trainer tabs only).
    SDL_Texture *placeholder_tex_ = nullptr;

    /// Hint bar texture ("B Back   A Select" / "L/R Switch Tab").
    SDL_Texture *hint_tex_ = nullptr;

    bool textures_built_ = false;  ///< true after BuildTextures() has run.

    // ── W13-SAVE-PARSER: PartyBox live data ───────────────────────────────────

    /// Current decoded save, populated when entering PartyBox from TitlePicker.
    SwShSave current_save_;

    /// Whether current_save_ has been populated for the active title.
    bool save_loaded_ = false;

    /// SwishResult from the last ParseFile call (shown on failure).
    SwishResult save_parse_result_ = SwishResult::Ok;

    /// Why the last TryLoadSave() did not yield a displayable party.  Lets the
    /// A-handler show a distinct, honest message per case instead of one
    /// catch-all string (which previously misreported a wrong-path miss as
    /// "empty or unsupported format").
    enum class LoadFailReason : uint8_t {
        None,           ///< Load succeeded — no failure.
        NoParser,       ///< SD backup exists but no parser for this game (BDSP…).
        NoSdBackup,     ///< No SD backup directory found for the focused game.
        FileMissing,    ///< Backup dir found but no readable 'main' save inside.
        ParseFailed,    ///< 'main' read but SwishCrypto/parse rejected it.
    };
    LoadFailReason load_fail_reason_ = LoadFailReason::None;

    /// Which party slot is focused in PartyBox mode.
    int party_focus_ = 0;

    /// Per-slot nickname/species textures (6 slots).
    SDL_Texture *party_name_tex_[6]   = {};

    /// Per-slot level textures.
    SDL_Texture *party_level_tex_[6]  = {};

    /// Per-slot held-item textures.
    SDL_Texture *party_item_tex_[6]   = {};

    /// Per-slot shiny indicator textures (only non-null for shiny slots).
    SDL_Texture *party_shiny_tex_[6]  = {};

    /// "Empty slot" placeholder texture (greyed out when slot is unoccupied).
    SDL_Texture *party_empty_tex_ = nullptr;

    /// Parse-failure message texture (shown when save could not be read).
    SDL_Texture *parse_error_tex_ = nullptr;

    /// "Detail view in v3.6" toast texture — shown on A press in PartyBox.
    SDL_Texture *detail_defer_tex_ = nullptr;

    /// Frames remaining to show the detail-defer toast.
    int detail_toast_frames_ = 0;

    // ── W13-SAVE-PARSER: helpers ──────────────────────────────────────────────

    /// (Re)build all party slot textures from current_save_.  Called after
    /// a successful parse.  Frees any previously cached slot textures first.
    void BuildPartyTextures(SDL_Renderer *r);

    /// Free all party slot SDL textures.
    void FreePartyTextures();

    /// Render the real PartyBox grid from current_save_.
    void RenderPartyBox(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Render the parse-failure panel (shown when save load fails).
    void RenderParseError(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Try to load a save for the game at title_focus_.
    /// Populates current_save_ / save_parse_result_ / save_loaded_.
    void TryLoadSave(SDL_Renderer *r);

    // ── Box view (collapsible PC boxes) ───────────────────────────────────────
    // Toggled with Y inside the Party/Box tab.  BDSP only for now (the box parser
    // is host-verified against a real Sav8BS save); SwSh box storage (SCBlocks)
    // is a follow-up.  Collapsed-by-default box list avoids overwhelming the user
    // with 1200 slots — expand one box at a time.
    bool box_mode_       = false;    ///< false = party grid, true = box list/view
    bool box_loaded_     = false;    ///< boxes parsed for the current save
    bool box_supported_  = false;    ///< true only for BDSP saves
    int  box_list_sel_   = 0;        ///< highlighted box (0..kBoxCount-1)
    int  box_expanded_   = -1;       ///< expanded box index, or -1 (list view)
    std::vector<BoxSlotLite> box_slots_;          ///< occupied slots only
    int  box_counts_[kBoxCount] = {};             ///< per-box occupied count
    std::string loaded_save_path_;                ///< path backing the current view

    /// Lazily parse the PC boxes for the loaded save (BDSP).  No-op if already
    /// loaded or unsupported.  Called when the box view is first opened.
    void LoadBoxesIfNeeded();

    /// Handle input while the box sub-view is active (Party/Box tab).
    void BoxModeInput(u64 keys_down);

    /// Render the collapsible box view (box list, or one expanded box).
    void RenderBoxView(SDL_Renderer *r, s32 ox, s32 oy) const;

    // ── Pokémon detail view ───────────────────────────────────────────────────
    // Pressing A on a party slot OR a box slot opens a full read-out of the
    // decoded Pokémon (PK8/PB8 share field names, so one struct serves both).
    struct MonDetail {
        bool     valid     = false;
        uint16_t species   = 0;
        uint8_t  level     = 0;
        uint8_t  nature    = 0;
        uint16_t ability   = 0;
        uint8_t  gender    = 0;     ///< 0=M, 1=F, 2=unknown
        bool     shiny     = false;
        uint16_t held_item = 0;
        uint32_t pid       = 0;
        uint16_t tid       = 0;
        uint16_t sid       = 0;
        uint8_t  iv[6]     = {};    ///< HP, Atk, Def, SpA, SpD, Spe
        uint16_t moves[4]  = {};
        char     name[32]  = {};
        char     ot[24]    = {};
    };
    bool      detail_open_  = false;  ///< detail panel shown over party/box
    MonDetail detail_;                ///< the Pokémon currently displayed
    int       box_slot_sel_ = 0;      ///< selected occupied slot in an expanded box

    /// Render the full-detail panel for detail_ (over the current view).
    void RenderDetail(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Build a MonDetail from a decoded PK8 (SwSh) or PB8 (BDSP) — both share
    /// field names, so one template serves both.  Defined in the .cpp.
    template <typename T> static MonDetail BuildDetail(const T &m);

    // ── Trainer tab ───────────────────────────────────────────────────────────
    // Populated in TryLoadSave from the parsed save's MyStatus block.  BDSP is
    // host-verified (MyStatus8b @ 0x79BB4); SwSh is pending a key fix + a real
    // SwSh save to verify, so trainer_.valid stays false for SwSh for now.
    struct TrainerInfo {
        bool     valid  = false;
        char     ot[24] = {};
        uint16_t tid    = 0;
        uint16_t sid    = 0;
        uint32_t money  = 0;
        uint8_t  gender = 0;   ///< 0=M, 1=F
    };
    TrainerInfo trainer_;

    /// Render the Trainer tab from trainer_.
    void RenderTrainer(SDL_Renderer *r, s32 ox, s32 oy) const;

    // ── Items / Bag tab ───────────────────────────────────────────────────────
    // Populated in TryLoadSave from the bag (BDSP MyItem8b @ 0x0563C, host-
    // verified).  SwSh bag is pending the SwSh key fix, so bag_supported_ stays
    // false for SwSh for now.
    std::vector<BagItemLite> bag_items_;   ///< held items (count > 0)
    bool bag_supported_ = false;           ///< true only for BDSP saves
    int  bag_sel_       = 0;               ///< highlighted bag row (scroll)
    void RenderInventory(SDL_Renderer *r, s32 ox, s32 oy) const;

    // ── Theme ──────────────────────────────────────────────────────────────

    QdTheme theme_;

    // ── Private helpers ────────────────────────────────────────────────────

    /// Rasterise all SDL_Texture* objects on first render.
    void BuildTextures(SDL_Renderer *r);

    /// Release all cached SDL textures.
    void FreeTextures();

    /// W12-SAVE-DISCO: apply scan results to save_counts_[] and rebuild
    /// save_count_textures_[].  Called lazily from OnRender after the scan
    /// completes.
    void ApplyScanResult(SDL_Renderer *r);

    /// Render the title-picker list.
    void RenderTitlePicker(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Render the tab bar + placeholder body for non-TitlePicker modes.
    void RenderPanel(SDL_Renderer *r, s32 ox, s32 oy) const;

    /// Helper: blit a texture at (x, y) using SDL_QueryTexture for size.
    static void BlitTex(SDL_Renderer *r, SDL_Texture *tex, s32 x, s32 y);
};

// ── QdSaveEditorHostLayout ────────────────────────────────────────────────────

/// IMenuLayout host wrapper for QdSaveEditorLayout.
///
/// Required pattern: MenuApplication::GetLayout<IMenuLayout>() is an unchecked
/// static_pointer_cast, so every active layout MUST be a real IMenuLayout
/// subclass.  A bare pu::ui::Layout here would Data Abort at 0x0 on the first
/// HOME or GameCardMount message.  See qd_VaultHostLayout.hpp for the full
/// crash-chain description.
class QdSaveEditorHostLayout : public ul::menu::ui::IMenuLayout {
public:
    // PU_SMART_CTOR expands to: using Ref = std::shared_ptr<T>; + static Ref New(...);
    // so we do NOT also declare `using Ref = ...` ourselves (redeclaration error).
    explicit QdSaveEditorHostLayout(QdSaveEditorLayout::Ref element);

    PU_SMART_CTOR(QdSaveEditorHostLayout)

    // ── IMenuLayout pure-virtual obligations ──────────────────────────────

    void OnMenuInput(u64 keys_down, u64 keys_up,
                     u64 keys_held,
                     pu::ui::TouchPoint touch_pos) override;

    // HOME press returns the user to the main desktop.
    bool OnHomeButtonPress() override;

    // No per-layout sfx in this PR.
    void LoadSfx() override;
    void DisposeSfx() override;

private:
    QdSaveEditorLayout::Ref editor_element_;
};

} // namespace ul::menu::qdesktop
