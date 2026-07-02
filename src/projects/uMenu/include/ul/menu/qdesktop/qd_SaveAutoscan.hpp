// qd_SaveAutoscan.hpp — SD-card save-location autoscanner for uMenu (W12-SAVE-DISCO).
//
// Scans well-known save paths for Pokémon save files and returns a list of
// { game_index, save_dir, count } records keyed to the kGameNames[] ordering
// in QdSaveEditorLayout.  The scan result is cached per Open() call; a
// subsequent Rescan() is required to refresh.
//
// Design constraints:
//   - NO libnx save-data IPC; filesystem reads only (dirent / stat).
//   - NO actual save parsing — TitlePicker shows "N saves found" only.
//   - Must compile for aarch64-none-elf; no STL exceptions.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace ul::menu::qdesktop {

// ── SaveScanEntry ─────────────────────────────────────────────────────────────
// One detected save location, keyed to a game index in kGameNames[].
// game_index == kOtherGameIndex means "unattributed generic saves".

struct SaveScanEntry {
    int         game_index;   ///< Index into QdSaveEditorLayout::kGameNames[].
    std::string save_dir;     ///< Absolute path of the save directory found.
    int         save_count;   ///< Number of candidate save files/slots inside.
};

// ── SaveScanResult ────────────────────────────────────────────────────────────
// Aggregate result for one autoscan pass.

struct SaveScanResult {
    bool                       scan_done     = false;  ///< true after at least one scan ran.
    bool                       scan_ok       = false;  ///< true if sdmc: was accessible.
    int                        paths_probed  = 0;      ///< Total directory paths stat-checked.
    int                        paths_skipped = 0;      ///< Paths that did not exist / open.
    std::vector<SaveScanEntry> entries;                ///< Detected saves, one per game/dir.
};

// ── QdSaveAutoscan ────────────────────────────────────────────────────────────
// Utility class (stateless except for cached results) that walks candidate save
// directories and populates a SaveScanResult.  The result is cached between
// Open() calls; call Rescan() to clear the cache and re-run.

class QdSaveAutoscan {
public:
    // game_index value used for saves that could not be attributed to a known game.
    static constexpr int kOtherGameIndex = 5;

    QdSaveAutoscan() = default;
    ~QdSaveAutoscan() = default;

    // Returns the cached result (running a fresh scan on the first call or
    // after Rescan()).
    const SaveScanResult& GetResult();

    // Invalidate the cache so the next GetResult() re-scans.
    void Rescan();

private:
    SaveScanResult result_;

    // Run the full scan and populate result_.
    void RunScan();

    // Count save-like files (main, main.sav, *.sav, *.bin, *.dat) in a directory.
    // depth=1: also dives into immediate subdirs (for JKSV user-uid level).
    static int CountSaveFiles(const char *dir_path, int depth = 0);

    // Check whether a directory exists (stat-based).
    static bool DirExists(const char *path);

    // Probe a single path: log, stat, and return DirExists result.
    static bool ProbeDir(SaveScanResult &result, const char *path);
};

} // namespace ul::menu::qdesktop
