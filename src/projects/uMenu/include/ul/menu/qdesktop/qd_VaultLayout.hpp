// qd_VaultLayout.hpp — Finder-style NRO file browser element for uMenu C++ (v1.0.0).
// Stage 1 of docs/45_HBMenu_Replacement_Design.md: vault skeleton.
//
// v1.10.3.10: Inherits QdContentElement (passive renderer pattern).
// QdWindow owns viewport arithmetic — scroll, scale, clip rect.
// QdVaultLayout is now a pure content painter: it reports natural canvas
// dimensions via GetNaturalW()/GetNaturalH() and paints at natural (0,0)
// coords.  Scroll state, SetContentSize, SetOwnerWindow, and viewport-culling
// have all been removed — those responsibilities belong to QdWindow.
//
// Two-pane UI: sidebar (favourites/roots) + main pane (files/folders, grid view).
// NRO launch: smi::LaunchHomebrewLibraryApplet.
// Icon decode: ExtractNroIcon from qd_NroAsset.hpp + QdIconCache.
#pragma once
#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_TextViewer.hpp>
#include <ul/menu/qdesktop/qd_ImageViewer.hpp>
#include <ul/menu/qdesktop/qd_FolderClassifier.hpp>
#include <ul/menu/qdesktop/qd_ContextMenu.hpp>
#include <SDL2/SDL.h>
#include <functional>

namespace ul::menu::qdesktop {

// ── Vault layout pixel constants ──────────────────────────────────────────────
// All values are in 1920×1080 layout space (×1.5 from Rust 1280×720 reference).

/// Width of the left sidebar panel.
static constexpr s32 VAULT_SIDEBAR_W = 270;

/// Height of the top path/nav bar inside the vault.
static constexpr s32 VAULT_PATHBAR_H = 48;

/// Usable vertical range = screen height minus topbar (48) and dock (108).
static constexpr s32 VAULT_BODY_TOP  = 48;
static constexpr s32 VAULT_BODY_H    = 1080 - 48 - 108; // 924 px

/// Grid cell dimensions for the main pane.
static constexpr s32 VAULT_CELL_W    = 120;
static constexpr s32 VAULT_CELL_H    = 108;
static constexpr s32 VAULT_CELL_GAP  = 12;

/// Icon render size inside a cell (fits within VAULT_CELL_W).
static constexpr s32 VAULT_ICON_SIZE = 72;

// ── QdVaultLayout ────────────────────────────────────────────────────────────

/// Finder-style vault file browser — passive content element (v1.10.3.10).
///
/// Hosted inside a QdWindow.  QdWindow drives all scroll / scale / clip
/// arithmetic; this class only paints content at natural (1:1) coordinates.
/// The natural canvas width is fixed at DEFAULT_WIN_W; the natural height
/// is dynamic: it grows as more entries fill additional grid rows so that
/// QdWindow can compute VSB travel distance.
class QdVaultLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdVaultLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdVaultLayout>(theme);
    }

    explicit QdVaultLayout(const QdTheme &theme);
    ~QdVaultLayout();

    // ── QdContentElement interface ─────────────────────────────────────────

    /// Fixed-width natural canvas: DEFAULT_WIN_W px (matches the window frame).
    s32 GetNaturalW() const override;

    /// Dynamic natural height: grows with the number of grid rows.
    /// QdWindow reads this to size the vertical scrollbar.
    s32 GetNaturalH() const override;

    /// v2.0.0: Vault opts into width-bound scale + VSB-scrolling.  Default
    /// uniform-scale shrinks file-grid cells to ~41 % at default window size
    /// because natural_h grows with entries (~1168 px at 30 entries vs 480 px
    /// viewport).  Width-bound keeps cells at design size; vertical overflow
    /// surfaces the VSB the user expects from a file manager.
    bool PrefersWidthBoundScale() const override { return true; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Open a directory path and populate the main pane.
    /// Safe to call before the first frame; triggers a fresh directory scan.
    void Navigate(const char *path);

    /// Set a category filter for NRO entries.  When filter != FolderIdx::None,
    /// ScanCurrentDirectory skips any .nro whose QdFolderClassifier bucket does
    /// not match filter (or filter2, if provided).  Folders and non-NRO files
    /// are always shown regardless of the filter so the directory tree remains
    /// navigable.  Call before Navigate() to pre-filter the initial scan, or
    /// after to re-scan with the new filter applied immediately.
    /// FolderIdx::None (the default) disables filtering — all entries shown.
    /// filter2 is optional; pass FolderIdx::None to use a single-bucket filter.
    /// Example: Games maps to both NxGames and ThirdPartyGames — pass both.
    void SetCategoryFilter(FolderIdx filter,
                           FolderIdx filter2 = FolderIdx::None);

    // W11-SAVE Part 2: optional callback invoked when "Edit Pokémon save" is
    // selected from the file context menu.  Wired by OpenVaultWindow() to call
    // QdDesktopIconsElement::OpenSaveEditorWindow so QdVaultLayout does not
    // need a direct dependency on QdDesktopIconsElement.
    // If nullptr, the menu option is still shown but performs a no-op (safe
    // fallback during transitions when the callback is not yet wired).
    //
    // W12 BUG-VAULT-CRASH: HW v3.3 crashed in std::_Function_base::~_Function_base()
    // when this field was assigned in OpenVaultWindow — the OLD value read at
    // offset +72 contained garbage (manager pointer 0xffe07affc0889aff), causing
    // `blr x3` to PC=garbage Alignment Fault.  Root cause: implicit default-
    // construction was not happening on this make_shared path — possibly a
    // class-layout / .o staleness interaction across the W11 agents that all
    // touched related files.  Explicit `{}` initializer forces the default
    // ctor and is the defensive fix; pair with a clean rebuild.
    std::function<void()> on_open_save_editor{};

    // W12-CHEATS: callback invoked when "View Cheats" is selected from the
    // context menu for an installed-application entry.  Wired by OpenVaultWindow()
    // to call QdDesktopIconsElement::OpenCheatsWindow(app_id) so QdVaultLayout
    // does not need a direct dependency on QdDesktopIconsElement.
    // The u64 argument is the app_id of the focused entry (non-zero).
    // If nullptr, the menu option is still shown but performs a no-op.
    std::function<void(u64)> on_open_cheats{};

    // B3.1-MODS: callback invoked when "View Mods" is selected from the
    // context menu for an installed-application entry.  Wired by OpenVaultWindow()
    // to call QdDesktopIconsElement::OpenModsWindow(app_id).
    // The u64 argument is the app_id of the focused entry (non-zero).
    std::function<void(u64)> on_open_mods{};

    std::string GetDebugState() const override;

private:
    // ── Vault entry ────────────────────────────────────────────────────────

    static constexpr size_t MAX_ENTRIES = 128;
    static constexpr size_t MAX_PATH    = 256;

    enum class EntryKind : u8 {
        Folder,      ///< directory
        Nro,         ///< .nro homebrew (ASET icon path)
        NcaNspXci,   ///< .nca / .nsp / .xci archive
        TextFile,    ///< .txt / .log / .md config prose
        ImageFile,   ///< .png / .jpg / .bmp / .gif
        AudioFile,   ///< .mp3 / .wav / .ogg / .flac
        ConfigFile,  ///< .json / .toml / .ini / .cfg
        OtherFile,   ///< anything else
    };

    // Task D: sort mode
    enum class SortMode : u8 { ByName, ByKind };

    struct Entry {
        char       name[64];
        char       full_path[MAX_PATH];
        EntryKind  kind;
        SDL_Texture *icon_tex;   ///< lazy-built; nullptr until first render
        bool       icon_decoded; ///< true once DecodeNroIcon has been attempted
    };

    // ── Sidebar ────────────────────────────────────────────────────────────

    static constexpr size_t SIDEBAR_ROOT_COUNT = 12;  // W12 extension: +6 filesystem shortcuts
    struct SidebarRoot { const char *label; const char *path; };
    static const SidebarRoot SIDEBAR_ROOTS[SIDEBAR_ROOT_COUNT];

    // ── State ──────────────────────────────────────────────────────────────

    QdTheme    theme_;
    char       cwd_[MAX_PATH];      ///< current working directory path
    Entry      entries_[MAX_ENTRIES];
    size_t     entry_count_;
    size_t     focus_idx_;          ///< currently highlighted entry (D-pad)

    /// Category filter for NRO entries.  FolderIdx::None = no filter (show all).
    /// Set via SetCategoryFilter() before or after Navigate().
    /// filter2_ is the optional second bucket (e.g. ThirdPartyGames alongside NxGames).
    FolderIdx  category_filter_  = FolderIdx::None;
    FolderIdx  category_filter2_ = FolderIdx::None;

    // ── Sidebar input state (stabilize-6 / RC-C2) ──────────────────────────
    // Touch tap or D-pad LEFT-at-col-0 enters sidebar mode; sidebar_idx_ is
    // the highlighted sidebar slot (0..SIDEBAR_ROOT_COUNT-1). The
    // sb_was_touch_active_last_frame_ latch matches the canonical
    // edge-trigger pattern used at qd_DesktopIcons.cpp:339, 1968.
    bool   sidebar_focused_ = false;
    size_t sidebar_idx_     = 0u;
    bool   sb_was_touch_active_last_frame_ = false;

    // ── Touch-drag scroll state (2026-05-06) ───────────────────────────────
    // Finger-flick / drag-to-scroll for the entry grid.  Layered on top of
    // QdWindow's VSB scroll: drag_view_offset_y_ is added to the rendered
    // entry y-position (subtracted, since positive offset = content scrolled
    // up); EntryRect() bakes the offset in so render and hit-test stay in
    // sync.  Drag is engaged only when the touch starts inside the grid
    // area (right of sidebar, below pathbar).  Tap-vs-drag deadband is
    // DRAG_DEADBAND_PX — below the threshold the gesture falls through to
    // the existing tap-to-launch behaviour on touch-release.
    static constexpr s32 DRAG_DEADBAND_PX = 8;
    s32  drag_view_offset_y_     = 0;     ///< current scroll offset (0 = top)
    bool drag_in_progress_       = false; ///< true while finger is down inside the grid
    bool drag_passed_deadband_   = false; ///< true once movement > DRAG_DEADBAND_PX
    s32  drag_start_touch_y_     = 0;     ///< y at touch-down
    s32  drag_start_offset_y_    = 0;     ///< drag_view_offset_y_ at touch-down
    s32  drag_start_touch_x_     = 0;     ///< x at touch-down (for tap fallback hit-test)
    bool drag_was_touch_active_  = false; ///< previous-frame touch-active latch

    // ── RC-C3 (P2) auto-repeat state for D-pad ─────────────────────────────
    u32    dpad_held_frames_up_    = 0u;
    u32    dpad_held_frames_down_  = 0u;
    u32    dpad_held_frames_left_  = 0u;
    u32    dpad_held_frames_right_ = 0u;

    // ── Task B: ZL context menu — unified QdContextMenu primitive ─────────
    // vault_ctx_menu_: the shared context-menu overlay (replaces bespoke popup)
    // vault_ctx_target_entry_: entries_[] index the menu was opened for
    // vault_ctx_opt_*: per-option visible index (-1 = not present in this open)
    //   Set by BuildVisibleContextMenuOptions() at open time; reset each open.
    QdContextMenu vault_ctx_menu_;
    size_t        vault_ctx_target_entry_  = 0;

    int vault_ctx_opt_open_         = -1;
    int vault_ctx_opt_launch_app_   = -1;
    int vault_ctx_opt_properties_   = -1;
    int vault_ctx_opt_rename_       = -1;
    int vault_ctx_opt_delete_       = -1;
    int vault_ctx_opt_cut_          = -1;
    int vault_ctx_opt_copy_         = -1;
    int vault_ctx_opt_paste_        = -1;
    int vault_ctx_opt_new_folder_   = -1;
    int vault_ctx_opt_save_editor_  = -1;  // W11-SAVE Part 2: "Edit Pokémon save"
    int vault_ctx_opt_cheats_       = -1;  // W12-CHEATS: "View Cheats"
    int vault_ctx_opt_mods_         = -1;  // B3.1-MODS: "View Mods"
    int vault_ctx_opt_cancel_       = -1;

    // W12-CHEATS / B3.1-MODS: app_id of the focused entry when the ctx menu was opened.
    // Set by BuildVisibleContextMenuOptions(); consumed by DispatchContextMenuOption().
    u64 vault_ctx_app_id_           = 0;

    // ── Clipboard state for Cut / Copy / Paste ─────────────────────────────
    char   clipboard_path_[MAX_PATH] = {};
    bool   clipboard_is_cut_         = false;
    bool   has_clipboard_            = false;

    // ── Task D: sort mode and dotfile toggle ────────────────────────────────
    SortMode sort_mode_      = SortMode::ByName;
    bool     show_dotfiles_  = false;

    // Bottom hint bar texture — rendered once in ctor, freed in dtor.
    // Displays keybind legend: "B / + Close  •  A Open  •  Y Sort  •  Up/Down Navigate"
    SDL_Texture *hint_bar_tex_;

    // Cached SDL text textures for sidebar labels.
    // 6 sidebar entries, rendered once and reused.
    SDL_Texture *sidebar_tex_[SIDEBAR_ROOT_COUNT];

    // Per-entry name text textures; nullptr until first render of that slot.
    SDL_Texture *name_tex_[MAX_ENTRIES];

    QdIconCache cache_;

    // ── Viewer overlay ─────────────────────────────────────────────────────
    // One or the other is active at a time; viewer_active_ gates routing.
    QdTextViewer::Ref  text_viewer_;
    QdImageViewer::Ref image_viewer_;
    bool               viewer_active_ = false;

    // ── Private helpers ────────────────────────────────────────────────────

    /// Read entries from cwd_ into entries_[].
    /// Clears existing entries and resets focus/scroll.
    void ScanCurrentDirectory();

    /// Destroy all per-entry SDL_Texture* objects (icon_tex + name_tex_).
    /// Called before every Navigate() and in the destructor.
    void FreeEntryTextures();

    /// Attempt to extract and cache the ASET icon for entry e.
    /// On failure populates the cache with a DJB2 fallback icon.
    /// Sets e.icon_decoded = true regardless of outcome.
    /// Returns true if a real JPEG was decoded.
    bool DecodeNroIcon(Entry &e);

    /// Render the left sidebar panel.
    void RenderSidebar(SDL_Renderer *r, s32 origin_x, s32 origin_y) const;

    /// Render the main file/folder pane.
    void RenderMainPane(SDL_Renderer *r, s32 origin_x, s32 origin_y);

    /// Navigate to the parent of cwd_ (no-op at root "sdmc:/").
    void NavigateUp();

    /// Enter the focused entry: descend into folder or launch NRO.
    void EnterFocused();

    /// Compute grid columns for the given main pane pixel width.
    /// Callers pass GetNaturalW(); result drives cell layout.
    static s32 MainPaneCols(s32 content_w);

    /// Compute the pixel rect of entry slot i in the main pane.
    /// Returns false if i is outside the visible window.
    /// Bakes the touch-drag scroll offset (drag_view_offset_y_) into out_y so
    /// render and hit-test sites observe the same shifted coordinate space.
    bool EntryRect(size_t i, s32 &out_x, s32 &out_y, s32 origin_x, s32 origin_y) const;

    /// Maximum allowed value of drag_view_offset_y_ given the current entry
    /// count.  Returns 0 when all rows fit in one screen (no scroll needed).
    /// Conservative: clamps to the natural-canvas height minus the pathbar so
    /// the grid stops dragging when the last row is roughly in view.
    s32 MaxScrollOffsetY() const;

    /// Delegates to vault_ctx_menu_.Render(r); no-op if not open.
    void RenderContextMenu(SDL_Renderer *r) const;

    /// Called after vault_ctx_menu_ closes; maps GetSelectedIndex() to action.
    void DispatchContextMenuOption();

    /// Builds QdContextMenuItem list for vault_ctx_target_entry_, assigns
    /// per-opt index fields, then calls vault_ctx_menu_.Open().
    /// Filters out LaunchAsApp when the entry is not an .nro.
    void BuildVisibleContextMenuOptions();

    /// Launch vault_ctx_target_entry_ as a homebrew application (takes over
    /// the configured HomebrewApplicationTakeoverApplicationId title from
    /// uSystem).  Mirrors the fade/finalize sequence in EnterFocused().
    /// No-op if the entry is missing or not an .nro.
    void DoLaunchAsApplication();

    // ── File-operation helpers (hbmenu-style context menu) ─────────────────

    /// Open the swkbd inline keyboard and return the entered string.
    /// header is the guide text shown above the input field (max 64 chars).
    /// initial is pre-filled text; pass "" for an empty field.
    /// out_buf receives the result (null-terminated, capacity = MAX_PATH).
    /// Returns true on successful commit, false on cancel.
    bool PromptText(const char *header, const char *initial,
                    char *out_buf, size_t out_capacity);

    /// Show a two-choice swkbd prompt ("Yes" / "No") for destructive ops.
    /// Returns true if the user confirmed (chose "Yes").
    bool ConfirmDelete(const char *path);

    /// Rename the entry at ctx_menu_entry_ interactively via swkbd.
    void DoRename();

    /// Delete the entry at ctx_menu_entry_ (file or directory).
    /// Prompts for confirmation before proceeding.
    void DoDelete();

    /// Mark the entry at ctx_menu_entry_ as the cut source.
    void DoCut();

    /// Mark the entry at ctx_menu_entry_ as the copy source.
    void DoCopy();

    /// Paste the clipboard item into cwd_.
    /// Moves (and removes source) if clipboard_is_cut_, copies otherwise.
    void DoPaste();

    /// Prompt for a new folder name via swkbd and create it in cwd_.
    void DoNewFolder();

    /// Classify a non-.nro file by its extension into the appropriate EntryKind.
    /// ext must point to the first char AFTER the dot (already lower-cased, null-terminated).
    static EntryKind ClassifyByExtension(const char *ext);
};

} // namespace ul::menu::qdesktop
