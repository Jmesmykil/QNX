// qd_SaveBackup.hpp — JKSV-style save backup & restore (v3.6 absorb wave 1).
//
// Mounts a per-(app_id, user) account save partition via libnx
// fsdevMountSaveData(), then performs recursive directory copy in either
// direction:
//
//   BackupSave  : save-FS  → sdmc:/JKSV/<gameDir>/<timestamp>/...
//   RestoreSave : sdmc:/JKSV/<gameDir>/<timestamp>/... → save-FS  (+commit)
//
// Layout mirrors JKSV's `/JKSV/<TitleName [TID]>/<DateTime>/` convention so
// users can switch between JKSV-NRO and Q OS backups interchangeably.  The
// timestamp is `YYYYMMDD-HHMMSS` in UTC (matches JKSV's default).
//
// The mount uses a stable device-name `qosbackup` so concurrent mounts of
// the same partition by other code paths don't collide.  Unmount on every
// exit path including failure.
//
// Returned `Result` semantics (corrected 2026-06-12 to match the impl):
//   - ResultSuccess                      = success. For RestoreSave this also
//                                          means the FS was COMMITTED.
//   - MAKERESULT(Module_Libnx,BadInput)  = invalid uid / app_id / empty args /
//                                          missing source dir.
//   - MAKERESULT(Module_Libnx,IoError)   = copy tree failed (see log). For
//                                          RestoreSave this means NOTHING was
//                                          committed — the live save is intact
//                                          (the partial write rolls back on
//                                          unmount-without-commit). [P0 fix]
//   - any libnx Result                   = forwarded from fsdevMountSaveData /
//                                          fsdevCommitDevice.
//
// The implementation is intentionally synchronous — backup of a ~50 MB save
// (Pokémon Sword / Shield typical size) runs in ~3-6 seconds on FAT32 SD,
// well within the user's "wait briefly while a dialog shows" budget.  The
// caller is expected to show a "Backing up…" notification before the call
// and a "Backup complete." toast after.
//
// Backup layout (2026-06-12): sdmc:/JKSV/<gameDir>/<UTC-timestamp>_u<32hex-uid>/
// — the owning AccountUid is encoded as a folder suffix so restore can match a
// backup back to the user that created it (prevents cross-user overwrite).
// JKSV-created folders (no _u suffix) are still listed and treated as
// "unknown owner" by the restore path.

#pragma once

#include <ul/ul_Result.hpp>
#include <string>
#include <vector>

#include <switch.h>

namespace ul::menu::qdesktop {

struct BackupListEntry {
    std::string folder_name;   ///< e.g. "20260527-183300_u0123...."
    std::string full_path;     ///< full sdmc:/JKSV/<gameDir>/<folder_name>/
    u64         epoch_seconds; ///< POSIX time decoded from folder_name (for sort).
    AccountUid  owner_uid{};   ///< decoded from the _u<32hex> suffix, if present.
    bool        has_owner=false; ///< true when folder_name carried a valid _u suffix.
};

class QdSaveBackup {
public:
    QdSaveBackup()  = delete;
    ~QdSaveBackup() = delete;

    // ── Backup ────────────────────────────────────────────────────────────
    //
    // Copy the Account save partition for (app_id, uid) into a new
    // sdmc:/JKSV/<gameDir>/<UTC-timestamp>/ directory.  Creates parent
    // directories as needed.
    //
    // @param app_id    Switch application ID (lowercase 16-hex form via parser).
    // @param uid       Account UID that owns the save (must be valid).
    // @param game_dir  Display directory under /JKSV/.  Recommended format:
    //                  "<TitleName> [<tid_hex>]" — matches JKSV's convention.
    //                  Empty string falls back to plain "<tid_hex>".
    // @param out_path  On success, populated with the full path of the new
    //                  backup directory (caller can show it in a toast).
    // @return ResultSuccess on success; libnx Result otherwise.
    static Result BackupSave(u64                app_id,
                              AccountUid         uid,
                              const std::string &game_dir,
                              std::string       *out_path);

    // ── Restore ───────────────────────────────────────────────────────────
    //
    // Copy sdmc:/JKSV/<game_dir>/<backup_folder>/ contents BACK onto the
    // Account save partition for (app_id, uid).  Existing files are
    // overwritten; files in the live save that aren't in the backup are
    // left in place (additive restore — safer default).  Commits the FS at
    // the end.
    //
    // Data-safety guarantees:
    //   * Commit-on-success only: a failed copy is NOT committed, so the live
    //     save rolls back untouched on unmount.
    //   * R3a source validation: refuses (BadInput) if the chosen backup folder
    //     holds no non-empty regular files — an empty/garbage backup can never
    //     overwrite a real save.
    //   * R3b pre-restore snapshot: if a live save exists, it is auto-backed-up
    //     first; if that snapshot fails the restore ABORTS (so even a
    //     successful restore of the *wrong* backup is recoverable).
    //
    // @return ResultSuccess on success; libnx Result otherwise.
    static Result RestoreSave(u64                app_id,
                               AccountUid         uid,
                               const std::string &game_dir,
                               const std::string &backup_folder);

    // ── List backups ──────────────────────────────────────────────────────
    //
    // Enumerate existing backups for a given game_dir.  Returns entries
    // sorted by epoch_seconds DESCENDING (most recent first).  Folders
    // whose names don't parse as YYYYMMDD-HHMMSS are still listed but with
    // epoch_seconds=0 (sorted last).
    static std::vector<BackupListEntry> ListBackups(const std::string &game_dir);

    // ── Helpers ───────────────────────────────────────────────────────────
    //
    // Build the canonical game_dir name from a NACP title and TID.  Result
    // is filesystem-safe (slashes / colons / control chars stripped).
    static std::string MakeGameDir(const std::string &title_name, u64 app_id);

    // UTC timestamp string for the current moment (YYYYMMDD-HHMMSS).
    static std::string CurrentTimestamp();
};

}  // namespace ul::menu::qdesktop
