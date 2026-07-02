// qd_CheatTitleResolver.cpp — async NACP title resolver for Cheats UI.
//
// See qd_CheatTitleResolver.hpp for the API contract.  This implementation
// owns:
//   - g_cache:     std::unordered_map<u64, std::string> protected by g_mtx.
//   - g_loaded:    bool — has the on-disk cache file been read into memory?
//   - g_in_flight: std::atomic<bool> — is a bg resolution pass running?
//   - on_resolved: std::function<void()> callback invoked once when bg done.
//
// Cache file path: sdmc:/ulaunch/cache/cheat_titles.tsv

#include <ul/menu/qdesktop/qd_CheatTitleResolver.hpp>
#include <ul/util/util_String.hpp>
#include <ul/ul_Result.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <switch.h>
#include <sys/stat.h>

// v3.6 redesign 2026-05-28: removed std::thread.  The detached background
// thread was racing newlib's reent against SDL2_mixer's audio thread reading
// streaming MP3 from FILE*, producing a NULL deref at offset 0x28 of NULL
// when the FILE struct got displaced by concurrent malloc traffic.  The
// resolver now runs SYNCHRONOUSLY on the main thread when StartResolve is
// called — typical wall-clock is ~200-500 ms on first Cheats-window open
// (one nsListApplicationRecord pass + ≤ N NACP fetches for new TIDs).
// The on-disk cache means subsequent boots see real names from frame 0.
// Heap pressure is eliminated by making the 24 KB NACP buffer a namespace
// static (BSS, allocated once at process start, never freed).

namespace ul::menu::qdesktop {

namespace {

constexpr const char *kCacheDir  = "sdmc:/ulaunch/cache";
constexpr const char *kCachePath = "sdmc:/ulaunch/cache/cheat_titles.tsv";

std::mutex                                g_mtx;
std::unordered_map<std::uint64_t,
                   std::string>           g_cache;
bool                                      g_loaded     = false;
std::atomic<bool>                         g_in_flight  { false };

// ── Disk format ──────────────────────────────────────────────────────────
//
// One title per line, tab-separated:
//   <tid_hex_lower>\t<utf8_name>\n
//
// Parser tolerates blank lines + lines without a tab (skipped).  Names are
// truncated at the first '\n' or '\t' on read; names containing tab chars
// are escaped on write.

void LoadCacheLocked_NoFsLock() {
    g_cache.clear();
    g_loaded = true;
    FILE *f = std::fopen(kCachePath, "rb");
    if (!f) {
        return;  // First run — cache file does not exist yet.
    }
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        char *tab = std::strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        const char *tid_str  = line;
        char       *name_str = tab + 1;
        // Strip trailing \r\n if present.
        for (char *p = name_str; *p; ++p) {
            if (*p == '\n' || *p == '\r') {
                *p = '\0';
                break;
            }
        }
        if (*tid_str == '\0' || *name_str == '\0') continue;
        // Parse 16-char hex TID.
        std::uint64_t tid = 0;
        if (std::sscanf(tid_str, "%016llx",
                        reinterpret_cast<unsigned long long *>(&tid)) != 1) {
            continue;
        }
        g_cache[tid] = std::string(name_str);
    }
    std::fclose(f);
    UL_LOG_INFO("CheatTitleResolver: loaded %zu cached titles from disk",
                g_cache.size());
}

void WriteCacheLocked() {
    // Caller MUST hold g_mtx.
    ::mkdir(kCacheDir, 0777);  // EEXIST is fine.
    // Write to .tmp then rename for atomicity on platforms that support it.
    // newlib/FAT32: rename-over-existing is NOT atomic; emulate via unlink+rename
    // which is sufficient for a cache file (corruption-window is fine — we'd
    // just re-resolve on next boot).
    std::string tmp = std::string(kCachePath) + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        UL_LOG_WARN("CheatTitleResolver: cannot open %s for write errno=%d",
                    tmp.c_str(), errno);
        return;
    }
    for (const auto &kv : g_cache) {
        // Escape tabs and newlines in the name (very rare for game titles
        // but defensive).
        std::string safe_name;
        safe_name.reserve(kv.second.size());
        for (char c : kv.second) {
            if (c == '\t' || c == '\n' || c == '\r') {
                safe_name.push_back(' ');
            } else {
                safe_name.push_back(c);
            }
        }
        std::fprintf(f, "%016llx\t%s\n",
                     static_cast<unsigned long long>(kv.first),
                     safe_name.c_str());
    }
    std::fflush(f);
    std::fclose(f);
    // Best-effort rename-over.  Unlink old first to maximise compatibility
    // with FAT32 / newlib rename semantics.
    std::remove(kCachePath);
    if (std::rename(tmp.c_str(), kCachePath) != 0) {
        UL_LOG_WARN("CheatTitleResolver: rename %s -> %s failed errno=%d",
                    tmp.c_str(), kCachePath, errno);
    } else {
        UL_LOG_INFO("CheatTitleResolver: cache written (%zu entries)",
                    g_cache.size());
    }
}

// Extract a usable name from a NACP control struct.  Tries current system
// language first, then English (American), then any non-empty language.
std::string PickNacpName(const NacpStruct &nacp) {
    u64 lang_code = 0;
    SetLanguage lang = SetLanguage_ENUS;
    if (R_SUCCEEDED(setGetSystemLanguage(&lang_code))) {
        setMakeLanguage(lang_code, &lang);
    }
    // First: current system language.
    if (lang < SetLanguage_Total) {
        const NacpLanguageEntry &le = nacp.lang[lang];
        if (le.name[0] != '\0') {
            return std::string(le.name);
        }
    }
    // Second: American English.
    {
        const NacpLanguageEntry &le = nacp.lang[SetLanguage_ENUS];
        if (le.name[0] != '\0') {
            return std::string(le.name);
        }
    }
    // Third: any non-empty.
    for (size_t i = 0; i < 16; ++i) {
        const NacpLanguageEntry &le = nacp.lang[i];
        if (le.name[0] != '\0') {
            return std::string(le.name);
        }
    }
    return {};
}

// v3.6 redesign: namespace-static NS buffers.
// nsListApplicationRecord batch: 32 entries × sizeof(NsApplicationRecord)
//   (≤ 1 KB) — allocated in BSS at boot, reused across all resolve calls.
// nsGetApplicationControlData buffer: 24 KB — allocated in BSS at boot.
// These large stable allocations DO NOT participate in the malloc churn
// that displaced the streaming-MP3 FILE struct in the v3.6 wave-1 build.
constexpr s32 kNsBatchSize = 32;
NsApplicationRecord       g_ns_batch[kNsBatchSize];
NsApplicationControlData  g_ns_ctrl;

// Synchronous resolver — runs on the calling (main) thread.  Replaces the
// detached-thread version that crashed the SDL2_mixer audio thread.
void ResolveSync(const std::vector<std::uint64_t> &tids) {
    // 1. Snapshot the installed-app set via nsListApplicationRecord.
    std::unordered_set<std::uint64_t> installed;
    {
        s32 offset = 0;
        for (;;) {
            s32 count = 0;
            const Result rc =
                nsListApplicationRecord(g_ns_batch, kNsBatchSize, offset, &count);
            if (R_FAILED(rc) || count <= 0) {
                if (R_FAILED(rc)) {
                    UL_LOG_WARN("CheatTitleResolver: nsListApplicationRecord "
                                "rc=0x%08X at offset=%d", rc, offset);
                }
                break;
            }
            for (s32 i = 0; i < count; ++i) {
                installed.insert(static_cast<std::uint64_t>(
                    g_ns_batch[i].application_id));
            }
            if (count < kNsBatchSize) break;
            offset += count;
        }
    }
    UL_LOG_INFO("CheatTitleResolver: %zu apps installed on system",
                installed.size());

    // 2. Per-TID NACP lookup using the static 24 KB buffer.
    size_t resolved_count        = 0;
    size_t skipped_not_installed = 0;
    size_t skipped_cached        = 0;
    size_t failed                = 0;
    for (std::uint64_t tid : tids) {
        if (installed.find(tid) == installed.end()) {
            ++skipped_not_installed;
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_cache.find(tid) != g_cache.end()) {
                ++skipped_cached;
                continue;
            }
        }
        u64 actual_size = 0;
        const Result rc = nsGetApplicationControlData(
            NsApplicationControlSource_Storage,
            tid,
            &g_ns_ctrl,
            sizeof(g_ns_ctrl),
            &actual_size);
        if (R_FAILED(rc) || actual_size < sizeof(NacpStruct)) {
            ++failed;
            UL_LOG_WARN("CheatTitleResolver: NACP fetch 0x%016llX rc=0x%08X "
                        "size=%llu", static_cast<unsigned long long>(tid),
                        rc, static_cast<unsigned long long>(actual_size));
            continue;
        }
        std::string name = PickNacpName(g_ns_ctrl.nacp);
        if (name.empty()) {
            ++failed;
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_cache[tid] = std::move(name);
        }
        ++resolved_count;
    }

    // 3. Write cache to disk.
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        WriteCacheLocked();
    }
    UL_LOG_INFO("CheatTitleResolver: resolve done — resolved=%zu cached=%zu "
                "not_installed=%zu failed=%zu",
                resolved_count, skipped_cached, skipped_not_installed, failed);
}

}  // namespace

std::string QdCheatTitleResolver::Lookup(std::uint64_t tid) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_loaded) {
        LoadCacheLocked_NoFsLock();
    }
    auto it = g_cache.find(tid);
    if (it == g_cache.end()) return {};
    return it->second;
}

void QdCheatTitleResolver::StartResolve(
        const std::vector<std::uint64_t> &tids,
        std::function<void()>             on_resolved) {
    if (tids.empty()) {
        if (on_resolved) on_resolved();
        return;
    }
    bool expected = false;
    if (!g_in_flight.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
        UL_LOG_INFO("CheatTitleResolver: StartResolve no-op (resolution "
                    "already in flight)");
        if (on_resolved) on_resolved();
        return;
    }
    // Ensure cache is loaded from disk first so we skip already-cached TIDs.
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_loaded) {
            LoadCacheLocked_NoFsLock();
        }
    }
    // v3.6 redesign 2026-05-28: SYNCHRONOUS resolve on calling (main) thread.
    // The previous std::thread + detach pattern raced newlib's per-thread
    // _reent struct against SDL2_mixer's audio thread reading streaming MP3
    // from a FILE*, NULL-derefing offset 0x28 inside __sread.  Running the
    // NS IPC on the same thread that called us (main UI thread) means the
    // audio thread is never re-entered into newlib stdio concurrently with
    // us; SDL_mixer keeps streaming undisturbed.
    ResolveSync(tids);
    // Persist updated cache to disk for next boot.
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        WriteCacheLocked();
    }
    g_in_flight.store(false, std::memory_order_release);
    if (on_resolved) on_resolved();
}

bool QdCheatTitleResolver::IsBusy() {
    return g_in_flight.load(std::memory_order_acquire);
}

std::size_t QdCheatTitleResolver::CacheSize() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_cache.size();
}

}  // namespace ul::menu::qdesktop
