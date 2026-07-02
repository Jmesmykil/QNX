// qd_SaveBackup.cpp — JKSV-style save backup & restore.
// See qd_SaveBackup.hpp for the API contract.
//
// Implementation notes:
//   - Mounts the save FS via fsdevMountSaveData("qosbackup", app_id, uid).
//   - Walks the mounted FS with opendir/readdir, mirroring the directory
//     tree at the destination path.  Files are copied byte-stream via
//     fopen/fread/fwrite in 16 KB blocks.
//   - Unmounts via fsdevUnmountDevice("qosbackup") on every exit path
//     (success and error).  Without this, subsequent BackupSave calls hit
//     EBUSY when libnx refuses to re-mount the same device name.
//   - fsdevCommitDevice("qosbackup") after restore to push Horizon's
//     write-back cache to the actual save partition.  Without commit, the
//     restored bytes only exist in Horizon's RAM and a reboot loses them.

#include <ul/menu/qdesktop/qd_SaveBackup.hpp>
#include <ul/ul_Result.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>
#include <algorithm>
#include <string>

namespace ul::menu::qdesktop {

namespace {

constexpr const char *kJksvRoot       = "sdmc:/JKSV";
constexpr const char *kSaveMountName  = "qosbackup";
constexpr size_t      kCopyBufSize    = 16 * 1024;  // 16 KB block.

// ── Path safety ──────────────────────────────────────────────────────────
//
// Strip any character that's invalid in FAT32 / newlib filenames, plus
// directory separators (we don't want a NACP name with '/' in it to
// directory-traverse).
std::string Sanitize(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        // Strip control + special FAT32 prohibited chars.
        if (static_cast<unsigned char>(c) < 0x20) continue;
        switch (c) {
            case '/': case '\\': case ':': case '*':
            case '?': case '"':  case '<': case '>':
            case '|':
                out.push_back(' ');
                break;
            default:
                out.push_back(c);
        }
    }
    // Trim trailing dots/spaces (FAT32 dislikes them).
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.empty()) out = "_";
    return out;
}

// Create directory and all parents (mkdir -p).  Returns 0 on success, -1
// otherwise.  EEXIST treated as success.
int MkdirP(const std::string &path) {
    if (path.empty()) return -1;
    // Walk the path, creating each segment.  Skip the "sdmc:" prefix segment.
    size_t start = 0;
    if (path.rfind("sdmc:", 0) == 0) {
        // Find the position after the first '/' that follows "sdmc:".
        size_t after = path.find('/', 5);
        if (after == std::string::npos) {
            // No path after "sdmc:" — nothing to create.
            return 0;
        }
        start = after + 1;
    }
    for (size_t i = start; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            std::string segment = path.substr(0, i);
            if (segment.empty()) continue;
            if (::mkdir(segment.c_str(), 0777) != 0 && errno != EEXIST) {
                UL_LOG_WARN("QdSaveBackup: mkdir(%s) errno=%d",
                            segment.c_str(), errno);
                return -1;
            }
        }
    }
    return 0;
}

// v3.6 redesign 2026-05-28: namespace-static copy buffer.
// 16 KB is allocated in BSS at process start — no malloc/free traffic per
// file copy.  Eliminates the heap churn that the audio-thread MP3 NULL-deref
// investigation flagged.  Concurrent backup operations are not supported
// (the buffer is a singleton); BackupSave / RestoreSave are called from
// SaveEditor button handlers which serialise naturally.
char g_copy_buf[kCopyBufSize];

// Copy a single file (src → dst).  Returns 0 on success, -1 otherwise.
int CopyOneFile(const std::string &src, const std::string &dst) {
    FILE *in = std::fopen(src.c_str(), "rb");
    if (!in) {
        UL_LOG_WARN("QdSaveBackup: fopen(%s) errno=%d",
                    src.c_str(), errno);
        return -1;
    }
    FILE *out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        UL_LOG_WARN("QdSaveBackup: fopen(%s, wb) errno=%d",
                    dst.c_str(), errno);
        std::fclose(in);
        return -1;
    }
    int rc = 0;
    for (;;) {
        size_t n = std::fread(g_copy_buf, 1, kCopyBufSize, in);
        if (n == 0) {
            if (std::ferror(in)) {
                UL_LOG_WARN("QdSaveBackup: fread err on %s", src.c_str());
                rc = -1;
            }
            break;
        }
        size_t w = std::fwrite(g_copy_buf, 1, n, out);
        if (w != n) {
            UL_LOG_WARN("QdSaveBackup: fwrite short on %s (n=%zu w=%zu)",
                        dst.c_str(), n, w);
            rc = -1;
            break;
        }
    }
    // P0 fix: surface flush/close errors. newlib fully-buffers writes, so an
    // ENOSPC on the final partial block surfaces only at fflush/fclose time —
    // discarding these returns reported a TRUNCATED copy as success.
    if (std::fflush(out) != 0) {
        UL_LOG_WARN("QdSaveBackup: fflush failed on %s errno=%d", dst.c_str(), errno);
        rc = -1;
    }
    if (std::fclose(out) != 0) {
        UL_LOG_WARN("QdSaveBackup: fclose failed on %s errno=%d", dst.c_str(), errno);
        rc = -1;
    }
    std::fclose(in);
    return rc;
}

// Recursive directory copy.  Creates dst_root as needed, walks src_root,
// re-creates sub-directories under dst_root, copies each regular file.
// Returns 0 on success, -1 otherwise.
int CopyTreeImpl(const std::string &src_root, const std::string &dst_root) {
    if (MkdirP(dst_root) != 0) {
        return -1;
    }
    DIR *d = ::opendir(src_root.c_str());
    if (!d) {
        UL_LOG_WARN("QdSaveBackup: opendir(%s) errno=%d",
                    src_root.c_str(), errno);
        return -1;
    }
    int rc = 0;
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }
        std::string s_child = src_root + "/" + de->d_name;
        std::string d_child = dst_root + "/" + de->d_name;
        struct stat st;
        if (::stat(s_child.c_str(), &st) != 0) {
            UL_LOG_WARN("QdSaveBackup: stat(%s) errno=%d",
                        s_child.c_str(), errno);
            rc = -1;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (CopyTreeImpl(s_child, d_child) != 0) {
                rc = -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (CopyOneFile(s_child, d_child) != 0) {
                rc = -1;
            }
        }
    }
    ::closedir(d);
    return rc;
}

// R3: recursively tally regular files + total bytes under `root`.  Used to
// validate a restore source is non-empty BEFORE it is allowed to overwrite the
// live save (an empty/garbage backup folder must never wipe a real save).
void CountTree(const std::string &root, int &out_files, u64 &out_bytes) {
    DIR *d = ::opendir(root.c_str());
    if (!d) {
        return;
    }
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }
        const std::string child = root + "/" + de->d_name;
        struct stat st;
        if (::stat(child.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            CountTree(child, out_files, out_bytes);
        } else if (S_ISREG(st.st_mode)) {
            ++out_files;
            out_bytes += static_cast<u64>(st.st_size);
        }
    }
    ::closedir(d);
}

// Parse "YYYYMMDD-HHMMSS" into a POSIX epoch (UTC).  Returns 0 on parse fail.
u64 ParseTimestamp(const std::string &name) {
    // Accept an optional "_u<uid>" suffix after the 15-char YYYYMMDD-HHMMSS
    // stamp; the sscanf below consumes only the leading date fields.
    if (name.size() < 15 || name[8] != '-') return 0;
    int y, M, d, h, m, s;
    if (std::sscanf(name.c_str(), "%4d%2d%2d-%2d%2d%2d",
                    &y, &M, &d, &h, &m, &s) != 6) {
        return 0;
    }
    struct tm tmv = {};
    tmv.tm_year = y - 1900;
    tmv.tm_mon  = M - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min  = m;
    tmv.tm_sec  = s;
    // Use timegm if available; on newlib we fall back to mktime which is
    // local-time-based, but for sort purposes that's fine (monotonic).
    time_t t = ::mktime(&tmv);
    return (t > 0) ? static_cast<u64>(t) : 0;
}

// Convert u64 app_id to "0100abf008968000"-style lowercase hex.
std::string TidHex(u64 app_id) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(app_id));
    return std::string(buf);
}

// ── Owning-uid folder suffix (P0 cross-user fix) ───────────────────────────
//
// Encode the 128-bit AccountUid as "_u<32 lowercase hex>" appended to the
// timestamp folder, so RestoreSave can match a backup to the user that
// created it instead of overwriting whichever user mounts first.
std::string UidSuffix(AccountUid uid) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "_u%016llx%016llx",
                  static_cast<unsigned long long>(uid.uid[0]),
                  static_cast<unsigned long long>(uid.uid[1]));
    return std::string(buf);
}

// Parse the "_u<32hex>" suffix back into an AccountUid.  Returns false when
// the folder has no such suffix (e.g. a JKSV-created backup).
bool ParseUidSuffix(const std::string &name, AccountUid *out) {
    const size_t pos = name.rfind("_u");
    if (pos == std::string::npos) return false;
    const char *p = name.c_str() + pos + 2;
    if (std::strlen(p) != 32) return false;
    char hi_s[17], lo_s[17];
    std::memcpy(hi_s, p, 16);      hi_s[16] = '\0';
    std::memcpy(lo_s, p + 16, 16); lo_s[16] = '\0';
    unsigned long long hi = 0, lo = 0;
    if (std::sscanf(hi_s, "%16llx", &hi) != 1) return false;
    if (std::sscanf(lo_s, "%16llx", &lo) != 1) return false;
    out->uid[0] = static_cast<u64>(hi);
    out->uid[1] = static_cast<u64>(lo);
    return true;
}

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────

std::string QdSaveBackup::MakeGameDir(const std::string &title_name,
                                       u64                 app_id) {
    std::string safe_title = Sanitize(title_name);
    std::string tid_hex    = TidHex(app_id);
    if (safe_title.empty()) {
        return tid_hex;
    }
    return safe_title + " [" + tid_hex + "]";
}

std::string QdSaveBackup::CurrentTimestamp() {
    time_t now = ::time(nullptr);
    struct tm utc;
    ::gmtime_r(&now, &utc);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                  utc.tm_year + 1900,
                  utc.tm_mon + 1,
                  utc.tm_mday,
                  utc.tm_hour,
                  utc.tm_min,
                  utc.tm_sec);
    return std::string(buf);
}

Result QdSaveBackup::BackupSave(u64                app_id,
                                  AccountUid         uid,
                                  const std::string &game_dir_in,
                                  std::string       *out_path) {
    // Validate inputs.
    if (!accountUidIsValid(&uid)) {
        UL_LOG_WARN("QdSaveBackup::BackupSave: invalid uid");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    if (app_id == 0) {
        UL_LOG_WARN("QdSaveBackup::BackupSave: app_id=0");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    // P0 fix: free-space floor — fail fast with a clear rc instead of
    // silently truncating mid-copy if the SD is critically low.  A statvfs
    // failure is non-fatal (CopyOneFile's fflush/fclose checks still catch a
    // real mid-copy ENOSPC).
    {
        struct statvfs vfs;
        if (::statvfs("sdmc:/", &vfs) == 0) {
            const u64 free_bytes =
                static_cast<u64>(vfs.f_bsize) * static_cast<u64>(vfs.f_bfree);
            constexpr u64 kFreeFloor = 64ull * 1024 * 1024;  // 64 MiB
            if (free_bytes < kFreeFloor) {
                UL_LOG_WARN("QdSaveBackup::BackupSave: SD low (%llu B free) — "
                            "refusing backup",
                            static_cast<unsigned long long>(free_bytes));
                return MAKERESULT(Module_Libnx, LibnxError_IoError);
            }
        }
    }

    const std::string game_dir   = game_dir_in.empty()
                                      ? TidHex(app_id)
                                      : Sanitize(game_dir_in);
    const std::string timestamp  = CurrentTimestamp();
    // P0 cross-user fix: encode the owning uid as a folder suffix so restore
    // can match this backup back to the user that created it.
    const std::string dst_root   =
        std::string(kJksvRoot) + "/" + game_dir + "/" + timestamp + UidSuffix(uid);

    // v3.6.1d: try the READ-ONLY save mount first.  uMenu as qlaunch-
    // replacement has restrictive SystemApplet NPDM that may not grant
    // arbitrary read-write access to user-app save partitions (hbloader
    // gets these via its permissive 0xFF FS flags; we don't).  Read-only
    // mount uses a different FS service path with lighter permission
    // requirements — sufficient for backup.  If RO also fails, fall
    // back to RW so the caller still gets the original rc for diagnosis.
    Result rc_mount =
        fsdevMountSaveDataReadOnly(kSaveMountName, app_id, uid);
    if (R_FAILED(rc_mount)) {
        UL_LOG_WARN("QdSaveBackup::BackupSave: fsdevMountSaveDataReadOnly"
                    "(0x%016llX) rc=0x%08X — trying RW fallback",
                    static_cast<unsigned long long>(app_id), rc_mount);
        rc_mount = fsdevMountSaveData(kSaveMountName, app_id, uid);
        if (R_FAILED(rc_mount)) {
            UL_LOG_WARN("QdSaveBackup::BackupSave: fsdevMountSaveData"
                        "(0x%016llX) rc=0x%08X (RW fallback also failed)",
                        static_cast<unsigned long long>(app_id), rc_mount);
            return rc_mount;
        }
    }
    const std::string src_root = std::string(kSaveMountName) + ":/";

    UL_LOG_INFO("QdSaveBackup::BackupSave: %s -> %s",
                src_root.c_str(), dst_root.c_str());

    // Ensure JKSV root exists (idempotent).
    ::mkdir(kJksvRoot, 0777);

    const int rc_copy = CopyTreeImpl(src_root, dst_root);
    ::fsdevUnmountDevice(kSaveMountName);

    if (rc_copy != 0) {
        UL_LOG_WARN("QdSaveBackup::BackupSave: copy tree failed");
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    if (out_path) {
        *out_path = dst_root;
    }
    UL_LOG_INFO("QdSaveBackup::BackupSave: done -> %s", dst_root.c_str());
    return ResultSuccess;
}

Result QdSaveBackup::RestoreSave(u64                app_id,
                                   AccountUid         uid,
                                   const std::string &game_dir,
                                   const std::string &backup_folder) {
    if (!accountUidIsValid(&uid) || app_id == 0
            || game_dir.empty() || backup_folder.empty()) {
        UL_LOG_WARN("QdSaveBackup::RestoreSave: bad input");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    // H-4: sanitize backup_folder before building the restore path so a
    // folder named "../../atmosphere/..." cannot escape sdmc:/JKSV/.
    const std::string safe_backup_folder = Sanitize(backup_folder);
    const std::string src_root =
        std::string(kJksvRoot) + "/" + game_dir + "/" + safe_backup_folder;

    struct stat st;
    if (::stat(src_root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        UL_LOG_WARN("QdSaveBackup::RestoreSave: source dir missing %s",
                    src_root.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    // R3a: validate the backup actually carries restorable data BEFORE touching
    // the live save.  Without this, choosing an empty/garbage folder would copy
    // nothing over the live save and (after commit) effectively wipe it.
    int   src_files = 0;
    u64   src_bytes = 0;
    CountTree(src_root, src_files, src_bytes);
    if (src_files == 0 || src_bytes == 0) {
        UL_LOG_WARN("QdSaveBackup::RestoreSave: source has no restorable files "
                    "(%d files, %llu bytes) — refusing to overwrite live save",
                    src_files, static_cast<unsigned long long>(src_bytes));
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    UL_LOG_INFO("QdSaveBackup::RestoreSave: source OK (%d files, %llu bytes)",
                src_files, static_cast<unsigned long long>(src_bytes));

    // R3b: snapshot the CURRENT live save before overwriting it, so a wrong or
    // corrupt-but-complete backup is recoverable (the commit-on-success guard
    // below only protects against a *failed* copy, not a successful bad one).
    // Probe first: if no live save is mountable there's nothing to protect and
    // we proceed; if one EXISTS, the snapshot is mandatory — abort if it fails.
    {
        Result rc_probe =
            fsdevMountSaveDataReadOnly(kSaveMountName, app_id, uid);
        if (R_SUCCEEDED(rc_probe)) {
            ::fsdevUnmountDevice(kSaveMountName);   // release before snapshot remounts
            std::string snap_path;
            const Result rc_snap = BackupSave(app_id, uid, game_dir, &snap_path);
            if (R_FAILED(rc_snap)) {
                UL_LOG_WARN("QdSaveBackup::RestoreSave: pre-restore snapshot "
                            "FAILED rc=0x%08X — ABORTING to protect live save",
                            rc_snap);
                return rc_snap;
            }
            UL_LOG_INFO("QdSaveBackup::RestoreSave: pre-restore snapshot -> %s",
                        snap_path.c_str());
        } else {
            UL_LOG_INFO("QdSaveBackup::RestoreSave: no existing live save to "
                        "snapshot (rc=0x%08X) — proceeding", rc_probe);
        }
    }

    const Result rc_mount =
        fsdevMountSaveData(kSaveMountName, app_id, uid);
    if (R_FAILED(rc_mount)) {
        UL_LOG_WARN("QdSaveBackup::RestoreSave: fsdevMountSaveData(0x%016llX) "
                    "rc=0x%08X",
                    static_cast<unsigned long long>(app_id), rc_mount);
        return rc_mount;
    }
    const std::string dst_root = std::string(kSaveMountName) + ":/";

    UL_LOG_INFO("QdSaveBackup::RestoreSave: %s -> %s",
                src_root.c_str(), dst_root.c_str());

    const int rc_copy = CopyTreeImpl(src_root, dst_root);

    // P0 CRITICAL fix: commit ONLY when the copy fully succeeded.  Horizon
    // savedata is transactional — fsdevCommitDevice finalizes the journal,
    // and unmounting WITHOUT commit discards the pending writes (rollback).
    // Committing a half-finished copy would persist a CORRUPTED save over the
    // user's live data.  On copy failure we skip the commit so the live save
    // is left exactly as it was.
    Result rc_commit = ResultSuccess;
    if (rc_copy == 0) {
        rc_commit = ::fsdevCommitDevice(kSaveMountName);
        if (R_FAILED(rc_commit)) {
            UL_LOG_WARN("QdSaveBackup::RestoreSave: fsdevCommitDevice rc=0x%08X",
                        rc_commit);
        }
    } else {
        UL_LOG_WARN("QdSaveBackup::RestoreSave: copy failed (rc_copy=%d) — "
                    "skipping commit; live save rolled back on unmount", rc_copy);
    }
    ::fsdevUnmountDevice(kSaveMountName);

    if (rc_copy != 0) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    if (R_FAILED(rc_commit)) {
        return rc_commit;
    }
    UL_LOG_INFO("QdSaveBackup::RestoreSave: done");
    return ResultSuccess;
}

std::vector<BackupListEntry> QdSaveBackup::ListBackups(
        const std::string &game_dir) {
    std::vector<BackupListEntry> out;
    if (game_dir.empty()) return out;

    const std::string root = std::string(kJksvRoot) + "/" + game_dir;
    DIR *d = ::opendir(root.c_str());
    if (!d) return out;
    struct dirent *de;
    while ((de = ::readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        if (de->d_type != DT_DIR) continue;
        BackupListEntry e;
        e.folder_name    = de->d_name;
        e.full_path      = root + "/" + de->d_name;
        e.epoch_seconds  = ParseTimestamp(e.folder_name);
        e.has_owner      = ParseUidSuffix(e.folder_name, &e.owner_uid);
        out.push_back(std::move(e));
    }
    ::closedir(d);

    // Sort descending by epoch (most recent first); unparseable entries
    // sort last.
    std::sort(out.begin(), out.end(),
              [](const BackupListEntry &a, const BackupListEntry &b) {
                  if (a.epoch_seconds == 0 && b.epoch_seconds != 0) return false;
                  if (b.epoch_seconds == 0 && a.epoch_seconds != 0) return true;
                  return a.epoch_seconds > b.epoch_seconds;
              });
    return out;
}

}  // namespace ul::menu::qdesktop
