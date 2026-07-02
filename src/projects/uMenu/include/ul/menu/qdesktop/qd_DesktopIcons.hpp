// qd_DesktopIcons.hpp — Auto-grid desktop icon element for uMenu C++ SP3 (v1.3.0).
// Ported from tools/mock-nro-desktop-gui/src/desktop_icons.rs + wm.rs.
// Scans sdmc:/switch/*.nro once at construction; paints icon cells every frame.
// SP2 additions: dock magnify animation, prev_magnify_center_ sentinel.
// SP3 additions: Application entries via SetApplicationEntries(); JPEG icon loading.
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_HelpOverlay.hpp>
#include <ul/menu/qdesktop/qd_Tooltip.hpp>
#include <ul/menu/qdesktop/qd_TaskManager.hpp>       // v1.9: full-screen process manager modal
#include <ul/menu/qdesktop/qd_HotCornerDropdown.hpp>       // v1.9: hot-corner popout dropdown
#include <ul/menu/qdesktop/qd_HotCornerRightDropdown.hpp> // v1.10.3.11: top-right hot-corner dropdown
#include <ul/menu/qdesktop/qd_WindowManager.hpp>     // v1.10: floating window manager
#include <ul/menu/qdesktop/qd_ContextMenu.hpp>       // v1.10.3: ZL context menus
#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_IconCategory.hpp>
#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <ul/menu/qdesktop/qd_Cursor.hpp>
// v1.9.2: devtools windows (qd_NxlinkWindow / qd_UsbSerialWindow / qd_LogFlushWindow)
// removed.  Their files were deleted from the qdesktop source tree and the ZL
// hot-zone toggle that opened the dev popup is gone.  Task-manager (long-press
// dock tile 0) is the supported diagnostic surface going forward.
#include <ul/menu/menu_Entries.hpp>
#include <string>
#include <array>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace ul::menu::qdesktop {

// v1.7.0-stabilize-7 Slice 4 (O-B): forward declaration so the
// ConsumePendingLaunchpadFolder() static accessor in QdDesktopIconsElement
// can return an AutoFolderIdx without including qd_AutoFolders.hpp here
// (which would create a circular include — qd_AutoFolders.hpp itself
// #includes this file for ClassifyKind).
enum class AutoFolderIdx : u8;

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
// v1.8.10: removed Payloads (was value 3, unused dead enum value); ScanPayloads
// now assigns IconCategory::Extras directly.  Builtin renumbered 4→3.
enum class IconCategory : u8 {
    Nintendo  = 0,  // Installed application whose title-id high byte falls in
                    // 0x01 (Nintendo first-party range, e.g. 0x0100XXXXXXXXXXXX).
                    // Result is cached at sdmc:/ulaunch/cache/nintendo-classify.bin.
    Homebrew  = 1,  // Any IconKind::Nro from sdmc:/switch/.
    Extras    = 2,  // Third-party installed applications, IconKind::Special (Album etc.),
                    // payload stubs, and any application whose title-id does not match
                    // the Nintendo range.
    Builtin   = 3,  // Q OS dock built-ins (Vault, Monitor, Control, About, AllPrograms).
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
// v2.9.0 Checkpoint A — iOS/Switch/Material consensus geometry.  Background-
// agent audit 2026-05-18 (HOS visual reference + pixel specs) confirmed the
// previous 140×130 (1.08:1 near-rectangle) with radius 12 (8.6% of width) read
// as "awkward" against iOS (~22% radius squircle) and Switch native (~10%
// radius on 256² square art).  At Q OS's 1920×1080 docked target, the right
// shape is a SQUARE tile at 192² with 22% corner radius (~42 px), 32 px Material
// 8dp×4 column gap, 8 cols × 4 rows = 32 visible per page.  Apps overflow
// gracefully via the v2.8.5 MAX_ICONS=512 capacity into additional Launchpad
// pages.  Grid math:
//   horiz: LEFT(80) + 8 × 224 + 7 × 32 = 80 + 1792 + (gap budget absorbed into
//          CELL_W) = 1920 ✓  (CELL_W=224 = BG_W=192 + 32 gap budget)
//   vert:  TOP(80) + 4 × CELL_H(236) = 1024 px; dock at ~1024 ✓
//          (CELL_H=236 = BG_H=192 + 32 row gap + 12 title gap)
static constexpr s32 ICON_CELL_W     = 224;
static constexpr s32 ICON_CELL_H     = 236;
static constexpr s32 ICON_BG_INSET   = 16;
static constexpr s32 ICON_BG_W       = 192;
static constexpr s32 ICON_BG_H       = 192;
// v2.9.3 — separate dock icon sizing.  PaintIconCell uses ICON_BG_W/H above
// for everything by default, but with v2.9.0's bump to 192² that overflows
// the DOCK_H=148 band at the bottom of the screen (938 + 192 = 1130 > 1080).
// Dock builtins (Vault/Monitor/About/AllPrograms/Tasks/Nintendo) now use
// these dock-specific dimensions instead.  Square 128 squircles match the
// macOS/iOS dock pattern of having smaller icons in the persistent dock
// vs the home grid.  Bottom math: 938 + 128 = 1066, with 14 px buffer to
// screen bottom — safe.  Magnification scales WIDTH only (qd_DesktopIcons.cpp
// :3458) so height stays 128 regardless of hover.
static constexpr s32 DOCK_ICON_BG_W  = 128;
static constexpr s32 DOCK_ICON_BG_H  = 128;
static constexpr s32 ICON_GRID_GAP_X = 32;
// 80 px top breathing room — was 72.  +8 because the larger tiles need more
// room around the topbar.
static constexpr s32 ICON_GRID_TOP   = 80;
// Centered grid: (1920 - 8*224) / 2 = (1920 - 1792) / 2 = 64; round to 80 to
// match the 8dp Material rhythm on the left edge too.
static constexpr s32 ICON_GRID_LEFT  = 80;
// 8 cols matches iOS/HOS aesthetic at this tile size.  Was 9 cols at 172 px
// (too cramped + too rectangular).
static constexpr s32 ICON_GRID_COLS  = 8;
// 4 rows × 236 px = 944 px; dock at ~1024 ✓.  Was 5 rows at 168 px.
static constexpr s32 ICON_GRID_MAX_ROWS = 4;
// v2.8.5 — was 48.  Background-agent audit 2026-05-18 confirmed the
// missing-games bug: static population (6 builtins + 34 NROs + 8 payloads
// = 48) exhausted the entire pool BEFORE SetApplicationEntries got a turn,
// silently dropping all 27 installed games (logged as "in=27 added=0").
// Bumped to 512 to remove the ceiling for any realistic library size.
// Memory cost: ~300 KB across ~8 std::array members (NroEntry ~500 B/slot
// × 512 = 256 KB main + texture handles + state flags + label dimensions).
// Trivial on 3.5 GB Switch RAM.  This is a constexpr so it's pure compile-
// time array sizing — no heap allocation, no runtime resize, no migration
// from std::array to std::vector required.  Comment at qd_Launchpad.hpp:106
// noting "touching it would resize 5+ desktop-side arrays" is now stale;
// std::array<T, MAX_ICONS> handles that automatically.
static constexpr size_t MAX_ICONS    = 512;

// Number of Q OS built-in dock icons pre-populated at construction.
// Cycle K-noextras: dropped 6 → 4 across two passes — Terminal removed first,
// then VaultSplit (a no-op duplicate of Vault) dropped.
// Cycle K-TrackD: bumped 4 → 5 — AllPrograms (QdLaunchpad) added as slot 4.
// v1.10.3.1: dropped 5 → 4 — Control (dev-menu) removed from production build.
//   Remaining dock: Vault=0, Monitor=1, About=2, AllPrograms=3.
// Neutral dock hit-test math uses this constant directly so total_expanded_w
// (4*140 + 3*28 = 644) and expanded_start_x ((1920-644)/2 = 638) recompute
// automatically. Keep this constant authoritative — single source for both the
// visual centering and the HitTest cache size.
// v2.0.4: bumped 4 → 5 to add the Task Manager dock icon.  Existing slots
// 0-3 (Vault/Monitor/About/AllPrograms) keep their identities; new slot 4 =
// Tasks, dispatching to QdDesktopIconsElement::OpenTaskManagerWindow.
// v2.3.4: bumped 5 → 6 to add the Nintendo Apps dock icon.  New slot 5 =
// Nintendo, dispatching to QdDesktopIconsElement::OpenNintendoAppsWindow.
//
// v2.9.12: dropped 6 → 5.  About removed from the dock per creator
// directive 2026-05-18 evening: "We should also remove it from the dock
// but save the icon for later use possibly.  Its already in the top left
// hot corner."  DockAbout.png stays in romfs and every .ultheme bundle
// for future re-introduction.  Top-left hot-corner dropdown
// (qd_HotCornerDropdown.cpp, case 4) remains the user-visible About
// entry point.
// New slot order: Vault=0, Monitor=1, AllPrograms=2, Tasks=3, Nintendo=4.
//
// W12-SAVE-DISCO: bumped 5 → 6 to add the Save Editor dock icon.
// New slot 5 = Saves, dispatching to OpenSaveEditorWindow().
// Neutral total: 6*128 + 5*32 = 928 px < 1920 — fits without overflow.
// Slot order: Vault=0, Monitor=1, AllPrograms=2, Tasks=3, Nintendo=4, Saves=5.
//
// W12-CHEATS-WB wave 3: bumped 6 → 7 to add the Cheats dock icon.
// New slot 6 = Cheats, dispatching to OpenCheatsWindow(0) (global TitleList).
// Neutral total: 7*128 + 6*32 = 896 + 192 = 1088 px < 1920 — fits without overflow.
// Magnified max (1.4×): 7*179 + 6*32 = 1253 + 192 = 1445 px < 1920 — also fine.
// Slot order: Vault=0, Monitor=1, AllPrograms=2, Tasks=3, Nintendo=4, Saves=5, Cheats=6.
static constexpr size_t BUILTIN_ICON_COUNT = 7;

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

// ── v1.7.0-stabilize-2: NroEntry struct-size pin (A7 + A8) ────────────────
// The 2026-04-13 v1.6.10 hard-crash on hardware traced to a struct shift in
// either NroEntry or LpItem -- the libnx IPC command table is allocated by
// pluton:gen with a hard-coded sizeof(NroEntry); growing the struct silently
// corrupts the table at runtime (hbsrv->launch fails, applet sequence aborts,
// uSystem panics with LibnxError_AppletCmdidNotFound). To prevent any
// future regression of that exact failure mode, this assert pins the size
// at the v1.6.x reconstruction-snapshot value (HEAD 562e554, 2026-04-26).
//
// If this assert fires:
//   1. Do NOT change the constant -- the goal is to detect changes, not absorb them.
//   2. Audit the diff for new fields. Move them to a side table keyed by
//      stable_id (see ClassifyKind comment above for the convention).
//   3. Only update the pinned value as part of a deliberately co-ordinated
//      libnx-ext IPC table bump, never as a side effect of a feature commit.
//
// Computed value: 1632 bytes on aarch64 ARM64 with GCC's standard layout,
// derived from a host-side probe (/tmp/sizeof_probe) that mirrors the
// struct verbatim. The host probe and devkitA64 produce the same layout
// because every field is a primitive type or a fixed-size char array; both
// targets use 8-byte alignment for u64.
static_assert(sizeof(NroEntry) == 1632,
              "NroEntry size shifted -- v1.6.10 IPC crash risk; "
              "use a side table for new state, do not extend the struct");
static_assert(alignof(NroEntry) == 8,
              "NroEntry alignment shifted -- IPC layout assumption violated");

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

// ── Input-source latch ────────────────────────────────────────────────────
//
// Tracks whether the most recent meaningful input came from the D-pad or from
// the mouse / touch surface.  Used to implement mutual exclusion between the
// D-pad focus highlight and the cursor hover ring so only one selection
// indicator is ever visible.
//
// Transition rules (see QdDesktopIconsElement::OnInput for implementation):
//   DPAD  <- D-pad Up/Down/Left/Right pressed.
//   MOUSE <- mouse cursor moved >4 px OR touch tap/down detected.
//   A / B / X / Y / L / R / ZL / ZR do NOT change the source.
//
// Render semantics (OR semantics with Worker-Plutonium-Hekate auto-hide):
//   DPAD  -> cursor_ref_->SetVisible(false); D-pad focus highlight rendered.
//   MOUSE -> cursor_ref_->SetVisible(true);  D-pad focus highlight suppressed.
//
// Default is DPAD because uMenu boots with the Switch controllers active and
// the user must make an explicit mouse/touch gesture to switch mode.
enum class InputSource : u8 {
    DPAD  = 0,
    MOUSE = 1,
};

// ── CellRenderState (v1.8.19) ─────────────────────────────────────────────
// Per-slot state enum that encodes exactly which render branch PaintIconCell
// should take for slot [i].  Written on cache transitions; read as a switch
// dispatch in PaintIconCell so the per-frame conditional chain is O(1) with
// zero extra cache hits.
//
// Values:
//   Unknown       — initial state; PaintIconCell must determine the branch
//                   and update the slot on first paint.
//   BgraReady     — BGRA pixel data is in QdIconCache and an SDL_Texture has
//                   been built from it.  PaintIconCell blits icon_tex_[idx].
//   SpecialPng    — icon_tex_[idx] was loaded from a Special PNG asset (via
//                   TryFindLoadImage).  No cache lookup needed.
//   DefaultFallback — icon_tex_[idx] holds the DefaultHomebrew/Application
//                   PNG.  texture_replaceable_[idx] is true; next frame where
//                   BGRA arrives flips to BgraReady.
//   GlyphOnly     — no texture available; PaintIconCell renders the glyph
//                   character + background colour block only.
enum class CellRenderState : u8 {
    Unknown         = 0,
    BgraReady       = 1,
    SpecialPng      = 2,
    DefaultFallback = 3,
    GlyphOnly       = 4,
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

    // v1.8.22a RetroArch crash fix: stop the background prewarm thread and
    // join it.  Called from MenuApplication::Finalize() before
    // smi::TerminateMenu() terminates the process via IPC.  TerminateMenu does
    // NOT unwind the C++ stack, so the element's destructor never fires; without
    // this hook, the prewarm thread keeps running into uLoader_apl's process
    // startup and triggers a Data Abort (0x4A8) on RetroArch launch.
    //
    // Idempotent: if the thread is already stopped or was never started, the
    // joinable() guard makes this a no-op.
    inline void StopPrewarmThread() {
        prewarm_stop_.store(true, std::memory_order_release);
        if (prewarm_thread_.joinable()) {
            prewarm_thread_.join();
        }
    }

    // K+1 Phase 1: Delete the Nintendo-classify cache file and clear the
    // in-process map.  Called when MenuMessage::ApplicationRecordsChanged is
    // received so the next IsNintendoPublisher call recomputes results against
    // the updated catalog.
    static void InvalidateNintendoClassifyCache();

    // Bug #2/#3 fix (v1.8): Reset the favorites in-process cache so the next
    // EnsureFavoritesLoaded() call reloads from disk.  Called from
    // SetApplicationEntries() to ensure favorites resolve correct icon indices
    // after a game-resume reload (uMenu does not restart on resume; statics persist).
    static void InvalidateFavoritesCache();

    // Fix D (v1.6.12): Look up the auto-folder ClassifyKind for an entry by its
    // stable ID string.  Returns ClassifyKind::Unknown if the ID is not in the
    // side table (e.g. entry was added outside the three scan functions).
    // Stable ID format: see ClassifyKind comment above.
    static ClassifyKind GetAutoFolderKind(const std::string &stable_id);

    // v1.7.0-stabilize-7 Slice 4 (O-B): consume any pending Launchpad folder
    // pre-filter set by a desktop-folder tap.  Returns the AutoFolderIdx that
    // should be applied to active_folder_, then resets the pending state.  If
    // no folder tap is pending, returns AutoFolderIdx::None and is a no-op.
    // Called from QdLaunchpadElement::Open() exactly once per Open cycle.
    static AutoFolderIdx ConsumePendingLaunchpadFolder();

    // v1.7.0-stabilize-7 Slice 4 (O-B): mark the desktop folder grid layout
    // as needing a recompute on the next paint.  Called by SetApplicationEntries
    // and SetSpecialEntries after the icon set changes so the folder counts
    // refresh.  No-op if the layout is already dirty.
    static void MarkDesktopFolderLayoutDirty();

    // v3.1.1 — Drop cached desktop-folder textures so the next paint
    // reloads them from the active-theme cache.  Called by the "Change
    // Icons" right-click action after CacheActiveTheme has extracted a
    // new .ultheme into sdmc:/ulaunch/cache/active/.
    static void InvalidateDesktopFolderTextures();

    // v1.10: window-manager openers.  Each method creates a QdWindow, sets its
    // content layout to the matching windowed layout, and calls wm_.OpenWindow().
    // Defined in qd_DesktopIcons_WmBridge.cpp to keep this file focused on
    // icon-grid and dock concerns.
    void OpenVaultWindow();
    void OpenSettingsWindow();
    void OpenMonitorWindow();
    void OpenAboutWindow();
    // v1.10.3.7 (Feature E): open a filtered folder view as an independent window.
    // folder_idx must be in [0, kDesktopFolderCount); out-of-range is a no-op.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenFolderWindow(size_t folder_idx);
    // v1.10.3.11: open the Task Manager as an independent floating window.
    // Lists all open and minimized windows with per-row Focus/Minimize/Close actions.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenTaskManagerWindow();
    // v2.0: open the Nintendo built-in apps launcher as an independent floating window.
    // 4×2 tile grid — Album, Controllers, Mii Editor, Profile, Web Browser,
    // System Settings, Keyboard, Error Info.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenNintendoAppsWindow();

    // W11-SAVE: open the Pokémon save editor as an independent floating window (960×600).
    // Singleton-guarded: if the window is already open it is focused; if minimised it
    // is restored.  Wired from qd_VaultLayout's "Edit Pokémon save" context-menu entry
    // and from any future dock shortcut.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenSaveEditorWindow();

    // W12-CHEATS: open the Atmosphère cheat browser as an independent floating window
    // (960×600).  Singleton-guarded via FindWindowByTitle("Cheats").
    // app_id == 0 → TitleList mode (browse all installed cheats).
    // app_id != 0 → jump directly into CheatList for that title.
    // Wired from the Vault ctx menu "View Cheats" entry and the desktop icon ctx menu.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenCheatsWindow(u64 app_id = 0);

    // B3.1-MODS: open the LayeredFS mod manager as an independent floating window
    // (960×600).  Singleton-guarded via FindWindowByTitle("Mods").
    // app_id == 0 → TitleList mode (browse all installed mods).
    // app_id != 0 → jump directly into SlotList for that title.
    // Wired from the Vault ctx menu "View Mods" entry and the desktop icon ctx menu.
    // Defined in qd_DesktopIcons_WmBridge.cpp alongside the other openers.
    void OpenModsWindow(u64 app_id = 0);

    // FIX-2 (2026-06-19): /closeallwin debug-server route accessor.
    // Closes every open window and every minimized dock entry without any
    // coordinate tap.  UI-thread only (called from OnRenderFrame).
    // Defined inline here; no WmBridge file needed (it's a one-liner).
    void CloseAllWindows() {
        // Drain open windows back-to-front (CloseWindow mutates the vector; we
        // re-check back() each iteration so no iterator is ever invalidated).
        while (!wm_.GetOpenWindows().empty()) {
            wm_.CloseWindow(wm_.GetOpenWindows().back().get());
        }
        // Drain minimized dock entries.
        while (!wm_.GetMinimizedEntries().empty()) {
            wm_.CloseMinimizedEntry(wm_.GetMinimizedEntries().back().get());
        }
    }

    // Debug-server accessors — UI-thread only (called from OnRenderFrame).
    //
    // SnapshotIconsJson: build a JSON array string of all current icon entries.
    // Each element: {"idx":<n>,"name":"...","nro_path":"...","kind":<k>,"icon_loaded":<b>}
    // Called by qd_DebugServer.cpp to service the /icons route.
    std::string SnapshotIconsJson() const;

    // LaunchIconForDebug: public entry over the private LaunchIcon(i), used by
    // qd_DebugServer.cpp for /launchnro/<idx>. Defined in the .cpp because it
    // self-selects a profile first (autonomous test-rig: app launches need a
    // valid g_SelectedUser or uSystem returns rc=0x3257C InvalidSelectedUser).
    void LaunchIconForDebug(size_t i);

    // W10-BUG1B: render the window layer (windows + context menus + snap
    // preview) at the correct Z-order.  Called from QdWindowOverlay::OnRender,
    // which is added to MainMenuLayout AFTER the top-bar status elements so
    // windows occlude the top-bar instead of rendering below it.
    // NOT called from QdDesktopIconsElement::OnRender — that call was removed
    // to avoid the old Z-order bug where the status bar rendered over windows.
    void RenderWindowLayer(pu::ui::render::Renderer::Ref &drawer);

private:
    // v1.7.0-stabilize-7 Slice 4 (O-B): recompute per-folder counts and rects.
    // Called lazily from PaintDesktopFolders when g_desktop_folder_layout_dirty
    // is set; safe to call from any frame.
    void RecomputeDesktopFolders();

    // v1.7.0-stabilize-7 Slice 4 (O-B): paint the 6-folder desktop grid.
    // Called from OnRender after the dock paint loop, before the hot corner.
    // Honors dev_icons_on (the same flag that previously gated the icon grid).
    void PaintDesktopFolders(SDL_Renderer *r, s32 x, s32 y);

    // v1.7.0-stabilize-7 Slice 5 (O-F): paint the favorites strip.
    // Called from OnRender between the dock loop and the hot-corner block.
    // Renders up to FAV_STRIP_VISIBLE tiles for items in g_favorites_list_
    // that resolve to an active icons_[] index.
    void PaintFavoritesStrip(SDL_Renderer *r, s32 x, s32 y);

    // v1.7.0-stabilize-7 Slice 5 (O-F): hit-test the favorites strip.
    // Returns the icons_[] index whose favorite tile contains (tx, ty), or
    // SIZE_MAX if no tile matches.  Layout-relative; caller adds (x, y).
    size_t HitTestFavorites(s32 tx, s32 ty) const;

    // v1.7.0-stabilize-7 Slice 4 (O-B): unified focus-ring hit-test.
    // Returns:
    //   0..(kDesktopFolderCount-1)                          -> desktop folder fi
    //   kDesktopFolderCount..kDesktopFolderCount+BIC-1       -> dock slot
    //                                                          (subtract kDesktopFolderCount
    //                                                          for the icons_[] index)
    //   SIZE_MAX                                            -> no hit
    // Where BIC = BUILTIN_ICON_COUNT.  This replaces the old icons_[]-indexed
    // HitTest for the desktop's primary input path now that the per-icon
    // grid is gone (Slice 4 Phase 1 strip).
    size_t HitTestDesktop(s32 tx, s32 ty) const;
    QdTheme theme_;
    std::array<NroEntry, MAX_ICONS> icons_;
    size_t icon_count_;
    size_t dpad_focus_index_;       // D-pad focused icon (keyboard nav); mutated only by D-pad/stick
    // Bug #4 fix (v1.8): D-pad focus index within the favorites strip.
    // SIZE_MAX = "not in favorites strip mode".  Set to 0..FAV_STRIP_VISIBLE-1
    // when Up is pressed from dpad_focus_index_==0 (folder row 0); set back to
    // SIZE_MAX when Down is pressed from the strip (returns to folder row 0).
    // Left/Right wrap within the visible strip slots; at edges the strip scrolls.
    size_t fav_strip_focus_index_;
    // v1.8.25: index into g_favorites_list_ of the leftmost visible strip tile.
    // Strip renders favorites [scroll_offset_ .. scroll_offset_ + FAV_STRIP_VISIBLE).
    // Reset to 0 in the constructor and whenever the strip layout is reset.
    size_t fav_strip_scroll_offset_;
    size_t mouse_hover_index_;  // Cursor-hover icon; mutated only by cursor hit-test (ZR path)
    // v1.8.18: cache_ removed — replaced by GetSharedIconCache() singleton so
    // Desktop and Launchpad share one QdIconCache object.

    // SP2-F13: sentinel pattern — -1 means "no previous magnify center".
    int32_t prev_magnify_center_;   // previous frame's magnify center slot (-1 = none)
    int32_t magnify_center_;        // current frame's magnify center slot (-1 = none)
    int32_t frame_tick_;            // monotonic tick counter (incremented in AdvanceTick)

    // SP3: index of the first Application entry within icons_.
    // Set by the constructor (= icon_count_ after ScanNros()) and held fixed.
    // SetApplicationEntries() truncates icon_count_ back to this value, then appends.
    size_t app_entry_start_idx_;

    // v2.2.1: index of the first NRO/Payload entry within icons_.
    // Set in the constructor to icon_count_ after PopulateBuiltins() — i.e. the
    // "after_builtins" boundary that AdvanceTick's rescan branch uses to reset
    // icon_count_ before re-running ScanNros() + ScanPayloads().
    // Read-only after construction; never modified by any scan function.
    size_t builtin_end_idx_;

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

    // QoL-T1 (2026-05-19) — long-press touch state.  Tracks when the
    // current touch began so we can synthesize a ZL press after 500 ms
    // of still-held touch (universal "tap and hold for context menu"
    // gesture).  Reset when the touch lifts or moves > kLongPressJitterPx.
    u64    long_press_start_tick_   = 0;        // armGetSystemTick at TouchDown
    s32    long_press_start_x_      = -1;       // touch position at TouchDown
    s32    long_press_start_y_      = -1;
    bool   long_press_fired_        = false;    // edge-trigger guard

    // v1.8.23: coyote-timing state (tap-vs-hold + double-launch suppression + dpad repeat)
    struct InputCoyoteState {
        u64 down_tick        = 0;          // armGetSystemTick at TouchDown / button-down
        u64 last_launch_tick = 0;          // armGetSystemTick at last successful Launch
        u32 dpad_held_frames[4] = {0,0,0,0};   // up/down/left/right
    };
    InputCoyoteState coyote_;

    // v1.7.0-stabilize-2 (REC-02 corrected): tracks which input modality
    // produced the most recent meaningful event.
    //   true  = last meaningful input was a D-pad direction or A button press;
    //           PaintIconCell SUPPRESSES the cursor-hover ring so only ONE
    //           highlight (the dpad-focus ring) is visible during pad nav.
    //   false = last meaningful input was a cursor motion or a touch event;
    //           the cursor-hover ring is rendered normally so ZR-launches-
    //           cursor-target keeps working.
    // Set true on Up/Down/Left/Right/A press; set false on cursor.x/y change
    // (delta != 0) or any touch arrival. Initial state is false because boot
    // begins in cursor mode (cursor at screen centre).
    //
    // Not the same as `dpad_focus_index_ < icon_count_` (which would default
    // to slot 0 and permanently kill the hover ring) -- this is an explicit
    // input-modality flag that never confuses "no D-pad press yet" with
    // "D-pad active".
    bool   last_input_was_dpad_;

    // v1.8 Input-source latch: formalises last_input_was_dpad_ into a
    // two-value enum for mutual-exclusion D-pad ⇄ mouse/touch mode switching.
    // Drives both cursor visibility (via cursor_ref_->SetVisible()) and whether
    // the D-pad focus highlight is rendered.  Set on every meaningful input
    // frame; A/B/X/Y/L/R/ZL/ZR do NOT change it.
    // last_input_was_dpad_ is derived from this: DPAD→true, MOUSE→false.
    InputSource active_input_source_;

    // v1.7.0-stabilize-2 (REC-02 helper): cached previous-frame cursor X/Y so
    // the OnInput delta check can detect motion without polling cursor_ref_
    // every cell. -1 sentinel = "no previous sample yet" (first frame).
    s32    prev_cursor_x_;
    s32    prev_cursor_y_;

    // Optional cursor reference for A-button-as-click (injected by layout).
    QdCursorElement::Ref cursor_ref_;

    // v1.9.2: dev-window panel members + dev_popup_open_ removed.  The three
    // panels (Nxlink / UsbSerial / LogFlush) and the ZL hot-zone toggle that
    // controlled them have been deleted.  Task manager (qd_TaskManager.hpp)
    // is the v1.9 successor — opened by long-pressing dock tile 0.

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

    // v1.9: per-slot ownership flag for NS-service icon cache textures.
    // When GetSharedNsIconCache().Get() provides the texture for a Special
    // applet slot, the cache owns the SDL_Texture — FreeCachedText() must
    // NOT call SDL_DestroyTexture on cache-owned pointers (double-free).
    // Initialised false; set true only when NS cache populates icon_tex_.
    // Reset to false alongside icon_tex_ reset in FreeCachedText().
    std::array<bool, MAX_ICONS> icon_tex_ns_owned_;

    // v1.7.0-stabilize-2 (REC-03 option B): per-slot "this texture is the
    // DefaultHomebrew/DefaultApplication fallback PNG and should be replaced
    // when real BGRA arrives" flag.
    //
    // Why this exists: the cold-load lazy fallback in PaintIconCell installs
    // a default PNG into icon_tex_[slot] when the cache lookup misses
    // (NACP or NRO ASET hasn't returned yet). Without this flag the slot is
    // permanently stuck on the default icon: the BGRA cache may populate
    // milliseconds later, but icon_tex_[slot] is already non-null so the
    // replacement guard at lines ~1190-1194 of qd_DesktopIcons.cpp keeps
    // the placeholder forever -- the v1.6.x "first frame wins" frame race
    // creator reported.
    //
    // The fix flagged by REC-03 option (a) -- pre-warm extension to NROs at
    // Initialize() -- adds 6-20 s of boot delay (per A9 audit) and busts the
    // 15 s boot budget. Option (b) (this flag) keeps the fast cold-load path
    // and only replaces the texture once when real data arrives.
    //
    // Lifecycle:
    //   - Set to true in PaintIconCell at the moment a Default*.png fallback
    //     is loaded into icon_tex_[slot].
    //   - On the NEXT frame, if `bgra` from cache_.Get(...) is non-null AND
    //     this flag is true, free icon_tex_[slot], rebuild from BGRA, and
    //     clear the flag. The slot now displays the real icon.
    //   - Reset to false when the slot is reused (FreeCachedText path) or
    //     when SetApplicationEntries truncates icon_count_.
    std::array<bool, MAX_ICONS> texture_replaceable_;

    // v1.8.19: per-slot CellRenderState.  Written by PaintIconCell on first paint
    // and on every cache-state transition (BGRA arriving, fallback installed, etc.).
    // Read at the top of PaintIconCell's icon-section as a switch dispatch so the
    // per-frame multi-branch conditional chain becomes O(1) once the state is known.
    // Initialised to CellRenderState::Unknown in the constructor; reset to Unknown
    // by FreeCachedText() when a slot is recycled by SetApplicationEntries.
    std::array<CellRenderState, MAX_ICONS> slot_render_state_;

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

    // F2b (stabilize-6 / O-C): Load the shipped Application icon from disk
    // instead of asking NS at library-applet runtime.  Tries two paths in order:
    //   1. sdmc:/ulaunch/cache/app/<APPID16HEX_UPPER>.jpg
    //      (uSystem-populated by app_ControlCache.cpp; primary path once
    //      fork uSystem is deployed to /atmosphere/contents/0100000000001000/)
    //   2. sdmc:/switch/qos-app-icons/<APPID16HEX_LOWER>.jpg
    //      (creator-curated manual drop; permanent fallback for titles
    //      uSystem could not extract)
    // On success, the JPEG is decoded via LoadJpegIconToCache and the cache
    // is keyed by cache_key.  Returns true on success (cache populated),
    // false if both paths missed or decode failed (caller falls through to
    // LoadNsIconToCache or gray fallback).
    bool LoadAppIconFromUSystemCache(u64 app_id, const char *cache_key);

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

    // v1.8.13 (UnifiedDesktopPrewarm): Walk every entry in icons_[0..icon_count_)
    // and populate the BGRA cache via the same three load functions that the
    // Launchpad's Open() prewarm uses.
    //
    // v1.8.15 (PrewarmThread): promoted to background thread.  Called from
    // OnRender's first-call branch via SpawnPrewarmThread().  Returns immediately;
    // the thread runs PrewarmAllIcons() concurrently with the render loop.
    // Thread safety: all cache_.Get/Put calls are protected by cache_mutex_.
    void PrewarmAllIcons();

    // v1.8.15: Spawn the background prewarm thread.  Called exactly once from
    // OnRender's first-call branch.  Detaches or joins in destructor via
    // prewarm_stop_ flag; see destructor for join strategy.
    void SpawnPrewarmThread();

    // ── v1.8.15 threading members ─────────────────────────────────────────────
    // v1.8.18: cache_mutex_ removed — replaced by GetSharedIconCacheMutex()
    // so Desktop and Launchpad synchronise on the same std::mutex.
    // All cache Get/Put callers continue to use std::lock_guard; they now call
    // GetSharedIconCacheMutex() instead of cache_mutex_.

    // Stop flag: set to true by destructor to signal the background thread to
    // exit after its current loop iteration.  The thread polls this flag between
    // entries.  Declared atomic so the write from the main thread is visible to
    // the background thread without requiring the shared mutex for the poll.
    std::atomic<bool> prewarm_stop_;

    // Background prewarm thread.  Default-constructed (not joinable) until
    // SpawnPrewarmThread() assigns it.  Joined in the destructor after
    // prewarm_stop_ is set, guaranteeing the thread exits before the process
    // shuts down.  Strategy: join (not detach) so the destructor never returns
    // while the thread holds a `this` pointer to freed memory.
    std::thread prewarm_thread_;

    // Cycle K-TrackD: QdLaunchpadElement reads icons_[] and icon_count_
    // directly when building its snapshot in Open().  Grant friend access here
    // so no public accessors (which would widen the API surface for all callers)
    // are needed.
    friend class QdLaunchpadElement;

    // v1.8.25: help overlay (Home + Capture/Share trigger).  pending_open_help_
    // is set by OnInput when the trigger combo is detected; consumed by the
    // next OnRender which has the SDL_Renderer* needed for Open().
    QdHelpOverlay help_overlay_;
    bool          pending_open_help_ = false;

    // v1.8.27: shared hover tooltip — reused for dock icon labels and
    // desktop folder "Name (N)" labels. One instance; only one tooltip
    // visible at a time (dock/folder focus is mutually exclusive).
    QdTooltip tooltip_;

    // v1.9: full-screen task manager modal. Triggered by a 30-frame hold
    // on dock tile 0 (tile0_hold_frames_ counter, reset on release).
    // Non-copyable; owns SDL textures and service session handles.
    // Must be declared after tooltip_ so destructor order is deterministic.
    int            tile0_hold_frames_ = 0;
    QdTaskManager  task_mgr_;

    // v1.9: hot-corner popout dropdown. Renders above tooltips, below help
    // overlay (Z-order per qd_HotCornerDropdown.hpp API contract).
    // Non-copyable; owns SDL textures. Must be declared after task_mgr_ so
    // destructor order is deterministic (dropdown_ destroyed first).
    QdHotCornerDropdown dropdown_;

    // v1.10.3.11: top-right hot-corner dropdown. Renders at the same Z-tier
    // as dropdown_ (above tooltips, below help overlay). Non-copyable; owns
    // SDL textures. Must be declared after dropdown_ so destructor order is
    // deterministic (right_dropdown_ destroyed first).
    QdHotCornerRightDropdown right_dropdown_;

    // v1.10: floating window manager. Renders at Z=4 (above desktop icons/
    // favorites, below hot-corner widget). Non-copyable; owns QdWindow and
    // QdMinimizedDockEntry shared_ptrs via vectors. Must be declared after
    // dropdown_ so destructor order is deterministic (wm_ destroyed first).
    QdWindowManager wm_;

    // v1.10.3: snap-zone preview overlay state. Set each frame by OnInput
    // when a cursor-drag is active and the cursor is within SNAP_EDGE_THRESHOLD
    // of a screen edge. Cleared when no drag is active. Rendered in OnRender
    // as a semi-transparent tinted rect above wm_.RenderAll output.
    bool       snap_preview_active_ = false;
    SnapTarget snap_preview_target_ = SnapTarget::None;

    // W10-BUG1A: hot-corner suppression after snap.  Set to kSnapCooldownFrames
    // immediately after ApplySnap() commits (ZR-up with a valid snap target).
    // Decremented every OnInput frame; hot-corner activation is suppressed while
    // > 0.  Prevents the hot-corner dropdown from opening on the same frame (or
    // next few frames) that a snap finishes, because the cursor position can still
    // be in the corner zone during the ZR-up frame.
    // 30 frames ≈ 500 ms at 60 fps — long enough for the cursor to move away.
    int snap_corner_cooldown_frames_ = 0;
    static constexpr int kSnapCooldownFrames = 30;

    // v1.10.3: ZL context menu overlay. Shared by all ZL invocation surfaces
    // (window chrome, desktop folder tile, dock tile, empty desktop).
    QdContextMenu context_menu_;

    // v1.10.3: context-menu action dispatch state.
    // Set by the ZL handler when opening context_menu_; consumed in OnInput
    // the frame after the menu closes with a confirmed selection.
    // v1.10.3.1: DesktopFolder added (Fix 1) — ZL over a folder tile now opens
    // a context menu with "Re-classify Apps" and "Cancel".
    // Z2.1 (2026-05-19): EmptyDesktop added — ZL on truly-empty area opens
    // [New Custom Folder, Change Wallpaper ▸, Refresh, Cancel].  Replaces
    // the previous Path 4 no-op.
    enum class CtxSurface : u8 {
        None = 0,
        Window,        // cursor is over an open QdWindow
        Dock,          // cursor / D-pad is over a dock tile (context_menu_dock_idx_)
        DesktopFolder, // cursor / D-pad is over a desktop folder tile (context_menu_folder_idx_)
        EmptyDesktop,  // Z2.1 — cursor is on empty desktop area (Path 4)
        FavoriteItem,  // Z2.5 — cursor is on a favorites-strip tile (context_menu_fav_idx_)
        TopBar,        // Z2.9 — cursor is on the top status bar (y < TOPBAR_H, not in hot-corner)
    };
    CtxSurface   context_menu_surface_    = CtxSurface::None;
    QdWindow*    context_menu_window_     = nullptr;  // valid when surface==Window
    size_t       context_menu_dock_idx_   = 0;        // valid when surface==Dock
    size_t       context_menu_folder_idx_ = 0;        // valid when surface==DesktopFolder
    size_t       context_menu_fav_idx_    = 0;        // Z2.5 — icon idx for FavoriteItem
    // Option indices stored when menu opens so the selection dispatcher knows
    // which item maps to which action without re-building the list.
    int ctx_opt_close_           = -1;
    int ctx_opt_minimize_        = -1;
    int ctx_opt_maximize_        = -1;
    int ctx_opt_open_            = -1;
    int ctx_opt_close_other_     = -1;
    int ctx_opt_reclassify_      = -1;  // valid when surface==DesktopFolder
    int ctx_opt_cancel_          = -1;
    // Z2.1 — EmptyDesktop option indices.  "Change Wallpaper ▸" is a submenu
    // (10 theme packs); the rest are leafs.  Dispatcher reads sub_index from
    // GetSelection() when ctx_opt_change_wallpaper_ matches parent_index.
    int ctx_opt_new_folder_       = -1;
    int ctx_opt_change_wallpaper_ = -1;
    int ctx_opt_refresh_          = -1;
    // Z2.3b — "Show All Hidden Icons" recovery option on Empty Desktop.
    // Calls QdVisibility::Get().Clear() so any hidden favorites reappear.
    int ctx_opt_show_hidden_      = -1;
    // Z2.2 — DesktopFolder additions.  "Choose Folder Theme ▸" reuses the
    // unified 10-pack theme picker (palette+wallpaper+folder is one SSOT).
    int ctx_opt_folder_open_      = -1;
    int ctx_opt_folder_theme_     = -1;
    // Z2.5 — FavoriteItem option indices.  All leaf items.
    int ctx_opt_fav_launch_       = -1;
    int ctx_opt_fav_remove_       = -1;
    // BUG-9 (2026-05-19) — Add to Favorites on Dock surface ctx menu for
    // entries (NRO / Application) that aren't already favorited.
    int ctx_opt_fav_add_          = -1;
    // Z2.3b (v3.1.1 BUG-4 fix) — Hide on favorites strip.  Writes the
    // FavKey-style stable_id ("nro:<path>" / "app:<hex>") to QdVisibility.
    int ctx_opt_fav_hide_         = -1;
    // Z2.3a — Move to Folder ▸ on FavoriteItem (was Dock — dead code there).
    // Submenu carries the 8 FolderIdx names; dispatcher reads sub_index.
    int ctx_opt_dock_move_folder_ = -1;
    // Z2.7 — Window additions.  Snap Left / Snap Right / Move to Center.
    int ctx_opt_win_snap_left_    = -1;
    int ctx_opt_win_snap_right_   = -1;
    int ctx_opt_win_center_       = -1;
    // Z2.9 — Top-bar option indices.  All leaf items.
    int ctx_opt_topbar_lock_      = -1;
    int ctx_opt_topbar_about_     = -1;
    // W12-CHEATS — Dock surface "View Cheats" for installed Application entries.
    int ctx_opt_open_cheats_      = -1;
    // B3.1-MODS — Dock surface "View Mods" for installed Application entries.
    int ctx_opt_open_mods_        = -1;

    // v1.10.3: window-minimize deferred to OnRender where Renderer::Ref is
    // available (MinimizeWindow needs SDL_SetRenderTarget snapshot).
    // Set by OnInput context-menu dispatch; consumed and cleared by OnRender.
    QdWindow* pending_ctx_minimize_win_ = nullptr;
};

} // namespace ul::menu::qdesktop
