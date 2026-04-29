// qd_DesktopIcons.cpp — Auto-grid desktop icon element for uMenu C++ SP3 (v1.3.0).
// Ported from tools/mock-nro-desktop-gui/src/desktop_icons.rs + wm.rs.
// Scans sdmc:/switch/*.nro once at construction; paints icon cells every frame.
// SP2 additions: dock magnify animation via UpdateDockMagnify + two-pass OnRender.
// SP3 additions: SetApplicationEntries() ingests installed Switch apps; JPEG icon loading
//   via LoadJpegIconToCache(); LaunchIcon() dispatches smi::LaunchApplication for apps.

#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>     // LP_HOTCORNER_W/H (hot corner geometry SSOT)
#include <ul/menu/qdesktop/qd_AutoFolders.hpp>  // Fix D (v1.6.12): auto-folder side table
#include <ul/menu/qdesktop/qd_NroAsset.hpp>
#include <ul/menu/qdesktop/qd_IconCategory.hpp>
#include <ul/menu/qdesktop/qd_Anim.hpp>
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>  // Cycle D5: dev toggles
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/menu/ui/ui_Common.hpp>  // ShowSettingsMenu/ShowAlbum/etc. for Special launch
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cerrno>             // v1.7.0-stabilize-7 Slice 5: SaveFavorites errno reporting
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>      // v1.7.0-stabilize-7 Slice 5: g_favorites_set_
#include <vector>
#include <mutex>              // v1.8.15: background prewarm thread; v1.8.18: shared mutex via GetSharedIconCacheMutex()
#include <thread>             // v1.8.15: prewarm_thread_
#include <atomic>             // v1.8.15: prewarm_stop_

// v1.8.20 (Change 2): kernel-direct file I/O for LoadJpegIconToCache.
//   Uses fsdevGetDeviceFileSystem + fsFsOpenFile + fsFileRead instead of IMG_Load.
//   BMP images decoded without SDL (direct pixel memcpy from BITMAPFILEHEADER).
//   JPEG images decoded with SDL_RWFromMem + IMG_Load_RW (no disk I/O from SDL).
#include <switch/runtime/devices/fs_dev.h>  // fsdevGetDeviceFileSystem

// v1.8.23: coyote-timing tick source (a93c4636 research; tick rate = 19.2 MHz).
#include <switch/arm/counter.h>  // armGetSystemTick()

// Pull in SDL2 directly (sdl2_Types.hpp aliases Renderer = SDL_Renderer*).
#include <SDL2/SDL.h>
// SDL2_image: used for JPEG decoding of Application icon files.
#include <SDL2/SDL_image.h>
// Plutonium text rendering — RenderText, GetDefaultFont, DefaultFontSize.
#include <pu/ui/ui_Types.hpp>
// MenuApplication — needed for FadeOutToNonLibraryApplet + Finalize on game launch.
#include <ul/menu/ui/ui_MenuApplication.hpp>

// Global menu application instance (defined in main.cpp).  Used in LaunchIcon
// for IconKind::Application to gracefully suspend uMenu before the game takes
// the foreground; without this the system applet state is wrong and Home
// won't return to the desktop after the game runs.
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

// Cycle G1 (SP4.15): read suspended-app state so LaunchIcon can route
// Application launches through Resume / close-and-relaunch instead of always
// firing a fresh smi::LaunchApplication.  Defined in main.cpp.
extern ul::menu::ui::GlobalSettings g_GlobalSettings;

namespace ul::menu::qdesktop {

// ── Nintendo-classify cache ───────────────────────────────────────────────────
// Persistent flat binary at CLASSIFY_CACHE_PATH.
// Each record is 12 bytes: { u64 app_id, u8 is_nintendo, u8[3] pad }
// The cache is loaded once into a static map on first use and flushed to disk
// whenever a new entry is added.  InvalidateNintendoClassifyCache() removes the
// file so the next IsNintendoPublisher call starts from an empty map.

static constexpr const char *CLASSIFY_CACHE_PATH =
    "sdmc:/ulaunch/cache/nintendo-classify.bin";

// Record layout must match the binary format exactly.
#pragma pack(push, 1)
struct NintendoClassifyRecord {
    u64 app_id;
    u8  is_nintendo;
    u8  pad[3];
};
#pragma pack(pop)
static_assert(sizeof(NintendoClassifyRecord) == 12,
              "NintendoClassifyRecord must be 12 bytes");

// Static in-process cache: app_id -> is_nintendo bool.
// Loaded lazily from disk, written back on new entries.
static std::unordered_map<u64, bool> s_nintendo_classify_map;
static bool s_classify_map_loaded = false;

// Load the cache file into s_nintendo_classify_map (once per process).
static void LoadNintendoClassifyCache() {
    if (s_classify_map_loaded) {
        return;
    }
    s_classify_map_loaded = true; // mark loaded even if file absent

    FILE *f = fopen(CLASSIFY_CACHE_PATH, "rb");
    if (!f) {
        return; // File absent on first boot; starts empty.
    }

    NintendoClassifyRecord rec;
    while (fread(&rec, sizeof(rec), 1, f) == 1) {
        s_nintendo_classify_map[rec.app_id] = (rec.is_nintendo != 0u);
    }
    fclose(f);
}

// Append one new record to the cache file and update the in-process map.
static void PersistNintendoClassifyEntry(u64 app_id, bool is_nintendo) {
    s_nintendo_classify_map[app_id] = is_nintendo;

    // Ensure the cache directory exists (mirrors QdIconCache::EnsureDir pattern).
    // sdmc:/ulaunch/cache/ is already created by QdIconCache::EnsureDir, but we
    // create it explicitly here in case the classify path differs.
    mkdir("sdmc:/ulaunch", 0755);
    mkdir("sdmc:/ulaunch/cache", 0755);

    FILE *f = fopen(CLASSIFY_CACHE_PATH, "ab");
    if (!f) {
        UL_LOG_WARN("qdesktop: cannot open nintendo-classify cache for append: %s",
                    CLASSIFY_CACHE_PATH);
        return;
    }
    NintendoClassifyRecord rec;
    rec.app_id      = app_id;
    rec.is_nintendo = is_nintendo ? 1u : 0u;
    rec.pad[0] = rec.pad[1] = rec.pad[2] = 0u;
    fwrite(&rec, sizeof(rec), 1, f);
    fclose(f);
}

// static
void QdDesktopIconsElement::InvalidateNintendoClassifyCache() {
    s_nintendo_classify_map.clear();
    s_classify_map_loaded = false;
    remove(CLASSIFY_CACHE_PATH);
    UL_LOG_INFO("qdesktop: nintendo-classify cache invalidated");
}

// Note: InvalidateFavoritesCache() is defined later in this file,
// after the file-scope static globals it touches (g_favorites_loaded_,
// g_favorites_list_, g_favorites_set_) are declared.  See the definition
// near the favorites-management block.

// static
bool QdDesktopIconsElement::IsNintendoPublisher(u64 app_id) {
    LoadNintendoClassifyCache();

    auto it = s_nintendo_classify_map.find(app_id);
    if (it != s_nintendo_classify_map.end()) {
        return it->second;
    }

    // Not in cache: apply the title-id heuristic.
    // Nintendo first-party titles have their top byte (bits 56..63) equal to 0x01.
    // This covers the 0x0100xxxxxxxxxxxx range (e.g. Zelda BOTW = 0x01007EF00011E000).
    const u8 top_byte = static_cast<u8>((app_id >> 56u) & 0xFFu);
    const bool result = (top_byte == 0x01u);

    PersistNintendoClassifyEntry(app_id, result);
    return result;
}

// static
IconCategory QdDesktopIconsElement::ClassifyEntry(const NroEntry &e) {
    switch (e.kind) {
        case IconKind::Builtin:
            return IconCategory::Builtin;
        case IconKind::Nro:
            return IconCategory::Homebrew;
        case IconKind::Special:
            return IconCategory::Extras;
        case IconKind::Application:
            return IsNintendoPublisher(e.app_id)
                ? IconCategory::Nintendo
                : IconCategory::Extras;
    }
    return IconCategory::Extras;
}

// ── Fix D (v1.6.12): Auto-folder classification side table ────────────────────
// Finer-grained classification for folder assignment.  Stored in a static
// unordered_map keyed by a per-kind stable ID string rather than by pointer or
// struct field to AVOID extending NroEntry, LpItem, or any libnx-visible struct
// (extending those would corrupt the libnx IPC command table).
//
// Stable ID convention:
//   NRO entry          → nro_path string   (e.g. "sdmc:/switch/sys-clk.nro")
//   Application entry  → "app:<hex16>"     (e.g. "app:01007EF00011E000")
//   Payload entry      → "payload:<fname>" (e.g. "payload:Atmosphere.bin")
//   Builtin/Special    → "builtin:<name>"  (e.g. "builtin:Settings")
//
// The table is populated by the three scan functions (ScanNros,
// SetApplicationEntries, ScanPayloads) and by PopulateBuiltins.
// Consumers call GetAutoFolderKind(stable_id) to retrieve the kind.

// ClassifyKind is declared in qd_DesktopIcons.hpp (included above).

// Name-substring rules applied (in order) when classifying NRO homebrew.
// First matching rule wins.  Comparison is case-insensitive on the entry name.
// app_id_* fields are used for Application entries (name_substr==nullptr means
// skip the name check; apply the app_id mask/value check instead).
struct AutoFolderSpec {
    const char  *name_substr; // substring to match against entry name; nullptr=skip name check
    u64          app_id_mask; // 0 = skip app_id check
    u64          app_id_value;
    ClassifyKind kind;
};

// Helper: case-insensitive substring search (avoids <strings.h> dependency).
static bool CiStrStr(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') { return true; }
    if (!haystack) { return false; }
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        for (; needle[j] != '\0'; ++j) {
            const char h = static_cast<char>(
                (haystack[i + j] >= 'A' && haystack[i + j] <= 'Z')
                ? haystack[i + j] + 32 : haystack[i + j]);
            const char n = static_cast<char>(
                (needle[j] >= 'A' && needle[j] <= 'Z')
                ? needle[j] + 32 : needle[j]);
            if (h != n) { break; }
        }
        if (needle[j] == '\0') { return true; }
    }
    return false;
}

// Ordered classification rules for NRO homebrew.  Emulator keywords take
// priority over generic tool keywords so "yuzu" ranks as Emulator, not Tool.
static const AutoFolderSpec kAutoFolderSpecs[] = {
    // Emulators (name substrings that strongly imply emulation).
    { "yuzu",      0, 0, ClassifyKind::Emulator },
    { "ryujinx",   0, 0, ClassifyKind::Emulator },
    { "retroarch", 0, 0, ClassifyKind::Emulator },
    { "melonds",   0, 0, ClassifyKind::Emulator },
    { "ppsspp",    0, 0, ClassifyKind::Emulator },
    { "desmume",   0, 0, ClassifyKind::Emulator },
    { "mupen64",   0, 0, ClassifyKind::Emulator },
    { "dolphin",   0, 0, ClassifyKind::Emulator },
    { "citra",     0, 0, ClassifyKind::Emulator },
    { "bsnes",     0, 0, ClassifyKind::Emulator },
    { "snes9x",    0, 0, ClassifyKind::Emulator },
    { "mgba",      0, 0, ClassifyKind::Emulator },
    { "nestopia",  0, 0, ClassifyKind::Emulator },
    { "mednafen",  0, 0, ClassifyKind::Emulator },
    // System utilities / tweaks.
    { "sys-clk",   0, 0, ClassifyKind::SystemUtil },
    { "sysclk",    0, 0, ClassifyKind::SystemUtil },
    { "edizon",    0, 0, ClassifyKind::SystemUtil },
    { "goldleaf",  0, 0, ClassifyKind::SystemUtil },
    { "tinfoil",   0, 0, ClassifyKind::SystemUtil },
    { "awoo",      0, 0, ClassifyKind::SystemUtil },
    { "dbi",       0, 0, ClassifyKind::SystemUtil },
    { "hekate",    0, 0, ClassifyKind::SystemUtil },
    { "nxmtp",     0, 0, ClassifyKind::SystemUtil },
    { "umanager",  0, 0, ClassifyKind::SystemUtil },
    { "nxovl",     0, 0, ClassifyKind::SystemUtil },
    { "nxtheme",   0, 0, ClassifyKind::SystemUtil },
    // v1.7.0-stabilize-7 (Slice 4 / O-B Phase 4): extended SystemUtil set.
    // These NRO/payload names are commonly seen in CFW SD root and were
    // previously falling through to ClassifyKind::Unknown -> Other folder.
    // Routing them to SystemUtil sends them to the System folder where
    // configuration / management / recovery tools belong.
    { "daybreak",  0, 0, ClassifyKind::SystemUtil },
    { "sphaira",   0, 0, ClassifyKind::SystemUtil },
    { "lockpick",  0, 0, ClassifyKind::SystemUtil },
    { "linkalho",  0, 0, ClassifyKind::SystemUtil },
    { "netman",    0, 0, ClassifyKind::SystemUtil },
    { "tinwoo",    0, 0, ClassifyKind::SystemUtil },
    { "nxshell",   0, 0, ClassifyKind::SystemUtil },
    { "ftpd",      0, 0, ClassifyKind::SystemUtil },
    { "haze",      0, 0, ClassifyKind::SystemUtil },
    { "fusee",     0, 0, ClassifyKind::SystemUtil },
    { "hwfly",     0, 0, ClassifyKind::SystemUtil },
    { "picofly",   0, 0, ClassifyKind::SystemUtil },
    { "monitor",   0, 0, ClassifyKind::SystemUtil },
    { "vault",     0, 0, ClassifyKind::SystemUtil },
    // Generic homebrew tools.
    { "hbmenu",    0, 0, ClassifyKind::HomebrewTool },
    { "nro",       0, 0, ClassifyKind::HomebrewTool },
    // Sentinel — matches nothing; default for unrecognised entries.
    { nullptr,     0, 0, ClassifyKind::Unknown },
};

// Static side table: stable_id -> ClassifyKind.
// Populated incrementally by ScanNros, SetApplicationEntries, ScanPayloads,
// and PopulateBuiltins.  Never cleared between frames; rebuilt if Initialize()
// is called again.
static std::unordered_map<std::string, ClassifyKind> g_entry_classification_;

// F4 (stabilize-4): NS icon retry counter side table.
// Keyed by app_id (u64).  Tracks how many times LoadNsIconToCache returned
// false (NS service unavailable at startup) for each Application entry so we
// retry up to NS_ICON_MAX_RETRIES times before accepting the fallback gray
// block as the permanent icon.  Stored here (not in NroEntry) to keep the
// struct size pinned at 1632 bytes.
static constexpr u8 NS_ICON_MAX_RETRIES = 3;
static std::unordered_map<u64, u8> g_ns_icon_retry_;

// F4 (stabilize-5): RC-B3 — NRO icon retry cap.
// Mirrors g_ns_icon_retry_ for NRO entries.  Keyed by nro_path (std::string
// because NroEntry has no u64 app_id).  Same semantics: cap at 3 retries;
// after that the LRU gray fallback is accepted permanently.
// NOT added to NroEntry struct — that is pinned at 1632 bytes (static_assert).
static constexpr u8 NRO_ICON_MAX_RETRIES = 3;
static std::unordered_map<std::string, u8> g_nro_icon_retry_;

// v1.7.0-stabilize-7 Slice 4: forward declaration for the rounded-rect
// fallback used when Folder.png cannot be loaded. Definition is later in the
// file (~line 1771); declaring here lets PaintDesktopFolders compile in
// translation-unit order.
static void FillRoundRect(SDL_Renderer *r, SDL_Rect rect, int radius,
                           u8 cr, u8 cg, u8 cb, u8 ca);

// ── v1.7.0-stabilize-7 Slice 4 (O-B): desktop folder grid state ──────────────
// Per the O-B desktop redesign, the per-icon paint loop is replaced with a
// 6-folder categorized grid above the dock. All state is in file-scope side
// tables; NroEntry remains pinned at 1632 bytes per the static_assert.

// Folder identifiers — maps 1:1 to kDesktopFolders[] table indices below.
// Order is LOAD-BEARING (Phase 5 input math + Phase 3 LP filter switch use
// the underlying u8 values via static_cast<size_t>(DesktopFolderId)).
enum class DesktopFolderId : u8 {
    Games     = 0,
    Emulators = 1,
    Tools     = 2,
    System    = 3,
    QOS       = 4,
    Other     = 5,
};
static constexpr size_t kDesktopFolderCount = 6;

// Per-folder render constants — name, glyph fallback, tint colour for the
// (tinted) Folder.png base. Glyph is shown only when the cached name texture
// is unavailable (defensive; the lazy-build in PaintDesktopFolders almost
// always succeeds before the first frame finishes).
struct DesktopFolderSpec {
    DesktopFolderId id;
    const char     *name;
    char            glyph;
    u8              tint_r, tint_g, tint_b;
};
static constexpr DesktopFolderSpec kDesktopFolders[kDesktopFolderCount] = {
    { DesktopFolderId::Games,     "Games",     'G', 0x60u, 0xA5u, 0xFAu },
    { DesktopFolderId::Emulators, "Emulators", 'E', 0x4Au, 0xDEu, 0x80u },
    { DesktopFolderId::Tools,     "Tools",     'T', 0xFBu, 0xBFu, 0x24u },
    { DesktopFolderId::System,    "System",    'S', 0xC0u, 0x84u, 0xFCu },
    { DesktopFolderId::QOS,       "Q OS",      'Q', 0xA7u, 0x8Bu, 0xFAu },
    { DesktopFolderId::Other,     "Other",     '?', 0x80u, 0x80u, 0x80u },
};

// Per-folder live counts (recomputed by RecomputeDesktopFolders).
static size_t g_desktop_folder_counts[kDesktopFolderCount] = {0,0,0,0,0,0};
// Per-folder cached rectangles (set by RecomputeDesktopFolders; also recomputed
// inside PaintDesktopFolders to add the layout (x,y) origin).
static SDL_Rect g_desktop_folder_rects[kDesktopFolderCount] = {};
// Layout-dirty flag — set true on construction and whenever the icon set
// changes (SetApplicationEntries / SetSpecialEntries / scan completion).
static bool g_desktop_folder_layout_dirty = true;
// Tinted base bg texture (Folder.png) — lazily loaded by PaintDesktopFolders.
static SDL_Texture *g_desktop_folder_bg_tex = nullptr;
// Cached per-folder name + count text textures (lazy-build, count_tex rebuilt
// when the displayed count changes).
static SDL_Texture *g_desktop_folder_name_tex[kDesktopFolderCount]   = {};
static int          g_desktop_folder_name_w[kDesktopFolderCount]     = {};
static int          g_desktop_folder_name_h[kDesktopFolderCount]     = {};
static SDL_Texture *g_desktop_folder_count_tex[kDesktopFolderCount]  = {};
static int          g_desktop_folder_count_w[kDesktopFolderCount]    = {};
static int          g_desktop_folder_count_h[kDesktopFolderCount]    = {};
static size_t       g_desktop_folder_last_count[kDesktopFolderCount] = {
    SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX
};

// Phase 3: pending Launchpad pre-filter — single u8 side-table consumed by
// QdLaunchpadElement::Open() exactly once. Set when a desktop folder is
// tapped/launched; LoadMenu(Launchpad) follows.
static AutoFolderIdx g_pending_lp_folder = AutoFolderIdx::None;

// Layout — 3×2 grid above the dock.
// v1.8.23: favorites strip relocated from y=58 (was above this grid) to y=726
// (between this grid and the dock). The 22 px buffer above DF_GRID_Y previously
// reserved for the strip is no longer required, but DF_GRID_Y is left at 210 to
// preserve the v1.7.0-stable folder rect baseline.
// Folders span y=210..650 (top + 2*200 + 40 = 650). Strip at y=726..856.
// Dock starts at y=932; gap from folder bottom to dock top = 282 px.
static constexpr s32 DF_TILE_W = 400;
static constexpr s32 DF_TILE_H = 200;
static constexpr s32 DF_GAP_X  = 60;
static constexpr s32 DF_GAP_Y  = 40;
static constexpr s32 DF_COLS   = 3;
static constexpr s32 DF_ROWS   = 2;
// Centred horizontally: (1920 - (3*400 + 2*60)) / 2 = (1920 - 1320) / 2 = 300.
static constexpr s32 DF_GRID_X = (1920 - (DF_COLS * DF_TILE_W
                                          + (DF_COLS - 1) * DF_GAP_X)) / 2;
// 210 px from screen top. Folder bottom = 210 + 2*200 + 40 = 650.
// Hot corner widget at (0,0) still clear (folder grid starts well below y=48
// topbar). v1.8.23: strip is now BELOW this grid (FAV_STRIP_TOP=726).
static constexpr s32 DF_GRID_Y = 210;

// Compute the screen-relative rect for folder index fi (0..5), without any
// layout (x,y) translation. Caller adds (x,y) before drawing or hit-testing.
static void ComputeDesktopFolderRect(size_t fi, SDL_Rect &out) {
    const size_t col = fi % static_cast<size_t>(DF_COLS);
    const size_t row = fi / static_cast<size_t>(DF_COLS);
    out.x = DF_GRID_X + static_cast<s32>(col) * (DF_TILE_W + DF_GAP_X);
    out.y = DF_GRID_Y + static_cast<s32>(row) * (DF_TILE_H + DF_GAP_Y);
    out.w = DF_TILE_W;
    out.h = DF_TILE_H;
}

// Map a single NroEntry to one of the 6 desktop folders.
//
// Routing rules (first match wins):
//   1. IconKind::Builtin entries are dock-only and never participate in the
//      desktop folder grid; routed to Other as a defensive default for any
//      caller that bypasses the dock-skip in RecomputeDesktopFolders.
//   2. IconKind::Special entries are system applets (Settings/Album/etc.) and
//      always belong in System.
//   3. NRO entries with "qos" / "qos-" in name or path land in Q OS — this is
//      a name preempt because ClassifyKind has no QosApp value and Q OS NROs
//      otherwise ride the HomebrewTool/Emulator buckets.
//   4. Everything else delegates to GetAutoFolderKind via stable_id, mapping
//      ClassifyKind values to folders per the table in O-B §"Folder definitions".
static DesktopFolderId ClassifyDesktopFolder(const NroEntry &entry) {
    if (entry.kind == IconKind::Builtin) {
        return DesktopFolderId::Other;
    }
    if (entry.kind == IconKind::Special) {
        return DesktopFolderId::System;
    }
    // Q OS name preempt (covers qos / qos-mock-* / qos-test-harness etc.).
    if (entry.kind == IconKind::Nro && entry.nro_path[0] != '\0'
            && (CiStrStr(entry.name, "qos")
                || CiStrStr(entry.nro_path, "qos-"))) {
        return DesktopFolderId::QOS;
    }
    // Fall through: build stable_id and consult the side table.
    std::string sid;
    if (entry.kind == IconKind::Nro) {
        sid = entry.nro_path;
    } else if (entry.kind == IconKind::Application) {
        char hex[32];
        snprintf(hex, sizeof(hex), "app:%016llx",
                 static_cast<unsigned long long>(entry.app_id));
        sid = hex;
    } else {
        // Unknown kind — defensive default.
        return DesktopFolderId::Other;
    }
    const ClassifyKind ck = QdDesktopIconsElement::GetAutoFolderKind(sid);
    switch (ck) {
        case ClassifyKind::NintendoGame:
        case ClassifyKind::ThirdPartyGame: return DesktopFolderId::Games;
        case ClassifyKind::Emulator:       return DesktopFolderId::Emulators;
        case ClassifyKind::HomebrewTool:   return DesktopFolderId::Tools;
        case ClassifyKind::SystemUtil:
        case ClassifyKind::Payload:        return DesktopFolderId::System;
        case ClassifyKind::Builtin:        return DesktopFolderId::Other;
        case ClassifyKind::Unknown:
        default:                           return DesktopFolderId::Other;
    }
}

// ── v1.7.0-stabilize-7 Slice 5 (O-F): favorites store ────────────────────────
// Per the O-F design, user favorites are persisted to
// sdmc:/ulaunch/qos-favorites.toml as a one-line-per-entry tag-prefixed list.
// The runtime keeps a parallel std::unordered_set for O(1) IsFavorite lookup
// against the FavKey representation. Cap = 12 entries to keep RAM <2 KB and
// avoid pagination on the desktop strip (6 visible + scroll deferred).

enum class FavoriteKind : u8 {
    Nro     = 0,
    App     = 1,
    Builtin = 2,
    Special = 3,
};

struct FavoriteEntry {
    FavoriteKind kind;
    // 769 bytes mirrors NroEntry::nro_path / icon_path (FS_MAX_PATH).  For
    // App / Builtin / Special this is a short string ("01007ef000118000",
    // "Vault", "Settings"); for Nro it is the full SD path.
    char         id[769];
};

static std::vector<FavoriteEntry>           g_favorites_list_;
static std::unordered_set<std::string>      g_favorites_set_;
static bool                                 g_favorites_loaded_ = false;

// v1.8.19: Negative-load memoization for icon_path entries.
// Keyed by icon_path (or "app:<hex16>" for NS-cache paths).
// Once LoadJpegIconToCache() or LoadAppIconFromUSystemCache() returns false
// for a given path, that path is inserted here so PrewarmAllIcons() skips it
// on every subsequent prewarm pass without issuing a disk read or NS call.
// Lifetime: static storage duration; cleared implicitly when the process exits.
static std::unordered_set<std::string>      g_has_no_asset_;

// static
// Bug #2/#3 fix (v1.8): clear the in-process favorites cache so that the next
// EnsureFavoritesLoaded() call re-reads from disk.  This is called from
// SetApplicationEntries() every time the icon set is rebuilt (e.g. on game-
// resume) because uMenu does NOT restart after a guest title exits — the
// process is merely suspended and then resumed, so g_favorites_loaded_ stays
// true and EnsureFavoritesLoaded() returns early, leaving g_favorites_list_
// populated with stale icon indices that no longer correspond to the freshly
// rebuilt icons_[] array (Bug #2 symptom: only 1 favorite visible after
// resume).  Clearing the loaded flag forces a full reload on the next access,
// which rebuilds the strip against the current icons_[] layout (Bug #3
// symptom: black-square textures also disappear because the icon_tex_[] slots
// are all freed and re-created by PaintFavoritesStrip's lazy init path).
//
// Defined here (post-globals) so the static-storage references resolve.
void QdDesktopIconsElement::InvalidateFavoritesCache() {
    g_favorites_loaded_ = false;
    g_favorites_list_.clear();
    g_favorites_set_.clear();
    UL_LOG_INFO("qdesktop: favorites cache invalidated (will reload from disk)");
}

// Toast-pending state for the next OnRender frame (g_MenuApplication is
// available there but using ShowNotification synchronously inside OnInput
// during a touch-up is fine; we do the latter).
static constexpr size_t  MAX_FAVORITES = 12u;
static constexpr const char *FAVORITES_PATH =
    "sdmc:/ulaunch/qos-favorites.toml";
static constexpr const char *FAVORITES_TMP_PATH =
    "sdmc:/ulaunch/qos-favorites.toml.tmp";

// Build the FavKey string for an LpItem-shaped pair of fields. The Set/Map
// uses these as keys for membership testing; the TOML serializer emits the
// same form so a round-trip Load -> contains() returns true.
//
// Format mirrors the FavoriteKind enum:
//   Nro      -> "nro:<full_sd_path>"
//   App      -> "app:<hex16>"
//   Builtin  -> "builtin:<name>"
//   Special  -> "special:<name>"
static std::string FavKey(FavoriteKind kind, const char *id) {
    switch (kind) {
        case FavoriteKind::Nro:     return std::string("nro:")     + id;
        case FavoriteKind::App:     return std::string("app:")     + id;
        case FavoriteKind::Builtin: return std::string("builtin:") + id;
        case FavoriteKind::Special: return std::string("special:") + id;
    }
    return std::string();  // unreachable; keeps the compiler from complaining
}

// Parse a single line of qos-favorites.toml into out_kind / out_id. Returns
// true on a recognized prefix; false on unrecognized / blank lines so the
// caller can skip them silently. Trailing newlines are stripped before the
// id copy.
static bool ParseFavoriteLine(const char *line, FavoriteKind &out_kind,
                               char *out_id, size_t out_id_cap) {
    if (line == nullptr || out_id == nullptr || out_id_cap == 0u) {
        return false;
    }
    const char *prefix = nullptr;
    if (strncmp(line, "nro:", 4) == 0) {
        out_kind = FavoriteKind::Nro;
        prefix   = line + 4;
    } else if (strncmp(line, "app:", 4) == 0) {
        out_kind = FavoriteKind::App;
        prefix   = line + 4;
    } else if (strncmp(line, "builtin:", 8) == 0) {
        out_kind = FavoriteKind::Builtin;
        prefix   = line + 8;
    } else if (strncmp(line, "special:", 8) == 0) {
        out_kind = FavoriteKind::Special;
        prefix   = line + 8;
    } else {
        return false;  // unrecognized / blank
    }
    // Copy with trailing-newline strip and bounds check.
    size_t i = 0u;
    for (; prefix[i] != '\0' && prefix[i] != '\n' && prefix[i] != '\r'
           && i + 1u < out_id_cap; ++i) {
        out_id[i] = prefix[i];
    }
    out_id[i] = '\0';
    return out_id[0] != '\0';
}

// Lazy loader — idempotent. Called from every entry point that mutates or
// queries the store so OnInput and OnRender both see the same loaded state.
static void EnsureFavoritesLoaded() {
    if (g_favorites_loaded_) {
        return;
    }
    g_favorites_loaded_ = true;  // mark loaded even if file absent

    FILE *f = fopen(FAVORITES_PATH, "rb");
    if (f == nullptr) {
        return;  // first-boot or never-favorited: empty store
    }
    char line[1024];
    while (fgets(line, sizeof(line), f) != nullptr
            && g_favorites_list_.size() < MAX_FAVORITES) {
        FavoriteEntry fav;
        if (ParseFavoriteLine(line, fav.kind, fav.id, sizeof(fav.id))) {
            const std::string key = FavKey(fav.kind, fav.id);
            if (g_favorites_set_.find(key) == g_favorites_set_.end()) {
                g_favorites_list_.push_back(fav);
                g_favorites_set_.insert(key);
            }
        }
    }
    fclose(f);
}

// Atomic save: write to qos-favorites.toml.tmp, fsync (flush), close, rename.
// rename(2) on FAT32/exFAT is atomic for files <512 KB which our store always
// is (<2 KB even at full capacity). Caller must not rely on the on-disk file
// being current synchronously — fsync is best-effort on libnx FS but the
// rename closes the window where a power loss would leave the .tmp readable
// and the canonical file truncated.
static bool SaveFavorites() {
    mkdir("sdmc:/ulaunch", 0777);   // B62: ensure parent dir exists; idempotent (no-op if already present)
    FILE *f = fopen(FAVORITES_TMP_PATH, "wb");
    if (f == nullptr) {
        UL_LOG_WARN("qdesktop: SaveFavorites: fopen(tmp) failed errno=%d", errno);
        return false;
    }
    for (const FavoriteEntry &fav : g_favorites_list_) {
        const char *prefix = "nro:";
        switch (fav.kind) {
            case FavoriteKind::Nro:     prefix = "nro:";     break;
            case FavoriteKind::App:     prefix = "app:";     break;
            case FavoriteKind::Builtin: prefix = "builtin:"; break;
            case FavoriteKind::Special: prefix = "special:"; break;
        }
        if (fprintf(f, "%s%s\n", prefix, fav.id) < 0) {
            fclose(f);
            UL_LOG_WARN("qdesktop: SaveFavorites: fprintf failed");
            return false;
        }
    }
    fflush(f);
    fclose(f);
    // v1.8.12 B62-deeper: Switch's Horizon FsService RenameFile IPC returns
    // errno=17 (EEXIST) when destination exists — POSIX overwrite semantics
    // do NOT hold on Switch. Remove canonical first; ENOENT (no canonical
    // yet on first-write) is the expected non-error case.
    if (remove(FAVORITES_PATH) != 0 && errno != ENOENT) {
        UL_LOG_WARN("qdesktop: SaveFavorites: remove(canonical) failed errno=%d", errno);
        // continue — rename may still succeed via overwrite path on some FS
    }
    // POSIX rename on FAT32/exFAT after the destination has been unlinked.
    if (rename(FAVORITES_TMP_PATH, FAVORITES_PATH) != 0) {
        UL_LOG_WARN("qdesktop: SaveFavorites: rename failed errno=%d", errno);
        return false;
    }
    // v1.8.11 B62-deep: explicitly commit Horizon FsService write-back cache
    // to physical SD media. Without this, fflush+fclose+rename only push the
    // bytes into Horizon's per-FS write-back; a reboot before Horizon's own
    // periodic flush silently discards the file. Best-effort: log on failure
    // but do not fail the save (Horizon may have already flushed).
    const Result commit_rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(commit_rc)) {
        UL_LOG_WARN("qdesktop: SaveFavorites: fsdevCommitDevice rc=0x%x", commit_rc);
    }
    UL_LOG_INFO("qdesktop: SaveFavorites: %zu favorites flushed to %s",
                g_favorites_list_.size(), FAVORITES_PATH);
    return true;
}

// Build a FavoriteEntry from an NroEntry. The id field stores whichever
// representation the FavoriteKind expects (nro_path / app_id hex / name).
// Returns true on success; false if the entry kind is unsupported (which
// should never happen for the four IconKind values we route).
static bool FavEntryFromNroEntry(const NroEntry &entry, FavoriteEntry &out) {
    if (entry.kind == IconKind::Nro && entry.nro_path[0] != '\0') {
        out.kind = FavoriteKind::Nro;
        snprintf(out.id, sizeof(out.id), "%s", entry.nro_path);
        return true;
    }
    if (entry.kind == IconKind::Application && entry.app_id != 0u) {
        out.kind = FavoriteKind::App;
        snprintf(out.id, sizeof(out.id), "%016llx",
                 static_cast<unsigned long long>(entry.app_id));
        return true;
    }
    if (entry.kind == IconKind::Builtin) {
        out.kind = FavoriteKind::Builtin;
        snprintf(out.id, sizeof(out.id), "%s", entry.name);
        return true;
    }
    if (entry.kind == IconKind::Special) {
        out.kind = FavoriteKind::Special;
        snprintf(out.id, sizeof(out.id), "%s", entry.name);
        return true;
    }
    return false;
}

// O(1) membership test against the precomputed key set. Lazy-load is
// idempotent so repeated callers in OnRender are cheap.
static bool IsFavorite(const NroEntry &entry) {
    EnsureFavoritesLoaded();
    FavoriteEntry probe;
    if (!FavEntryFromNroEntry(entry, probe)) {
        return false;
    }
    const std::string key = FavKey(probe.kind, probe.id);
    return g_favorites_set_.find(key) != g_favorites_set_.end();
}

// Toggle favorite state for the entry. Returns true if the entry is now
// favorited (added), false if it was removed or could not be added (cap hit).
// Persistence is synchronous on every call; the file is small enough that
// this is well under the per-frame budget.
static bool ToggleFavorite(const NroEntry &entry) {
    EnsureFavoritesLoaded();
    FavoriteEntry probe;
    if (!FavEntryFromNroEntry(entry, probe)) {
        UL_LOG_WARN("qdesktop: ToggleFavorite: unsupported entry kind=%d",
                    static_cast<int>(entry.kind));
        return false;
    }
    const std::string key = FavKey(probe.kind, probe.id);
    auto it = g_favorites_set_.find(key);
    if (it != g_favorites_set_.end()) {
        // Currently a favorite — remove.
        g_favorites_set_.erase(it);
        for (auto lit = g_favorites_list_.begin();
             lit != g_favorites_list_.end(); ++lit) {
            if (lit->kind == probe.kind
                    && strncmp(lit->id, probe.id, sizeof(lit->id)) == 0) {
                g_favorites_list_.erase(lit);
                break;
            }
        }
        SaveFavorites();
        UL_LOG_INFO("qdesktop: ToggleFavorite: removed %s", key.c_str());
        return false;
    }
    // Not a favorite — add (with cap check).
    if (g_favorites_list_.size() >= MAX_FAVORITES) {
        if (g_MenuApplication != nullptr) {
            g_MenuApplication->ShowNotification(
                "Favorites full (max 12)");
        }
        UL_LOG_INFO("qdesktop: ToggleFavorite: cap reached (%zu)",
                    g_favorites_list_.size());
        return false;
    }
    g_favorites_list_.push_back(probe);
    g_favorites_set_.insert(key);
    SaveFavorites();
    UL_LOG_INFO("qdesktop: ToggleFavorite: added %s", key.c_str());
    return true;
}

// Public Launchpad shims — Patch 2 calls these without including the desktop's
// FavoriteEntry / FavoriteKind (which live in this anonymous-namespace-style
// file-scope block). The shims accept LpItem by reference; LpItem fields
// (kind via icon_category + app_id + nro_path + name) carry enough info to
// reconstruct the FavoriteEntry shape for the lookup / toggle.
//
// Note: LpItem doesn't carry an explicit IconKind enum — it carries
// is_builtin, app_id, nro_path, and icon_path. We reconstruct the FavoriteKind
// from those fields:
//   is_builtin     -> Builtin
//   app_id != 0    -> App
//   nro_path != ""  -> Nro
//   else            -> Special  (Switch system applet, no app_id, no nro_path)
static FavoriteKind FavKindFromLpItem(const LpItem &item) {
    if (item.is_builtin) {
        return FavoriteKind::Builtin;
    }
    if (item.app_id != 0u) {
        return FavoriteKind::App;
    }
    if (item.nro_path[0] != '\0') {
        return FavoriteKind::Nro;
    }
    return FavoriteKind::Special;
}

// Build a FavoriteEntry from an LpItem snapshot using the same id format as
// FavEntryFromNroEntry above so the keys round-trip identically.
static FavoriteEntry FavEntryFromLpItem(const LpItem &item) {
    FavoriteEntry out;
    out.kind = FavKindFromLpItem(item);
    switch (out.kind) {
        case FavoriteKind::Nro:
            snprintf(out.id, sizeof(out.id), "%s", item.nro_path);
            break;
        case FavoriteKind::App:
            snprintf(out.id, sizeof(out.id), "%016llx",
                     static_cast<unsigned long long>(item.app_id));
            break;
        case FavoriteKind::Builtin:
        case FavoriteKind::Special:
            snprintf(out.id, sizeof(out.id), "%s", item.name);
            break;
    }
    return out;
}

// Public LpItem shims (declared in qd_DesktopIcons.hpp public block; defined
// here so they can access the anonymous file-scope state).
bool ToggleFavoriteByLpItem(const LpItem &item) {
    EnsureFavoritesLoaded();
    const FavoriteEntry probe = FavEntryFromLpItem(item);
    if (probe.id[0] == '\0') {
        return false;  // Defensive: empty id means LpItem was malformed.
    }
    const std::string key = FavKey(probe.kind, probe.id);
    auto it = g_favorites_set_.find(key);
    if (it != g_favorites_set_.end()) {
        // Remove path.
        g_favorites_set_.erase(it);
        for (auto lit = g_favorites_list_.begin();
             lit != g_favorites_list_.end(); ++lit) {
            if (lit->kind == probe.kind
                    && strncmp(lit->id, probe.id, sizeof(lit->id)) == 0) {
                g_favorites_list_.erase(lit);
                break;
            }
        }
        SaveFavorites();
        if (g_MenuApplication != nullptr) {
            g_MenuApplication->ShowNotification("Removed from favorites");
        }
        UL_LOG_INFO("qdesktop: ToggleFavoriteByLpItem: removed %s", key.c_str());
        return false;
    }
    if (g_favorites_list_.size() >= MAX_FAVORITES) {
        if (g_MenuApplication != nullptr) {
            g_MenuApplication->ShowNotification("Favorites full (max 12)");
        }
        UL_LOG_INFO("qdesktop: ToggleFavoriteByLpItem: cap reached (%zu)",
                    g_favorites_list_.size());
        return false;
    }
    g_favorites_list_.push_back(probe);
    g_favorites_set_.insert(key);
    SaveFavorites();
    if (g_MenuApplication != nullptr) {
        g_MenuApplication->ShowNotification("Added to favorites");
    }
    UL_LOG_INFO("qdesktop: ToggleFavoriteByLpItem: added %s", key.c_str());
    return true;
}

bool IsFavoriteByLpItem(const LpItem &item) {
    EnsureFavoritesLoaded();
    const FavoriteEntry probe = FavEntryFromLpItem(item);
    if (probe.id[0] == '\0') {
        return false;
    }
    const std::string key = FavKey(probe.kind, probe.id);
    return g_favorites_set_.find(key) != g_favorites_set_.end();
}

// Classify an NRO entry against kAutoFolderSpecs[].
// Loop stops when both name_substr == nullptr and app_id_mask == 0 (sentinel).
// Returns the first matching ClassifyKind, or ClassifyKind::Unknown.
static ClassifyKind ClassifyNroAutoFolder(const NroEntry &e) {
    for (const AutoFolderSpec *spec = kAutoFolderSpecs;
         spec->name_substr != nullptr || spec->app_id_mask != 0;
         ++spec) {
        if (spec->name_substr != nullptr) {
            if (CiStrStr(e.name, spec->name_substr)) {
                return spec->kind;
            }
        } else if (spec->app_id_mask != 0) {
            if ((e.app_id & spec->app_id_mask) == spec->app_id_value) {
                return spec->kind;
            }
        }
    }
    return ClassifyKind::Unknown;
}

// Public accessor — declared in qd_DesktopIcons.hpp.
// static
ClassifyKind QdDesktopIconsElement::GetAutoFolderKind(const std::string &stable_id) {
    auto it = g_entry_classification_.find(stable_id);
    if (it != g_entry_classification_.end()) {
        return it->second;
    }
    return ClassifyKind::Unknown;
}

// ── v1.7.0-stabilize-7 Slice 4 (O-B): folder-grid statics ─────────────────────

// Public accessor (Phase 3) — Launchpad consumes the pending pre-filter.
// static
AutoFolderIdx QdDesktopIconsElement::ConsumePendingLaunchpadFolder() {
    const AutoFolderIdx out = g_pending_lp_folder;
    g_pending_lp_folder = AutoFolderIdx::None;
    return out;
}

// Public marker (Phase 2) — SetApplicationEntries / SetSpecialEntries call
// this so the next paint refreshes the folder counts.
// static
void QdDesktopIconsElement::MarkDesktopFolderLayoutDirty() {
    g_desktop_folder_layout_dirty = true;
}

// Map a desktop folder bucket to the matching Launchpad AutoFolderIdx for
// the pre-filter handoff. None => unfiltered fallback (used for "Other"
// where there is no clean Launchpad bucket; user gets the all-items view).
static AutoFolderIdx DesktopFolderToLpFilter(DesktopFolderId fid) {
    switch (fid) {
        case DesktopFolderId::Games:     return AutoFolderIdx::NxGames;
        case DesktopFolderId::Emulators: return AutoFolderIdx::Homebrew;
        case DesktopFolderId::Tools:     return AutoFolderIdx::Homebrew;
        case DesktopFolderId::System:    return AutoFolderIdx::System;
        case DesktopFolderId::QOS:       return AutoFolderIdx::Homebrew;
        case DesktopFolderId::Other:     return AutoFolderIdx::None;
    }
    return AutoFolderIdx::None;
}

// Trigger Launchpad open with a pre-set active_folder_. The pending side
// table is consumed by Open() exactly once.
static void OpenLaunchpadFiltered(DesktopFolderId fid) {
    if (g_MenuApplication == nullptr) {
        return;
    }
    g_pending_lp_folder = DesktopFolderToLpFilter(fid);
    UL_LOG_INFO("qdesktop: desktop folder tap fid=%u -> LoadMenu(Launchpad) filter=%u",
                static_cast<unsigned>(fid),
                static_cast<unsigned>(g_pending_lp_folder));
    g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Launchpad);
}

// Recompute counts and rects. Counts are zeroed and re-tallied from icons_;
// rects are deterministic from ComputeDesktopFolderRect (no-op repeat is fine).
void QdDesktopIconsElement::RecomputeDesktopFolders() {
    for (size_t fi = 0; fi < kDesktopFolderCount; ++fi) {
        g_desktop_folder_counts[fi] = 0u;
        ComputeDesktopFolderRect(fi, g_desktop_folder_rects[fi]);
    }
    for (size_t i = 0; i < icon_count_; ++i) {
        const NroEntry &e = icons_[i];
        // Builtins live in the dock — exclude from folder counts so the
        // 6-folder grid never advertises an entry that is already painted
        // separately at the bottom of the screen.
        if (e.kind == IconKind::Builtin) {
            continue;
        }
        const size_t fi = static_cast<size_t>(ClassifyDesktopFolder(e));
        if (fi < kDesktopFolderCount) {
            ++g_desktop_folder_counts[fi];
        }
    }
    g_desktop_folder_layout_dirty = false;
}

void QdDesktopIconsElement::PaintDesktopFolders(SDL_Renderer *r, s32 x, s32 y) {
    if (g_desktop_folder_layout_dirty) {
        RecomputeDesktopFolders();
    }
    // Lazy-load Folder.png base texture (one-time per process).
    if (g_desktop_folder_bg_tex == nullptr) {
        g_desktop_folder_bg_tex =
            ::ul::menu::ui::TryFindLoadImage("ui/Main/EntryIcon/Folder");
    }

    for (size_t fi = 0; fi < kDesktopFolderCount; ++fi) {
        const DesktopFolderSpec &spec = kDesktopFolders[fi];
        SDL_Rect cell;
        ComputeDesktopFolderRect(fi, cell);
        cell.x += x;
        cell.y += y;

        // ── 1. Background — tinted Folder.png OR FillRoundRect fallback ──
        if (g_desktop_folder_bg_tex != nullptr) {
            SDL_SetTextureColorMod(g_desktop_folder_bg_tex,
                                   spec.tint_r, spec.tint_g, spec.tint_b);
            SDL_SetTextureAlphaMod(g_desktop_folder_bg_tex, 0xD8u);
            SDL_SetTextureBlendMode(g_desktop_folder_bg_tex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(r, g_desktop_folder_bg_tex, nullptr, &cell);
        } else {
            FillRoundRect(r, cell, 18,
                          spec.tint_r, spec.tint_g, spec.tint_b, 0xC0u);
        }

        // ── 2. Cached name texture (lazy-build) ───────────────────────────
        if (g_desktop_folder_name_tex[fi] == nullptr && spec.name != nullptr) {
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            g_desktop_folder_name_tex[fi] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::MediumLarge),
                std::string(spec.name), wh);
            if (g_desktop_folder_name_tex[fi] != nullptr) {
                SDL_QueryTexture(g_desktop_folder_name_tex[fi], nullptr, nullptr,
                                 &g_desktop_folder_name_w[fi],
                                 &g_desktop_folder_name_h[fi]);
            }
        }
        if (g_desktop_folder_name_tex[fi] != nullptr) {
            SDL_Rect ndst {
                cell.x + (cell.w - g_desktop_folder_name_w[fi]) / 2,
                cell.y + 28,
                g_desktop_folder_name_w[fi],
                g_desktop_folder_name_h[fi]
            };
            SDL_RenderCopy(r, g_desktop_folder_name_tex[fi], nullptr, &ndst);
        }

        // ── 3. Cached count texture (rebuild on count change) ────────────
        // v1.8.2 LRU fix: g_desktop_folder_count_tex[] is assigned via RenderText
        // (LRU cache-owned).  Do NOT call SDL_DestroyTexture on cache-owned ptrs.
        // Simply null the pointer to trigger re-render; the LRU evicts the old entry.
        const size_t cur_count = g_desktop_folder_counts[fi];
        if (cur_count != g_desktop_folder_last_count[fi]) {
            g_desktop_folder_count_tex[fi] = nullptr;  // LRU-owned; do NOT destroy
            g_desktop_folder_last_count[fi] = cur_count;
        }
        if (g_desktop_folder_count_tex[fi] == nullptr) {
            char count_buf[32];
            snprintf(count_buf, sizeof(count_buf), "(%zu)", cur_count);
            const pu::ui::Color cc { 0xFFu, 0xFFu, 0xFFu, 0xC0u };
            g_desktop_folder_count_tex[fi] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                std::string(count_buf), cc);
            if (g_desktop_folder_count_tex[fi] != nullptr) {
                SDL_QueryTexture(g_desktop_folder_count_tex[fi], nullptr, nullptr,
                                 &g_desktop_folder_count_w[fi],
                                 &g_desktop_folder_count_h[fi]);
            }
        }
        if (g_desktop_folder_count_tex[fi] != nullptr) {
            SDL_Rect cdst {
                cell.x + (cell.w - g_desktop_folder_count_w[fi]) / 2,
                cell.y + cell.h - g_desktop_folder_count_h[fi] - 24,
                g_desktop_folder_count_w[fi],
                g_desktop_folder_count_h[fi]
            };
            SDL_RenderCopy(r, g_desktop_folder_count_tex[fi], nullptr, &cdst);
        }

        // ── 4. Glyph fallback if name texture failed ─────────────────────
        if (g_desktop_folder_name_tex[fi] == nullptr) {
            const std::string gs(1, spec.glyph);
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            SDL_Texture *gtex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                gs, wh);
            if (gtex != nullptr) {
                int gw = 0, gh = 0;
                SDL_QueryTexture(gtex, nullptr, nullptr, &gw, &gh);
                SDL_Rect gdst {
                    cell.x + (cell.w - gw) / 2,
                    cell.y + (cell.h - gh) / 2,
                    gw, gh
                };
                SDL_RenderCopy(r, gtex, nullptr, &gdst);
                pu::ui::render::DeleteTexture(gtex);
            }
        }

        // ── 5. Focus / hover ring ────────────────────────────────────────
        // Focus highlights the currently dpad-focused folder (indices 0..5
        // of the unified focus ring). Mouse hover provides a softer ring
        // when cursor_ref_ is over the cell.
        // v1.8 Input-source latch: D-pad focus ring only shown in DPAD mode;
        // mouse hover ring only shown in MOUSE mode.
        const bool focused = (active_input_source_ == InputSource::DPAD)
                          && (dpad_focus_index_ == fi);
        const bool hovered = (active_input_source_ == InputSource::MOUSE)
                          && (mouse_hover_index_ == fi);
        if (focused) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
            // 3 px white border via 4 strips at one-pixel insets.
            for (int t = 0; t < 3; ++t) {
                SDL_Rect b_top { cell.x - t, cell.y - t,
                                  cell.w + 2 * t, 1 };
                SDL_Rect b_bot { cell.x - t, cell.y + cell.h + t - 1,
                                  cell.w + 2 * t, 1 };
                SDL_Rect b_lft { cell.x - t, cell.y - t,
                                  1, cell.h + 2 * t };
                SDL_Rect b_rgt { cell.x + cell.w + t - 1, cell.y - t,
                                  1, cell.h + 2 * t };
                SDL_RenderFillRect(r, &b_top);
                SDL_RenderFillRect(r, &b_bot);
                SDL_RenderFillRect(r, &b_lft);
                SDL_RenderFillRect(r, &b_rgt);
            }
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        } else if (hovered) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x80u);
            SDL_RenderDrawRect(r, &cell);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
    }
}

// ── v1.7.0-stabilize-7 Slice 5 (O-F): favorites-strip render ─────────────────

// Geometry — coexists with the Slice 4 folder grid and the dock.
// v1.8.23: relocated favorites strip from y=58 (under topbar) to BETWEEN the
// folder grid (ends at y=650) and the dock (top at y=932). Strip is 130 px
// tall (ICON_BG_H), lives at y=726..856 -> 76 px clearance above the dock and
// 76 px clearance below the folder grid (centred in the 282 px gap).
static constexpr s32 FAV_STRIP_VISIBLE = 6;
// FAV_STRIP_W = 6 * ICON_BG_W + 5 * ICON_GRID_GAP_X = 6*140 + 5*28 = 980
static constexpr s32 FAV_STRIP_W = FAV_STRIP_VISIBLE * ICON_BG_W
    + (FAV_STRIP_VISIBLE - 1) * ICON_GRID_GAP_X;
static constexpr s32 FAV_STRIP_LEFT = (1920 - FAV_STRIP_W) / 2;  // 470
static constexpr s32 FAV_STRIP_TOP  = 726;  // v1.8.23: between folders (y=210..650) and dock (y=932..1080).
// Per-tile spacing on the strip (icon width + horizontal gap).
static constexpr s32 FAV_TILE_SPACING = ICON_BG_W + ICON_GRID_GAP_X;  // 168

// v1.8.23: coyote-timing constants (a93c4636 research; tick rate = 19.2 MHz)
static constexpr u64 TAP_MAX_TICKS          = 4'800'000ULL;   // 250 ms — tap-vs-hold ceiling
static constexpr u64 RELAUNCH_LOCKOUT_TICKS = 5'760'000ULL;   // 300 ms — double-launch suppression
static constexpr u32 DPAD_REPEAT_DELAY_F    = 18u;            // 300 ms — initial dpad-held delay
static constexpr u32 DPAD_REPEAT_INTERVAL_F = 9u;             // 150 ms — dpad-held repeat interval
// CLICK_TOLERANCE_PX=24 already exists at qd_DesktopIcons.cpp:~1220 — reused; no duplicate.

// Resolve a FavoriteEntry to an icons_[] index, or SIZE_MAX if the favorite
// no longer matches any entry (auto-prune candidate). Caller is expected to
// hold icon_count_ stable across the call (true on the main thread).
static size_t ResolveFavoriteToIconIdx(const std::array<NroEntry, MAX_ICONS> &icons,
                                        size_t icon_count,
                                        const FavoriteEntry &fav) {
    for (size_t i = 0u; i < icon_count; ++i) {
        const NroEntry &e = icons[i];
        FavoriteEntry probe;
        if (!FavEntryFromNroEntry(e, probe)) {
            continue;
        }
        if (probe.kind == fav.kind
                && strncmp(probe.id, fav.id, sizeof(probe.id)) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

void QdDesktopIconsElement::PaintFavoritesStrip(SDL_Renderer *r, s32 x, s32 y) {
    EnsureFavoritesLoaded();
    if (g_favorites_list_.empty()) {
        return;
    }
    // Direct cursor query for hover detection — favorites strip uses its own
    // fav_strip_focus_index_ (Bug #4 / v1.8) for D-pad and mouse hover, distinct
    // from dpad_focus_index_ which covers folders + dock.  v1.8.23 made the
    // strip a full peer in the dpad cycle (Folders <-> Favorites <-> Dock).
    s32 cursor_x = -1, cursor_y = -1;
    if (cursor_ref_ != nullptr) {
        cursor_x = cursor_ref_->GetCursorX();
        cursor_y = cursor_ref_->GetCursorY();
    }
    size_t painted = 0u;
    const size_t cap = (g_favorites_list_.size() < static_cast<size_t>(FAV_STRIP_VISIBLE))
                        ? g_favorites_list_.size()
                        : static_cast<size_t>(FAV_STRIP_VISIBLE);
    for (size_t fi = 0u; fi < cap; ++fi) {
        const FavoriteEntry &fav = g_favorites_list_[fi];
        const size_t idx = ResolveFavoriteToIconIdx(icons_, icon_count_, fav);
        if (idx >= icon_count_) {
            // Stale favorite — entry was uninstalled. Skip painting; touch
            // path emits a toast and prunes when the user taps it. Painting
            // a placeholder ring would be confusing UX.
            continue;
        }
        // Cell origin within the strip.
        const s32 cell_x = FAV_STRIP_LEFT
            + static_cast<s32>(painted) * FAV_TILE_SPACING + x;
        const s32 cell_y = FAV_STRIP_TOP + y;
        NroEntry &entry = icons_[idx];
        // Bug #4 (v1.8): highlight the D-pad focused strip slot.
        // `painted` is the paint-position index (0..FAV_STRIP_VISIBLE-1) which
        // corresponds 1:1 with fav_strip_focus_index_ when the strip is active.
        // v1.8 Input-source latch: D-pad focus ring only shown in DPAD mode.
        const bool dpad_focused  = (active_input_source_ == InputSource::DPAD)
                                && (fav_strip_focus_index_ != SIZE_MAX)
                                && (fav_strip_focus_index_ == painted);
        const bool mouse_hovered =
            (cursor_x >= cell_x && cursor_x < cell_x + ICON_BG_W
             && cursor_y >= cell_y && cursor_y < cell_y + ICON_BG_H);
        PaintIconCell(r, entry, idx, cell_x, cell_y,
                      dpad_focused, mouse_hovered);
        ++painted;
    }
}

size_t QdDesktopIconsElement::HitTestFavorites(s32 tx, s32 ty) const {
    // Vertical bounds: ICON_BG_H (130 px) not ICON_CELL_H (168 px).
    // ICON_CELL_H includes 38 px of under-label space; touch on that gap was
    // mis-routing to HitTestFavorites instead of falling through.  v1.8.2 fix.
    if (ty < FAV_STRIP_TOP || ty >= FAV_STRIP_TOP + ICON_BG_H) {
        return SIZE_MAX;
    }
    // Horizontal bounds on the strip overall.
    if (tx < FAV_STRIP_LEFT
            || tx >= FAV_STRIP_LEFT
                     + static_cast<s32>(FAV_STRIP_VISIBLE) * FAV_TILE_SPACING) {
        return SIZE_MAX;
    }
    // Resolve to a paint-position; iterate live list to skip stale entries
    // (mirrors PaintFavoritesStrip's painted-counter advance).
    size_t painted = 0u;
    const size_t cap = (g_favorites_list_.size() < static_cast<size_t>(FAV_STRIP_VISIBLE))
                        ? g_favorites_list_.size()
                        : static_cast<size_t>(FAV_STRIP_VISIBLE);
    for (size_t fi = 0u; fi < cap; ++fi) {
        const FavoriteEntry &fav = g_favorites_list_[fi];
        const size_t idx = ResolveFavoriteToIconIdx(icons_, icon_count_, fav);
        if (idx >= icon_count_) {
            continue;
        }
        const s32 cell_x = FAV_STRIP_LEFT
            + static_cast<s32>(painted) * FAV_TILE_SPACING;
        if (tx >= cell_x && tx < cell_x + ICON_BG_W) {
            return idx;
        }
        ++painted;
    }
    return SIZE_MAX;
}

// ── Click tolerance ───────────────────────────────────────────────────────────
// Maximum Euclidean pixel displacement (squared comparison to avoid sqrt) from
// TouchDown to TouchUp that still classifies as a click rather than a drag.
// 24 px in 1920×1080 layout space ≈ 16 px on the physical 1280×720 panel.
static constexpr s32 CLICK_TOLERANCE_PX = 24;

// ── Built-in dock icon table ──────────────────────────────────────────────────
// 6 entries: { display_name, glyph_char, r, g, b }
// Verbatim from desktop_icons.rs BUILTIN_ICONS.
struct BuiltinIconDef {
    const char *name;
    char        glyph;
    u8          r, g, b;
};

// Cycle K-noextras: Terminal AND VaultSplit dropped per creator decision —
// VaultSplit was a duplicate Vault dispatcher with no unique behavior; gone.
// Cycle K-TrackD: AllPrograms (QdLaunchpad) added as slot 4.
// PopulateBuiltins assigns dock_slot = i, so the 5 slots are:
// Vault=0, Monitor=1, Control=2, About=3, AllPrograms=4.
// Dispatch switch mirrors this.
static constexpr BuiltinIconDef BUILTIN_ICON_DEFS[BUILTIN_ICON_COUNT] = {
    { "Vault",       'V', 0x7D, 0xD3, 0xFC },
    { "Monitor",     'M', 0x4A, 0xDE, 0x80 },
    { "Control",     'C', 0x4A, 0xDE, 0x80 },
    { "About",       'A', 0xF8, 0x71, 0x71 },
    { "AllPrograms", 'P', 0xA7, 0x8B, 0xFA },
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdDesktopIconsElement::QdDesktopIconsElement(const QdTheme &theme)
    : theme_(theme), icon_count_(0), dpad_focus_index_(0),
      // Bug #4 fix (v1.8): not in favorites strip mode at construction.
      fav_strip_focus_index_(SIZE_MAX),
      mouse_hover_index_(SIZE_MAX),
      prev_magnify_center_(-1), magnify_center_(-1), frame_tick_(0),
      app_entry_start_idx_(0),
      pressed_(false), down_x_(0), down_y_(0),
      last_touch_x_(0), last_touch_y_(0),
      down_idx_(MAX_ICONS), was_touch_active_last_frame_(false),
      // v1.7.0-stabilize-2 (REC-02): default to cursor mode at boot. Cursor
      // is positioned at screen centre on entry; D-pad has not been pressed.
      // Hover ring is enabled until the first D-pad/A press flips the flag.
      last_input_was_dpad_(false),
      // v1.8 Input-source latch: DPAD is the natural boot default on Switch,
      // but we set MOUSE (same as last_input_was_dpad_=false above) so the
      // hover ring is shown immediately and cursor is visible at first render.
      // First D-pad press will flip it to DPAD and hide the cursor.
      active_input_source_(InputSource::MOUSE),
      prev_cursor_x_(-1),
      prev_cursor_y_(-1),
      cursor_ref_(nullptr),
      // Task 9 (v1.8): dev windows start null; constructed below.
      nxlink_win_(nullptr),
      usb_win_(nullptr),
      log_win_(nullptr),
      // Task 9 (v1.8): popup hidden at boot.
      dev_popup_open_(false),
      // v1.8.15 Fix B: prewarm thread starts stopped; thread is not joinable
      // until SpawnPrewarmThread() assigns it from OnRender's first-call branch.
      prewarm_stop_(false)
{
    UL_LOG_INFO("qdesktop: QdDesktopIconsElement ctor entry");

    // Initialise dock-slot hit-test rect cache with neutral (no-magnify) positions
    // so HitTest returns correct results even before the first OnRender call.
    // Neutral positions mirror the OnRender two-pass layout with scale=100 (ICON_BG_W):
    //   total_expanded_w = BUILTIN_ICON_COUNT * ICON_BG_W + (BUILTIN_ICON_COUNT-1) * ICON_GRID_GAP_X
    //                    = 5*140 + 4*28 = 812  (K-TrackD: 5 slots)
    //   expanded_start_x = (1920 - 812) / 2 = 554
    //   slot i x = 554 + i * (ICON_BG_W + ICON_GRID_GAP_X)
    {
        static constexpr s32 kNeutralTotalW =
            static_cast<s32>(BUILTIN_ICON_COUNT) * ICON_BG_W
            + (static_cast<s32>(BUILTIN_ICON_COUNT) - 1) * ICON_GRID_GAP_X;
        static constexpr s32 kNeutralStartX = (1920 - kNeutralTotalW) / 2;
        for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
            dock_slot_x_[i] = kNeutralStartX
                + static_cast<s32>(i) * (ICON_BG_W + ICON_GRID_GAP_X);
            dock_slot_w_[i] = ICON_BG_W;
        }
    }

    // Initialise per-slot cached textures.  std::array<T*, N> default-
    // constructs to indeterminate pointer values, so we explicitly null them.
    for (size_t i = 0; i < MAX_ICONS; ++i) {
        name_text_tex_[i]  = nullptr;
        glyph_text_tex_[i] = nullptr;
        icon_tex_[i]        = nullptr;
        // Dimension cache — zeroed until lazy-create writes real values.
        name_text_w_[i]   = 0;
        name_text_h_[i]   = 0;
        glyph_text_w_[i]  = 0;
        glyph_text_h_[i]  = 0;
        // v1.7.0-stabilize-2 (REC-03 option B): no slot has a fallback PNG yet,
        // so nothing is replaceable at boot. The flag flips to true the first
        // time PaintIconCell installs a Default*.png fallback.
        texture_replaceable_[i] = false;
        // v1.8.19: initial render state is Unknown; PaintIconCell will classify
        // on first paint and set the correct state for future O(1) dispatch.
        slot_render_state_[i] = CellRenderState::Unknown;
    }

    // Ensure icon cache dir exists before any icon I/O (shared singleton).
    const bool cache_dir_ok = GetSharedIconCache().EnsureDir();
    UL_LOG_INFO("qdesktop: icon cache dir ensure result = %d", cache_dir_ok ? 1 : 0);

    // Fix D (v1.6.12): clear the auto-folder side table before any scan so stale
    // entries from a previous enumeration (e.g. re-init after SD remount) do not
    // accumulate.  RegisterClassification() is called by each Scan/Populate function
    // below immediately after constructing each entry.
    ClearClassifications();

    // Pre-populate built-in dock icons, then scan NRO files, then payloads.
    PopulateBuiltins();
    const size_t after_builtins = icon_count_;
    ScanNros();
    const size_t after_nros = icon_count_;
    ScanPayloads(); // Fix C (v1.6.12): Hekate payload entries with creator icons
    const size_t after_scan = icon_count_;

    // Record where Application entries will begin.  SetApplicationEntries() truncates
    // icon_count_ back to this index and re-appends on every call.
    app_entry_start_idx_ = icon_count_;

    UL_LOG_INFO("qdesktop: builtins=%zu nros=%zu payloads=%zu app_slot_start=%zu total_static=%zu",
                after_builtins, after_nros - after_builtins,
                after_scan - after_nros, app_entry_start_idx_, after_scan);

    // Task 9 (v1.8): create dev-popup panel instances.  Positions are set
    // relative to the top-right corner; stacked vertically with 8 px gaps.
    // NxlinkWindow: 480×260; UsbSerialWindow: 480×260; LogFlushWindow: 480×180.
    // Panels are positioned so their RIGHT edge is at x=1912 (8 px inset from
    // the right screen edge) and the TOP edge of the topmost panel is at y=40
    // (just below the top-right hot-zone strip).
    static constexpr s32 kDevPopupRightEdge = 1912;
    static constexpr s32 kDevPopupTopY      = 40;
    static constexpr s32 kDevPopupGap       = 8;
    nxlink_win_ = QdNxlinkWindow::New(theme_);
    usb_win_    = QdUsbSerialWindow::New(theme_);
    log_win_    = QdLogFlushWindow::New(theme_);
    {
        const s32 nx_x  = kDevPopupRightEdge - QdNxlinkWindow::PANEL_W;
        const s32 nx_y  = kDevPopupTopY;
        const s32 usb_x = kDevPopupRightEdge - QdUsbSerialWindow::PANEL_W;
        const s32 usb_y = nx_y + QdNxlinkWindow::PANEL_H + kDevPopupGap;
        const s32 log_x = kDevPopupRightEdge - QdLogFlushWindow::PANEL_W;
        const s32 log_y = usb_y + QdUsbSerialWindow::PANEL_H + kDevPopupGap;
        nxlink_win_->SetPos(nx_x, nx_y);
        usb_win_->SetPos(usb_x, usb_y);
        log_win_->SetPos(log_x, log_y);
    }
    UL_LOG_INFO("qdesktop: dev popup windows created (nxlink/usb/logflush)");
}

QdDesktopIconsElement::~QdDesktopIconsElement() {
    // v1.8.15 Fix B: Signal the background prewarm thread to stop, then join
    // it before any member destruction.  The atomic write is visible to the
    // thread immediately (no cache_mutex_ needed for the flag itself).
    // Join guarantees the thread has released its `this` pointer before
    // cache_ and icon_tex_[] are freed below.
    prewarm_stop_ = true;
    if (prewarm_thread_.joinable()) {
        prewarm_thread_.join();
    }

    // Free cached name/glyph text textures (created lazily in PaintIconCell).
    // SDL_DestroyTexture is null-safe, but we guard explicitly for clarity.
    for (size_t i = 0; i < MAX_ICONS; ++i) {
        FreeCachedText(i);
    }
    // Cycle I: free the rounded-bg mask texture (built lazily in PaintIconCell).
    if (round_bg_tex_ != nullptr) {
        SDL_DestroyTexture(round_bg_tex_);
        round_bg_tex_ = nullptr;
    }
    // A-2a (v1.7.2): free the process-lifetime folder-grid background texture.
    // g_desktop_folder_bg_tex is a file-scope static lazy-loaded in
    // PaintDesktopFolders; it is never freed elsewhere, so we own it here.
    if (g_desktop_folder_bg_tex != nullptr) {
        SDL_DestroyTexture(g_desktop_folder_bg_tex);
        g_desktop_folder_bg_tex = nullptr;
    }
    // A-2b (v1.7.2): free the per-folder name-label text textures.
    // g_desktop_folder_name_tex[kDesktopFolderCount] entries are lazily built
    // in PaintDesktopFolders (one per folder slot) and never freed elsewhere.
    // Dimension caches are zeroed alongside the pointer so a subsequent
    // PaintDesktopFolders call (if any) rebuilds them from scratch.
    for (size_t fi = 0; fi < kDesktopFolderCount; ++fi) {
        if (g_desktop_folder_name_tex[fi] != nullptr) {
            pu::ui::render::DeleteTexture(g_desktop_folder_name_tex[fi]);
            g_desktop_folder_name_w[fi] = 0;
            g_desktop_folder_name_h[fi] = 0;
        }
    }
}

// ── FreeCachedText ─────────────────────────────────────────────────────────────
// Resets the cached name + glyph text slots for one icon so a subsequent paint
// will re-rasterise.  v1.8.2 LRU fix: name_text_tex_ and glyph_text_tex_ are
// LRU cache-owned pointers returned by RenderText/RenderTextAutoFit.  Calling
// SDL_DestroyTexture on them is a double-free.  Just null the pointers so the
// next paint triggers a re-render via the lazy-cache path.
void QdDesktopIconsElement::FreeCachedText(size_t entry_idx) {
    if (entry_idx >= MAX_ICONS) {
        return;
    }
    // LRU-owned — do NOT call SDL_DestroyTexture.  Null to force re-render.
    name_text_tex_[entry_idx]  = nullptr;
    name_text_w_[entry_idx]    = 0;
    name_text_h_[entry_idx]    = 0;
    glyph_text_tex_[entry_idx] = nullptr;
    glyph_text_w_[entry_idx]   = 0;
    glyph_text_h_[entry_idx]   = 0;
    // Also free the cached icon BGRA texture for this slot.
    // This is the texture that was previously allocated every frame; it is
    // now allocated once here (lazily in PaintIconCell) and freed here on slot
    // reset, ensuring the GPU pool sees only one allocation per active icon.
    if (icon_tex_[entry_idx] != nullptr) {
        SDL_DestroyTexture(icon_tex_[entry_idx]);
        icon_tex_[entry_idx] = nullptr;
    }
    // v1.7.0-stabilize-2 (REC-03 option B): clear the replaceable-flag so the
    // next paint of this slot starts from a known state. Either:
    //   - the slot will be rebuilt from BGRA on the next frame and the flag
    //     stays false (real icon path), or
    //   - PaintIconCell will install a Default*.png fallback again and flip
    //     the flag back to true. Either way, the slot's "needs replacement"
    //     state is recomputed from scratch.
    if (entry_idx < MAX_ICONS) {
        texture_replaceable_[entry_idx] = false;
        // v1.8.19: reset CellRenderState so PaintIconCell re-classifies on
        // the next paint after the slot is recycled for a new entry.
        slot_render_state_[entry_idx] = CellRenderState::Unknown;
    }
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdDesktopIconsElement::AdvanceTick() {
    static bool logged_once = false;
    if (!logged_once) {
        UL_LOG_INFO("qdesktop: AdvanceTick first call (F-06 tick site active)");
        logged_once = true;
    }
    GetSharedIconCache().AdvanceTick();
    ++frame_tick_;
}

// ── UpdateDockMagnify ─────────────────────────────────────────────────────────
// SP2-F12: dock proximity zone uses 30px (×1.5 from Rust 20px).
// SP2-F13: magnify_center_ = -1 when cursor is outside the dock zone.
//
// Dock layout constants (×1.5 from Rust):
//   SCREEN_H = 1080, DOCK_H = 108 → dock_nominal_top = 1080 - 108 = 972
//   DOCK_SLOT_SIZE = 84, DOCK_SLOT_GAP = 18
//   Proximity threshold: DOCK_SLOT_SIZE + DOCK_SLOT_GAP = 102
//
// Magnify scale table (×100 fixed-point, mirrors wm.rs):
//   dist 0 → 140, dist 1 → 120, dist 2 → 105, dist ≥3 → 100

// Cycle J-tweak2: trimmed 168 → 148 so a 5th icon row fits above the dock.
// Dock backdrop now occupies y=932..1080. Dock slot icons (84 px after the
// auto-centering math) still sit comfortably inside the 148-px band, and
// horizontal rebalancing is unaffected because it scales off ICON_BG_W.
//
// These file-local constants shadow the qd_WmConstants.hpp values to avoid the
// ODR conflict when qd_WmConstants.hpp is pulled in transitively through
// ui_MenuApplication.hpp → qd_AboutLayout.hpp.
// v1.8.3 B35: revert kDockH from 108 back to 148.  The v1.8.2 shrink to 108
// was intended to align the input dead zone with the visual dock band, but it
// broke tap input — the 40 px gap between kDockNominalTop and the top of the
// rendered dock pixels caused misses on the topmost dock icon row.  148 is the
// original value that was HW-confirmed through v1.7.0-stable.
static constexpr int32_t kDockH           = 148;
static constexpr int32_t kDockNominalTop  = 1080 - kDockH;  // 932
static constexpr int32_t kDockSlotSize    = 84;     // ×1.5 from Rust 56
static constexpr int32_t kDockSlotGap     = 18;     // ×1.5 from Rust 12
static constexpr int32_t kDockProxZone    = 30;     // SP2-F12: ×1.5 from Rust 20
static constexpr int32_t kDockProxThresh  = kDockSlotSize + kDockSlotGap; // 102

void QdDesktopIconsElement::UpdateDockMagnify(int32_t cursor_y) {
    prev_magnify_center_ = magnify_center_;

    // Cursor must be in or near the dock zone.
    if (cursor_y < kDockNominalTop - kDockProxZone) {
        magnify_center_ = -1;
        return;
    }

    // Find the nearest builtin dock slot by horizontal centre.
    // Built-in slots 0..BUILTIN_ICON_COUNT are the dock icons.
    // For simplicity we treat them as evenly spaced starting from ICON_GRID_LEFT.
    // The dock centre of slot i = ICON_GRID_LEFT + i * (DOCK_SLOT_SIZE + DOCK_SLOT_GAP)
    //                             + DOCK_SLOT_SIZE / 2.
    // (We do not need cursor_x here — SP2 magnify is proximity-by-slot-index only,
    //  matching the Rust wm.rs cursor_y-only proximity check for dock zone entry.)

    // No cursor_x available in AdvanceTick (input-driven context), so we use the
    // nearest slot determination based purely on dock-zone entry for SP2.
    // The Rust wm.rs magnify logic picks the nearest slot to cursor_x when in zone.
    // Since UpdateDockMagnify is called without cursor_x in SP2, we keep the last
    // focused dock slot as the magnify center, defaulting to slot 0 on first entry.
    // Full cursor_x wiring is deferred to SP3 (input method refactor).

    if (magnify_center_ == -1) {
        // Entering dock zone: start at slot 0.
        magnify_center_ = 0;
    }
    // magnify_center_ persists across frames while cursor_y is in zone.
}

// Returns magnify scale ×100 for dock slot `slot_idx` given current magnify center.
// dist 0→140, 1→120, 2→105, else→100.
static int32_t dock_magnify_scale_x100(int32_t slot_idx, int32_t magnify_center) {
    if (magnify_center < 0) {
        return 100;
    }
    const int32_t dist = (slot_idx >= magnify_center)
                       ? (slot_idx - magnify_center)
                       : (magnify_center - slot_idx);
    switch (dist) {
        case 0: return 140;
        case 1: return 120;
        case 2: return 105;
        default: return 100;
    }
}

// ── PopulateBuiltins ─────────────────────────────────────────────────────────

void QdDesktopIconsElement::PopulateBuiltins() {
    for (size_t i = 0; i < BUILTIN_ICON_COUNT; ++i) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        NroEntry &e = icons_[icon_count_];
        // Copy display name (guaranteed to fit in 64 bytes).
        strncpy(e.name, BUILTIN_ICON_DEFS[i].name, sizeof(e.name) - 1u);
        e.name[sizeof(e.name) - 1u] = '\0';
        e.glyph        = BUILTIN_ICON_DEFS[i].glyph;
        e.bg_r         = BUILTIN_ICON_DEFS[i].r;
        e.bg_g         = BUILTIN_ICON_DEFS[i].g;
        e.bg_b         = BUILTIN_ICON_DEFS[i].b;
        e.nro_path[0]  = '\0';
        e.icon_path[0] = '\0';
        e.is_builtin   = true;
        e.dock_slot    = static_cast<u8>(i);
        e.category     = NroCategory::QosApp;
        e.icon_category = IconCategory::Builtin;
        e.icon_loaded  = false;
        e.kind         = IconKind::Builtin;
        e.app_id       = 0;
        e.special_subtype = 0;
        // Fix D (v1.6.12): stable ID for builtin entries is "builtin:<name>".
        {
            char stable_id[80];
            snprintf(stable_id, sizeof(stable_id), "builtin:%s",
                     BUILTIN_ICON_DEFS[i].name);
            g_entry_classification_[stable_id] = ClassifyKind::Builtin;
            RegisterClassification(stable_id, ClassifyKind::Builtin);
        }
        ++icon_count_;
    }
}

// ── ScanNros ─────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::ScanNros() {
    // A-3 (v1.7.2): clear the local NRO classification side table before each
    // scan pass so entries for NRO files that have been deleted since the last
    // scan do not accumulate as zombies.  Without this clear, a deleted NRO
    // retains its ClassifyKind entry indefinitely, causing GetAutoFolderKind()
    // to return a stale bucket for a stable_id that no longer maps to any
    // live icon slot.
    g_entry_classification_.clear();

    DIR *d = opendir("sdmc:/switch/");
    if (!d) {
        return; // SD not mounted or directory absent — silently skip.
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        const char *fname = ent->d_name;

        // Skip hidden files (leading dot).
        if (fname[0] == '.') {
            continue;
        }

        // Skip non-.nro files.
        const size_t flen = strlen(fname);
        if (flen < 5u) { // ".nro" + at least 1 char
            continue;
        }
        if (strcmp(fname + flen - 4u, ".nro") != 0) {
            continue;
        }

        // Build the full path "sdmc:/switch/<fname>".
        char nro_path[768];
        int written = snprintf(nro_path, sizeof(nro_path), "sdmc:/switch/%s", fname);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(nro_path)) {
            continue; // Path too long — skip.
        }

        // Build the display name: stem = fname without ".nro".
        char stem[64];
        const size_t stem_len = flen - 4u; // strip ".nro"
        if (stem_len == 0u || stem_len >= sizeof(stem)) {
            continue;
        }
        memcpy(stem, fname, stem_len);
        stem[stem_len] = '\0';

        // Classify: no NACP name available at scan time — pass empty string.
        CategoryResult cat = Classify("", stem);

        NroEntry &e = icons_[icon_count_];
        // snprintf always null-terminates and silences -Wstringop-truncation.
        snprintf(e.name,     sizeof(e.name),     "%s", stem);
        e.glyph        = cat.glyph;
        e.bg_r         = cat.r;
        e.bg_g         = cat.g;
        e.bg_b         = cat.b;
        snprintf(e.nro_path, sizeof(e.nro_path), "%s", nro_path);
        e.icon_path[0] = '\0';
        e.is_builtin   = false;
        e.dock_slot    = 0;
        e.category     = cat.category;
        e.icon_category = IconCategory::Homebrew;
        e.icon_loaded  = false;
        e.kind         = IconKind::Nro;
        e.app_id       = 0;
        e.special_subtype = 0;
        // Fix D (v1.6.12): classify into the auto-folder side table.
        // Stable ID for NRO entries is the nro_path string.
        {
            const ClassifyKind ck = ClassifyNroAutoFolder(e);
            g_entry_classification_[nro_path] = ck;
            RegisterClassification(nro_path, ck);
        }
        ++icon_count_;
    }

    closedir(d);

    // v1.6.11 Fix 2: also scan sdmc:/ (SD root) for top-level NRO files such as
    // hbmenu.nro and uManager.nro.  The original loop only covers sdmc:/switch/,
    // so SD-root NROs were never added to icons_[] and their ExtractNroIcon call
    // was never reached -- they always fell through to the neutral-gray fallback.
    DIR *d_root = opendir("sdmc:/");
    if (d_root) {
        struct dirent *ent_root;
        while ((ent_root = readdir(d_root)) != nullptr) {
            if (icon_count_ >= MAX_ICONS) {
                break;
            }
            const char *fname = ent_root->d_name;

            // Skip hidden / dot entries.
            if (fname[0] == '.') {
                continue;
            }

            // Accept only files whose names end in ".nro".
            const size_t flen = strlen(fname);
            if (flen < 5u) { // minimum: "x.nro"
                continue;
            }
            if (strcmp(fname + flen - 4u, ".nro") != 0) {
                continue;
            }

            // Full path: "sdmc:/<fname>"
            char nro_path[768];
            int written = snprintf(nro_path, sizeof(nro_path), "sdmc:/%s", fname);
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(nro_path)) {
                continue; // Path too long -- skip.
            }

            // Display stem: filename without the ".nro" suffix.
            char stem[64];
            const size_t stem_len = flen - 4u;
            if (stem_len == 0u || stem_len >= sizeof(stem)) {
                continue;
            }
            memcpy(stem, fname, stem_len);
            stem[stem_len] = '\0';

            // Classify by stem name (no NACP available at scan time).
            CategoryResult cat = Classify("", stem);

            NroEntry &e = icons_[icon_count_];
            snprintf(e.name,     sizeof(e.name),     "%s", stem);
            e.glyph        = cat.glyph;
            e.bg_r         = cat.r;
            e.bg_g         = cat.g;
            e.bg_b         = cat.b;
            snprintf(e.nro_path, sizeof(e.nro_path), "%s", nro_path);
            e.icon_path[0] = '\0';
            e.is_builtin   = false;
            e.dock_slot    = 0;
            e.category     = cat.category;
            e.icon_category = IconCategory::Homebrew;
            e.icon_loaded  = false;
            e.kind         = IconKind::Nro;
            e.app_id       = 0;
            e.special_subtype = 0;
            // Fix D (v1.6.12): side table for auto-folder classification.
            {
                const ClassifyKind ck = ClassifyNroAutoFolder(e);
                g_entry_classification_[nro_path] = ck;
                RegisterClassification(nro_path, ck);
            }
            ++icon_count_;
        }
        closedir(d_root);
    }
}

// ── ScanPayloads (Fix C, v1.6.12) ────────────────────────────────────────────
// Scan sdmc:/bootloader/payloads/ for *.bin files and add each one as a
// Payloads-category NroEntry.  icon_path is resolved via ResolvePayloadIcon()
// so any creator-supplied .jpg/.bmp/.png is used for that payload's icon cell.
// If no icon file is found, icon_path is left empty and the generic glyph/bg is
// used.  This function is called once from Initialize().

void QdDesktopIconsElement::ScanPayloads() {
    DIR *d = opendir("sdmc:/bootloader/payloads/");
    if (!d) {
        UL_LOG_INFO("qdesktop: ScanPayloads: sdmc:/bootloader/payloads/ not found"
                    " -- no payload entries added");
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }
        const char *fname = ent->d_name;

        // Skip hidden files.
        if (fname[0] == '.') {
            continue;
        }

        // Skip files that don't end in ".bin" (Hekate payload extension).
        const size_t flen = strlen(fname);
        if (flen < 5u) { // ".bin" + at least 1 char
            continue;
        }
        if (strcmp(fname + flen - 4u, ".bin") != 0) {
            continue;
        }

        NroEntry &e = icons_[icon_count_];

        // Display name: filename without the ".bin" suffix, truncated to 63 chars.
        const size_t stem_len = flen - 4u;
        const size_t copy_len = stem_len < (sizeof(e.name) - 1u)
                              ? stem_len : (sizeof(e.name) - 1u);
        memcpy(e.name, fname, copy_len);
        e.name[copy_len] = '\0';

        // Glyph: 'P' (Payload indicator visible when no icon is loaded).
        e.glyph  = 'P';
        // Background colour: dark amber (#4A3800) — distinct from NRO green.
        e.bg_r   = 0x4A;
        e.bg_g   = 0x38;
        e.bg_b   = 0x00;

        // No NRO body — this is a .bin payload.
        e.nro_path[0] = '\0';

        // Resolve creator-supplied icon; leave empty if none found.
        const std::string resolved = ResolvePayloadIcon(fname);
        if (!resolved.empty()) {
            strncpy(e.icon_path, resolved.c_str(), sizeof(e.icon_path) - 1u);
            e.icon_path[sizeof(e.icon_path) - 1u] = '\0';
        } else {
            e.icon_path[0] = '\0';
        }

        e.is_builtin      = false;
        e.dock_slot       = 0xFF;             // not a dock item
        e.category        = NroCategory::QosApp; // best fit for sort
        e.icon_category   = IconCategory::Extras;   // v1.8.10: Payloads removed from enum; payloads → Extras
        e.icon_loaded     = false;
        e.kind            = IconKind::Special; // no ASET, custom launch path
        e.app_id          = 0;
        e.special_subtype = 0;

        // Fix D (v1.6.12): register payload in the auto-folder side table.
        // Stable ID for payload entries is "payload:<fname>" (fname includes .bin).
        {
            char stable_id[72];
            snprintf(stable_id, sizeof(stable_id), "payload:%s", fname);
            g_entry_classification_[stable_id] = ClassifyKind::Payload;
            RegisterClassification(stable_id, ClassifyKind::Payload);
        }

        ++icon_count_;
    }
    closedir(d);

    UL_LOG_INFO("qdesktop: ScanPayloads: done, icon_count_ now %zu", icon_count_);
}

// ── HashToColor ───────────────────────────────────────────────────────────────
// u32 DJB2 on first 16 bytes of name → HSL(hue, 0.55, 0.40) → RGB.
// Mirrors desktop_icons.rs hash_to_color + hsl_to_rgb exactly.

// static
void QdDesktopIconsElement::HashToColor(const char *name,
                                         u8 &out_r, u8 &out_g, u8 &out_b)
{
    u32 h = 5381u;
    const u8 *p = reinterpret_cast<const u8 *>(name);
    for (size_t i = 0u; i < 16u && p[i] != '\0'; ++i) {
        h = h * 33u + static_cast<u32>(p[i]);
    }
    const u32 hue_deg = h % 360u;

    // Delegate to qd_NroAsset.hpp HSL→RGB which uses the identical formula.
    HslToRgb(hue_deg, 0.55f, 0.40f, out_r, out_g, out_b);
}

// ── CellRect ─────────────────────────────────────────────────────────────────

bool QdDesktopIconsElement::CellRect(size_t i, s32 &out_x, s32 &out_y) const {
    if (i >= icon_count_) {
        return false;
    }
    const size_t col = i % static_cast<size_t>(ICON_GRID_COLS);
    const size_t row = i / static_cast<size_t>(ICON_GRID_COLS);
    if (row >= static_cast<size_t>(ICON_GRID_MAX_ROWS)) {
        return false;
    }
    out_x = ICON_GRID_LEFT
            + static_cast<s32>(col) * (ICON_CELL_W + ICON_GRID_GAP_X);
    out_y = ICON_GRID_TOP
            + static_cast<s32>(row) * ICON_CELL_H;
    return true;
}

// ── HitTest ───────────────────────────────────────────────────────────────────

size_t QdDesktopIconsElement::HitTest(s32 tx, s32 ty) const {
    for (size_t i = 0u; i < icon_count_; ++i) {
        const auto &entry = icons_[i];
        s32 cx, cy, cw, ch;
        if (entry.is_builtin && i < BUILTIN_ICON_COUNT) {
            // Dock slot — use the per-frame cached rect that matches the
            // current magnify state.  dock_slot_x_[]/dock_slot_w_[] are
            // written by OnRender each frame before OnInput is called.
            cx = dock_slot_x_[i];
            cy = kDockNominalTop;
            cw = dock_slot_w_[i];
            ch = kDockH;
        } else {
            // Grid icon — use the nominal CellRect.
            if (!CellRect(i, cx, cy)) {
                continue;
            }
            cw = ICON_CELL_W;
            ch = ICON_CELL_H;
        }
        if (tx >= cx && ty >= cy && tx < cx + cw && ty < cy + ch) {
            return i;
        }
    }
    return MAX_ICONS; // sentinel: no hit
}

// ── HitTestDesktop ───────────────────────────────────────────────────────────
// Unified hit-test against the Slice 4 desktop layout (folders 0..5 + dock
// 6..10).  The icon-grid path is gone — every hit is either a folder, a
// dock cell, or no hit at all.  Favorites strip hit-testing is separate
// (HitTestFavorites) so the strip can take priority over folder/dock for
// the small overlap region near y=622.

size_t QdDesktopIconsElement::HitTestDesktop(s32 tx, s32 ty) const {
    // ── 1. Folder grid (3×2) ─────────────────────────────────────────────────
    for (size_t fi = 0u; fi < kDesktopFolderCount; ++fi) {
        const SDL_Rect &fr = g_desktop_folder_rects[fi];
        if (tx >= fr.x && ty >= fr.y
                && tx < fr.x + fr.w && ty < fr.y + fr.h) {
            return fi;
        }
    }
    // ── 2. Dock (5 cells, magnify-aware) ─────────────────────────────────────
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        if (tx >= dock_slot_x_[i] && ty >= kDockNominalTop
                && tx < dock_slot_x_[i] + dock_slot_w_[i]
                && ty < kDockNominalTop + kDockH) {
            return kDesktopFolderCount + i;
        }
    }
    return SIZE_MAX;
}

// ── FillRoundRect ─────────────────────────────────────────────────────────────
// Software-rasterised rounded-corner filled rectangle.
//
// Algorithm — horizontal span fill, zero libm, no background-colour dependency:
//
//   The rect is divided into three bands:
//     [top arc band]   rows 0 .. radius-1   — width varies with arc
//     [middle band]    rows radius .. h-radius-1 — full width
//     [bottom arc band] rows h-radius .. h-1 — symmetric to top
//
//   For each row in an arc band we compute the half-width of the inscribed
//   circle at that row using the integer identity:
//     dy  = distance from the row to the nearest corner-centre row
//     span_half = floor(sqrt(r² - dy²))
//   We avoid sqrt by iterating dx from radius down to 0 and testing
//   dx*dx + dy*dy <= r*r (one comparison per candidate column, at most
//   radius iterations per row, O(radius²) total for both arc bands).
//
//   SDL_RenderFillRect is called once per scanline — ~2*radius + inner_height
//   rects total.  At radius=12, ICON_BG_H=144px, that is 12+120+12 = 144 rects,
//   well under 1 ms on Tegra X1.
//
// Parameters:
//   r          — SDL renderer
//   rect       — destination rectangle (includes rounded corners)
//   radius     — corner radius, pixels (auto-clamped to half of shorter side)
//   cr,cg,cb,ca — fill colour RGBA
static void FillRoundRect(SDL_Renderer *r, SDL_Rect rect, int radius,
                           u8 cr, u8 cg, u8 cb, u8 ca)
{
    // Clamp.
    const int half_w = rect.w / 2;
    const int half_h = rect.h / 2;
    if (radius > half_w) { radius = half_w; }
    if (radius > half_h) { radius = half_h; }

    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);

    if (radius <= 0) {
        SDL_RenderFillRect(r, &rect);
        return;
    }

    const int r2 = radius * radius;

    // Top arc band: row indices 0 .. radius-1 (relative to rect.y).
    for (int row = 0; row < radius; ++row) {
        // dy = distance from this row to the corner-centre row (= radius-1).
        const int dy = radius - 1 - row;
        // Find largest dx such that dx*dx + dy*dy <= r2.
        // Start from dx=radius and walk down until the condition holds.
        int dx = radius;
        while (dx > 0 && (dx * dx + dy * dy) > r2) {
            --dx;
        }
        // Span: from (rect.x + radius - dx) to (rect.x + rect.w - radius + dx - 1)
        const int span_x = rect.x + radius - dx;
        const int span_w = rect.w - 2 * (radius - dx);
        if (span_w > 0) {
            SDL_Rect span { span_x, rect.y + row, span_w, 1 };
            SDL_RenderFillRect(r, &span);
        }
    }

    // Middle band (full width, all rows between the two arc bands).
    const int mid_y = rect.y + radius;
    const int mid_h = rect.h - 2 * radius;
    if (mid_h > 0) {
        SDL_Rect mid { rect.x, mid_y, rect.w, mid_h };
        SDL_RenderFillRect(r, &mid);
    }

    // Bottom arc band: symmetric to top.
    for (int row = 0; row < radius; ++row) {
        const int dy = row;  // distance from corner-centre row (= rect.h - radius)
        int dx = radius;
        while (dx > 0 && (dx * dx + dy * dy) > r2) {
            --dx;
        }
        const int span_x = rect.x + radius - dx;
        const int span_w = rect.w - 2 * (radius - dx);
        if (span_w > 0) {
            SDL_Rect span { span_x, rect.y + rect.h - radius + row, span_w, 1 };
            SDL_RenderFillRect(r, &span);
        }
    }
}

// ── PaintIconCell ─────────────────────────────────────────────────────────────

// Renders one icon cell at grid-pixel position (x, y).
// Layout (C++ ×1.5 from Rust):
//   background rect: (x + ICON_BG_INSET, y + 6, ICON_BG_W, ICON_BG_H)
//   icon texture:    same rect, 64×64 BGRA scaled via SDL
//   focus ring:      stroke rect 1px outside background
//
// Hover: saturate-add 40 to each R/G/B channel.
// Text labels are out-of-scope for SP1 (no TTF font through raw SDL path).

void QdDesktopIconsElement::PaintIconCell(SDL_Renderer *r,
                                           const NroEntry &entry,
                                           size_t entry_idx,
                                           s32 x, s32 y,
                                           bool is_dpad_focused,
                                           bool is_mouse_hovered)
{
    // The background rect origin.
    // Rust reference: bg_x = x + ICON_BG_INSET; bg_y = y + 4 (at 1280×720).
    // C++ (×1.5): bg_y = y + 6.
    const s32 bg_x = x + ICON_BG_INSET;
    const s32 bg_y = y + 6;

    // Mouse hover drives the brightness lift; D-pad focus does not
    // (the focus ring makes selection visible without colour shift).
    const bool is_hovered = is_mouse_hovered;

    // F7 (stabilize-4): use neutral gray bg when a real icon texture is already
    // cached for this slot.  The colored bg comes from MakeFallbackIcon's
    // hash-derived palette (or the app control data's dominant color) and is
    // only meaningful as a visual placeholder before the icon JPEG loads.  Once
    // icon_tex_[entry_idx] is populated the icon covers 88% of the bg rect; the
    // visible colored strip at the inset edges distracts from cover art.  Use
    // the same #3A3A3A neutral gray that MakeFallbackIcon emits as its body —
    // it's consistent and invisible behind real icons.
    // Note: icon_tex_ is indexed by entry_idx but can only be read after the
    // size guard (entry_idx < MAX_ICONS); fall back to colored if out of range.
    const bool icon_ready = (entry_idx < MAX_ICONS && icon_tex_[entry_idx] != nullptr);
    const u8 base_r = icon_ready ? static_cast<u8>(0x3A) : entry.bg_r;
    const u8 base_g = icon_ready ? static_cast<u8>(0x3A) : entry.bg_g;
    const u8 base_b = icon_ready ? static_cast<u8>(0x3A) : entry.bg_b;

    // saturating_add(40) per Rust desktop_icons.rs hover path.
    const u8 fill_r = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_r) + 40))
        : base_r;
    const u8 fill_g = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_g) + 40))
        : base_g;
    const u8 fill_b = is_hovered
        ? static_cast<u8>(std::min(255, static_cast<int>(base_b) + 40))
        : base_b;

    // ── 1. Background rounded rectangle ───────────────────────────────────
    // Cycle I (boot-speed fix): use cached white rounded-rect mask texture
    // tinted via SDL_SetTextureColorMod. Replaces the per-frame FillRoundRect
    // call which issued 144 SDL_RenderFillRect ops per icon (17 icons × 144
    // × 60 Hz ≈ 147 K fills/sec ≈ 440 ms/sec on Tegra X1 — the boot
    // slowdown the user reported in the H-cycle).
    //
    // Lazy-build the mask texture on first paint; once cached, paint cost
    // collapses to: 1 × SDL_SetTextureColorMod + 1 × SDL_RenderCopy per icon
    // (17 icons × 60 Hz = ~1 020 calls/sec, about 0.7 ms/sec total).
    SDL_Rect bg_rect { bg_x, bg_y, ICON_BG_W, ICON_BG_H };
    if (round_bg_tex_ == nullptr) {
        round_bg_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          ICON_BG_W, ICON_BG_H);
        if (round_bg_tex_ != nullptr) {
            SDL_SetTextureBlendMode(round_bg_tex_, SDL_BLENDMODE_BLEND);
            SDL_Texture *prev_target = SDL_GetRenderTarget(r);
            if (SDL_SetRenderTarget(r, round_bg_tex_) == 0) {
                // Clear to fully transparent so the corner triangles stay
                // alpha=0 — that's what makes the icon edges round.
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0x00u);
                SDL_RenderClear(r);
                // Render rounded shape in WHITE so SDL_SetTextureColorMod
                // can tint it to any fill colour at no additional cost.
                SDL_Rect mask_rect { 0, 0, ICON_BG_W, ICON_BG_H };
                FillRoundRect(r, mask_rect, 12, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
                SDL_SetRenderTarget(r, prev_target);
                UL_LOG_INFO("qdesktop: round_bg_tex built %dx%d radius=12",
                            ICON_BG_W, ICON_BG_H);
            } else {
                // Render-target unsupported on this backend — drop the texture
                // and fall back to per-frame FillRoundRect (still correct,
                // just slower). Next paint will not retry.
                SDL_DestroyTexture(round_bg_tex_);
                round_bg_tex_ = nullptr;
                UL_LOG_WARN("qdesktop: round_bg_tex SetRenderTarget failed,"
                            " falling back to per-frame FillRoundRect");
            }
        }
    }
    if (round_bg_tex_ != nullptr) {
        SDL_SetTextureColorMod(round_bg_tex_, fill_r, fill_g, fill_b);
        SDL_RenderCopy(r, round_bg_tex_, nullptr, &bg_rect);
    } else {
        // Fallback path: per-frame software fill (only fires if render-target
        // creation failed once at boot — fail open rather than blank icons).
        FillRoundRect(r, bg_rect, 12, fill_r, fill_g, fill_b, 0xFFu);
    }

    // ── v1.8.19: Boolean dispatch — skip classification for stable states ───
    // CellRenderState::BgraReady  — icon_tex_[entry_idx] holds real BGRA art;
    //                               skip all classification, go directly to blit.
    // CellRenderState::SpecialPng — icon_tex_[entry_idx] holds a loaded PNG;
    //                               skip classification, go to blit.
    // CellRenderState::GlyphOnly  — no texture; go straight to glyph section.
    // CellRenderState::DefaultFallback — skip 2a/2c but recheck BGRA (2b/2b').
    // CellRenderState::Unknown    — run full classification below.
    const bool rs_stable_tex = (entry_idx < MAX_ICONS
        && (slot_render_state_[entry_idx] == CellRenderState::BgraReady
            || slot_render_state_[entry_idx] == CellRenderState::SpecialPng));
    const bool rs_glyph_only = (entry_idx < MAX_ICONS
        && slot_render_state_[entry_idx] == CellRenderState::GlyphOnly);
    const bool rs_default_fallback = (entry_idx < MAX_ICONS
        && slot_render_state_[entry_idx] == CellRenderState::DefaultFallback);

    // Hoist bgra + special_tex_ready so that goto targets below do not jump
    // over their initialisation (which would be ill-formed in C++).
    // They are populated by the classification sections when the state is
    // Unknown or DefaultFallback; for stable-tex fast paths they stay nullptr/false.
    const u8 *bgra = nullptr;
    bool special_tex_ready = false;

    // For stable-texture states jump directly past classification.
    // For glyph-only, skip ALL texture sections and jump to section 3.
    // For default-fallback, skip 2a and 2c; only run 2b/2b' below.
    if (rs_stable_tex) {
        // icon_tex_[entry_idx] is already populated; fall through to render.
        goto lbl_render_icon_tex;
    }
    if (rs_glyph_only) {
        goto lbl_render_glyph;
    }

    // ── 2a. Cycle J: Special-icon PNG lazy-load ──────────────────────────
    // The 8 SpecialEntry kinds (Settings/Album/Themes/Controllers/MiiEdit/
    // WebBrowser/UserPage/Amiibo) each have a PNG asset already shipped in
    // romfs at default/ui/Main/EntryIcon/<Name>.png. Previously these slots
    // rendered as colored squares because SetSpecialEntries left icon_path
    // empty and PaintIconCell only loaded textures for Application/NRO kinds.
    // This branch runs BEFORE the BGRA cache lookup so the existing render
    // block (lines below) blits whichever icon_tex_ we populate.
    // v1.8.19: skipped when rs_default_fallback (2a already ran on a prior frame).
    if (!rs_default_fallback
            && entry.kind == IconKind::Special && entry_idx < MAX_ICONS
            && icon_tex_[entry_idx] == nullptr) {
        using ET = ::ul::menu::EntryType;
        const char *asset_path = nullptr;
        switch (static_cast<ET>(entry.special_subtype)) {
            case ET::SpecialEntrySettings:    asset_path = "ui/Main/EntryIcon/Settings";    break;
            case ET::SpecialEntryAlbum:       asset_path = "ui/Main/EntryIcon/Album";       break;
            case ET::SpecialEntryThemes:      asset_path = "ui/Main/EntryIcon/Themes";      break;
            case ET::SpecialEntryControllers: asset_path = "ui/Main/EntryIcon/Controllers"; break;
            case ET::SpecialEntryMiiEdit:     asset_path = "ui/Main/EntryIcon/MiiEdit";     break;
            case ET::SpecialEntryWebBrowser:  asset_path = "ui/Main/EntryIcon/WebBrowser";  break;
            case ET::SpecialEntryAmiibo:      asset_path = "ui/Main/EntryIcon/Amiibo";      break;
            // SpecialEntryUserPage has no static asset (account avatar comes
            // from acc:: at runtime — defer until UserCard-style handling).
            default: break;
        }
        if (asset_path != nullptr) {
            icon_tex_[entry_idx] = ::ul::menu::ui::TryFindLoadImage(asset_path);
            if (icon_tex_[entry_idx] != nullptr) {
                UL_LOG_INFO("qdesktop: Special icon loaded subtype=%u path=%s",
                            static_cast<unsigned>(entry.special_subtype),
                            asset_path);
                // v1.8.19: transition to SpecialPng — stable; skip 2a next frame.
                slot_render_state_[entry_idx] = CellRenderState::SpecialPng;
            } else {
                UL_LOG_WARN("qdesktop: Special icon load FAILED subtype=%u path=%s"
                            " (active theme resource missing) — will fall back to glyph",
                            static_cast<unsigned>(entry.special_subtype),
                            asset_path);
            }
        }
    }

    // ── 2a-romfs. v1.8.21: Payload entries with a romfs-backed icon_path ──
    // ResolvePayloadIcon() returns "romfs:/default/ui/Main/PayloadIcon/<name>.png"
    // when the Q OS themed bundle PNG matches the payload stem, and when no
    // creator-supplied sdmc primary_path was found on the SD card.
    //
    // These romfs paths CANNOT go through LoadJpegIconToCache (sdmc-only IPC)
    // or the BGRA cache (populated only by LoadJpegIconToCache / ExtractNroIcon).
    // Load them here via pu::ui::render::LoadImageFromFile (POSIX IMG_Load,
    // which works on any device with a mounted filesystem including romfs).
    //
    // Guard: entry.kind == Special (payload entries always have kind=Special);
    //        icon_path starts with "romfs:/";
    //        icon_tex_[entry_idx] == nullptr (not yet loaded this slot);
    //        !rs_default_fallback (2a-romfs already ran on a prior frame → stable).
    if (!rs_default_fallback
            && entry.kind == IconKind::Special
            && entry_idx < MAX_ICONS
            && icon_tex_[entry_idx] == nullptr
            && entry.icon_path[0] == 'r' && entry.icon_path[1] == 'o'
            && entry.icon_path[2] == 'm' && entry.icon_path[3] == 'f'
            && entry.icon_path[4] == 's' && entry.icon_path[5] == ':') {
        icon_tex_[entry_idx] =
            ::pu::ui::render::LoadImageFromFile(entry.icon_path);
        if (icon_tex_[entry_idx] != nullptr) {
            UL_LOG_INFO("qdesktop: romfs payload icon loaded path=%s",
                        entry.icon_path);
            // Mark slot stable — 2a-romfs will be skipped on subsequent frames.
            slot_render_state_[entry_idx] = CellRenderState::SpecialPng;
        } else {
            UL_LOG_WARN("qdesktop: romfs payload icon FAILED path=%s"
                        " (romfs mount missing or asset not in romfs.bin?)",
                        entry.icon_path);
        }
    }

    // ── 2b. Cached icon texture blit ───────────────────────────────────────
    // Determine which path string to use as the cache key:
    //   - NRO entries    → nro_path  (ASET JPEG encoded per NRO spec)
    //   - Application    → icon_path (SD-card JPEG, may be empty)
    //   - Special PNG    → already loaded into icon_tex_[entry_idx] above
    //                      (Settings/Album/Themes/etc. via 2a TryFindLoadImage)
    //   - Special JPEG   → icon_path (creator-supplied payload icon, hbmenu/
    //                      uManager/Hekate; path was set by ScanPayloads or
    //                      qd_HekateIni; populated into the cache by the
    //                      lazy-load branch in OnRender)
    //   - Builtin        → no icon (nro_path and icon_path are both empty)
    if (entry.kind == IconKind::Application && entry.icon_path[0] != '\0') {
        // v1.8.18: use shared singleton + shared mutex.
        // Background prewarm thread may be writing to the cache concurrently.
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        bgra = GetSharedIconCache().Get(entry.icon_path);
    } else if (entry.kind == IconKind::Special && entry.icon_path[0] != '\0') {
        // v1.7.0-stabilize-2: Special entries with a JPEG icon_path go through
        // the BGRA cache, same as Applications. Without this branch ScanPayloads
        // and qd_HekateIni entries fell through to the gray-square fallback
        // because section 2a only handles romfs PNGs (special_subtype-keyed).
        // v1.8.18: shared singleton + shared mutex.
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        bgra = GetSharedIconCache().Get(entry.icon_path);
    } else if (entry.nro_path[0] != '\0') {
        // v1.8.18: shared singleton + shared mutex.
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        bgra = GetSharedIconCache().Get(entry.nro_path);
    }

    // Cycle J: also render when Special branch above populated icon_tex_
    // (bgra is nullptr for Special since they don't go through QdIconCache).
    // v1.8.19: special_tex_ready was hoisted before the goto dispatches; update it here.
    special_tex_ready = (entry.kind == IconKind::Special
                         && entry_idx < MAX_ICONS
                         && icon_tex_[entry_idx] != nullptr);

    // ── 2b'. v1.7.0-stabilize-2 (REC-03 option B): frame-race replacement ──
    // If we have a fallback PNG installed in icon_tex_[entry_idx] (the cold-
    // load path on a prior frame ran before BGRA was ready) AND the BGRA
    // cache has now populated for this slot, replace the texture in place.
    // Without this guard, the slot is permanently locked to the fallback.
    //
    // Note: Special slots are NOT replaceable -- their texture is the real
    // PNG (Settings.png, Album.png, etc.), not a fallback. We gate on
    // texture_replaceable_[entry_idx] which is only true after a Default*
    // PNG was installed in section 2c on a prior frame.
    if (bgra != nullptr
            && entry_idx < MAX_ICONS
            && texture_replaceable_[entry_idx]
            && icon_tex_[entry_idx] != nullptr) {
        UL_LOG_INFO("qdesktop: REC-03 frame-race replace slot=%zu kind=%u name='%s'"
                    " (default fallback -> real BGRA)",
                    entry_idx, static_cast<unsigned>(entry.kind), entry.name);
        SDL_DestroyTexture(icon_tex_[entry_idx]);
        icon_tex_[entry_idx] = nullptr;
        texture_replaceable_[entry_idx] = false;
        // The streaming-texture rebuild in section 2b below sees the null
        // pointer and rebuilds from BGRA on the same frame.
        // v1.8.19: state will be set to BgraReady below when the texture is built.
    }

    // ── 2c. Default-icon fallback (Cycle K-defaulticons) ──────────────────
    // When a non-Special entry has no BGRA in the cache (NRO with no embedded
    // icon, Application whose NS/JPEG load failed, or Builtin dock icon),
    // load a default PNG into icon_tex_[entry_idx] exactly once — so EVERY
    // entry displays a real image instead of a colored square + ASCII glyph.
    //
    // Priority chain:
    //   Nro          → DefaultHomebrew.png
    //   Application  → DefaultApplication.png
    //   Builtin      → DefaultApplication.png  (dock shortcuts look like apps)
    //   catch-all    → Empty.png
    //
    // This branch fires only when:
    //   (a) no BGRA is in the icon cache for this slot, AND
    //   (b) no Special PNG was already loaded (Special is handled in 2a), AND
    //   (c) icon_tex_[entry_idx] has not yet been populated (lazy, runs once).
    // v1.8.19: rs_default_fallback (state==DefaultFallback) means 2c already ran
    // on a prior frame; skip it to avoid repeated TryFindLoadImage calls.
    if (!rs_default_fallback
            && bgra == nullptr && !special_tex_ready
            && entry_idx < MAX_ICONS
            && icon_tex_[entry_idx] == nullptr) {
        // Cycle K-iconsfix: Builtin entries get a per-name Dock<Name>.png lookup
        // (e.g., "Vault" → "ui/Main/EntryIcon/DockVault") generated by the asset
        // pass.  If that PNG is missing on disk, drop through to the generic
        // DefaultApplication fallback so the icon is still rendered as art —
        // never a colored-square + ASCII glyph (creator directive: "EVERYTHING
        // uses Icons").  Application/Nro/* keep their per-kind fallbacks.
        bool builtin_handled = false;
        const char *fallback_path = nullptr;
        switch (entry.kind) {
            case IconKind::Nro:
                fallback_path = "ui/Main/EntryIcon/DefaultHomebrew";
                break;
            case IconKind::Application:
                fallback_path = "ui/Main/EntryIcon/DefaultApplication";
                break;
            case IconKind::Builtin: {
                // First try the slot-specific PNG: Dock<Name>.png.  This loop
                // is single-threaded UI render, so a function-static buffer is
                // safe and avoids a per-paint heap alloc.
                static char dock_path[128];
                snprintf(dock_path, sizeof(dock_path),
                         "ui/Main/EntryIcon/Dock%s", entry.name);
                icon_tex_[entry_idx] = ::ul::menu::ui::TryFindLoadImage(dock_path);
                if (icon_tex_[entry_idx] != nullptr) {
                    UL_LOG_INFO("qdesktop: dock icon loaded for '%s' path=%s",
                                entry.name, dock_path);
                    builtin_handled = true;
                    // Builtin Dock<Name>.png is the REAL icon for that slot --
                    // not a fallback waiting to be replaced. Leave the
                    // replaceable flag clear so 2b' does not destroy it.
                    // v1.8.19: stable PNG — skip 2a/2c on every subsequent frame.
                    slot_render_state_[entry_idx] = CellRenderState::SpecialPng;
                } else {
                    UL_LOG_WARN("qdesktop: Dock%s.png missing — falling back"
                                " to DefaultApplication.png", entry.name);
                    fallback_path = "ui/Main/EntryIcon/DefaultApplication";
                    // The Default* PNG IS a fallback. Mark the slot for
                    // replacement once real BGRA arrives. The actual texture
                    // load happens below in `if (!builtin_handled) { ... }`.
                }
                break;
            }
            default:
                fallback_path = "ui/Main/EntryIcon/Empty";
                break;
        }
        if (!builtin_handled) {
            icon_tex_[entry_idx] = ::ul::menu::ui::TryFindLoadImage(fallback_path);
            if (icon_tex_[entry_idx] != nullptr) {
                UL_LOG_INFO("qdesktop: default icon loaded for '%s' kind=%u path=%s",
                            entry.name,
                            static_cast<unsigned>(entry.kind),
                            fallback_path);
                // v1.7.0-stabilize-2 (REC-03 option B): the Default* / Empty PNG
                // is a fallback only -- the real icon may arrive later when the
                // BGRA cache populates from a NACP/ASET load. Mark the slot
                // replaceable so section 2b' destroys this texture once real
                // data arrives.
                texture_replaceable_[entry_idx] = true;
                // v1.8.19: transition to DefaultFallback — 2c is done; only 2b/2b'
                // need to run on subsequent frames to check for BGRA promotion.
                slot_render_state_[entry_idx] = CellRenderState::DefaultFallback;
            } else {
                // No PNG asset on disk — colored-square fallback will fire below.
                UL_LOG_WARN("qdesktop: default icon MISSING for '%s' kind=%u path=%s"
                            " (romfs asset absent — check default-theme packaging)",
                             entry.name,
                             static_cast<unsigned>(entry.kind),
                             fallback_path);
            }
        }
    }

    // Combine: either a BGRA cache entry, a Special PNG, or the default fallback
    // PNG loaded in 2c — all three paths populate icon_tex_[entry_idx].
    // v1.8.19: On the BgraReady/SpecialPng fast path bgra==nullptr and
    // special_tex_ready==false, but icon_tex_[entry_idx] is already set.
    // rs_stable_tex covers that case in the combined condition below.

    // v1.8.19: lbl_render_icon_tex — entry point for BgraReady / SpecialPng
    // fast paths that jump here directly without running classification.
    lbl_render_icon_tex:
    // All variables used here (bgra, special_tex_ready, rs_stable_tex,
    // entry_idx) are declared before the goto dispatches above.
    if ((bgra != nullptr || special_tex_ready
                || (bgra == nullptr && !special_tex_ready
                    && entry_idx < MAX_ICONS && icon_tex_[entry_idx] != nullptr)
                || rs_stable_tex) && entry_idx < MAX_ICONS) {
        // Lazily build the per-slot icon texture on first paint; reuse every
        // subsequent frame.  Previously the code created and destroyed a new
        // SDL_Texture here each frame (~1 200 GPU allocs/sec at 20 icons × 60 fps),
        // fragmenting the Switch's 8 MB GPU pool and causing progressive lag.
        //
        // Rebuild if the texture has not been created yet for this slot.
        // Invalidation (when icon_loaded flips to false and FreeCachedText is
        // called on slot reset) already sets icon_tex_[entry_idx] = nullptr, so
        // the condition below will re-create the texture on the next render.
        // Cycle J: only build a streaming texture from BGRA when we're on the
        // App/NRO path. Special icons populated icon_tex_ above via
        // TryFindLoadImage and must NOT be overwritten with a wrong-format
        // streaming texture.
        if (icon_tex_[entry_idx] == nullptr && bgra != nullptr) {
            // The cache buffer is BGRA byte-order in memory: bytes [B,G,R,A]
            // (qd_IconCache::ScaleToBgra64 explicitly writes dst[0]=B, dst[1]=G,
            // dst[2]=R, dst[3]=A — see qd_IconCache.cpp:146-149).
            //
            // SDL pixel-format constants use big-endian word notation, so on
            // AArch64 LE the memory byte order is reversed from the constant
            // name:
            //   SDL_PIXELFORMAT_ARGB8888 → memory bytes [B,G,R,A]   ← we want this
            //   SDL_PIXELFORMAT_ABGR8888 → memory bytes [R,G,B,A]   ← OLD value, wrong
            //   SDL_PIXELFORMAT_RGBA8888 → memory bytes [A,B,G,R]
            //   SDL_PIXELFORMAT_BGRA8888 → memory bytes [A,R,G,B]
            //
            // Using ABGR8888 here previously caused a persistent R↔B swap on
            // every cached icon (skin tones purple, sky orange) because the
            // texture interpreted byte[0] as R while the cache wrote B there.
            icon_tex_[entry_idx] = SDL_CreateTexture(r,
                                                      SDL_PIXELFORMAT_ARGB8888,
                                                      SDL_TEXTUREACCESS_STREAMING,
                                                      static_cast<int>(CACHE_ICON_W),
                                                      static_cast<int>(CACHE_ICON_H));
            if (icon_tex_[entry_idx] != nullptr) {
                SDL_UpdateTexture(icon_tex_[entry_idx], nullptr, bgra,
                                  static_cast<int>(CACHE_ICON_W) * 4);
                // v1.8.19: first BGRA→texture build → BgraReady from now on.
                slot_render_state_[entry_idx] = CellRenderState::BgraReady;
            }
        }
        if (icon_tex_[entry_idx] != nullptr) {
            // Cycle I: 4px inset on all sides so the icon JPEG's square corners
            // don't poke out past the rounded background. This loses 8px of
            // detail (about 5% of icon area) but eliminates the visible sharp
            // outline at the rounded corners — the user-reported regression
            // after FillRoundRect landed in the H-cycle.
            constexpr s32 ICON_INSET_PX = 4;
            SDL_Rect dst {
                bg_x + ICON_INSET_PX,
                bg_y + ICON_INSET_PX,
                ICON_BG_W - 2 * ICON_INSET_PX,
                ICON_BG_H - 2 * ICON_INSET_PX
            };
            SDL_RenderCopy(r, icon_tex_[entry_idx], nullptr, &dst);
        }
        // When jumping here from a stable state we must not fall into glyph.
        goto lbl_after_glyph;
    }

    // v1.8.19: lbl_render_glyph — entry point for GlyphOnly fast path.
    lbl_render_glyph:
    // ── 3. Glyph (only when no JPEG art AND no Special/default PNG was blitted) ──
    // Lazy-render once per slot, then reuse the cached SDL_Texture every
    // frame.  Glyph uses Medium font for visibility on the colored block.
    // Cycle J: also skip glyph if Special PNG was loaded above.
    // Cycle K-defaulticons: also skip if a default fallback PNG was loaded in 2c.
    // The glyph + colored-square path is now the absolute last resort for when
    // no PNG asset exists at all on disk — a UL_LOG_ERROR fires in 2c in that case.
    // B63 Fix A (v1.8.14): replace the !default_tex_ready guard with the actual
    // observable condition "nothing was rendered above" — i.e. icon_tex_[i]==nullptr.
    // When BGRA is absent AND no texture survived the main render block (either
    // default PNG load failed, or 2b' destroyed the replaceable default without a
    // real icon to upload), route to the colored-block + glyph path so the cell is
    // never left black/empty.  Cells that DO have icon_tex_ set skip section 3
    // unchanged.
    if (bgra == nullptr && !special_tex_ready && entry_idx < MAX_ICONS
            && icon_tex_[entry_idx] == nullptr) {
        if (glyph_text_tex_[entry_idx] == nullptr && entry.glyph != '\0') {
            const std::string glyph_str(1, entry.glyph);
            // Render in white; the background block already provides contrast.
            const pu::ui::Color glyph_color { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            glyph_text_tex_[entry_idx] = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                glyph_str, glyph_color);
            // Cache dimensions once — texture size is immutable after creation.
            if (glyph_text_tex_[entry_idx] != nullptr) {
                SDL_QueryTexture(glyph_text_tex_[entry_idx], nullptr, nullptr,
                                 &glyph_text_w_[entry_idx], &glyph_text_h_[entry_idx]);
            }
        }
        if (glyph_text_tex_[entry_idx] != nullptr) {
            const int gw = glyph_text_w_[entry_idx];
            const int gh = glyph_text_h_[entry_idx];
            SDL_Rect gdst {
                bg_x + (ICON_BG_W - gw) / 2,
                bg_y + (ICON_BG_H - gh) / 2,
                gw, gh
            };
            SDL_RenderCopy(r, glyph_text_tex_[entry_idx], nullptr, &gdst);
            // v1.8.19: glyph rendered → GlyphOnly state (skip all texture
            // classification every subsequent frame for this slot).
            slot_render_state_[entry_idx] = CellRenderState::GlyphOnly;
        }
    }
    // v1.8.19: lbl_after_glyph — merge point for icon-texture fast path.
    lbl_after_glyph:;

    // ── 4. Name label (below the icon block) ──────────────────────────────
    // Cycle I: auto-fit text via system-wide RenderTextAutoFit helper. Replaces
    // the previous "truncate at 16 chars + ellipsis" path. Shrinks font size
    // (Small -> falls through smaller variants) until the rasterised width
    // fits within ICON_BG_W. Long names show fully with smaller font instead
    // of being mutilated by ellipsis.
    // Cycle K-docktext: skip name text rendering entirely for dock builtins.
    // Dock icons live in the bottom dock band (DOCK_NOMINAL_TOP=932, DOCK_H=148);
    // their bg_y is 932+6=938 and ICON_BG_H is 130, so a label at bg_y +
    // ICON_BG_H + 4 = 1072 starts only 8 px above the screen bottom (1080) and
    // its glyph height (~30 px) clips below the screen. The dock is a tight
    // bottom strip with no room for under-icon labels — and per creator
    // directive ("EVERYTHING uses Icons") the dock is icon-only by design.
    // Grid icons keep their labels (they sit in the spacious icon grid above).
    if (entry_idx < MAX_ICONS && entry.name[0] != '\0' && !entry.is_builtin) {
        if (name_text_tex_[entry_idx] == nullptr) {
            const pu::ui::Color name_color { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            const std::string name_str(entry.name,
                                       strnlen(entry.name, sizeof(entry.name)));
            name_text_tex_[entry_idx] = ::ul::menu::ui::RenderTextAutoFit(
                name_str, name_color,
                static_cast<u32>(ICON_BG_W),
                pu::ui::DefaultFontSize::Small);
            // Cache dimensions once — texture size is immutable after creation.
            if (name_text_tex_[entry_idx] != nullptr) {
                SDL_QueryTexture(name_text_tex_[entry_idx], nullptr, nullptr,
                                 &name_text_w_[entry_idx], &name_text_h_[entry_idx]);
            }
        }
        if (name_text_tex_[entry_idx] != nullptr) {
            const int nw = name_text_w_[entry_idx];
            const int nh = name_text_h_[entry_idx];
            // Centre horizontally under the bg rect; pad 4 px below.
            SDL_Rect ndst {
                bg_x + (ICON_BG_W - nw) / 2,
                bg_y + ICON_BG_H + 4,
                nw, nh
            };
            SDL_RenderCopy(r, name_text_tex_[entry_idx], nullptr, &ndst);
        }
    }

    // ── 5. Focus / hover rings ────────────────────────────────────────────
    // D-pad focus: full-opacity white ring (hard selection indicator).
    if (is_dpad_focused) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
        SDL_Rect ring { bg_x - 1, bg_y - 1, ICON_BG_W + 2, ICON_BG_H + 2 };
        SDL_RenderDrawRect(r, &ring);
    }
    // Mouse hover: half-opacity white ring drawn 1 px further out (softer).
    // Blended separately so both can appear simultaneously (e.g. cursor lands
    // on the D-pad focused icon: inner hard ring + outer soft ring stack).
    //
    // v1.7.0-stabilize-2 (REC-02 corrected): suppress the hover ring when the
    // last meaningful input was a D-pad press. This kills the "double-
    // highlight" visual artefact where both rings showed at once during
    // controller navigation. The flag flips back to false on cursor motion or
    // touch arrival so ZR-launches-cursor-target keeps working in mouse mode.
    const bool show_hover_ring = is_mouse_hovered && !last_input_was_dpad_;
    if (show_hover_ring) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x80u);
        SDL_Rect soft_ring { bg_x - 2, bg_y - 2, ICON_BG_W + 4, ICON_BG_H + 4 };
        SDL_RenderDrawRect(r, &soft_ring);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── v1.7.0-stabilize-7 Slice 5 (O-F Patch 5): star overlay ────────────────
    // Mirrors qd_Launchpad.cpp:PaintCell star. With Slice 4 stripping the
    // desktop grid, this fires for:
    //   - dock builtins (rare: user could favorite a Builtin via Launchpad Y),
    //   - favorites strip tiles (via PaintFavoritesStrip which calls into
    //     PaintIconCell with the icons_[] entry).
    if (IsFavorite(entry)) {
        static SDL_Texture *star_tex = nullptr;
        if (star_tex == nullptr) {
            const pu::ui::Color amber { 0xFBu, 0xBFu, 0x24u, 0xFFu };
            star_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "*", amber);
        }
        if (star_tex != nullptr) {
            int sw = 0, sh = 0;
            SDL_QueryTexture(star_tex, nullptr, nullptr, &sw, &sh);
            SDL_Rect sdst {
                bg_x + ICON_BG_W - sw - 4,
                bg_y + 4,
                sw, sh
            };
            SDL_RenderCopy(r, star_tex, nullptr, &sdst);
        }
    }
}

// ── PrewarmAllIcons ───────────────────────────────────────────────────────────
//
// v1.8.13 (UnifiedDesktopPrewarm): Boot-phase icon cache prewarm.
//
// Iterates icons_[0..icon_count_) and calls the same three load helpers that
// Launchpad::Open()'s prewarm uses, with identical cache-key logic to
// PaintIconCell's BGRA lookup (section 2b).  Running this once before the first
// desktop frame ensures PaintFavoritesStrip → PaintIconCell finds real BGRA data
// in the cache instead of returning nullptr (which triggers the fallback gray
// colored block).
//
// Architecture choice: Option A (minimum surface area).
//   - No changes to Launchpad::Open()'s prewarm — it stays in place.
//   - Cache idempotency: QdIconCache::Get is a fast hash-table lookup; a second
//     prewarm pass (if Launchpad opens after boot) costs only cache hits, no
//     re-decoding.
//   - Called from OnRender's first-call branch via a static bool guard so the
//     prewarm runs exactly once per uMenu instance.
void QdDesktopIconsElement::PrewarmAllIcons() {
    size_t prewarm_hit = 0u;
    const size_t total = icon_count_;

    for (size_t i = 0u; i < total; ++i) {
        // v1.8.15 Fix B: poll the stop flag between every entry so the
        // destructor's join() completes promptly when uMenu shuts down.
        if (prewarm_stop_.load(std::memory_order_relaxed)) {
            UL_LOG_INFO("qdesktop: PrewarmAllIcons: stopped early at entry %zu/%zu",
                        i, total);
            return;
        }

        const NroEntry &e = icons_[i];

        // NRO-backed entries: load via ASET extraction.
        // Cache key = nro_path (mirrors PaintIconCell section 2b NRO branch).
        if (e.nro_path[0] != '\0') {
            if (LoadNroIconToCache(e.nro_path, e.nro_path)) {
                ++prewarm_hit;
            }
            continue;
        }

        // Application and Special entries with an icon_path JPEG:
        // distinguish "app:<hex>" NS-cache keys from literal SD paths and route
        // accordingly (same logic as Launchpad::Open() prewarm F2b path).
        if (e.icon_path[0] != '\0') {
            // v1.8.19 Edit 3: skip paths that already failed a prior prewarm pass.
            if (g_has_no_asset_.count(e.icon_path) != 0u) {
                continue;
            }
            // v1.8.21: romfs: paths are compile-time PNG assets bundled in the
            // uMenu romfs (set by ResolvePayloadIcon when a themed bundle icon
            // matches the payload stem).  They are loaded lazily in PaintIconCell
            // section 2a-romfs via pu::ui::render::LoadImageFromFile (POSIX
            // IMG_Load, works on the romfs: device).  LoadJpegIconToCache is
            // sdmc-only (fsFsOpenFile on "sdmc") and cannot load romfs paths;
            // calling it here would silently write a gray fallback to the BGRA
            // cache and block the real PNG from ever rendering.  Skip prewarm for
            // these entries — they cost zero I/O until first paint.
            if (e.icon_path[0] == 'r' && e.icon_path[1] == 'o' &&
                e.icon_path[2] == 'm' && e.icon_path[3] == 'f' &&
                e.icon_path[4] == 's' && e.icon_path[5] == ':') {
                continue;  // lazy-loaded in PaintIconCell section 2a-romfs
            }
            const bool has_ns_key = (e.icon_path[0] == 'a' &&
                                     e.icon_path[1] == 'p' &&
                                     e.icon_path[2] == 'p' &&
                                     e.icon_path[3] == ':');
            if (has_ns_key && e.app_id != 0u) {
                if (LoadAppIconFromUSystemCache(e.app_id, e.icon_path)) {
                    ++prewarm_hit;
                } else {
                    // v1.8.22c Edit 1: the "deferred to OnRender" comment was
                    // historical fiction — that path was deleted long ago and
                    // never restored.  When the uSystem JPG drop is missing
                    // (game never launched, or fork uSystem hasn't been
                    // installed to flush the cache), fall through to a direct
                    // NS storage/CacheOnly read here so prewarm produces a
                    // real icon instead of poisoning the cache with gray.
                    // Only memoize after BOTH paths fail.
                    if (LoadNsIconToCache(e.app_id, e.icon_path)) {
                        ++prewarm_hit;
                    } else {
                        g_has_no_asset_.insert(e.icon_path);
                    }
                }
                continue;
            }
            if (LoadJpegIconToCache(e.icon_path, e.icon_path)) {
                ++prewarm_hit;
            } else {
                // Memoize: JPEG load failed — do not retry on subsequent prewarm passes.
                g_has_no_asset_.insert(e.icon_path);
            }
            continue;
        }

        // Application entries with empty icon_path: try the uSystem on-disk
        // cache using the synthesised "app:<hex16>" cache key that PaintIconCell
        // reads so the cache-key is consistent.
        if (e.app_id != 0u) {
            char app_cache_key[32];
            snprintf(app_cache_key, sizeof(app_cache_key),
                     "app:%016llx",
                     static_cast<unsigned long long>(e.app_id));
            // v1.8.19 Edit 3: skip app_cache_key paths that already failed.
            if (g_has_no_asset_.count(app_cache_key) == 0u) {
                if (LoadAppIconFromUSystemCache(e.app_id, app_cache_key)) {
                    ++prewarm_hit;
                } else {
                    // v1.8.22c Edit 1 (mirror): try NS direct read before
                    // marking blacklisted — see comment above for rationale.
                    if (LoadNsIconToCache(e.app_id, app_cache_key)) {
                        ++prewarm_hit;
                    } else {
                        g_has_no_asset_.insert(app_cache_key);
                    }
                }
            }
        }
        // Builtin and Special-PNG entries have no BGRA path (Builtin uses
        // DockXxx.png loaded lazily; Special PNG loaded in PaintIconCell section
        // 2a via TryFindLoadImage — not the BGRA cache). Nothing to prewarm.
    }

    UL_LOG_INFO("qdesktop: PrewarmAllIcons: total=%zu hit=%zu", total, prewarm_hit);
}

// ── SpawnPrewarmThread ────────────────────────────────────────────────────────
//
// v1.8.15 Fix B: Launches PrewarmAllIcons() on a background std::thread.
// Called once from OnRender's first-render branch.
//
// Threading contract:
//   - prewarm_stop_ is initialised false in the constructor.
//   - The destructor sets prewarm_stop_ = true then calls prewarm_thread_.join()
//     before freeing cache_ or icon_tex_[], so the lambda's `this` is always valid.
//   - Every GetSharedIconCache().Put() call inside PrewarmAllIcons() and its helpers is wrapped
//     in a short-scope std::lock_guard<std::mutex> on cache_mutex_.
//   - Every GetSharedIconCache().Get() call in PaintIconCell is similarly guarded, so the
//     render thread never races with the prewarm writer.

void QdDesktopIconsElement::SpawnPrewarmThread() {
    // Safety: do nothing if the thread is already running (e.g. duplicate call).
    if (prewarm_thread_.joinable()) {
        return;
    }
    prewarm_stop_.store(false, std::memory_order_relaxed);
    prewarm_thread_ = std::thread([this]() {
        PrewarmAllIcons();
    });
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdDesktopIconsElement::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                      const s32 x, const s32 y)
{
    // F-06 fix: LRU tick is owned by the public AdvanceTick() method.
    // OnRender does NOT call cache_.AdvanceTick() here to avoid double-counting
    // if the layout calls AdvanceTick() once per frame externally.
    // The QDESKTOP_MODE integration block in ui_MainMenuLayout.cpp does NOT call
    // AdvanceTick() on children — so tick advancement currently relies solely on
    // QdDesktopIconsElement::AdvanceTick() being called once per frame by the owner.

    // Obtain the raw SDL_Renderer* through Plutonium's accessor.
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    {
        static bool first_render_done = false;
        if (!first_render_done) {
            UL_LOG_INFO("qdesktop: DesktopIcons OnRender first call renderer=%p icons=%zu at x=%d y=%d",
                        static_cast<void*>(r), icon_count_, x, y);
            // v1.8.15 Fix B (BackgroundPrewarm): spawn the background prewarm
            // thread instead of blocking the render loop.  PaintIconCell's
            // cache_.Get calls are now mutex-guarded so the render thread never
            // races with the prewarm writer.  Fix A (icon_tex_[] == nullptr gate)
            // from v1.8.14 ensures cells paint a colored block during the prewarm
            // window rather than going black, so the UX is smooth.
            SpawnPrewarmThread();
            first_render_done = true;
        }
    }
    if (!r) {
        return;
    }

    // ── Per-frame cursor hit-test for mouse_hover_index_ ─────────────────────
    // Track cursor position every frame so the hover ring stays in sync with
    // the pointer even when no button is pressed.  This is a read-only operation
    // (no launch side-effects) -- it only updates the visual highlight.
    //
    // v1.7.0-stabilize-7 Slice 4: switched from icons_[]-indexed HitTest() to
    // unified HitTestDesktop() (folders 0..5 + dock 6..10).  PaintFavoritesStrip
    // does its own cursor query for hover detection; the strip's D-pad nav is
    // tracked separately in fav_strip_focus_index_ (Bug #4 v1.8 / cycle v1.8.23).
    if (cursor_ref_ != nullptr) {
        const s32 cx = cursor_ref_->GetCursorX();
        const s32 cy = cursor_ref_->GetCursorY();
        mouse_hover_index_ = HitTestDesktop(cx, cy);

        // v1.8 Input-source latch (upgraded from v1.7.0-stabilize-2 REC-02):
        // cursor motion flips the active input source to MOUSE so the hover
        // ring renders again after a stretch of D-pad navigation.
        //
        // 4 px Manhattan threshold (spec requirement): prevents micro-jitter on
        // the analog stick or touch digitiser from flip-flopping the latch on
        // every frame.  abs(dx)+abs(dy) > 4 is coarser than per-axis but faster
        // to evaluate than Euclidean and sufficient for a 1920×1080 layout.
        //
        // The first call sees the -1 sentinel (uninitialized) and only seeds
        // prev_cursor_*; subsequent calls compare against the previous sample.
        if (prev_cursor_x_ != -1 && prev_cursor_y_ != -1) {
            const s32 manhattan = std::abs(cx - prev_cursor_x_)
                                + std::abs(cy - prev_cursor_y_);
            if (manhattan > 4) {
                active_input_source_ = InputSource::MOUSE;
                last_input_was_dpad_ = false;
                if (cursor_ref_) {
                    cursor_ref_->SetVisible(true);
                }
            }
        }
        prev_cursor_x_ = cx;
        prev_cursor_y_ = cy;
    } else {
        mouse_hover_index_ = SIZE_MAX;
    }

    // ── Top bar background (translucent dark strip behind status widgets) ────
    // The Plutonium time/date/battery/connection elements are instantiated in
    // ui_MainMenuLayout.cpp and rendered by Plutonium on top of whatever the
    // canvas shows.  Without a background strip they are invisible over the
    // animated wallpaper.  Height = 48 px (qd_WmConstants.hpp TOPBAR_H ×1.5).
    // Drawn here so it is always behind all icon and dock layers.
    //
    // Cycle D5 dev toggle: when g_dev_topbar_enabled is false the strip is
    // suppressed.  The Plutonium time/battery widgets that float above this
    // strip are owned by ui_MainMenuLayout and continue to render — their
    // visibility is intentionally NOT gated here because the user's primary
    // need for the toggle is "show me the wallpaper without the dark band
    // along the top".
    static constexpr int32_t TOPBAR_H_PX = 48;
    if (::ul::menu::qdesktop::g_dev_topbar_enabled.load(std::memory_order_relaxed)) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xB0u);
        SDL_Rect topbar_bg { 0, y, 1920, TOPBAR_H_PX };
        SDL_RenderFillRect(r, &topbar_bg);
        // 1px bottom border for visual separation from the icon grid.
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x30u);
        SDL_Rect topbar_border { 0, y + TOPBAR_H_PX - 1, 1920, 1 };
        SDL_RenderFillRect(r, &topbar_border);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── SP2: Two-pass dock magnify layout for builtin dock slots ─────────────
    // Pass 1: compute magnify-scaled widths for each builtin slot.
    // Pass 2: walk left-to-right accumulating actual x positions.
    // Non-builtin icons (NRO grid) are rendered at nominal CellRect positions.

    // Magnify-scaled sizes for builtin slots [0, BUILTIN_ICON_COUNT).
    s32 magnify_w[BUILTIN_ICON_COUNT];
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        const s32 scale_x100 = dock_magnify_scale_x100(static_cast<s32>(i), magnify_center_);
        // Scaled width = ICON_BG_W * scale / 100, bounded to [ICON_BG_W, ICON_BG_W*2].
        const s32 sw = static_cast<s32>(
            static_cast<int64_t>(ICON_BG_W) * scale_x100 / 100LL);
        magnify_w[i] = sw;
    }

    // Compute total expanded dock width for re-centering.
    s32 total_expanded_w = 0;
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        total_expanded_w += magnify_w[i];
        if (i + 1u < BUILTIN_ICON_COUNT) {
            total_expanded_w += ICON_GRID_GAP_X;
        }
    }

    // Centre the expanded dock band within the screen (1920px).
    s32 expanded_start_x = (1920 - total_expanded_w) / 2;

    // Pre-compute each builtin slot's actual left edge in Pass 2.
    s32 builtin_slot_x[BUILTIN_ICON_COUNT];
    {
        s32 walk = expanded_start_x;
        for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
            builtin_slot_x[i] = walk;
            walk += magnify_w[i] + ICON_GRID_GAP_X;
        }
    }

    // Cache the live dock-slot rects so HitTest matches the visual exactly.
    // Updated every frame because magnify_w[] changes when cursor enters the dock.
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        dock_slot_x_[i] = builtin_slot_x[i];
        dock_slot_w_[i] = magnify_w[i];
    }

    // ── Dock panel (translucent backdrop behind builtin slots) ───────────────
    // Visually delineates the dock zone (y = DOCK_NOMINAL_TOP..1080) from the
    // icon grid above it.  Drawn BEFORE the icon loop so dock builtins paint
    // on top.  Alpha-blended black at ~38% opacity.  Width = full screen.
    //
    // Cycle D5 dev toggle: when g_dev_dock_enabled is false the dock panel
    // backdrop is suppressed AND the BUILTIN_ICON_COUNT slots in the icon
    // loop below are skipped (see the in-loop guard).
    const bool dev_dock_on = ::ul::menu::qdesktop::g_dev_dock_enabled.load(
        std::memory_order_relaxed);
    const bool dev_icons_on = ::ul::menu::qdesktop::g_dev_icons_enabled.load(
        std::memory_order_relaxed);
    if (dev_dock_on) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0x60u);
        SDL_Rect dock_panel { 0, kDockNominalTop + y, 1920, kDockH };
        SDL_RenderFillRect(r, &dock_panel);
        // Thin top border line for definition (1px white at 25% alpha).
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x40u);
        SDL_Rect dock_border { 0, kDockNominalTop + y, 1920, 1 };
        SDL_RenderFillRect(r, &dock_border);
        // Restore opaque draw colour for downstream filled rects.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── v1.7.0-stabilize-7 Slice 4 (O-B) — dock-only paint loop ──────────────
    //
    // The pre-stabilize-7 paint loop walked every entry in icons_[] and painted
    // dock builtins (i < BUILTIN_ICON_COUNT) plus an N-row × M-column grid of
    // NRO / Application / Special icons above the dock. Per the O-B mandate
    // ("remove all the icons off of the desktop so we can make the folders
    // work"), we now paint ONLY the 5 dock builtins from icons_[], plus the
    // 6-folder categorized grid (PaintDesktopFolders) and the favorites strip
    // (PaintFavoritesStrip).
    //
    // The Launchpad continues to show every entry — it has its own pre-warm
    // pass (qd_Launchpad.cpp::Open) that populates the icon cache for first-
    // page items, including the F2b dual-fallback shipped-icon read for
    // Application entries (Slice 3 stabilize-6). Lazy-load no longer fires on
    // the desktop because we no longer paint NRO / Application / Special
    // tiles here.
    for (size_t i = 0u; i < BUILTIN_ICON_COUNT; ++i) {
        if (!dev_dock_on) { break; }
        if (i >= icon_count_) { break; }  // defensive: dock not yet populated
        const s32 cell_x = builtin_slot_x[i] + x;
        const s32 cell_y = kDockNominalTop + y;
        NroEntry &entry = icons_[i];
        // v1.8 Input-source latch: D-pad focus only in DPAD mode; hover only in MOUSE mode.
        const bool dpad_focused  = (active_input_source_ == InputSource::DPAD)
                                && (dpad_focus_index_ == kDesktopFolderCount + i);
        const bool mouse_hovered = (active_input_source_ == InputSource::MOUSE)
                                && (mouse_hover_index_ == kDesktopFolderCount + i);
        PaintIconCell(r, entry, i, cell_x, cell_y, dpad_focused, mouse_hovered);
    }

    // ── v1.7.0-stabilize-7 Slice 4 (O-B): folder grid + Slice 5 (O-F): strip ─
    if (dev_icons_on) {
        PaintDesktopFolders(r, x, y);
        PaintFavoritesStrip(r, x, y);
    }

    // ── v1.7.0-stabilize-2: hot-corner widget at top-left (96x72) ────────────
    // The widget is a small visual affordance that tells the user where to
    // tap to open the Launchpad. Renders LAST (after the icon loop and after
    // the topbar) so it is on top in z-order; the OnInput handler at the
    // matching geometry edge-triggers LoadMenu(MenuType::Launchpad) on tap.
    //
    // Geometry: 96 wide x 72 tall, anchored at (x, y) (the layout's origin,
    // typically (0, 0)). Width matches LP_HOTCORNER_W (96) so the close-side
    // handler in qd_Launchpad.cpp uses the same hit-box dimensions.
    // F7 (stabilize-5): P1 — Block B completed; fixed SDL_BLENDMODE_BLEND →
    // SDL_BLENDMODE_NONE on the fill so desktop icons below don't bleed alpha.
    // Dot grid removed; "Q" glyph rendered instead (mirrors Block A in
    // qd_Launchpad.cpp lines 652-670 — same font, color, size, centering).
    {
        constexpr s32 HC_W = LP_HOTCORNER_W;  // 96 (defined in qd_Launchpad.hpp)
        constexpr s32 HC_H = LP_HOTCORNER_H;  // 72
        const s32 hcx = x;
        const s32 hcy = y;

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);  // F7 (stabilize-5): fix alpha bleed
        // Fill: dark translucent so wallpaper shows through.
        SDL_SetRenderDrawColor(r, 0x10u, 0x10u, 0x14u, 0xC0u);
        SDL_Rect hc_bg { hcx, hcy, HC_W, HC_H };
        SDL_RenderFillRect(r, &hc_bg);
        // Border: 1px white-alpha so the box is visible against any wallpaper.
        SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x40u);
        SDL_Rect hc_top    { hcx,            hcy,            HC_W, 1 };
        SDL_Rect hc_bottom { hcx,            hcy + HC_H - 1, HC_W, 1 };
        SDL_Rect hc_left   { hcx,            hcy,            1,    HC_H };
        SDL_Rect hc_right  { hcx + HC_W - 1, hcy,            1,    HC_H };
        SDL_RenderFillRect(r, &hc_top);
        SDL_RenderFillRect(r, &hc_bottom);
        SDL_RenderFillRect(r, &hc_left);
        SDL_RenderFillRect(r, &hc_right);
        // "Q" glyph centred in the box — mirrors Block A (qd_Launchpad.cpp).
        static SDL_Texture *hc_tex = nullptr;
        if (!hc_tex) {
            const pu::ui::Color wh { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            hc_tex = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                "Q", wh);
        }
        if (hc_tex) {
            int tw = 0, th = 0;
            SDL_QueryTexture(hc_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect td { hcx + (HC_W - tw) / 2, hcy + (HC_H - th) / 2, tw, th };
            SDL_RenderCopy(r, hc_tex, nullptr, &td);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── Task 9 (v1.8): dev popup overlay ─────────────────────────────────────
    // Rendered LAST (highest z-order), only when dev_popup_open_ is true.
    // Each panel owns its own SDL_Renderer calls via its OnRender implementation.
    // We pass a null drawer Ref because the panels obtain the renderer directly
    // via pu::ui::render::GetMainRenderer() (same pattern as this element).
    if (dev_popup_open_) {
        pu::ui::render::Renderer::Ref null_drawer{};
        if (nxlink_win_)  nxlink_win_->OnRender(null_drawer, 0, 0);
        if (usb_win_)     usb_win_->OnRender(null_drawer, 0, 0);
        if (log_win_)     log_win_->OnRender(null_drawer, 0, 0);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────
//
// Touch state machine (click-vs-drag):
//   TouchDown  — record press origin + hit-test index; do NOT launch.
//   TouchMove  — update last position; do NOT launch.
//   TouchUp    — launch only when:
//                  (a) pressed_ is true
//                  (b) hit-test at lift position matches the down icon
//                  (c) Euclidean displacement from down to lift ≤ CLICK_TOLERANCE_PX
//
// A-button path — launch the icon under the cursor when cursor_ref_ is wired;
// fall back to focused_idx_ when it is not (the layout always wires it in
// QDESKTOP_MODE, but the fallback protects against null deref if order changes).

void QdDesktopIconsElement::OnInput(const u64 keys_down,
                                     const u64 keys_up,
                                     const u64 keys_held,
                                     const pu::ui::TouchPoint touch_pos)
{
    (void)keys_up;

    // v1.8.23: coyote-timing — dpad-held repeat lambda (mirrors qd_VaultLayout.cpp:1062-1073).
    // Accumulates per-direction held-frame counters in coyote_.dpad_held_frames[0..3]
    // (indices: 0=Up 1=Down 2=Left 3=Right) and fires a synthetic repeat event after
    // DPAD_REPEAT_DELAY_F frames, then every DPAD_REPEAT_INTERVAL_F frames.
    auto dpad_should_repeat = [this, &keys_held](u32 dir_idx, u64 btn_mask) -> bool {
        const bool is_held = (keys_held & btn_mask) != 0u;
        if (!is_held) {
            coyote_.dpad_held_frames[dir_idx] = 0u;
            return false;
        }
        ++coyote_.dpad_held_frames[dir_idx];
        if (coyote_.dpad_held_frames[dir_idx] <= DPAD_REPEAT_DELAY_F) {
            return false;
        }
        const u32 since_delay = coyote_.dpad_held_frames[dir_idx] - DPAD_REPEAT_DELAY_F;
        return (since_delay % DPAD_REPEAT_INTERVAL_F) == 0u;
    };
    const bool repeat_up    = dpad_should_repeat(0u, HidNpadButton_Up);
    const bool repeat_down  = dpad_should_repeat(1u, HidNpadButton_Down);
    const bool repeat_left  = dpad_should_repeat(2u, HidNpadButton_Left);
    const bool repeat_right = dpad_should_repeat(3u, HidNpadButton_Right);

    // ── Task 9 (v1.8): forward input to dev popup panels when open ────────────
    // When the dev overlay is visible, input goes to each panel first.
    // Panels handle their own button/touch interaction via their OnInput.
    // We do NOT return here — desktop input continues normally (ZL still
    // closes/opens the popup).  Panels that are not in focus simply ignore
    // irrelevant input.
    if (dev_popup_open_) {
        if (nxlink_win_)  nxlink_win_->OnInput(keys_down, keys_up, keys_held, touch_pos);
        if (usb_win_)     usb_win_->OnInput(keys_down, keys_up, keys_held, touch_pos);
        if (log_win_)     log_win_->OnInput(keys_down, keys_up, keys_held, touch_pos);
    }

    // ── v1.7.0-stabilize-7 Slice 4 (O-B Phase 5) — unified focus model ───────
    //
    // dpad_focus_index_ semantics changed in stabilize-7:
    //   0..(kDesktopFolderCount-1)            -> desktop folder fi
    //   kDesktopFolderCount..(kDFC + BIC - 1)  -> dock slot (icon idx = focus - kDFC)
    //
    // Total range: 0..10 (6 folders + 5 dock cells). The favorites strip uses
    // a parallel fav_strip_focus_index_ (Bug #4 v1.8); v1.8.23 wired it as a
    // full peer in the Up/Down cycle: Folders <-> Favorites <-> Dock.

    // ── A: launch focused folder OR dock builtin OR favorite ─────────────────
    if (keys_down & HidNpadButton_A) {
        // v1.8.23: A button on favorites uses the SAME LaunchIcon dispatch
        // path as touch and ZR (HitTestFavorites/ResolveFavoriteToIconIdx).
        // ResolveFavoriteToIconIdx handles all FavoriteKinds (Nro, App,
        // Builtin, Special) via FavEntryFromNroEntry round-trip, fixing the
        // earlier Nro-only restriction in the v1.8 path.
        if (fav_strip_focus_index_ != SIZE_MAX) {
            // Map fav_strip_focus_index_ (paint-position 0..FAV_STRIP_VISIBLE-1)
            // to a g_favorites_list_ index. Because PaintFavoritesStrip skips
            // stale (unresolved) favorites when computing `painted`, we walk
            // the same way to land on the same on-screen tile.
            if (!g_favorites_list_.empty()) {
                size_t painted = 0u;
                const size_t cap =
                    (g_favorites_list_.size() < static_cast<size_t>(FAV_STRIP_VISIBLE))
                        ? g_favorites_list_.size()
                        : static_cast<size_t>(FAV_STRIP_VISIBLE);
                for (size_t fi = 0u; fi < cap; ++fi) {
                    const FavoriteEntry &fav = g_favorites_list_[fi];
                    const size_t idx = ResolveFavoriteToIconIdx(icons_, icon_count_, fav);
                    if (idx >= icon_count_) {
                        continue;  // stale favorite: not painted
                    }
                    if (painted == fav_strip_focus_index_) {
                        // v1.8.23: coyote relaunch-lockout gate
                        {
                            const u64 now_tick = armGetSystemTick();
                            if ((now_tick - coyote_.last_launch_tick) < RELAUNCH_LOCKOUT_TICKS) {
                                UL_LOG_INFO("qdesktop: A fav coyote-lockout (within 300ms of last launch)");
                                return;
                            }
                        }
                        UL_LOG_INFO("qdesktop: A (strip fav_slot=%zu) → LaunchIcon(%zu)",
                                    fav_strip_focus_index_, idx);
                        LaunchIcon(idx);
                        coyote_.last_launch_tick = armGetSystemTick();
                        return;
                    }
                    ++painted;
                }
            }
            // Strip slot has no matching loaded icon: no-op (shows nothing to launch).
            UL_LOG_INFO("qdesktop: A (strip fav_slot=%zu) → no match, no-op",
                        fav_strip_focus_index_);
            return;
        }
        const size_t f = dpad_focus_index_;
        if (f < kDesktopFolderCount) {
            OpenLaunchpadFiltered(static_cast<DesktopFolderId>(f));
            return;
        } else if (f < kDesktopFolderCount + BUILTIN_ICON_COUNT
                   && (f - kDesktopFolderCount) < icon_count_) {
            // v1.8.23: coyote relaunch-lockout gate
            {
                const u64 now_tick = armGetSystemTick();
                if ((now_tick - coyote_.last_launch_tick) < RELAUNCH_LOCKOUT_TICKS) {
                    UL_LOG_INFO("qdesktop: A dock coyote-lockout (within 300ms of last launch)");
                    return;
                }
            }
            LaunchIcon(f - kDesktopFolderCount);
            coyote_.last_launch_tick = armGetSystemTick();
            return;
        }
    }

    // ── ZR: launch the cell under the cursor (folder or dock or favorite) ────
    if (keys_down & HidNpadButton_ZR) {
        // v1.8 Input-source latch: ZR is "mouse button pressed" — it fires a
        // cursor-targeted launch, so it unambiguously signals MOUSE mode.
        // Show cursor (may already be visible, no harm in re-setting).
        active_input_source_ = InputSource::MOUSE;
        last_input_was_dpad_ = false;
        if (cursor_ref_) {
            cursor_ref_->SetVisible(true);
        }
        if (cursor_ref_ != nullptr) {
            const s32 cx = cursor_ref_->GetCursorX();
            const s32 cy = cursor_ref_->GetCursorY();
            // Slice 5: favorites strip takes priority over folder/dock for
            // the small overlap region near y=622.
            const size_t fav_hit = HitTestFavorites(cx, cy);
            if (fav_hit < icon_count_) {
                // v1.8.23: coyote relaunch-lockout gate
                {
                    const u64 now_tick = armGetSystemTick();
                    if ((now_tick - coyote_.last_launch_tick) < RELAUNCH_LOCKOUT_TICKS) {
                        UL_LOG_INFO("qdesktop: ZR fav coyote-lockout (within 300ms of last launch)");
                        return;
                    }
                }
                LaunchIcon(fav_hit);
                coyote_.last_launch_tick = armGetSystemTick();
                return;
            }
            const size_t hit = HitTestDesktop(cx, cy);
            mouse_hover_index_ = hit;
            if (hit < kDesktopFolderCount) {
                OpenLaunchpadFiltered(static_cast<DesktopFolderId>(hit));
                return;
            } else if (hit < kDesktopFolderCount + BUILTIN_ICON_COUNT
                       && (hit - kDesktopFolderCount) < icon_count_) {
                // v1.8.23: coyote relaunch-lockout gate
                {
                    const u64 now_tick = armGetSystemTick();
                    if ((now_tick - coyote_.last_launch_tick) < RELAUNCH_LOCKOUT_TICKS) {
                        UL_LOG_INFO("qdesktop: ZR dock coyote-lockout (within 300ms of last launch)");
                        return;
                    }
                }
                LaunchIcon(hit - kDesktopFolderCount);
                coyote_.last_launch_tick = armGetSystemTick();
                return;
            }
        }
    }

    // ── Bug #46 fix (v1.8): ZL — dev popup hot-zone OR dock context menu ────────
    // ZL semantics (v1.8):
    //   • Touch pressed in top-right corner hot-zone [1890,1920)×[0,30) on the
    //     SAME frame as ZL: toggle the developer popup overlay.
    //   • Otherwise (ZL with no matching touch hit-zone): dock context menu
    //     (preserved Cycle G2 / stabilize-4 behavior).
    // The dev-popup hot-zone uses the last-recorded touch position because
    // keys_down fires on the same frame as the touch state update.
    if (keys_down & HidNpadButton_ZL) {
        // Check top-right corner hot-zone first.
        // We query the last-seen touch coords (last_touch_x_/y_) because the
        // touch state machine above may have already processed this frame's
        // touch and updated them.  If no touch was active this frame both are
        // 0 and the hit-test will miss (x=0 < 1890), so the dock path runs.
        static constexpr s32 kDevHotZoneX0 = 1890;
        static constexpr s32 kDevHotZoneY1 = 30;
        const bool in_dev_hotzone =
            was_touch_active_last_frame_
            && last_touch_x_ >= kDevHotZoneX0
            && last_touch_y_ < kDevHotZoneY1;
        if (in_dev_hotzone) {
            dev_popup_open_ = !dev_popup_open_;
            UL_LOG_INFO("qdesktop: ZL hot-zone → dev_popup_open_=%d", static_cast<int>(dev_popup_open_));
            return;
        }
        // Not in hot-zone: dock context menu (legacy behavior).
        size_t dock_idx = BUILTIN_ICON_COUNT;  // sentinel
        if (cursor_ref_ != nullptr) {
            const s32 cx = cursor_ref_->GetCursorX();
            const s32 cy = cursor_ref_->GetCursorY();
            const size_t h = HitTestDesktop(cx, cy);
            if (h >= kDesktopFolderCount
                    && h < kDesktopFolderCount + BUILTIN_ICON_COUNT) {
                dock_idx = h - kDesktopFolderCount;
            }
        }
        if (dock_idx >= BUILTIN_ICON_COUNT
                && dpad_focus_index_ >= kDesktopFolderCount
                && dpad_focus_index_ < kDesktopFolderCount + BUILTIN_ICON_COUNT) {
            dock_idx = dpad_focus_index_ - kDesktopFolderCount;
        }
        if (dock_idx < BUILTIN_ICON_COUNT
                && dock_idx < icon_count_
                && g_MenuApplication != nullptr) {
            const NroEntry &entry = icons_[dock_idx];
            UL_LOG_INFO("qdesktop: ZL context-menu open dock_idx=%zu name='%s' kind=%d",
                        dock_idx, entry.name, static_cast<int>(entry.kind));

            const u64 suspended_app_id = g_GlobalSettings.system_status.suspended_app_id;
            const bool this_is_suspended =
                entry.kind == IconKind::Application
                && entry.app_id != 0
                && suspended_app_id == entry.app_id;
            const bool other_is_suspended =
                suspended_app_id != 0 && !this_is_suspended;

            std::vector<std::string> opts;
            int opt_open    = -1;
            int opt_close   = -1;
            int opt_close_other = -1;

            if (this_is_suspended) {
                opt_open  = static_cast<int>(opts.size()); opts.emplace_back("Resume");
                opt_close = static_cast<int>(opts.size()); opts.emplace_back("Close");
            } else {
                opt_open = static_cast<int>(opts.size());
                opts.emplace_back(other_is_suspended ? "Open (close current first)"
                                                     : "Open");
                if (other_is_suspended) {
                    opt_close_other = static_cast<int>(opts.size());
                    opts.emplace_back("Close currently running game");
                }
            }
            opts.emplace_back("Cancel");
            const int cancel_idx = static_cast<int>(opts.size()) - 1;

            std::string body;
            if (this_is_suspended) {
                body = "This game is currently running.";
            } else if (other_is_suspended) {
                body = "A different game is currently running.";
            } else {
                body = "What would you like to do?";
            }

            const int choice = g_MenuApplication->DisplayDialog(
                std::string(entry.name),
                body,
                opts,
                true
            );
            if (choice < 0 || choice == cancel_idx) {
                UL_LOG_INFO("qdesktop: ZL context-menu cancelled");
            } else if (choice == opt_open) {
                LaunchIcon(dock_idx);
            } else if (choice == opt_close && this_is_suspended) {
                UL_LOG_INFO("qdesktop: ZL context-menu Close → TerminateApplication 0x%016llx",
                            static_cast<unsigned long long>(entry.app_id));
                const auto trc = smi::TerminateApplication();
                if (R_SUCCEEDED(trc)) {
                    g_GlobalSettings.ResetSuspendedApplication();
                    g_MenuApplication->ShowNotification("Closed running game.");
                } else {
                    UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X",
                                static_cast<unsigned>(trc));
                    g_MenuApplication->ShowNotification("Close failed");
                }
            } else if (choice == opt_close_other) {
                UL_LOG_INFO("qdesktop: ZL context-menu Close-current → TerminateApplication 0x%016llx",
                            static_cast<unsigned long long>(suspended_app_id));
                const auto trc = smi::TerminateApplication();
                if (R_SUCCEEDED(trc)) {
                    g_GlobalSettings.ResetSuspendedApplication();
                    g_MenuApplication->ShowNotification("Closed running game.");
                } else {
                    UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X",
                                static_cast<unsigned>(trc));
                    g_MenuApplication->ShowNotification("Close failed");
                }
            }
        }
    }

    // ── Bug #47 fix (v1.8): Y — toggle favorite under cursor ONLY ───────────────
    // Y semantics (v1.8): cursor hovering a favorited tile → un-favorite it.
    // Dock context menu is NO LONGER triggered by Y (ZL still handles it above).
    // This eliminates the Bug #47 regression where Y opened a dock context menu
    // instead of toggling the favorited tile under the cursor.
    if (keys_down & HidNpadButton_Y) {
        if (cursor_ref_ != nullptr) {
            const s32 cx = cursor_ref_->GetCursorX();
            const s32 cy = cursor_ref_->GetCursorY();
            const size_t fav_hit = HitTestFavorites(cx, cy);
            if (fav_hit < icon_count_) {
                UL_LOG_INFO("qdesktop: Y toggle-favorite icons_idx=%zu", fav_hit);
                ToggleFavorite(icons_[fav_hit]);
                return;
            }
        }
        // No favorited tile under cursor: no-op for Y (folders/dock have no Y action).
        UL_LOG_INFO("qdesktop: Y pressed, no favorite under cursor — no-op");
    }

    // ── v1.7.0-stabilize-2: hot-corner OPEN edge-trigger ─────────────────────
    // (Preserved from stabilize-6; F12 latch reorder retained.)
    {
        const bool touch_active_now = !touch_pos.IsEmpty();
        if (touch_active_now && !was_touch_active_last_frame_) {
            const s32 tx = static_cast<s32>(touch_pos.x);
            const s32 ty = static_cast<s32>(touch_pos.y);
            if (tx >= 0 && tx < LP_HOTCORNER_W
                    && ty >= 0 && ty < LP_HOTCORNER_H
                    && g_MenuApplication != nullptr) {
                // F12 (stabilize-6): set the latch BEFORE LoadMenu, not after.
                // LoadMenu calls FadeOut which busy-loops Application::OnRender;
                // each iteration recursively calls this OnInput. Without this
                // pre-LoadMenu update, the inner calls see
                // was_touch_active_last_frame_ == false and re-fire
                // LoadMenu(Launchpad).
                was_touch_active_last_frame_ = touch_active_now;
                UL_LOG_INFO("qdesktop: hot-corner OPEN tap edge tx=%d ty=%d -> LoadMenu(Launchpad)",
                            tx, ty);
                g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Launchpad);
                return;
            }
        }
    }

    // ── v1.7.0-stabilize-7 Slice 4 Phase 5: D-pad nav across folders + dock ──
    //
    // Folders (3×2) at indices 0..5: row 0 = 0,1,2 ; row 1 = 3,4,5.
    // Dock (5 cells) at indices 6..10.
    //
    // Up:    dock -> folder row 1 (column-mapped 5→3); folder row 1 -> row 0.
    // Down:  folder row 0 -> row 1; folder row 1 -> dock (column-mapped 3→5).
    // Left:  decrement (no wrap).
    // Right: increment (no wrap).
    const u64 dpad_a_mask = HidNpadButton_Up | HidNpadButton_Down
                          | HidNpadButton_Left | HidNpadButton_Right
                          | HidNpadButton_A;
    if (keys_down & dpad_a_mask) {
        last_input_was_dpad_ = true;
        // v1.8 Input-source latch: any directional D-pad key switches to DPAD
        // mode.  A alone does NOT change the source (matches spec: "A/B/X/Y/L/R/
        // ZL/ZR do NOT change source").  We check the directional bits only.
        if (keys_down & (HidNpadButton_Up | HidNpadButton_Down
                       | HidNpadButton_Left | HidNpadButton_Right)) {
            active_input_source_ = InputSource::DPAD;
            if (cursor_ref_) {
                cursor_ref_->SetVisible(false);
            }
        }
    }

    // v1.8.23: seamless dpad cycle Folders <-> Favorites <-> Dock.
    // Zones: Folders (dpad_focus_index_ in [0..kDesktopFolderCount)),
    //        Favorites (fav_strip_focus_index_ in [0..FAV_STRIP_VISIBLE)),
    //        Dock     (dpad_focus_index_ in [kDFC..kDFC+BIC)).
    // UP transitions:   Dock -> Favorites (skip if empty -> Folder row 1);
    //                   Favorites -> Folder row 1 (col-mapped);
    //                   Folder row 1 -> Folder row 0;
    //                   Folder row 0 -> Favorites (skip if empty -> stay).
    // DOWN transitions: Folder row 0 -> Folder row 1;
    //                   Folder row 1 -> Favorites (skip if empty -> Dock);
    //                   Favorites -> Dock;
    //                   Dock -> no-op (existing behavior).
    // LEFT/RIGHT stay within the active zone (no zone change).
    if ((keys_down & HidNpadButton_Up) || repeat_up) {
        if (fav_strip_focus_index_ != SIZE_MAX) {
            // v1.8.23: Favorites UP -> Folder row 1 (column-mapped from strip slot).
            // Strip slot 0..FAV_STRIP_VISIBLE-1 maps to folder col 0..DF_COLS-1
            // via truncation (strip is wider than folder grid).
            const size_t strip_col =
                (fav_strip_focus_index_ * static_cast<size_t>(DF_COLS)) / FAV_STRIP_VISIBLE;
            dpad_focus_index_ = static_cast<size_t>(DF_COLS) + strip_col;  // row 1 col = strip_col
            fav_strip_focus_index_ = SIZE_MAX;
            UL_LOG_INFO("qdesktop: dpad up (strip→row1) focus=%zu", dpad_focus_index_);
        } else {
            const size_t f = dpad_focus_index_;
            if (f >= kDesktopFolderCount
                    && f < kDesktopFolderCount + BUILTIN_ICON_COUNT) {
                // v1.8.23: Dock UP -> Favorites strip (skip-if-empty -> Folder row 1).
                // Map dock col (0..4) to strip slot (0..FAV_STRIP_VISIBLE-1).
                const size_t dock_i = f - kDesktopFolderCount;
                if (!g_favorites_list_.empty()) {
                    const size_t strip_slot =
                        (dock_i * FAV_STRIP_VISIBLE) / BUILTIN_ICON_COUNT;
                    fav_strip_focus_index_ = strip_slot < FAV_STRIP_VISIBLE ? strip_slot : 0u;
                    UL_LOG_INFO("qdesktop: dpad up (dock→strip) strip_slot=%zu", fav_strip_focus_index_);
                } else {
                    // Empty favorites: skip strip, fall through to folder row 1.
                    const size_t target_col =
                        (dock_i * static_cast<size_t>(DF_COLS)) / BUILTIN_ICON_COUNT;
                    dpad_focus_index_ = static_cast<size_t>(DF_COLS) + target_col;
                    UL_LOG_INFO("qdesktop: dpad up (dock→row1, no favs) -> focus=%zu", dpad_focus_index_);
                }
            } else if (f >= static_cast<size_t>(DF_COLS) && f < kDesktopFolderCount) {
                // Folder row 1 -> row 0.
                dpad_focus_index_ = f - static_cast<size_t>(DF_COLS);
                UL_LOG_INFO("qdesktop: dpad up (row1→row0) -> focus=%zu", dpad_focus_index_);
            } else {
                // f < DF_COLS (folder row 0) -> enter favorites strip.
                // Skip-if-empty: stay on row 0 when no favorites are loaded.
                if (!g_favorites_list_.empty()) {
                    const size_t folder_col = f;  // row 0 col == index
                    const size_t strip_slot =
                        (folder_col * FAV_STRIP_VISIBLE) / static_cast<size_t>(DF_COLS);
                    fav_strip_focus_index_ = strip_slot < FAV_STRIP_VISIBLE ? strip_slot : 0u;
                    UL_LOG_INFO("qdesktop: dpad up (row0→strip) strip_slot=%zu", fav_strip_focus_index_);
                } else {
                    UL_LOG_INFO("qdesktop: dpad up (row0, no favs) -> stay focus=%zu", dpad_focus_index_);
                }
            }
        }
    }
    if ((keys_down & HidNpadButton_Down) || repeat_down) {
        if (fav_strip_focus_index_ != SIZE_MAX) {
            // v1.8.23: Favorites DOWN -> Dock (column-mapped from strip slot).
            // Strip slot 0..FAV_STRIP_VISIBLE-1 maps to dock col 0..BIC-1.
            const size_t target_dock =
                (fav_strip_focus_index_ * BUILTIN_ICON_COUNT) / FAV_STRIP_VISIBLE;
            dpad_focus_index_ = kDesktopFolderCount
                              + (target_dock < BUILTIN_ICON_COUNT ? target_dock : 0u);
            fav_strip_focus_index_ = SIZE_MAX;
            UL_LOG_INFO("qdesktop: dpad down (strip→dock) focus=%zu", dpad_focus_index_);
        } else {
            const size_t f = dpad_focus_index_;
            if (f < static_cast<size_t>(DF_COLS)) {
                // Folder row 0 -> row 1.
                dpad_focus_index_ = f + static_cast<size_t>(DF_COLS);
                UL_LOG_INFO("qdesktop: dpad down (row0→row1) -> focus=%zu", dpad_focus_index_);
            } else if (f < kDesktopFolderCount) {
                // v1.8.23: Folder row 1 DOWN -> Favorites strip (skip-if-empty -> Dock).
                // Map folder col (0..DF_COLS-1) to strip slot (0..FAV_STRIP_VISIBLE-1).
                const size_t folder_col = f - static_cast<size_t>(DF_COLS);
                if (!g_favorites_list_.empty()) {
                    const size_t strip_slot =
                        (folder_col * FAV_STRIP_VISIBLE) / static_cast<size_t>(DF_COLS);
                    fav_strip_focus_index_ = strip_slot < FAV_STRIP_VISIBLE ? strip_slot : 0u;
                    UL_LOG_INFO("qdesktop: dpad down (row1→strip) strip_slot=%zu", fav_strip_focus_index_);
                } else {
                    // Empty favorites: skip strip, fall through to dock.
                    const size_t target_dock =
                        (folder_col * BUILTIN_ICON_COUNT) / static_cast<size_t>(DF_COLS);
                    dpad_focus_index_ = kDesktopFolderCount + target_dock;
                    UL_LOG_INFO("qdesktop: dpad down (row1→dock, no favs) -> focus=%zu", dpad_focus_index_);
                }
            } else {
                // f >= kDesktopFolderCount (already dock): no-op (existing).
                UL_LOG_INFO("qdesktop: dpad down (dock no-op) focus=%zu", dpad_focus_index_);
            }
        }
    }
    if ((keys_down & HidNpadButton_Left) || repeat_left) {
        if (fav_strip_focus_index_ != SIZE_MAX) {
            // Bug #4 fix (v1.8): in strip → move left within strip (clamp at 0).
            if (fav_strip_focus_index_ > 0u) {
                --fav_strip_focus_index_;
            }
            UL_LOG_INFO("qdesktop: dpad left (strip) slot=%zu", fav_strip_focus_index_);
        } else {
            if (dpad_focus_index_ > 0u) {
                --dpad_focus_index_;
            }
            UL_LOG_INFO("qdesktop: dpad left -> focus=%zu", dpad_focus_index_);
        }
    }
    if ((keys_down & HidNpadButton_Right) || repeat_right) {
        if (fav_strip_focus_index_ != SIZE_MAX) {
            // Bug #4 fix (v1.8): in strip → move right within strip
            // (clamp at FAV_STRIP_VISIBLE-1).
            if (fav_strip_focus_index_ + 1u < FAV_STRIP_VISIBLE) {
                ++fav_strip_focus_index_;
            }
            UL_LOG_INFO("qdesktop: dpad right (strip) slot=%zu", fav_strip_focus_index_);
        } else {
            const size_t max_idx = kDesktopFolderCount + BUILTIN_ICON_COUNT - 1u;
            if (dpad_focus_index_ < max_idx) {
                ++dpad_focus_index_;
            }
            UL_LOG_INFO("qdesktop: dpad right -> focus=%zu", dpad_focus_index_);
        }
    }

    // ── Touch click-vs-drag state machine (unified) ──────────────────────────
    //
    // Order of priority on TouchDown / TouchUp:
    //   1. Favorites strip (Slice 5 Patch 4) — highest; user taps the strip
    //      directly under the row.
    //   2. Folder grid + dock (Slice 4 Phase 5) — HitTestDesktop.
    //
    // The hot-corner OPEN block above already handles the corner; if that
    // fired we returned. So if we reach this block, the touch is NOT in
    // the corner.

    const bool touch_active_now = !touch_pos.IsEmpty();

    if (touch_active_now) {
        const s32 tx = touch_pos.x;
        const s32 ty = touch_pos.y;

        if (!was_touch_active_last_frame_) {
            // ── TouchDown ──────────────────────────────────────────────────
            pressed_      = true;
            down_x_       = tx;
            down_y_       = ty;
            last_touch_x_ = tx;
            last_touch_y_ = ty;
            // Favorites take priority over folders/dock.
            const size_t fav_hit = HitTestFavorites(tx, ty);
            if (fav_hit < icon_count_) {
                // Encode favorite hit as MAX_ICONS + icons_[]_idx so the
                // up-side can distinguish from a folder/dock hit.
                down_idx_ = MAX_ICONS + 1u + fav_hit;
            } else {
                down_idx_ = HitTestDesktop(tx, ty);  // 0..10 or SIZE_MAX
            }
            last_input_was_dpad_ = false;
            // v1.8 Input-source latch: touch-down is unambiguously a MOUSE/touch
            // event.  Switch to MOUSE mode and show cursor.  Spec: "touch = MOUSE
            // source, no auto-revert on touch lift."
            active_input_source_ = InputSource::MOUSE;
            if (cursor_ref_) {
                cursor_ref_->SetVisible(true);
            }
            // v1.8.23: coyote-timing — record the tick at finger-down.
            coyote_.down_tick = armGetSystemTick();
            UL_LOG_INFO("qdesktop: touch_down x=%d y=%d hit=%zu",
                        tx, ty, down_idx_);
        } else {
            // ── TouchMove ──────────────────────────────────────────────────
            last_touch_x_ = tx;
            last_touch_y_ = ty;
        }
    } else if (was_touch_active_last_frame_) {
        // ── TouchUp ────────────────────────────────────────────────────────
        UL_LOG_INFO("qdesktop: touch_up x=%d y=%d down_idx=%zu pressed=%d",
                    last_touch_x_, last_touch_y_, down_idx_,
                    static_cast<int>(pressed_));

        if (pressed_) {
            // Re-resolve the lift hit using the same priority order.
            const size_t fav_lift = HitTestFavorites(last_touch_x_, last_touch_y_);
            size_t lift_hit;
            if (fav_lift < icon_count_) {
                lift_hit = MAX_ICONS + 1u + fav_lift;
            } else {
                lift_hit = HitTestDesktop(last_touch_x_, last_touch_y_);
            }
            const s32 dx = last_touch_x_ - down_x_;
            const s32 dy = last_touch_y_ - down_y_;
            const s32 dist_sq = dx * dx + dy * dy;
            const s32 tol     = CLICK_TOLERANCE_PX;
            const s32 tol_sq  = tol * tol;

            // v1.8.23: coyote-timing — tap-window ceiling check (4c).
            // Reject taps held longer than 250 ms (TAP_MAX_TICKS); these are
            // deliberate holds, not misclicks.
            {
                const u64 now_tick = armGetSystemTick();
                const u64 elapsed  = (now_tick >= coyote_.down_tick)
                                     ? (now_tick - coyote_.down_tick) : 0ULL;
                if (elapsed > TAP_MAX_TICKS) {
                    UL_LOG_INFO("qdesktop: touch_up coyote-reject ticks=%llu > %llu (hold; cancel)",
                                static_cast<unsigned long long>(elapsed),
                                static_cast<unsigned long long>(TAP_MAX_TICKS));
                    pressed_ = false;
                    was_touch_active_last_frame_ = touch_active_now;
                    return;
                }
                // v1.8.23: coyote-timing — double-launch suppression lockout.
                if ((now_tick - coyote_.last_launch_tick) < RELAUNCH_LOCKOUT_TICKS) {
                    UL_LOG_INFO("qdesktop: touch_up coyote-lockout ticks=%llu < %llu",
                                static_cast<unsigned long long>(now_tick - coyote_.last_launch_tick),
                                static_cast<unsigned long long>(RELAUNCH_LOCKOUT_TICKS));
                    pressed_ = false;
                    was_touch_active_last_frame_ = touch_active_now;
                    return;
                }
            }

            if (lift_hit != SIZE_MAX && lift_hit == down_idx_
                    && dist_sq <= tol_sq) {
                // Cycle E1: clear ALL touch state BEFORE any nested-fade
                // operation (LaunchIcon / LoadMenu / OpenLaunchpadFiltered).
                pressed_                     = false;
                down_idx_                    = MAX_ICONS;
                was_touch_active_last_frame_ = touch_active_now;

                if (lift_hit > MAX_ICONS) {
                    // Favorites strip tap → resolve to icons_[] idx and launch.
                    const size_t icons_idx = lift_hit - MAX_ICONS - 1u;
                    if (icons_idx < icon_count_) {
                        // Slice 5 Patch 4: tap on stale favorite is impossible
                        // here (HitTestFavorites returned an in-range idx);
                        // launch through normal path.
                        UL_LOG_INFO("qdesktop: launch favorite icons_idx=%zu (touch click)",
                                    icons_idx);
                        LaunchIcon(icons_idx);
                        coyote_.last_launch_tick = armGetSystemTick();
                    }
                    return;
                }
                if (lift_hit < kDesktopFolderCount) {
                    // Folder tap → Launchpad pre-filtered (no lockout: no LaunchIcon call).
                    UL_LOG_INFO("qdesktop: folder tap fid=%zu (touch click)",
                                lift_hit);
                    OpenLaunchpadFiltered(static_cast<DesktopFolderId>(lift_hit));
                    return;
                }
                if (lift_hit < kDesktopFolderCount + BUILTIN_ICON_COUNT) {
                    // Dock cell tap → LaunchIcon(dock_i).
                    const size_t dock_i = lift_hit - kDesktopFolderCount;
                    if (dock_i < icon_count_) {
                        UL_LOG_INFO("qdesktop: launch dock_i=%zu (touch click)",
                                    dock_i);
                        LaunchIcon(dock_i);
                        coyote_.last_launch_tick = armGetSystemTick();
                    }
                    return;
                }
            }
        }

        pressed_   = false;
        down_idx_  = MAX_ICONS;
    }

    was_touch_active_last_frame_ = touch_active_now;
}

// ── LaunchIcon ────────────────────────────────────────────────────────────────

namespace {
    // Cycle E1 (SP4.13): re-entry guard.
    //
    // BUG (manifested in SP4.12.1 hardware test):
    //   Tapping a Themes/Settings icon caused infinite recursion:
    //
    //     QdDesktopIconsElement::OnInput     [user touch_up]
    //       LaunchIcon(themes_idx)
    //         ShowThemesMenu()
    //           MenuApplication::LoadMenu(MenuType::Themes, fade=true)
    //             Application::FadeOut()       [busy-loops CallForRender]
    //               Application::OnRender()    [per-frame, fires layout OnInput]
    //                 QdDesktopIconsElement::OnInput  [pressed_ STILL true,
    //                                                  was_touch_active_last_frame_
    //                                                  STILL true → re-enters
    //                                                  TouchUp branch]
    //                   LaunchIcon(themes_idx)        ← RE-ENTERS
    //                     ShowThemesMenu()            ← infinite recursion
    //
    //   ~2 KB of stack per recursion level (UL_LOG_INFO → LogImpl → vsnprintf).
    //   With a 1 MB main-thread stack the recursion depth tops out at ~500
    //   levels, then SP overruns the stack region → null deref in vsnprintf
    //   scratch.  Crash report 01777148075 confirms this exact chain.
    //
    //   For Application/Nro launches (Pokemon, RetroArch, Tinwoo) the same
    //   re-entry path fired smi::LaunchApplication TWICE with the same app_id,
    //   confusing uSystem's launch state machine and bouncing the game back
    //   to the desktop.  That's the "tries to load → black → back to desktop"
    //   pattern the user reported.
    //
    // FIX:
    //   A static atomic guards the function body.  Any call that finds the
    //   guard already set (i.e. we're already inside LaunchIcon, presumably
    //   from a nested OnInput fired by FadeOut's CallForRender loop) is
    //   discarded.  The guard is reset when the original call returns.
    //
    //   This is safe because LaunchIcon is only ever called on the main UI
    //   thread (from OnInput / Plutonium's render loop).  No cross-thread
    //   races possible.  std::atomic is used purely for the relaxed memory
    //   ordering on the load/store; a plain bool with relaxed access would
    //   work but std::atomic documents the intent.
    std::atomic<bool> g_launch_icon_in_flight{false};
}

void QdDesktopIconsElement::LaunchIcon(size_t i) {
    if (g_launch_icon_in_flight.load(std::memory_order_relaxed)) {
        UL_LOG_WARN("qdesktop: LaunchIcon(%zu) re-entered while another launch "
                    "is in flight — discarding (cycle E1 guard)", i);
        return;
    }
    g_launch_icon_in_flight.store(true, std::memory_order_relaxed);
    // Reset on every exit path via this scope-exit helper.
    struct LaunchIconReentryGuard {
        ~LaunchIconReentryGuard() {
            g_launch_icon_in_flight.store(false, std::memory_order_relaxed);
        }
    } reentry_guard;

    if (i >= icon_count_) {
        return;
    }
    const NroEntry &entry = icons_[i];

    switch (entry.kind) {
        case IconKind::Builtin:
            // Cycle J: wire each dock slot to its existing handler.
            // Cycle K-noterminal: dock_slot auto-renumbered after Terminal
            // removal — Vault=0, Monitor=1, Control=2, About=3.
            // Cycle K-TrackD: AllPrograms (QdLaunchpad) promoted as slot 4.
            switch (entry.dock_slot) {
                case 0: // Vault — uMenu's native file browser
                    if (g_MenuApplication) {
                        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Vault);
                    }
                    break;
                case 1: // Monitor — K-cycle promoted QdMonitorHostLayout
                    if (g_MenuApplication) {
                        UL_LOG_INFO("qdesktop: Builtin Monitor tapped -> LoadMenu(Monitor)");
                        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Monitor);
                    }
                    break;
                case 2: // Control — opens existing Dev menu (qd_HomeMiniMenu)
                    UL_LOG_INFO("qdesktop: Builtin Control tapped → ShowDevMenu");
                    ::ul::menu::qdesktop::ShowDevMenu();
                    break;
                case 3: // About — K-cycle promoted QdAboutLayout (direct IMenuLayout)
                    if (g_MenuApplication) {
                        UL_LOG_INFO("qdesktop: Builtin About tapped -> LoadMenu(About)");
                        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::About);
                    }
                    break;
                case 4: // AllPrograms — K-TrackD promoted QdLaunchpadHostLayout
                    if (g_MenuApplication) {
                        UL_LOG_INFO("qdesktop: Builtin AllPrograms tapped -> LoadMenu(Launchpad)");
                        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Launchpad);
                    }
                    break;
                case 5: // VaultSplit — opens regular Vault for now
                    if (g_MenuApplication) {
                        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Vault);
                    }
                    UL_LOG_INFO("qdesktop: Builtin VaultSplit tapped → Vault (split-mode TBD)");
                    break;
                default:
                    UL_LOG_WARN("qdesktop: LaunchIcon: unknown dock_slot=%u",
                                static_cast<unsigned>(entry.dock_slot));
                    break;
            }
            return;

        case IconKind::Nro:
            if (entry.nro_path[0] != '\0') {
                UL_LOG_INFO("qdesktop: LaunchHomebrewLibraryApplet '%s' — Finalize uMenu",
                            entry.nro_path);
                smi::LaunchHomebrewLibraryApplet(
                    std::string(entry.nro_path), std::string(""));
                // CRITICAL (cycle C1): mirror the upstream
                // MainMenuLayout::HandleHomebrewLaunch path
                // (ui_MainMenuLayout.cpp:1529-1531). Without these two calls
                // uMenu re-asserts foreground after smi::LaunchHomebrewLibraryApplet
                // returns, which silently kills hbloader before the NRO can
                // display. This is why Tinwoo (and every other NRO) appeared
                // to "do nothing" when launched from the desktop.
                if (g_MenuApplication) {
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                }
            } else {
                UL_LOG_WARN("qdesktop: LaunchIcon: Nro entry '%s' has empty nro_path — launch skipped",
                            entry.name);
            }
            return;

        case IconKind::Application:
            if (entry.app_id != 0) {
                if (g_MenuApplication == nullptr) {
                    UL_LOG_WARN("qdesktop: LaunchApplication: g_MenuApplication"
                                " null — skipping launch of 0x%016llx",
                                static_cast<unsigned long long>(entry.app_id));
                    return;
                }

                // ── Cycle G1 (SP4.15): suspended-app handling ──────────────
                // Upstream MainMenuLayout has rich resume/close-suspended
                // logic at ui_MainMenuLayout.cpp:175-189; that whole branch
                // was gated off in QDESKTOP_MODE awaiting QdContextMenu.
                // Now we wire the equivalent inline so the user can:
                //   • Press the SAME game's icon while it's suspended
                //     → smi::ResumeApplication() (no fresh launch)
                //   • Press a DIFFERENT game's icon while one is suspended
                //     → confirmation dialog → smi::TerminateApplication()
                //       then smi::LaunchApplication() the new one
                //
                // Without this, tapping any icon after a game has been
                // home-buttoned quietly does nothing because Horizon won't
                // create a second Application while one is already alive.
                // (Why D2 cycle's launch order is preserved below: see prior
                // commit history — 26fa3de E1 + 2d11260 D2 — the SP4.10
                // ordering Launch→fade→Finalize is hardware-validated and
                // we DO NOT reorder it.)
                const u64 suspended_app_id = g_GlobalSettings.system_status.suspended_app_id;
                if (suspended_app_id != 0) {
                    if (suspended_app_id == entry.app_id) {
                        UL_LOG_INFO("qdesktop: ResumeApplication 0x%016llx (matches suspended)",
                                    static_cast<unsigned long long>(entry.app_id));
                        const auto rrc = smi::ResumeApplication();
                        if (R_SUCCEEDED(rrc)) {
                            g_MenuApplication->FadeOutToNonLibraryApplet();
                            g_MenuApplication->Finalize();
                        } else {
                            UL_LOG_WARN("qdesktop: ResumeApplication failed rc=0x%X",
                                        static_cast<unsigned>(rrc));
                            g_MenuApplication->ShowNotification("Resume failed");
                        }
                        return;
                    }
                    // Different app already running — ask before terminating.
                    const auto opt = g_MenuApplication->DisplayDialog(
                        std::string(entry.name),
                        std::string("A different game is currently running.\n"
                                    "Close it to launch this one?"),
                        { std::string("Yes, close and launch"),
                          std::string("Cancel") },
                        true
                    );
                    if (opt != 0) {
                        UL_LOG_INFO("qdesktop: user declined close-suspended;"
                                    " keeping running app 0x%016llx",
                                    static_cast<unsigned long long>(suspended_app_id));
                        return;
                    }
                    const auto trc = smi::TerminateApplication();
                    if (R_FAILED(trc)) {
                        UL_LOG_WARN("qdesktop: TerminateApplication failed rc=0x%X"
                                    " — aborting new launch",
                                    static_cast<unsigned>(trc));
                        g_MenuApplication->ShowNotification("Close failed");
                        return;
                    }
                    g_GlobalSettings.ResetSuspendedApplication();
                    // fall through to fresh-launch path
                }

                // ── Fresh launch (no suspended app, or just terminated one).
                const Result rc = smi::LaunchApplication(entry.app_id);
                if (R_SUCCEEDED(rc)) {
                    UL_LOG_INFO("qdesktop: LaunchApplication ok 0x%016llx — fade + Finalize",
                                static_cast<unsigned long long>(entry.app_id));
                    g_MenuApplication->FadeOutToNonLibraryApplet();
                    g_MenuApplication->Finalize();
                } else {
                    UL_LOG_WARN("qdesktop: LaunchApplication(0x%016llx) failed rc=0x%X — desktop stays live",
                                static_cast<unsigned long long>(entry.app_id),
                                static_cast<unsigned>(rc));
                    g_MenuApplication->ShowNotification("Launch failed");
                }
            }
            return;

        case IconKind::Special: {
            // Dispatch to the matching ShowXxx() helper from ui_Common.
            // The mapping mirrors the upstream MainMenuLayout switch-case for
            // SpecialEntry types — see ui_MainMenuLayout.cpp:265-305.
            using ET = ul::menu::EntryType;
            const auto et = static_cast<ET>(entry.special_subtype);
            switch (et) {
                case ET::SpecialEntrySettings:    ::ul::menu::ui::ShowSettingsMenu(); break;
                case ET::SpecialEntryAlbum:       ::ul::menu::ui::ShowAlbum();        break;
                case ET::SpecialEntryThemes:      ::ul::menu::ui::ShowThemesMenu();   break;
                case ET::SpecialEntryControllers: ::ul::menu::ui::ShowController();   break;
                case ET::SpecialEntryMiiEdit:     ::ul::menu::ui::ShowMiiEdit();      break;
                case ET::SpecialEntryWebBrowser:  ::ul::menu::ui::ShowWebPage();      break;
                case ET::SpecialEntryUserPage:    ::ul::menu::ui::ShowUserPage();     break;
                case ET::SpecialEntryAmiibo:      ::ul::menu::ui::ShowCabinet();      break;
                default:
                    UL_LOG_WARN("qdesktop: Special launch unknown subtype=%u",
                                static_cast<unsigned>(entry.special_subtype));
                    break;
            }
            return;
        }
    }
    // Unreachable: exhaustive switch above covers all IconKind values.
}

// ── SetApplicationEntries ─────────────────────────────────────────────────────
// Replaces the Application section of the icon grid with freshly provided entries.
// Truncates icon_count_ back to app_entry_start_idx_ to discard stale apps from the
// previous call, then re-appends matching entries up to MAX_ICONS.
//
// Only entries satisfying ALL of the following are added:
//   - entry.Is<EntryType::Application>()
//   - entry.app_info.CanBeLaunched()     (has contents, not awaiting update)
//   - entry.app_info.app_id != 0
//
// The display name is taken from entry.control.name (non-empty) or synthesised as
// "App 0x<hex_app_id>" for titles with empty NACP.
//
// Fallback icon colour is DJB2-derived from the display name (same as NRO path).
// If entry.control.icon_path is non-empty (custom_icon_path), it is stored in
// NroEntry::icon_path so OnRender can load the JPEG on the first paint.

void QdDesktopIconsElement::SetApplicationEntries(
        const std::vector<ul::menu::Entry> &entries)
{
    // Bug #2/#3 fix (v1.8): invalidate the favorites in-process cache so the
    // strip resolves icon indices against the freshly-rebuilt icons_[] array on
    // the next PaintFavoritesStrip call.  Must happen BEFORE FreeCachedText so
    // any icon_tex_ entries that the strip relied on are freed in the loop below,
    // forcing lazy re-creation by PaintFavoritesStrip.
    InvalidateFavoritesCache();

    // v1.8.22b: clear the negative-load memoization set on every reload.
    // g_has_no_asset_ is keyed by icon_path / "app:<hex16>".  On the FIRST boot
    // prewarm the uSystem JPEG cache is empty (the game was never launched), so
    // LoadAppIconFromUSystemCache returns false and the key is inserted here.
    // When the user later launches and exits the game, uSystem writes the JPEG
    // and SetApplicationEntries is called again — but without this clear the key
    // remains blacklisted, PrewarmAllIcons skips it, and the icon stays gray
    // permanently even though the JPEG is now available on disk.
    // Clearing here is safe: g_has_no_asset_ is a negative-cache optimisation
    // only; missing it costs one extra disk-or-NS probe per entry per reload,
    // which is negligible compared to the prewarm thread's existing I/O budget.
    g_has_no_asset_.clear();

    // Truncate back to the Application slot boundary (idempotent reload).
    // Free cached name/glyph text textures for the slots about to be reused
    // so the next paint re-rasterises with the new entry's name + glyph.
    for (size_t i = app_entry_start_idx_; i < icon_count_; ++i) {
        FreeCachedText(i);
    }
    icon_count_ = app_entry_start_idx_;

    size_t added = 0;
    for (const auto &entry : entries) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }

        // Filter: must be a launchable Application.
        if (!entry.Is<ul::menu::EntryType::Application>()) {
            continue;
        }
        if (!entry.app_info.CanBeLaunched()) {
            continue;
        }
        if (entry.app_info.app_id == 0) {
            continue;
        }

        NroEntry &e = icons_[icon_count_];

        // ── Display name ─────────────────────────────────────────────────
        if (!entry.control.name.empty()) {
            snprintf(e.name, sizeof(e.name), "%s", entry.control.name.c_str());
        } else {
            snprintf(e.name, sizeof(e.name), "App %016llx",
                     static_cast<unsigned long long>(entry.app_info.app_id));
        }

        // ── Glyph and fallback colour ────────────────────────────────────
        // Application entries use Unknown category (glyph '?', grey) unless
        // the NACP name happens to match a known category keyword.  We try the
        // classifier on the NACP name and an empty file stem (no NRO path).
        {
            CategoryResult cat = Classify(e.name, "");
            e.glyph = cat.glyph;
            e.bg_r  = cat.r;
            e.bg_g  = cat.g;
            e.bg_b  = cat.b;
        }

        // ── Paths ─────────────────────────────────────────────────────────
        e.nro_path[0] = '\0'; // Applications have no NRO path.

        if (entry.control.custom_icon_path && !entry.control.icon_path.empty()) {
            snprintf(e.icon_path, sizeof(e.icon_path), "%s",
                     entry.control.icon_path.c_str());
        } else {
            // F5 (stabilize-4): pre-write the stable NS cache key into icon_path.
            // The lazy-load block in OnRender contains a routing guard that checks
            // the "app:" prefix to distinguish NS cache keys from real SD file
            // paths — so icon_path != '\0' safely routes to the NS branch, not
            // the custom-JPEG-on-SD branch.  PaintIconCell can then resolve a
            // cache_.Get hit on the first frame without waiting for the lazy-load
            // snprintf to run.  The key format "app:%016llx" is identical to the
            // one fabricated in OnRender so cache_.Get finds the same entry.
            snprintf(e.icon_path, sizeof(e.icon_path), "app:%016llx",
                     static_cast<unsigned long long>(entry.app_info.app_id));
        }

        // ── Metadata ─────────────────────────────────────────────────────
        e.is_builtin  = false;
        e.dock_slot   = 0;
        e.category    = NroCategory::Unknown;
        e.kind        = IconKind::Application;
        e.app_id      = entry.app_info.app_id;
        e.special_subtype = 0;
        // K+1 Phase 1: classify after app_id is set so IsNintendoPublisher
        // can apply the title-id heuristic.
        e.icon_category = ClassifyEntry(e);
        e.icon_loaded = false; // forces lazy load on first OnRender pass

        // Fix D (v1.6.12): populate auto-folder side table.
        // Stable ID for Application entries is "app:<hex16>".
        {
            char stable_id[32];
            snprintf(stable_id, sizeof(stable_id), "app:%016llx",
                     static_cast<unsigned long long>(e.app_id));
            const ClassifyKind ck =
                IsNintendoPublisher(e.app_id)
                ? ClassifyKind::NintendoGame
                : ClassifyKind::ThirdPartyGame;
            g_entry_classification_[stable_id] = ck;
            RegisterClassification(stable_id, ck);
        }

        ++icon_count_;
        ++added;
    }

    UL_LOG_INFO("qdesktop: SetApplicationEntries: in=%zu added=%zu total=%zu",
                entries.size(), added, icon_count_);

    // v1.7.0-stabilize-7 Slice 4: icon set changed -> folder counts must
    // refresh on the next paint.
    MarkDesktopFolderLayoutDirty();
}

// ── SetSpecialEntries ─────────────────────────────────────────────────────────
// Appends Switch system-applet shortcut icons (Settings, Album, Themes,
// Controllers, MiiEdit, WebBrowser, UserPage, Amiibo) to the icon grid.
// MUST be called AFTER SetApplicationEntries on the same Reload pass — this
// method does NOT truncate icon_count_, only appends.  Each Special entry is
// stored with kind=IconKind::Special and special_subtype=static_cast<u16>(EntryType).
//
// Display name policy:
//   - Use entry.control.name when non-empty (typically a localised label).
//   - Fall back to a hardcoded English label keyed off EntryType.
// Glyph + colour come from a fixed table per applet — NACP classifier does not
// know about system applets so we map them explicitly here.

void QdDesktopIconsElement::SetSpecialEntries(
        const std::vector<ul::menu::Entry> &entries)
{
    using ET = ul::menu::EntryType;
    struct SpecialDef {
        ET   type;
        const char *fallback_name;
        char glyph;
        u8   r, g, b;
    };
    static constexpr SpecialDef SPECIAL_DEFS[] = {
        { ET::SpecialEntrySettings,    "Settings",    'S', 0x60, 0xA5, 0xFA },
        { ET::SpecialEntryAlbum,       "Album",       'P', 0xF8, 0x71, 0x71 },  // 'P' for Photos
        { ET::SpecialEntryThemes,      "Themes",      'T', 0xC0, 0x84, 0xFC },
        { ET::SpecialEntryControllers, "Controllers", 'C', 0x4A, 0xDE, 0x80 },
        { ET::SpecialEntryMiiEdit,     "Mii",         'M', 0xFB, 0xBF, 0x24 },
        { ET::SpecialEntryWebBrowser,  "Browser",     'W', 0x38, 0xBD, 0xF8 },
        { ET::SpecialEntryUserPage,    "User",        'U', 0xE0, 0xE0, 0xF0 },
        { ET::SpecialEntryAmiibo,      "Amiibo",      'A', 0xF0, 0x70, 0xC0 },
    };

    // v1.2.7 fix for first-boot Special-entry invisibility:
    // Iterate the static SPECIAL_DEFS table unconditionally rather than
    // filtering the input vector. The input vector becomes a localized-name
    // override map. This avoids the bug where LoadEntries returns empty in
    // applet-mode privilege denial (rc=0x1F800), the records.bin fallback
    // wipes specials (records.bin contains apps only), and SetSpecialEntries
    // ends up with zero IsSpecial() matches in `entries`. The launch path
    // already keys off special_subtype, not entry_path, so the desktop click
    // dispatch works without an .m.json file on disk.
    std::unordered_map<ET, std::string> name_overrides;
    for (const auto &entry : entries) {
        if (entry.IsSpecial() && !entry.control.name.empty()) {
            name_overrides.emplace(entry.type, entry.control.name);
        }
    }

    size_t added = 0;
    for (const auto &def : SPECIAL_DEFS) {
        if (icon_count_ >= MAX_ICONS) {
            break;
        }

        NroEntry &e = icons_[icon_count_];

        auto it = name_overrides.find(def.type);
        if (it != name_overrides.end()) {
            snprintf(e.name, sizeof(e.name), "%s", it->second.c_str());
        } else {
            snprintf(e.name, sizeof(e.name), "%s", def.fallback_name);
        }

        e.glyph    = def.glyph;
        e.bg_r     = def.r;
        e.bg_g     = def.g;
        e.bg_b     = def.b;

        e.nro_path[0]  = '\0';
        e.icon_path[0] = '\0';
        e.is_builtin   = false;
        e.dock_slot    = 0;
        e.category     = NroCategory::Unknown;
        e.icon_category = IconCategory::Extras;
        e.icon_loaded  = false;
        e.kind         = IconKind::Special;
        e.app_id       = 0;
        e.special_subtype = static_cast<u16>(def.type);

        ++icon_count_;
        ++added;
    }

    UL_LOG_INFO("qdesktop: SetSpecialEntries: in=%zu added=%zu total=%zu (table-driven)",
                entries.size(), added, icon_count_);

    // v1.7.0-stabilize-7 Slice 4: icon set changed -> folder counts must
    // refresh on the next paint.
    MarkDesktopFolderLayoutDirty();
}

// ── LoadJpegIconToCache ───────────────────────────────────────────────────────
// v1.8.20 (Change 2): kernel-direct file I/O replaces IMG_Load (which internally
// calls fopen/fread behind the fsdev POSIX shim, adding an unnecessary buffering
// layer for a file that is read exactly once).
//
// Algorithm:
//   1. fsdevGetDeviceFileSystem("sdmc") → FsFileSystem*
//   2. fsFsOpenFile → FsFile; fsFileGetSize for capacity check.
//   3. fsFileRead(off=0, buf, file_size) — single read into heap buffer.
//   4. Close FsFile immediately (no fd held during decode).
//   5a. BMP fast path: detect "BM" magic bytes; parse pixel-data offset from
//       BITMAPFILEHEADER at buf[0x0A] (u32 LE); call SDL_RWFromMem + IMG_Load_RW.
//       (SDL_image handles BMP correctly from memory; the "fast path" label
//       means we avoid the double-open that the old IMG_Load path implied.)
//   5b. JPEG path: SDL_RWFromMem + IMG_Load_RW on the raw buffer.
//       Both paths converge at the SDL surface → ABGR8888 → cache Put() codepath.
//
// Fallback: any failure emits MakeFallbackIcon and returns false.
// The caller always receives a valid cache entry for cache_key regardless.

bool QdDesktopIconsElement::LoadJpegIconToCache(const char *jpeg_path,
                                                 const char *cache_key)
{
    // ── v1.8.22e B66 defense-in-depth: reject romfs:/ paths at function root ──
    // LoadJpegIconToCache is fundamentally sdmc-only — it calls
    // fsdevGetDeviceFileSystem("sdmc") below, strips the "sdmc:" prefix, and
    // hands the rest to fsFsOpenFile on the sdmc filesystem.  A romfs:/ path
    // fails fsFsOpenFile with rc=0x2EEA02 (FS module 2 / desc 6004 = path-not-
    // found), then falls through do_fallback() which Puts a gray MakeFallbackIcon
    // BGRA into the shared cache keyed by the romfs path, displacing the real
    // PNG that 2a-romfs branches in PaintIconCell / PaintCell load lazily via
    // pu::ui::render::LoadImageFromFile.
    //
    // Caller-side guards exist (PrewarmAllIcons :2653-2657, Launchpad::Open
    // :282-286) but the bug class is closed only by rejecting at the function
    // root.  Return false WITHOUT do_fallback() — no Put, no poison.
    if (jpeg_path != nullptr
            && jpeg_path[0] == 'r' && jpeg_path[1] == 'o'
            && jpeg_path[2] == 'm' && jpeg_path[3] == 'f'
            && jpeg_path[4] == 's' && jpeg_path[5] == ':') {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: romfs:/ path rejected at root "
                    "(use 2a-romfs LoadImageFromFile lazy path) '%s'", jpeg_path);
        return false;
    }

    // ── helper lambda: emit fallback and return false ─────────────────────────
    // v1.8.22c Edit 2: do NOT poison the cache with gray when the cache_key is
    // an "app:<hex>" Application key.  PaintIconCell section 2c installs
    // DefaultApplication.png per-frame on a Get-miss; a stored gray would
    // outrank that.  Worse, gray BGRAs persist to sdmc:/ulaunch/cache/icons.bgra
    // and survive process death/respawn — gray would lock in across boots.
    // For NRO / SD-JPEG keys the existing gray-block fallback stays.
    auto do_fallback = [&]() -> bool {
        const bool is_app_key = (cache_key != nullptr
                                 && cache_key[0] == 'a' && cache_key[1] == 'p'
                                 && cache_key[2] == 'p' && cache_key[3] == ':');
        if (is_app_key) {
            return false;  // let section 2c install DefaultApplication.png
        }
        u8 *fallback = MakeFallbackIcon(cache_key);
        if (fallback != nullptr) {
            std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
            GetSharedIconCache().Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    };

    // ── 1. Obtain sdmc FsFileSystem* ─────────────────────────────────────────
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (!sdmc) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: sdmc not mounted for '%s'", jpeg_path);
        return do_fallback();
    }

    // Strip "sdmc:" prefix — fsFsOpenFile takes paths without device prefix.
    const char *fs_path = jpeg_path;
    if (fs_path[0]=='s' && fs_path[1]=='d' && fs_path[2]=='m' &&
        fs_path[3]=='c' && fs_path[4]==':') {
        fs_path += 5;
    }

    // ── 2. Open file and get size ─────────────────────────────────────────────
    FsFile fsf;
    Result rc = fsFsOpenFile(sdmc, fs_path, FsOpenMode_Read, &fsf);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: fsFsOpenFile failed '%s' rc=0x%X",
                    jpeg_path, rc);
        return do_fallback();
    }
    s64 file_size = 0;
    rc = fsFileGetSize(&fsf, &file_size);
    if (R_FAILED(rc) || file_size <= 0 || file_size > (8 * 1024 * 1024)) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: bad file size '%s' sz=%lld rc=0x%X",
                    jpeg_path, static_cast<long long>(file_size), rc);
        fsFileClose(&fsf);
        return do_fallback();
    }

    // ── 3. Read entire file into heap buffer (single kernel IPC call) ─────────
    u8 *file_buf = new(std::nothrow) u8[static_cast<size_t>(file_size)];
    if (!file_buf) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: OOM for '%s' size=%lld",
                    jpeg_path, static_cast<long long>(file_size));
        fsFileClose(&fsf);
        return do_fallback();
    }
    u64 bytes_read = 0;
    rc = fsFileRead(&fsf, /*off=*/0, file_buf, static_cast<u64>(file_size), 0, &bytes_read);
    fsFileClose(&fsf);  // done with the FsFile; close immediately
    if (R_FAILED(rc) || static_cast<s64>(bytes_read) < file_size) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: read failed '%s' rc=0x%X got=%llu need=%lld",
                    jpeg_path, rc,
                    static_cast<unsigned long long>(bytes_read),
                    static_cast<long long>(file_size));
        delete[] file_buf;
        return do_fallback();
    }

    // ── 4. Detect BMP vs JPEG and decode via SDL_RWFromMem ───────────────────
    // Both BMP and JPEG are decoded through IMG_Load_RW — same code path,
    // single SDL_Surface ownership.  No pixel layout special-casing for BMP
    // vs JPEG at this level; SDL_image normalises the output surface format.
    SDL_RWops *rw = SDL_RWFromMem(file_buf, static_cast<int>(file_size));
    if (!rw) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: SDL_RWFromMem failed '%s': %s",
                    jpeg_path, SDL_GetError());
        delete[] file_buf;
        return do_fallback();
    }
    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);  // frees rw on return
    delete[] file_buf;                                   // raw file bytes done
    if (!raw) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: IMG_Load_RW failed '%s': %s",
                    jpeg_path, IMG_GetError());
        return do_fallback();
    }

    // ── 5. Convert to ABGR8888 (correct byte order for ScaleToBgra64) ────────
    // SDL_PIXELFORMAT_ABGR8888 on AArch64 LE stores pixels as [R,G,B,A] in
    // memory — the byte order that ScaleToBgra64 and cache Put() expect.
    // (SDL_PIXELFORMAT_RGBA8888 stores as [A,B,G,R] in memory on LE — wrong.)
    SDL_Surface *rgba_le = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (!rgba_le) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: ABGR convert failed '%s': %s",
                    jpeg_path, SDL_GetError());
        return do_fallback();
    }

    if (SDL_LockSurface(rgba_le) != 0) {
        UL_LOG_WARN("qdesktop: LoadJpegIconToCache: SDL_LockSurface failed '%s': %s",
                    jpeg_path, SDL_GetError());
        SDL_FreeSurface(rgba_le);
        return do_fallback();
    }

    const s32 src_w = rgba_le->w;
    const s32 src_h = rgba_le->h;
    const u8 *le_pixels = static_cast<const u8 *>(rgba_le->pixels);

    // v1.8.15 Fix B: short-scope lock — pixel data ready, only Put needs the lock.
    {
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        GetSharedIconCache().Put(cache_key, le_pixels, src_w, src_h);
    }

    SDL_UnlockSurface(rgba_le);
    SDL_FreeSurface(rgba_le);

    UL_LOG_INFO("qdesktop: LoadJpegIconToCache: loaded '%s' (%d×%d) → cache key %s",
                jpeg_path, src_w, src_h, cache_key);
    return true;
}

// ── LoadAppIconFromUSystemCache ───────────────────────────────────────────────
//
// F2b (stabilize-6 / O-C): shipped Application icon read from disk.
//
// uMenu runs as a library applet and is denied NsApplicationControlSource_Storage
// access by the firmware (rc=0x196002).  CacheOnly succeeds only when Horizon's
// own home menu has previously displayed the title and warmed the cache.  In
// CFW-only setups (the canonical Q OS deployment), the real home menu never
// runs, so every NS lookup misses regardless of which API is called.
//
// The fix is to read the JPEG from a disk path that uSystem (running as a
// SystemApplet) populates by extracting the same NACP icon bytes via its own
// nsextGetApplicationControlData call.  Two paths are tried in order:
//
//   1. sdmc:/ulaunch/cache/app/<APPID16HEX_UPPER>.jpg
//      Populated by app_ControlCache.cpp::CacheApplicationIcon() in fork
//      uSystem.  Primary path once fork uSystem replaces upstream v1.2.0
//      uSystem at /atmosphere/contents/0100000000001000/exefs.nsp.
//
//   2. sdmc:/switch/qos-app-icons/<APPID16HEX_LOWER>.jpg
//      Creator-curated manual drop.  Permanent fallback for titles uSystem
//      could not extract (or for users who choose not to deploy fork uSystem).
//
// The 8 MB upper bound matches MAX_NRO_BODY (historical) and protects against
// pathologically large icons; real NACP icons are ≤256 KB.

bool QdDesktopIconsElement::LoadAppIconFromUSystemCache(const u64 app_id,
                                                        const char *cache_key)
{
    char sd_path[96];

    // Path 1: uSystem cache (uppercase hex; matches FormatProgramId in
    // util_String.cpp:6-10 of the fork uSystem source).
    snprintf(sd_path, sizeof(sd_path),
             "sdmc:/ulaunch/cache/app/%016llX.jpg",
             static_cast<unsigned long long>(app_id));
    {
        struct stat st;
        if (stat(sd_path, &st) == 0 && st.st_size > 0
                && st.st_size <= (8 * 1024 * 1024)) {
            if (LoadJpegIconToCache(sd_path, cache_key)) {
                UL_LOG_INFO("qdesktop: shipped icon from uSystem cache app_id=0x%016llx",
                            static_cast<unsigned long long>(app_id));
                return true;
            }
        }
    }

    // Path 2: creator-curated manual drop (lowercase hex).
    snprintf(sd_path, sizeof(sd_path),
             "sdmc:/switch/qos-app-icons/%016llx.jpg",
             static_cast<unsigned long long>(app_id));
    {
        struct stat st;
        if (stat(sd_path, &st) == 0 && st.st_size > 0
                && st.st_size <= (8 * 1024 * 1024)) {
            if (LoadJpegIconToCache(sd_path, cache_key)) {
                UL_LOG_INFO("qdesktop: shipped icon from manual drop app_id=0x%016llx",
                            static_cast<unsigned long long>(app_id));
                return true;
            }
        }
    }

    return false;  // caller falls through to NS path / gray fallback
}

// ── LoadNsIconToCache ─────────────────────────────────────────────────────────
//
// Fetches the JPEG icon bytes for app_id directly from NS storage
// (NsApplicationControlSource_Storage), decodes them via SDL2_image using
// SDL_RWFromConstMem + IMG_Load_RW (no temporary file on SD card), applies the
// same double-convert path as LoadJpegIconToCache (RGBA8888 → ABGR8888), and
// inserts the result into cache_ under cache_key.
//
// On any failure the function generates a MakeFallbackIcon() solid-colour block
// and inserts that instead, so the caller always has a usable cache entry.
// Returns true if a real JPEG was decoded; false if the fallback was used.
//
// NsApplicationControlData is ~393 KB — heap-allocated to avoid stack overflow.

bool QdDesktopIconsElement::LoadNsIconToCache(const u64 app_id,
                                               const char *cache_key)
{
    // v1.8.22c Edit 2: cache_key for this function is always an "app:<hex>"
    // Application key (only call sites are PrewarmAllIcons + future lazy-load).
    // On any failure we MUST NOT Put a gray fallback — that would persist into
    // the on-disk icons.bgra blob and outrank section 2c's per-frame
    // DefaultApplication.png install.  The gray emit lambda below intentionally
    // does NOT call GetSharedIconCache().Put().  See do_fallback in
    // LoadJpegIconToCache for the same reasoning.
    auto fail_no_poison = [&]() -> bool {
        return false;
    };

    // Heap-allocate the control data struct (~393 KB — too large for stack).
    NsApplicationControlData *ctrl_data = new(std::nothrow) NsApplicationControlData;
    if (ctrl_data == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: OOM allocating NsApplicationControlData"
                    " for app_id=0x%016llx",
                    static_cast<unsigned long long>(app_id));
        return fail_no_poison();
    }

    u64 icon_size = 0;
    // First try NsApplicationControlSource_Storage (reads from storage if not
    // cached by the system).  In applet mode this can fail with
    // 0x196002 (permission denied) because the system hasn't exposed
    // read-only storage access to library applets on some firmware.
    // When it fails, retry with NsApplicationControlSource_CacheOnly — the
    // icon thumbnail is almost always warm in the NS cache after the home menu
    // has displayed it once.
    // F2 (stabilize-5): RC-B1 — use nsGetApplicationControlData (not nsext variant).
    // nsextGetApplicationControlData returns 0x196002 (permission denied) for library
    // applets on Erista firmware; the non-ext variant has applet-mode access.
    Result rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        app_id,
        ctrl_data,
        sizeof(NsApplicationControlData),
        &icon_size);

    if (R_FAILED(rc) || icon_size == 0) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: Storage source failed"
                    " for app_id=0x%016llx rc=0x%08x icon_size=%llu"
                    " — retrying with CacheOnly",
                    static_cast<unsigned long long>(app_id),
                    static_cast<unsigned int>(rc),
                    static_cast<unsigned long long>(icon_size));
        icon_size = 0;
        rc = nsGetApplicationControlData(  // F2 (stabilize-5): RC-B1
            NsApplicationControlSource_CacheOnly,
            app_id,
            ctrl_data,
            sizeof(NsApplicationControlData),
            &icon_size);
    }

    if (R_FAILED(rc) || icon_size == 0) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: both Storage and CacheOnly"
                    " failed for app_id=0x%016llx rc=0x%08x icon_size=%llu"
                    " — using fallback colour block",
                    static_cast<unsigned long long>(app_id),
                    static_cast<unsigned int>(rc),
                    static_cast<unsigned long long>(icon_size));
        delete ctrl_data;
        return fail_no_poison();
    }

    // Wrap the in-memory JPEG bytes in an SDL_RWops (no disk I/O).
    SDL_RWops *rw = SDL_RWFromConstMem(ctrl_data->icon, static_cast<int>(icon_size));
    if (rw == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_RWFromConstMem failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        delete ctrl_data;
        return fail_no_poison();
    }

    // IMG_Load_RW decodes the JPEG and frees rw (freesrc = 1).
    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);
    // rw is now freed regardless of success/failure — do not touch it again.
    delete ctrl_data;
    ctrl_data = nullptr;

    if (raw == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: IMG_Load_RW failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), IMG_GetError());
        return fail_no_poison();
    }

    // First conversion: normalize to RGBA8888.
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);

    if (rgba == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_ConvertSurface(RGBA) failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        return fail_no_poison();
    }

    // Second conversion: RGBA8888 → ABGR8888 to match the byte-order expected
    // by ScaleToBgra64 inside GetSharedIconCache().Put() (same reasoning as LoadJpegIconToCache).
    SDL_Surface *rgba_le = SDL_ConvertSurfaceFormat(rgba, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(rgba);

    if (rgba_le == nullptr) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_ConvertSurface(ABGR) failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        return fail_no_poison();
    }

    if (SDL_LockSurface(rgba_le) != 0) {
        UL_LOG_WARN("qdesktop: LoadNsIconToCache: SDL_LockSurface failed"
                    " for app_id=0x%016llx: %s",
                    static_cast<unsigned long long>(app_id), SDL_GetError());
        SDL_FreeSurface(rgba_le);
        return fail_no_poison();
    }

    const s32 src_w = rgba_le->w;
    const s32 src_h = rgba_le->h;
    const u8 *le_pixels = static_cast<const u8 *>(rgba_le->pixels);

    // v1.8.15 Fix B: short-scope lock around the cache mutation only.
    {
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        GetSharedIconCache().Put(cache_key, le_pixels, src_w, src_h);
    }

    SDL_UnlockSurface(rgba_le);
    SDL_FreeSurface(rgba_le);

    UL_LOG_INFO("qdesktop: LoadNsIconToCache: loaded app_id=0x%016llx"
                " (%d×%d icon_size=%llu) → cache key %s",
                static_cast<unsigned long long>(app_id),
                src_w, src_h,
                static_cast<unsigned long long>(icon_size),
                cache_key);
    return true;
}

// ── LoadNroIconToCache ────────────────────────────────────────────────────────
//
// Extract the JPEG icon from the ASET section of the NRO at nro_path via
// ExtractNroIcon, decode it, and insert the result into the icon cache under
// cache_key.  Falls back to a MakeFallbackIcon solid-colour block on any
// parse/decode failure.  Returns true if a real JPEG was decoded; false if the
// fallback was used.
//
// FreeNroIcon semantics (from qd_NroAsset.hpp):
//   FreeNroIcon(res) calls delete[] res.pixels (if non-null) then zeroes the
//   pointer and sets res.valid = false.  It is safe to call on an invalid result
//   (valid==false) because pixels is still a valid fallback buffer allocated by
//   MakeFallbackIcon inside ExtractNroIcon.  FreeNroIcon must be called in BOTH
//   the success and failure branches — see F-05 fix comment in NroIconResult.
//
// pixel snapshot rule: capture res.width/res.height to locals BEFORE calling
// FreeNroIcon so the log line reads from stack not from a freed struct.

bool QdDesktopIconsElement::LoadNroIconToCache(const char *nro_path,
                                               const char *cache_key)
{
    // Guard: reject null or empty path.
    if (nro_path == nullptr || nro_path[0] == '\0') {
        UL_LOG_WARN("qdesktop: LoadNroIconToCache: invalid nro_path (null or empty)"
                    " for cache_key=%s — using fallback colour block",
                    cache_key != nullptr ? cache_key : "(null)");
        u8 *fallback = MakeFallbackIcon(cache_key != nullptr ? cache_key : "");
        if (fallback != nullptr) {
            std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
            GetSharedIconCache().Put(cache_key, fallback,
                       static_cast<s32>(CACHE_ICON_W),
                       static_cast<s32>(CACHE_ICON_H));
            delete[] fallback;
        }
        return false;
    }

    // v1.8.19 negative-extract cache: if this path previously failed extraction,
    // skip the disk I/O and ASET parse entirely.  The file doesn't change while
    // uMenu is running, so a prior failure is deterministic across all passes.
    {
        const auto &failed_set = GetFailedExtractPaths();
        if (failed_set.count(nro_path) != 0) {
            UL_LOG_INFO("qdesktop: LoadNroIconToCache: skip known-failed path %s",
                        nro_path);
            return false;
        }
    }

    NroIconResult res = ExtractNroIcon(nro_path);

    // Sanity-check the result dimensions before we hand pixels to cache_.Put.
    // The 4096 upper bound guards against absurdly large JPEG blobs that
    // ExtractNroIcon accepted but that would overflow ScaleToBgra64's buffers.
    if (res.valid && res.pixels != nullptr
            && res.width > 0 && res.height > 0
            && res.width <= 4096 && res.height <= 4096) {
        // Snapshot dimensions to locals before FreeNroIcon zeroes the struct.
        const s32 snap_w = res.width;
        const s32 snap_h = res.height;
        // v1.8.15 Fix B: short-scope lock around the cache mutation only.
        // ExtractNroIcon (disk I/O + ASET parse) is outside the lock.
        {
            std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
            GetSharedIconCache().Put(cache_key, res.pixels, snap_w, snap_h);
        }
        FreeNroIcon(res);
        UL_LOG_INFO("qdesktop: LoadNroIconToCache: loaded %s (%d×%d) → cache key %s",
                    nro_path, snap_w, snap_h, cache_key);
        return true;
    }

    // Extraction failed or dimensions out of range — log diagnostics using
    // res.width/res.height directly here (safe: pixels not yet freed), then
    // free, then insert fallback.
    UL_LOG_WARN("qdesktop: LoadNroIconToCache: ExtractNroIcon failed or bad dims"
                " for %s valid=%d pixels=%p width=%d height=%d"
                " — using fallback colour block",
                nro_path,
                static_cast<int>(res.valid),
                static_cast<const void *>(res.pixels),
                res.width,
                res.height);
    FreeNroIcon(res);

    // v1.8.19: Record this path in the negative-extract cache so subsequent
    // prewarm passes don't pay the disk I/O cost again.
    GetFailedExtractPaths().insert(nro_path);

    u8 *fallback = MakeFallbackIcon(nro_path);
    if (fallback != nullptr) {
        // v1.8.15 Fix B: short-scope lock around the cache mutation only.
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        GetSharedIconCache().Put(cache_key, fallback,
                   static_cast<s32>(CACHE_ICON_W),
                   static_cast<s32>(CACHE_ICON_H));
        delete[] fallback;
    }
    return false;
}

} // namespace ul::menu::qdesktop
