// qd_VaultLayout.hpp — Finder-style NRO file browser element for uMenu C++ (v1.0.0).
// Stage 1 of docs/45_HBMenu_Replacement_Design.md: vault skeleton.
// Inherits pu::ui::elm::Element (same pattern as QdDesktopIconsElement).
// Two-pane UI: sidebar (favourites/roots) + main pane (files/folders, grid view).
// NRO launch: smi::LaunchHomebrewLibraryApplet.
// Icon decode: ExtractNroIcon from qd_NroAsset.hpp + QdIconCache.
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <SDL2/SDL.h>

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

/// Finder-style vault file browser.
/// Mounted as an Element on the desktop layout (visible when the user
/// opens the "Files" dock shortcut).  Hidden by default — call SetVisible(true)
/// and Navigate("sdmc:/switch/") to activate.
class QdVaultLayout : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdVaultLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdVaultLayout>(theme);
    }

    explicit QdVaultLayout(const QdTheme &theme);
    ~QdVaultLayout();

    // ── Element interface ──────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Open a directory path and populate the main pane.
    /// Safe to call before the first frame; triggers a fresh directory scan.
    void Navigate(const char *path);

private:
    // ── Vault entry ────────────────────────────────────────────────────────

    static constexpr size_t MAX_ENTRIES = 128;
    static constexpr size_t MAX_PATH    = 256;

    enum class EntryKind : u8 { Folder, Nro, OtherFile };

    struct Entry {
        char       name[64];
        char       full_path[MAX_PATH];
        EntryKind  kind;
        SDL_Texture *icon_tex;   ///< lazy-built; nullptr until first render
        bool       icon_decoded; ///< true once DecodeNroIcon has been attempted
    };

    // ── Sidebar ────────────────────────────────────────────────────────────

    static constexpr size_t SIDEBAR_ROOT_COUNT = 6;
    struct SidebarRoot { const char *label; const char *path; };
    static const SidebarRoot SIDEBAR_ROOTS[SIDEBAR_ROOT_COUNT];

    // ── State ──────────────────────────────────────────────────────────────

    QdTheme    theme_;
    char       cwd_[MAX_PATH];      ///< current working directory path
    Entry      entries_[MAX_ENTRIES];
    size_t     entry_count_;
    size_t     focus_idx_;          ///< currently highlighted entry (D-pad)
    s32        scroll_offset_;      ///< row scroll in main pane

    // Cached SDL text textures for sidebar labels.
    // 6 sidebar entries, rendered once and reused.
    SDL_Texture *sidebar_tex_[SIDEBAR_ROOT_COUNT];

    // Per-entry name text textures; nullptr until first render of that slot.
    SDL_Texture *name_tex_[MAX_ENTRIES];

    QdIconCache cache_;

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

    /// Compute grid columns for the main pane width.
    static s32 MainPaneCols();

    /// Compute the pixel rect of entry slot i in the main pane.
    /// Returns false if i is outside the visible window.
    bool EntryRect(size_t i, s32 &out_x, s32 &out_y, s32 origin_x, s32 origin_y) const;
};

} // namespace ul::menu::qdesktop
