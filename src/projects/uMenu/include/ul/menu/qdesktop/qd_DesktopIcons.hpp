// qd_DesktopIcons.hpp — Auto-grid desktop icon element for uMenu C++ SP3 (v1.3.0).
// Ported from tools/mock-nro-desktop-gui/src/desktop_icons.rs + wm.rs.
// Scans sdmc:/switch/*.nro once at construction; paints icon cells every frame.
// SP2 additions: dock magnify animation, prev_magnify_center_ sentinel.
// SP3 additions: Application entries via SetApplicationEntries(); JPEG icon loading.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_IconCategory.hpp>
#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <ul/menu/menu_Entries.hpp>
#include <string>
#include <array>
#include <vector>

namespace ul::menu::qdesktop {

// ── Icon kind discriminant ─────────────────────────────────────────────────
// Distinguishes the three sources of desktop entries.
enum class IconKind : u8 {
    Builtin     = 0,   // Pre-populated Q OS dock shortcut (e.g. Terminal, Vault)
    Nro         = 1,   // Homebrew NRO file scanned from sdmc:/switch/
    Application = 2,   // Installed Switch application (NSP/XCI, from SetApplicationEntries)
    Special     = 3,   // Switch system applet shortcut (Settings, Album, Themes, etc.)
};

// ── Icon grid constants (×1.5 from Rust 1280×720) ─────────────────────────
// Rust: ICON_CELL_W=96  → C++: 144
static constexpr s32 ICON_CELL_W     = 144;
// Rust: ICON_CELL_H=88  → C++: 132
static constexpr s32 ICON_CELL_H     = 132;
// Rust: ICON_BG_INSET=8 → C++: 12
static constexpr s32 ICON_BG_INSET   = 12;
// Rust: ICON_BG_W = ICON_CELL_W - ICON_BG_INSET*2 = 80 → C++: 120
static constexpr s32 ICON_BG_W       = 120;
// Rust: ICON_BG_H=52 → C++: 78
static constexpr s32 ICON_BG_H       = 78;
// Rust: ICON_GRID_GAP_X=16 → C++: 24
static constexpr s32 ICON_GRID_GAP_X = 24;
// Rust: ICON_GRID_TOP=48 → C++: 72
static constexpr s32 ICON_GRID_TOP   = 72;
// Rust: ICON_GRID_LEFT=24 → C++: 36
static constexpr s32 ICON_GRID_LEFT  = 36;
// Same column count — 11 fits within 1920 px
static constexpr s32 ICON_GRID_COLS  = 11;
// Same max row count — 6 rows stay above the dock area
static constexpr s32 ICON_GRID_MAX_ROWS = 6;
// Same icon cap as Rust MAX_ICONS
static constexpr size_t MAX_ICONS    = 48;

// Number of Q OS built-in dock icons pre-populated at construction.
static constexpr size_t BUILTIN_ICON_COUNT = 6;

// ── NroEntry ──────────────────────────────────────────────────────────────

// One entry in the auto-grid icon array.
struct NroEntry {
    // Display name (stripped of .nro suffix, builtin label, or application name).
    char     name[64];
    // Single ASCII glyph for the icon body.
    char     glyph;
    // Fallback background colour (category or DJB2-derived).
    u8       bg_r, bg_g, bg_b;
    // Absolute NRO path on sdmc: (empty for builtin and Application entries).
    // 769 = FS_MAX_PATH (0x301) — full Horizon path + NUL terminator (F-04 fix).
    char     nro_path[769];
    // Absolute path to the JPEG icon for Application entries.
    // Populated from EntryControlData::icon_path when custom_icon_path is set,
    // otherwise empty (icon falls back to hash-derived colour glyph).
    // 769 bytes: same bound as nro_path.
    char     icon_path[769];
    // True if this is a Q OS built-in dock app, not an NRO file.
    bool     is_builtin;
    // Dock slot index (only meaningful when is_builtin == true).
    u8       dock_slot;
    // NRO category badge.
    NroCategory category;
    // True after the icon pixel data has been loaded into cache on first paint.
    bool     icon_loaded;
    // Discriminant: Builtin, Nro, Application, or Special.
    IconKind kind;
    // Application title ID (only valid when kind == Application).
    u64      app_id;
    // Switch system applet selector (only valid when kind == Special).
    // Stores static_cast<u16>(EntryType) for one of the SpecialEntry* values
    // (Settings, Album, Themes, Controllers, MiiEdit, WebBrowser, UserPage,
    // Amiibo).  Used by LaunchIcon to dispatch to the correct ShowXxx().
    u16      special_subtype;
};

// ── QdDesktopIconsElement ─────────────────────────────────────────────────

// Pu Element that renders the full auto-grid of desktop icons.
// Covers the full screen (1920×1080) but only draws in the icon grid area.
// Input: A-button launch via smi::LaunchHomebrewLibraryApplet.
// Touch: tap on icon cell → launch same as A.
class QdDesktopIconsElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdDesktopIconsElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdDesktopIconsElement>(theme);
    }

    explicit QdDesktopIconsElement(const QdTheme &theme);
    ~QdDesktopIconsElement();

    s32 GetX() override { return 0; }
    s32 GetY() override { return 0; }
    s32 GetWidth() override  { return 1920; }
    s32 GetHeight() override { return 1080; }

    // Paint all visible icon cells. Loads icon pixel data lazily on first frame.
    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    // Handle A-button to launch focused icon; touch tap to launch touched icon.
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // Number of icons currently in the grid (built-ins + NRO files + applications).
    size_t IconCount() const { return icon_count_; }

    // Replace the Application section of the icon grid with the provided entries.
    // Idempotent: calling this a second time truncates and re-appends from the same
    // slot (app_entry_start_idx_), so stale entries from a previous scan are removed.
    // Only entries with type == EntryType::Application and CanBeLaunched() == true
    // are added.  The total grid is still capped at MAX_ICONS.
    // Must be called from the main thread (same thread as OnRender).
    void SetApplicationEntries(const std::vector<ul::menu::Entry> &entries);

    // Append Switch system-applet shortcut icons (Settings, Album, Themes,
    // Controllers, MiiEdit, WebBrowser, UserPage, Amiibo) to the grid.
    // Designed to run AFTER SetApplicationEntries so apps come first; this
    // method does NOT truncate.  Each Special entry is stored with
    // kind=IconKind::Special and special_subtype = static_cast<u16>(EntryType).
    // LaunchIcon dispatches to the matching ShowXxx() helper from ui_Common.
    // Must be called from the main thread (same thread as OnRender).
    void SetSpecialEntries(const std::vector<ul::menu::Entry> &entries);

    // Return the icon cell rectangle for index i.
    // Returns false if i is out of bounds or row exceeds ICON_GRID_MAX_ROWS.
    bool CellRect(size_t i, s32 &out_x, s32 &out_y) const;

    // Advance the icon cache tick counter and dock magnify state (call once per frame).
    void AdvanceTick();

    // Update dock magnify state from cursor position.
    // cursor_y: vertical cursor position in screen pixels.
    // Call from AdvanceTick / OnMenuUpdate once per frame.
    void UpdateDockMagnify(int32_t cursor_y);

private:
    QdTheme theme_;
    std::array<NroEntry, MAX_ICONS> icons_;
    size_t icon_count_;
    size_t focused_idx_;        // D-pad focused icon (keyboard nav, not SP1)
    QdIconCache cache_;

    // SP2-F13: sentinel pattern — -1 means "no previous magnify center".
    int32_t prev_magnify_center_;   // previous frame's magnify center slot (-1 = none)
    int32_t magnify_center_;        // current frame's magnify center slot (-1 = none)
    int32_t frame_tick_;            // monotonic tick counter (incremented in AdvanceTick)

    // SP3: index of the first Application entry within icons_.
    // Set by the constructor (= icon_count_ after ScanNros()) and held fixed.
    // SetApplicationEntries() truncates icon_count_ back to this value, then appends.
    size_t app_entry_start_idx_;

    // Scan sdmc:/switch/ for *.nro files and append entries to icons_.
    // Skips hidden files (starting with '.').
    // Called once from constructor.
    void ScanNros();

    // Pre-populate Q OS built-in dock icons before NRO scan.
    // Fills the first BUILTIN_ICON_COUNT slots of icons_.
    void PopulateBuiltins();

    // Paint one icon cell at grid position (x, y) on the SDL renderer.
    // Uses QdIconCache for real JPEG data; falls back to category colour.
    void PaintIconCell(SDL_Renderer *r,
                       const NroEntry &entry,
                       s32 x, s32 y,
                       bool is_focused);

    // Compute DJB2 hash colour for a name byte sequence (u32 DJB2 → HSL).
    // Matches desktop_icons.rs::hash_to_color / hsl_to_rgb exactly.
    static void HashToColor(const char *name, u8 &out_r, u8 &out_g, u8 &out_b);

    // Load an on-disk JPEG at jpeg_path and insert the result into the icon cache
    // keyed by cache_key.  Falls back to a hash-derived solid-colour block if the
    // file is absent, unreadable, or not a valid JPEG.
    // Returns true if a real JPEG was decoded; false if the fallback was used.
    bool LoadJpegIconToCache(const char *jpeg_path, const char *cache_key);

    // Launch the icon at index i via smi::LaunchHomebrewLibraryApplet.
    // No-op for built-in icons (SP1 scope).
    void LaunchIcon(size_t i);

    // Hit-test: returns index of icon whose cell contains (tx, ty).
    // Returns MAX_ICONS if no cell matches.
    size_t HitTest(s32 tx, s32 ty) const;
};

} // namespace ul::menu::qdesktop
