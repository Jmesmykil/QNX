// qd_NintendoAppsLayout.hpp — 4×2 tile grid of Nintendo built-in app launchers.
//
// Inherits QdContentElement (passive renderer contract — see qd_ContentElement.hpp).
// Natural canvas: 780×480 px (matches v1.10.3.10.5 default window dimensions).
//
// Tile grid geometry:
//   4 cols × 175 px + 3 gaps × 12 px = 736 px  → grid_x = (780 - 736) / 2 = 22 px
//   2 rows × 150 px + 1 gap × 12 px  = 312 px  → fits inside 374 px available
//                                                  (480 - 56 body_top - 50 hint bar)
//   body_top = 56 px (48 px topbar + 8 px padding)
//
// Input: touch hit-tests tiles in content-local coordinates (pre-translated by QdWindow).
//        Tap on tile i calls kNintendoApps[i].launch(), which blocks until the
//        launched applet exits.  No on_tick needed — launchers are synchronous.
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_NintendoApps.hpp>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <switch.h>   // CapsAlbumFileId — album-browser state

namespace ul::menu::qdesktop {

// ── Layout pixel constants ────────────────────────────────────────────────────

static constexpr s32 QD_NA_TILE_COLS = 4;
static constexpr s32 QD_NA_TILE_ROWS = 2;
static constexpr s32 QD_NA_TILE_W    = 175;
static constexpr s32 QD_NA_TILE_H    = 150;
static constexpr s32 QD_NA_TILE_GAP  =  12;
static constexpr s32 QD_NA_BODY_TOP  =  56;   ///< Below 48-px topbar + 8-px padding.

// ── QdNintendoAppsLayout ──────────────────────────────────────────────────────

/// Full-window 4×2 tile grid launching Nintendo built-in apps and applets.
/// No service sessions to open — each launcher call is self-contained.
class QdNintendoAppsLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdNintendoAppsLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdNintendoAppsLayout>(theme);
    }

    explicit QdNintendoAppsLayout(const QdTheme &theme);
    ~QdNintendoAppsLayout();

    /// Host-supplied hook to open the windowed Q OS Settings in-place.
    /// The Nintendo "Settings" tile calls this instead of launching an applet:
    /// stock System Settings is rendered inside qlaunch (which Q OS replaces)
    /// and the standalone "set" library applet is devkit-only (not on retail),
    /// so there is nothing to launch — Q OS Settings is the in-place settings
    /// app, exactly as upstream uLaunch routes its own "Settings".
    void SetOnOpenSettings(std::function<void()> cb) { on_open_settings_ = std::move(cb); }

    // ── QdContentElement interface ─────────────────────────────────────────────
    s32 GetNaturalW() const override { return 780; }
    s32 GetNaturalH() const override { return 480; }

    // ── Element positional stubs (QdContentElement base provides defaults) ──────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return GetNaturalW(); }
    s32 GetHeight() override { return GetNaturalH(); }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // Context hint for the WINDOW's chrome bottom bar (Tiles / album grid / image).
    // The host window calls this from on_tick so all windows show hints the SAME
    // way (the standard QdWindow status bar) — no per-window in-content hint strip.
    std::string GetBottomHint() const;

    // Hierarchical Back (B): pop album Image -> grid -> Tiles, then return false so
    // the window closes.  Consulted by QdWindow before it closes on B — so B is one
    // consistent back across every window (no "close + go back at once" conflict).
    bool OnBackRequested() override;

    std::string GetDebugState() const override;

private:
    // ── Private helpers ────────────────────────────────────────────────────────

    /// Compute the pixel rect of tile index i (row-major, 0-based).
    /// Returns the top-left (tx, ty) of the tile in natural coordinates.
    void TileRect(int i, s32 &tx, s32 &ty) const;

    /// Render one tile at natural-coordinate position (tx, ty).
    void RenderTile(SDL_Renderer *r, s32 tx, s32 ty, const NintendoApp &app,
                    bool hovered) const;

    // ── Album browser (caps:a screenshot viewer) ───────────────────────────────
    // The Album tile opens an in-window screenshot browser instead of an applet:
    // PhotoViewer (0x15) is hijacked to hbl on this setup, so the stock Album
    // applet can't be launched.  caps:a gives full read access to every
    // screenshot on NAND + SD; we list them and decode the selected JPEG into a
    // texture shown fit-to-window.  Per the QdWindow input contract, B closes
    // the whole window (chrome-level); X steps back one level
    // (Image -> List -> tile grid).
    enum class AlbumMode { Tiles, List, Image };

    struct AlbumItem {
        CapsAlbumFileId file_id;
        u64             size;
        char            label[32];        ///< "YYYY-MM-DD HH:MM:SS" (sized for u8/u16 worst case)
        SDL_Texture    *thumb_tex = nullptr;  ///< lazily-decoded 320x180 preview (grid)
        bool            thumb_failed = false; ///< don't retry a failed thumbnail decode
    };

    void OpenAlbumBrowser();          ///< caps:a enumerate -> album_items_, mode=List
    void CloseAlbumBrowser();         ///< free image + list, mode=Tiles
    bool LoadAlbumImage(int idx);     ///< caps:a load+decode selected -> album_img_tex_
    void FreeAlbumImage();
    void EnsureAlbumThumb(SDL_Renderer *r, int idx);  ///< lazy-decode the grid thumbnail for idx
    void FreeAlbumThumbs();           ///< free all decoded thumbnail textures
    void RenderAlbumList(SDL_Renderer *r, s32 ax, s32 ay);  ///< thumbnail GRID (non-const: lazy decode)
    void RenderAlbumImage(SDL_Renderer *r, s32 ax, s32 ay) const;
    void AlbumInput(u64 keys_down, const pu::ui::TouchPoint &touch);

    // ── State ──────────────────────────────────────────────────────────────────

    QdTheme theme_;
    int     hovered_idx_ = -1;   ///< Index of tile under touch/cursor, or -1.

    // Bottom hint bar — rendered once in ctor, freed in dtor.
    SDL_Texture *hint_bar_tex_;

    /// Cache of tile-icon textures keyed by NintendoApp::icon_path (the
    /// const char* literal is stable for the program lifetime — pointer
    /// equality is sufficient for keying).  Loaded lazily on first paint
    /// from RenderTile (which is const, hence `mutable`).  Each value is
    /// either a loaded SDL_Texture* or nullptr (sentinel meaning "load
    /// already attempted and failed; do not retry").  Freed in the dtor.
    mutable std::map<const char *, SDL_Texture *> icon_tex_cache_;

    // ── Album-browser runtime state ─────────────────────────────────────────────
    AlbumMode               album_mode_   = AlbumMode::Tiles;
    std::vector<AlbumItem>  album_items_;
    int                     album_sel_    = 0;       ///< selected list row
    int                     album_top_    = 0;       ///< first visible row (scroll)
    std::string             album_status_;           ///< non-empty = status/error line
    SDL_Texture            *album_img_tex_ = nullptr;///< decoded current screenshot
    int                     album_img_w_  = 0;
    int                     album_img_h_  = 0;
    /// Touch debounce: latched true after a tap triggers an action, cleared on
    /// release.  Stops one tap from acting twice (e.g. tap-back from Image was
    /// bleeding into the List handler and opening a file underneath).
    bool                    album_touch_latched_ = false;

    // Host hook: open the windowed Q OS Settings (set by OpenNintendoAppsWindow).
    std::function<void()>   on_open_settings_;
};

} // namespace ul::menu::qdesktop
