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
#include <ul/menu/qdesktop/qd_Cursor.hpp>
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

// ── Launchpad display category ─────────────────────────────────────────────
// Groups entries for Launchpad section headers.  Distinct from NroCategory
// (the 7-value glyph/colour classifier); this 5-value enum drives grouping.
// K+1 Phase 1: used by LpSortKind mapping in qd_Launchpad.cpp.
// K+1 Phase 2+ (deferred): Folders, Payloads scanner.
enum class IconCategory : u8 {
    Nintendo  = 0,  // Installed application whose title-id high byte falls in
                    // 0x01 (Nintendo first-party range, e.g. 0x0100XXXXXXXXXXXX).
                    // Result is cached at sdmc:/ulaunch/cache/nintendo-classify.bin.
    Homebrew  = 1,  // Any IconKind::Nro from sdmc:/switch/.
    Extras    = 2,  // Third-party installed applications, IconKind::Special (Album etc.),
                    // and any application whose title-id does not match the Nintendo range.
    Payloads  = 3,  // Reserved for future Hekate-payload integration. No entries match
                    // this value in Phase 1; the enum value exists so the mapping is complete.
    Builtin   = 4,  // Q OS dock built-ins (Vault, Monitor, Control, About, AllPrograms).
                    // Always rendered last in the Launchpad; never in the desktop grid alone.
};

// ── Icon grid constants (×1.5 from Rust 1280×720) ─────────────────────────
// Cycle J-tweak2: 5 rows to absorb installed homebrew + Specials overflow.
// User reported icons going off-screen at 4 rows × 9 cols = 36 slots when SD
// has 40-50+ entries. Now: slightly shorter cells (168 vs 200) + slightly
// shorter bg (130 vs 140 — still mostly square at 140w × 130h) → fits 5 rows
// without colliding the dock band (which also shrinks to 148 → grid-bottom
// gap = 20 px).
// Math horiz: LEFT(74) + 9*CELL_W(172) + 8*GAP_X(28) = 1846 px ≤ 1920 ✓
// Math vert:  TOP(72)  + 5*CELL_H(168) = 912 px, dock at 932 (DOCK_H=148) ✓
// Total grid slots: 9 × 5 = 45 (was 36). Dock has 4 builtins → 49 visible.
static constexpr s32 ICON_CELL_W     = 172;
static constexpr s32 ICON_CELL_H     = 168;
static constexpr s32 ICON_BG_INSET   = 16;
static constexpr s32 ICON_BG_W       = 140;
static constexpr s32 ICON_BG_H       = 130;
static constexpr s32 ICON_GRID_GAP_X = 28;
// Rust: ICON_GRID_TOP=48 → C++: 72
static constexpr s32 ICON_GRID_TOP   = 72;
// Centered grid: (1920 - 9*172 - 8*28) / 2 = (1920 - 1772) / 2 = 74
static constexpr s32 ICON_GRID_LEFT  = 74;
// Same column count — 11 fits within 1920 px
static constexpr s32 ICON_GRID_COLS  = 9;
// Cycle J-tweak2: bumped 4 → 5 rows to absorb installed homebrew + Specials
// without clipping past the dock. With CELL_H=168 and TOP=72,
// 5 rows = 912 px; dock starts at 932 (DOCK_H=148); 20 px gap.
// 9 cols × 5 rows = 45 grid slots, vs 36 before — kills off-screen overflow.
static constexpr s32 ICON_GRID_MAX_ROWS = 5;
// Same icon cap as Rust MAX_ICONS
static constexpr size_t MAX_ICONS    = 48;

// Number of Q OS built-in dock icons pre-populated at construction.
// Cycle K-noextras: dropped 6 → 4 across two passes — Terminal removed first,
// then VaultSplit (a no-op duplicate of Vault) dropped.
// Cycle K-TrackD: bumped 4 → 5 — AllPrograms (QdLaunchpad) added as slot 4.
// Neutral dock hit-test math uses this constant directly so total_expanded_w
// (5*140 + 4*28 = 812) and expanded_start_x ((1920-812)/2 = 554) recompute
// automatically. Keep this constant authoritative — single source for both the
// visual centering and the HitTest cache size.
static constexpr size_t BUILTIN_ICON_COUNT = 5;

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
    // NRO category badge (glyph/colour classifier, 7 values).
    NroCategory category;
    // Launchpad display category (grouping classifier, 5 values, K+1 Phase 1).
    IconCategory icon_category;
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

// ── Fix D (v1.6.12): Auto-folder classification kind ──────────────────────
// Finer-grained per-entry classification used to drive folder assignment in
// the K+1 Phase 2 folder system.  Stored in a static side table keyed by a
// stable ID string so NroEntry and LpItem are NOT extended (struct extension
// corrupts the libnx IPC command table).
//
// Stable ID convention per entry kind:
//   NRO       -> nro_path string  (e.g. "sdmc:/switch/sys-clk.nro")
//   Application -> "app:<hex16>"  (e.g. "app:01007ef000118000")
//   Payload   -> "payload:<fname>" (e.g. "payload:Atmosphere.bin")
//   Builtin   -> "builtin:<name>" (e.g. "builtin:Vault")
enum class ClassifyKind : u8 {
    Unknown        = 0,
    NintendoGame   = 1,
    ThirdPartyGame = 2,
    HomebrewTool   = 3,
    Emulator       = 4,
    SystemUtil     = 5,
    Payload        = 6,
    Builtin        = 7,
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

    // Wire a cursor element so OnInput can read the cursor position for
    // A-button-as-click.  Called from MainMenuLayout constructor after both
    // elements are created.  Ownership remains with the layout.
    void SetCursorRef(QdCursorElement::Ref cursor_ref) {
        cursor_ref_ = cursor_ref;
    }

    // K+1 Phase 1: Delete the Nintendo-classify cache file and clear the
    // in-process map.  Called when MenuMessage::ApplicationRecordsChanged is
    // received so the next IsNintendoPublisher call recomputes results against
    // the updated catalog.
    static void InvalidateNintendoClassifyCache();

    // Fix D (v1.6.12): Look up the auto-folder ClassifyKind for an entry by its
    // stable ID string.  Returns ClassifyKind::Unknown if the ID is not in the
    // side table (e.g. entry was added outside the three scan functions).
    // Stable ID format: see ClassifyKind comment above.
    static ClassifyKind GetAutoFolderKind(const std::string &stable_id);

private:
    QdTheme theme_;
    std::array<NroEntry, MAX_ICONS> icons_;
    size_t icon_count_;
    size_t dpad_focus_index_;   // D-pad focused icon (keyboard nav); mutated only by D-pad/stick
    size_t mouse_hover_index_;  // Cursor-hover icon; mutated only by cursor hit-test (ZR path)
    QdIconCache cache_;

    // SP2-F13: sentinel pattern — -1 means "no previous magnify center".
    int32_t prev_magnify_center_;   // previous frame's magnify center slot (-1 = none)
    int32_t magnify_center_;        // current frame's magnify center slot (-1 = none)
    int32_t frame_tick_;            // monotonic tick counter (incremented in AdvanceTick)

    // SP3: index of the first Application entry within icons_.
    // Set by the constructor (= icon_count_ after ScanNros()) and held fixed.
    // SetApplicationEntries() truncates icon_count_ back to this value, then appends.
    size_t app_entry_start_idx_;

    // ── Touch click-vs-drag state machine ────────────────────────────────────
    // Prevents drag-across-icons from triggering unintended launches.
    // A launch fires only on TouchUp when the finger has not moved more than
    // CLICK_TOLERANCE_PX from the TouchDown position and the hit-test still
    // resolves to the same icon as at TouchDown.
    bool   pressed_;                  // true while a finger is actively down
    s32    down_x_;                   // layout X at TouchDown
    s32    down_y_;                   // layout Y at TouchDown
    s32    last_touch_x_;             // layout X of most-recent TouchMove/Down
    s32    last_touch_y_;             // layout Y of most-recent TouchMove/Down
    size_t down_idx_;                 // HitTest result at TouchDown (MAX_ICONS = no hit)
    bool   was_touch_active_last_frame_; // previous frame's touch-active flag

    // Optional cursor reference for A-button-as-click (injected by layout).
    QdCursorElement::Ref cursor_ref_;

    // Dock-slot hit-test rects, refreshed every OnRender so HitTest matches the
    // dock's current magnify state.  Visual at lines 882-888 of the .cpp uses
    // builtin_slot_x[i] + kDockNominalTop; we mirror those values here so a tap
    // at the visual rect actually hits the icon.
    // Updated by OnRender (non-const); read by HitTest (const).  No mutable needed
    // because both methods operate on the same non-const object in the render path,
    // and HitTest is called from OnInput which runs after OnRender in the frame loop.
    s32 dock_slot_x_[BUILTIN_ICON_COUNT];
    s32 dock_slot_w_[BUILTIN_ICON_COUNT];

    // ── Cached text textures (rendered once, reused every frame) ─────────────
    // Plutonium's RenderText is expensive (TTF rasterisation + GPU upload).
    // Re-running it for every icon every frame costs ~5760 texture creates/sec
    // at 48 icons × 2 texts × 60fps.  We render once on first paint of each
    // slot and cache the SDL_Texture*; freed in the destructor.  Slots are
    // reset to nullptr when SetApplicationEntries() truncates icon_count_,
    // forcing re-rasterisation for new entries on the next paint.
    std::array<SDL_Texture *, MAX_ICONS> name_text_tex_;
    std::array<SDL_Texture *, MAX_ICONS> glyph_text_tex_;

    // ── Cached text-texture dimensions (immutable after lazy-create) ──────────
    // Eliminates one SDL_QueryTexture driver round-trip per texture per frame.
    // Written once alongside *_tex_ creation; never cleared independently.
    std::array<int, MAX_ICONS> name_text_w_;
    std::array<int, MAX_ICONS> name_text_h_;
    std::array<int, MAX_ICONS> glyph_text_w_;
    std::array<int, MAX_ICONS> glyph_text_h_;

    // ── Cached icon BGRA textures (created once per slot, reused every frame) ─
    // The BGRA pixel data from QdIconCache is uploaded to a streaming
    // SDL_Texture once and reused.  Previously the code created and destroyed
    // a texture every frame per icon (1 200 GPU allocs/sec at 20 icons × 60 fps),
    // fragmenting the Switch's 8 MB GPU pool and causing progressive lag.
    // Freed alongside name_text_tex_/glyph_text_tex_ in FreeCachedText() so the
    // same icon-reload reset path invalidates all three per-slot textures atomically.
    std::array<SDL_Texture *, MAX_ICONS> icon_tex_;

    // Cycle I (boot speed): cached white rounded-rect mask texture rendered ONCE
    // per Element. PaintIconCell uses SDL_SetTextureColorMod + SDL_RenderCopy
    // to tint and blit this in 2 calls instead of the 144 SDL_RenderFillRect
    // calls FillRoundRect would otherwise issue per icon per frame
    // (17 icons × 144 fills × 60 Hz = ~147 K fills/sec ≈ 440 ms/sec on Tegra X1).
    // Built lazily on first PaintIconCell call so the SDL renderer is ready.
    // Freed in destructor.
    SDL_Texture *round_bg_tex_ = nullptr;

    // Scan sdmc:/switch/ for *.nro files and append entries to icons_.
    // Skips hidden files (starting with '.').
    // Called once from constructor.
    void ScanNros();

    // Fix C (v1.6.12): scan sdmc:/bootloader/payloads/ for *.bin files and
    // append entries to icons_ with category=Payloads.  icon_path is resolved
    // via ResolvePayloadIcon() so creator-supplied art is used when available.
    // Called once from Initialize(), after ScanNros().
    void ScanPayloads();

    // Pre-populate Q OS built-in dock icons before NRO scan.
    // Fills the first BUILTIN_ICON_COUNT slots of icons_.
    void PopulateBuiltins();

    // Paint one icon cell at grid position (x, y) on the SDL renderer.
    // Uses QdIconCache for real JPEG data; falls back to category colour.
    // entry_idx indexes name_text_tex_/glyph_text_tex_ for the cached text
    // textures rendered lazily by this method.
    // is_dpad_focused: D-pad ring (full-opacity white ring, hard focus).
    // is_mouse_hovered: cursor-hover indicator (half-opacity ring, softer).
    void PaintIconCell(SDL_Renderer *r,
                       const NroEntry &entry,
                       size_t entry_idx,
                       s32 x, s32 y,
                       bool is_dpad_focused,
                       bool is_mouse_hovered);

    // Free a cached name/glyph text texture pair (no-op if both null).
    // Called by the destructor and when an Application slot is reused after
    // SetApplicationEntries truncation.
    void FreeCachedText(size_t entry_idx);

    // Compute DJB2 hash colour for a name byte sequence (u32 DJB2 → HSL).
    // Matches desktop_icons.rs::hash_to_color / hsl_to_rgb exactly.
    static void HashToColor(const char *name, u8 &out_r, u8 &out_g, u8 &out_b);

    // Load an on-disk JPEG at jpeg_path and insert the result into the icon cache
    // keyed by cache_key.  Falls back to a hash-derived solid-colour block if the
    // file is absent, unreadable, or not a valid JPEG.
    // Returns true if a real JPEG was decoded; false if the fallback was used.
    bool LoadJpegIconToCache(const char *jpeg_path, const char *cache_key);

    // Fetch an application icon from NS storage (NsApplicationControlData::icon),
    // decode it via SDL2_image, and insert the result into the icon cache keyed by
    // cache_key.  Falls back to a hash-derived solid-colour block on any NS or SDL
    // failure so the caller always has a cache entry after this call returns.
    // Returns true if a real JPEG was decoded; false if the fallback was used.
    bool LoadNsIconToCache(u64 app_id, const char *cache_key);

    // Extract the JPEG icon from the ASET section of the NRO at nro_path, decode it
    // via ExtractNroIcon, and insert the result into the icon cache keyed by
    // cache_key.  Falls back to a MakeFallbackIcon solid-colour block on any parse or
    // decode failure so the caller always has a usable cache entry after this call.
    // nro_path must be a non-null, non-empty null-terminated path string.
    // Returns true if a real JPEG was decoded; false if the fallback was used.
    bool LoadNroIconToCache(const char *nro_path, const char *cache_key);

    // Launch the icon at index i via smi::LaunchHomebrewLibraryApplet.
    // No-op for built-in icons (SP1 scope).
    void LaunchIcon(size_t i);

    // Hit-test: returns index of icon whose cell contains (tx, ty).
    // Returns MAX_ICONS if no cell matches.
    size_t HitTest(s32 tx, s32 ty) const;

    // K+1 Phase 1: Classify one NroEntry into its Launchpad display category.
    // Called from PopulateBuiltins, ScanNros, SetApplicationEntries, SetSpecialEntries.
    static IconCategory ClassifyEntry(const NroEntry &e);

    // K+1 Phase 1: Return true if app_id belongs to a Nintendo first-party title.
    // Heuristic: the high byte of the title-id (bits 56..63) is 0x01, which covers
    // the 0x0100xxxxxxxxxxxx range used by Nintendo's own published titles.
    // Result is cached in sdmc:/ulaunch/cache/nintendo-classify.bin as a flat array
    // of 12-byte records { u64 app_id, u8 result, u8[3] pad }.
    // Cache is read at first call; written back when new entries are classified.
    // Invalidated (file deleted) by InvalidateNintendoClassifyCache().
    static bool IsNintendoPublisher(u64 app_id);

    // Cycle K-TrackD: QdLaunchpadElement reads icons_[], icon_count_, and
    // cache_ directly when building its snapshot in Open().  Grant friend
    // access here so no public accessors (which would widen the API surface
    // for all callers) are needed.
    friend class QdLaunchpadElement;
};

} // namespace ul::menu::qdesktop
