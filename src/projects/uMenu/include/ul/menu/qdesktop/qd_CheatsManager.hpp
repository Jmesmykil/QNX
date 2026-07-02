// qd_CheatsManager.hpp — Atmosphère cheat code parser + sidecar enable/disable.
//
// W12-CHEATS (v3.4).
//
// Cheat files live at:
//   sdmc:/atmosphere/contents/<TID>/cheats/<BID>.txt
// where <TID> = 16-hex title-id, <BID> = first 16 hex chars of NSO build-id.
//
// The text format is:
//   [Cheat Name]
//   04000000 XXXXXXXX YYYYYYYY
//   ...
//
// Enable/disable model (v3.4 wave-3):
//   Primary:  .txt file edited in place — ';' prefix comments lines out.
//   Secondary: sidecar TOML at sdmc:/ulaunch/cheats-enabled/<TID>_<BID>.toml
//             retained as a fast-load cache so the UI avoids re-parsing the .txt
//             on every layout open.
//   Backup:   First write creates <bid>.txt.qos-backup (never overwritten).
//
// Master Code (first cheat) is ALWAYS enabled; WriteCheatEnabledState ignores
// any disable request for it.
//
// Static utility class; no global state.  All FS access uses stdio.
#pragma once

#include <string>
#include <vector>
#include <set>
#include <utility>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── CheatEntry ────────────────────────────────────────────────────────────────

/// One cheat code entry from a .txt file.
struct CheatEntry {
    std::string              name;    ///< Name without the surrounding brackets.
    std::vector<std::string> lines;  ///< Raw hex-code lines (e.g. "04000000 DEADBEEF 00000000").
    bool                     is_master_code = false;  ///< True for the first entry in the file.

    /// True when the parsed header line did NOT start with ';' (i.e. the cheat
    /// is currently enabled in the .txt file as Atmosphère would see it).
    /// W13-BUG-FIX: previously the UI's `enabled_` set defaulted to empty,
    /// treating every cheat as disabled even though .txt files default to all
    /// enabled.  Loading code must seed `enabled_` from `currently_enabled`
    /// when no sidecar TOML exists, so the first toggle does what the user
    /// expects.
    bool                     currently_enabled = true;
};

/// Parsed list of cheats from a single .txt file.
using CheatList = std::vector<CheatEntry>;

// ── CheatFile ─────────────────────────────────────────────────────────────────

/// Metadata returned by ScanInstalledCheats() for one discovered cheat file.
struct CheatFile {
    std::string tid;         ///< 16-char hex title-id (lower-case).
    std::string bid;         ///< 16-char hex build-id (lower-case).
    std::string title_name;  ///< Display name from ns:am, or "TID 0x<tid>" fallback.
    int         cheat_count; ///< Number of CheatEntry structs in the .txt file.
    std::string file_path;   ///< Full sdmc: path to the .txt file.
};

// ── QdCheatsManager ──────────────────────────────────────────────────────────

/// Static utility class for Atmosphère cheat code discovery + sidecar I/O.
class QdCheatsManager {
public:
    // No instances — pure static interface.
    QdCheatsManager()  = delete;
    ~QdCheatsManager() = delete;

    // ── Discovery ─────────────────────────────────────────────────────────

    /// Walk sdmc:/atmosphere/contents/ and return metadata for every
    /// <TID>/cheats/<BID>.txt found.  Results are sorted by title_name.
    static std::vector<CheatFile> ScanInstalledCheats();

    // ── Parsing ───────────────────────────────────────────────────────────

    /// Parse a single .txt cheat file into a list of CheatEntry structs.
    /// The first entry is always flagged as is_master_code.
    /// Returns an empty list on open / parse failure.
    static CheatList ParseCheatFile(const std::string &path);

    // ── Sidecar I/O ───────────────────────────────────────────────────────

    /// Read sdmc:/ulaunch/cheats-enabled/<tid>_<bid>.toml.
    /// Returns a set of enabled cheat names; empty = all disabled (except Master Code).
    /// If the file does not exist every non-master cheat is treated as disabled.
    static std::set<std::string> ReadEnabledSidecar(const std::string &tid,
                                                     const std::string &bid);

    /// Write sdmc:/ulaunch/cheats-enabled/<tid>_<bid>.toml with the given
    /// enabled set.  Creates the directory if it does not exist.
    static void WriteEnabledSidecar(const std::string &tid,
                                    const std::string &bid,
                                    const std::set<std::string> &enabled);

    // ── .txt writeback ────────────────────────────────────────────────────

    /// Rewrite a .txt cheat file so the comment/uncomment state of every
    /// cheat block matches @p states.  Each entry in states is a
    /// (cheat_name, enabled) pair.
    ///
    /// Algorithm:
    ///   - On first write (backup absent): copy original to <path>.qos-backup.
    ///   - Read whole file into memory.
    ///   - Walk line-by-line; when a '[Name]' header (possibly ';[Name]') is
    ///     found, look up Name in states.  Apply the desired enabled state:
    ///       enabled=true  → strip leading ';' from header and following code lines.
    ///       enabled=false → add ';' prefix to header and following code lines.
    ///   - Master Code (first cheat) is always kept enabled regardless of states.
    ///   - W13-DIRECT-WRITE: open the destination directly with mode "w"
    ///     (truncate), write all lines, fsync, close.  Previously used a
    ///     tmp+rename atomic-replace pattern, but FAT32 / newlib does NOT
    ///     support atomic rename-over-existing — and a remove+retry fallback
    ///     risked losing the file entirely if both renames failed.  The
    ///     .qos-backup made at the top of this function covers the (tiny)
    ///     corruption window of a direct write.
    ///   - Also calls WriteEnabledSidecar to keep the TOML cache in sync.
    ///
    /// @return 0 on success, -1 on I/O failure (original file may be
    ///         corrupted in the rare direct-write-interrupted case — fall
    ///         back to .qos-backup to recover).
    static int WriteCheatEnabledState(
        const std::string &cheat_file_path,
        const std::string &tid,
        const std::string &bid,
        const std::vector<std::pair<std::string, bool>> &states);

private:
    /// Build the full sidecar path for a given (tid, bid) pair.
    static std::string SidecarPath(const std::string &tid, const std::string &bid);
};

} // namespace ul::menu::qdesktop
