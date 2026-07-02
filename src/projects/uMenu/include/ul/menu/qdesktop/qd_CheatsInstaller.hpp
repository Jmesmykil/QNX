// qd_CheatsInstaller.hpp — HTTPS-based cheat bundle installer for Q OS.
//
// W14-CHEATS-INSTALLER (v3.5).
//
// Downloads the HamletDuFromage/switch-cheats-db community cheat bundle from
// GitHub Releases, extracts ONLY the entries whose TID matches an installed
// game, and writes them into sdmc:/atmosphere/contents/<TID>/cheats/.
//
// Design goals:
//   - Filter-first: never write 2,500+ TID dirs; only write TIDs installed on
//     this console (avoids the Atmosphère boot-scan hang).
//   - Idempotent: if a .qos-backup already exists for a cheat file, the live
//     .txt is preserved (user's toggle state is not clobbered).
//   - Off-thread: all network I/O happens on a dedicated libnx thread; the UI
//     thread polls InstallerProgress via GetProgress() (mutex-protected
//     snapshot) on each render frame.
//   - Filesystem-only: does NOT use dmnt:cht IPC bindings; all cheat data
//     goes directly to sdmc:.
//
// Transport: libnx ssl service + POSIX BSD sockets (TLS 1.2/1.3 via the
// Switch's native ssl:u service).  No libcurl dependency.
// ZIP: minizip (already in portlibs/switch) via the unzip.h API.
//
// Call sequence:
//   1. Construct QdCheatsInstaller.
//   2. Call StartInstall() — spawns worker thread, returns immediately.
//   3. Poll GetProgress() each frame; render progress bar / phase label.
//   4. When phase == Done or Failed, call ScanInstalledCheats() and refresh UI.
//   5. Call Stop() in the layout destructor (waits for thread exit).
#pragma once

#include <string>
#include <set>
#include <atomic>
#include <cstdint>
#include <switch.h>  // Thread, Mutex, u64, s32, Result

namespace ul::menu::qdesktop {

// ── InstallerProgress ─────────────────────────────────────────────────────────

/// Snapshot of the installer worker thread's state.  Safe to copy on the UI
/// thread; the installer serializes writes behind a mutex.
struct InstallerProgress {
    enum class Phase : uint8_t {
        Idle,
        EnumeratingInstalledGames,
        FetchingReleaseInfo,
        DownloadingBundle,
        Extracting,
        Done,
        Failed,
    };

    Phase       phase        = Phase::Idle;
    int         percent      = 0;    ///< [0,100] during DownloadingBundle; 0 otherwise.
    int         tids_done    = 0;    ///< TID directories written.
    int         files_written= 0;    ///< Individual cheat .txt files written.
    char        step[128]    = {};   ///< Current step description (null-terminated).
    char        error[256]   = {};   ///< Error message when phase == Failed.
};

// ── QdCheatsInstaller ────────────────────────────────────────────────────────

class QdCheatsInstaller {
public:
    QdCheatsInstaller()  = default;
    ~QdCheatsInstaller() = default;

    // Non-copyable / non-moveable (owns a Thread + Mutex).
    QdCheatsInstaller(const QdCheatsInstaller&)            = delete;
    QdCheatsInstaller& operator=(const QdCheatsInstaller&) = delete;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Spawn the installer worker thread.  Returns immediately.
    /// Must not be called more than once per object lifetime.
    void StartInstall();

    /// Thread-safe snapshot of current progress.  Call on UI thread each frame.
    InstallerProgress GetProgress() const;

    /// Signal the worker to abort and block until it exits.
    /// Safe to call even if StartInstall() was never called, or after the
    /// thread already finished.  Called from QdCheatsLayout dtor.
    void Stop();

    /// Enumerate installed game TIDs using ns:am nsListApplicationRecord.
    /// Returns a set of lower-case 16-char hex strings.
    /// Static so it can be called without a full installer object if needed.
    static std::set<std::string> EnumerateInstalledGameTids();

private:
    // ── Worker thread ─────────────────────────────────────────────────────

    static void WorkerEntry(void *arg);
    void RunInstall();

    // ── Progress helpers ──────────────────────────────────────────────────

    void UpdatePhase(InstallerProgress::Phase phase, const char *step);
    void UpdatePercent(int pct);
    void Fail(const char *msg);

    // ── Network helpers ───────────────────────────────────────────────────

    /// Perform a TLS GET to (host, port, path) using libnx ssl + BSD sockets.
    /// Appends response body bytes to @p body.  Returns false on any error.
    bool HttpsGet(const char *host, const char *path,
                  std::string &body);

    /// Parse the GitHub Releases JSON body for an asset named @p want_name.
    /// Extracts the "browser_download_url" field into @p out_url.
    /// Falls back to simple substring scan; no JSON library needed.
    static bool ParseAssetUrl(const std::string &body,
                              const char *want_name,
                              std::string &out_url);

    /// Fetch the latest release asset URL from GitHub Releases API.
    /// Tries "contents.zip" first, then "contents_complete.zip".
    bool FetchLatestReleaseUrl(std::string &out_url);

    /// Stream-download @p url to @p dest_path on sdmc:.
    /// Updates progress percent as data arrives.
    bool DownloadFile(const std::string &url, const char *dest_path);

    // ── Extraction helper ─────────────────────────────────────────────────

    /// Open the ZIP at @p zip_path, walk every entry, keep only those whose
    /// TID prefix appears in @p installed_tids, write kept entries to
    /// sdmc:/atmosphere/contents/<tid>/cheats/<file>.
    /// Respects .qos-backup idempotence rule: if backup exists, skip.
    /// Outputs the number of TID directories touched and files written.
    bool ExtractFiltered(const char *zip_path,
                         const std::set<std::string> &installed_tids,
                         int &out_tids_done,
                         int &out_files_written);

    // ── Utilities ─────────────────────────────────────────────────────────

    /// Ensure a directory (no trailing slash) exists; create it if absent.
    static void EnsureDir(const char *path);

    // ── State ─────────────────────────────────────────────────────────────

    Thread              thread_{};
    bool                thread_started_  = false;
    mutable Mutex       progress_lock_{};
    InstallerProgress   progress_{};
    std::atomic<bool>   abort_{false};

    // Stack for the worker thread (256 KiB — network + minizip decompression).
    // MUST be page-aligned (0x1000): libnx threadCreate() with a user-provided
    // stack calls svcMapMemory, which fails on a non-page-aligned address — that
    // was the "threadCreate failed" install error (stack was alignas(8)).
    static constexpr size_t kStackSize = 256 * 1024;
    alignas(0x1000) uint8_t stack_[kStackSize] = {};
};

} // namespace ul::menu::qdesktop
