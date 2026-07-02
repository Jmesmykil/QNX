// qd_ModsManager.hpp — Atmosphère LayeredFS mod discovery + enable/disable.
//
// B3.1 — launcher-side mod manager (no dmnt, no overlay — that is B3.4).
//
// Mod content lives at:
//   sdmc:/atmosphere/contents/<TID>/
//     romfs/                      — file-replacement LayeredFS
//     exefs/                      — exefs replacement / patchsets
//     exefs_patches/<name>/       — IPS / pchtxt patches
//     contents/<TID2>/            — nested title overrides (rare)
//
// A directory under sdmc:/atmosphere/contents/ is treated as a "mod set"
// for the owning title if it contains AT LEAST ONE of the following:
//   • a "romfs" directory (enabled) or "romfs.disabled" (disabled)
//   • an "exefs" directory (enabled) or "exefs.disabled" (disabled)
//   • any *.ips or *.pchtxt file under exefs_patches/ (or exefs_patches.disabled/)
//
// Enable/disable model (mirrors qd_SettingsLayout ToggleOverlay):
//   Enabled  → directory / file exists at canonical path.
//   Disabled → directory / file has ".disabled" suffix appended.
//   Toggle   → std::rename(canonical, canonical+".disabled")  or  vice versa.
//   Commit   → fsdevCommitDevice("sdmc") after each rename.
//
// Per-title state sidecar (mirrors qd_CheatsManager sidecar pattern):
//   Path:   sdmc:/ulaunch/mod-state/<TID>.toml
//   Format: disabled = ["romfs", "exefs"]   — names of disabled slots.
//   The sidecar is written after every toggle so the UI can restore toggle
//   state quickly on re-open without re-scanning all subdirs.
//
// Title-name resolution:
//   Uses QdCheatTitleResolver::Lookup + StartResolve (same as cheats UI).
//
// Static utility class; no global state.  All FS access uses POSIX stdio.

#pragma once

#include <string>
#include <vector>
#include <set>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── ModSlot ───────────────────────────────────────────────────────────────────

/// One togglable slot within a title's mod set.
/// A slot corresponds to a top-level subdirectory or a patch sub-directory
/// that can be renamed to/from "<name>.disabled" to suppress it.
struct ModSlot {
    std::string name;             ///< Canonical subdirectory name (e.g. "romfs", "exefs").
    std::string full_path;        ///< Absolute path to the enabled form of this slot.
                                  ///< May end with ".disabled" on disk; full_path always
                                  ///< points to the ENABLED form for rename logic.
    bool        is_enabled = true; ///< Current on-disk state (false → ".disabled" suffix present).
    bool        is_dir     = true; ///< True for directories; false for patch files.
};

// ── ModSet ────────────────────────────────────────────────────────────────────

/// All discoverable mod content for one title.
struct ModSet {
    std::string           tid;         ///< 16-char lower-case hex title-id.
    std::string           title_name;  ///< Display name (NACP or "TID 0x<tid>" fallback).
    std::vector<ModSlot>  slots;       ///< Individual togglable content slots.
};

// ── QdModsManager ─────────────────────────────────────────────────────────────

/// Static utility class for LayeredFS mod discovery + sidecar I/O.
class QdModsManager {
public:
    // No instances — pure static interface.
    QdModsManager()  = delete;
    ~QdModsManager() = delete;

    // ── Discovery ─────────────────────────────────────────────────────────

    /// Walk sdmc:/atmosphere/contents/ and return a ModSet for every TID
    /// that contains at least one mod slot (romfs, exefs, or patch files).
    /// Both enabled and *.disabled forms are detected and included.
    /// Results are sorted by title_name.
    static std::vector<ModSet> ScanInstalledMods();

    // ── Toggle ────────────────────────────────────────────────────────────

    /// Toggle the enabled state of one slot within @p mod_set.
    /// Applies the ".disabled"-suffix rename trick identical to ToggleOverlay()
    /// in qd_SettingsLayout.cpp:931 — the canonical FAT32-safe enable/disable
    /// pattern used throughout Q OS.
    ///
    /// Algorithm:
    ///   enabled  → rename(path, path + ".disabled")
    ///   disabled → rename(path + ".disabled", path)
    ///   Then fsdevCommitDevice("sdmc") to flush the FAT32 dir-entry cache.
    ///   Then WriteStateSidecar(tid, ...) to persist the toggle.
    ///
    /// @return true on success; false on rename failure (errno logged).
    static bool ToggleSlot(ModSet &mod_set, size_t slot_idx);

    // ── Sidecar I/O ───────────────────────────────────────────────────────

    /// Read sdmc:/ulaunch/mod-state/<tid>.toml.
    /// Returns the set of slot names that are DISABLED.
    /// Empty return → sidecar absent; caller should derive state from disk.
    static std::set<std::string> ReadStateSidecar(const std::string &tid);

    /// Write sdmc:/ulaunch/mod-state/<tid>.toml with the given disabled set.
    /// Creates the directory if it does not exist.
    static void WriteStateSidecar(const std::string &tid,
                                   const std::set<std::string> &disabled);

private:
    /// Build the full sidecar path for a given tid.
    static std::string SidecarPath(const std::string &tid);

    /// Scan one TID directory for mod slots; appends to @p out.
    /// Returns true if at least one slot was found.
    static bool ScanTidDirectory(const char *tid_name,
                                  std::vector<ModSlot> &out);
};

} // namespace ul::menu::qdesktop
