// qd_IconCache.hpp — LRU icon cache for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
//
// v1.8.18: GetSharedIconCache() / GetSharedIconCacheMutex() provide the
// process-wide singleton shared between Desktop and Launchpad.
// v1.8.20 Change 4: LoadFromDisk / SaveToDisk persist the full in-memory LRU
// as a single binary blob at sdmc:/ulaunch/cache/icons.bgra so icons survive
// across sessions without re-extracting NROs.  Both use kernel IPC (fsFsOpenFile /
// fsFileRead / fsFileWrite) rather than the fsdev POSIX shim.
// A1-OPT-1: GetSharedIconCache() now creates the singleton lazily on first call
// (heap-allocated, not static-storage) to defer the ~393 KB SD blob read off
// the pre-main() boot path.  SaveToDisk at exit is guaranteed via atexit().
#pragma once
#include <pu/Plutonium>
#include <switch/runtime/devices/fs_dev.h>
#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ul::menu::qdesktop {

// ── Cache constants ────────────────────────────────────────────────────────
static constexpr u32    CACHE_ICON_W       = 64;     // from icon_cache.rs CACHE_ICON_W
static constexpr u32    CACHE_ICON_H       = 64;     // from icon_cache.rs CACHE_ICON_H
static constexpr size_t CACHE_ENTRY_BYTES  = CACHE_ICON_W * CACHE_ICON_H * 4;  // 16384
// 2026-05-06 stabilize: reverted from 128 → 24 (Apr-26-era value).
// Evidence: at 128, std::array<IconCacheEntry, 128> = 128 × 16,408 =
// 2,100,224 B of static BSS in uMenu's library-applet image.  Combined
// with other BSS, this pushed uMenu past the FW20 SystemApplet pool
// ceiling — the kernel ran out of HIPC session memory during the
// uSystem→uMenu launch handshake and crashed with 2011-0102
// (ams::sf::hipc::ResultOutOfSessionMemory) at uSystem+0x36440 plus
// 2128-0100 in am at +0x4056c.  Confirmed against 9 crash reports today,
// all with byte-identical PC offsets.  The "well within Switch heap
// headroom" assessment in the prior comment was wrong — heap and the
// SystemApplet pool are separate budgets; FW20 tightened the latter.
// At 24, footprint matches the Apr-26 hardware-validated baseline that
// boots reliably.
//
// LRU thrashing tradeoff: a full Launchpad page (~80 icons) will now
// exceed the cap and re-load the oldest entries.  Acceptable cost for
// stable boot.  Only re-raise this cap after hardware-measuring total
// BSS against the FW20 pool budget on this exact firmware.
static constexpr size_t MEM_CACHE_CAP      = 24;
// On-disk directory.  Created by EnsureIconCacheDir() on first use.
// v2: directory bumped from "qos-icon-cache" → "qos-icon-cache-v2" because the
// v1 ring contains channel-scrambled NRO icons (qd_NroAsset RGBA8888 instead of
// ABGR8888). Old files are ignored, not migrated; user can delete the v1 dir
// from the SD root at leisure.
// F5 (stabilize-5): RC-B4 — directory bumped to "qos-icon-cache-v3" to invalidate
// any stale gray-block entries from builds where nsextGetApplicationControlData
// was returning 0x196002 (all NS icons landed as gray blobs in v2).
// v1.8.22g B66 root cause: directory bumped to "qos-icon-cache-v4" to invalidate
// stale gray-block entries written by v1.8.21/v1.8.22b LoadJpegIconToCache::do_fallback
// for romfs:/ keyed payload icons. Get() reads these on a fresh process, returns
// the gray, displaces the v1.8.22d 2a-romfs LoadImageFromFile lazy load. v3 files
// stay on SD and can be deleted at leisure.
// EnsureDir() also writes a generation.txt sentinel (see qd_IconCache.cpp).
static constexpr const char ICON_CACHE_DIR[]        = "sdmc:/switch/qos-icon-cache-v4/";
static constexpr const char ICON_CACHE_GENERATION[] = "1";  // bump when on-disk format changes

// Single LRU slot.
struct IconCacheEntry {
    u64  path_hash;     // DJB2 u64 of the NRO path
    u64  tick;          // frame counter at last access — LRU eviction key
    u8   pixels[CACHE_ENTRY_BYTES]; // 64×64 BGRA — disk format
    bool valid;         // true if the slot holds a real icon
};

// In-memory LRU + transparent on-disk persistence.
// Disk format per entry: 64×64 BGRA (16384 bytes).
// Filename: "<hash16hex>.rgba"  e.g. "0000000004b2a3f1.rgba"
class QdIconCache {
public:
    QdIconCache();
    ~QdIconCache();

    // Ensure the on-disk cache directory exists (idempotent).
    // Returns true if the directory exists or was created.
    bool EnsureDir();

    // Look up an icon by NRO path.
    // - Checks memory LRU first (updates tick on hit).
    // - Falls back to on-disk read if present.
    // - Returns nullptr if not cached.
    // - Returned pointer is into the entry's pixel buffer; valid until next Put or eviction.
    const u8 *Get(const char *nro_path);

    // Store an icon from a raw RGBA buffer (any size — will be resized to 64×64).
    // Evicts LRU slot if memory is full.
    // BUG-2 fix: Put() no longer calls WriteToDisk under the caller's lock.
    // Instead it enqueues the scaled BGRA into write_queue_ (a copy of the
    // 16 KB pixel buffer + the path hash).  Call DrainWriteQueue() AFTER
    // releasing GetSharedIconCacheMutex() to flush entries to SD off-lock.
    void Put(const char *nro_path,
             const u8 *rgba_pixels, s32 src_w, s32 src_h);

    // BUG-2 fix: drain the deferred-write queue.
    // Must be called WITHOUT holding GetSharedIconCacheMutex().
    // Swaps the queue out under a brief re-acquisition, then writes each
    // entry to SD while holding NO lock.  Safe to call from the prewarm thread.
    void DrainWriteQueue();

    // BUG-7 fix: drain the deferred-read request queue.
    // Must be called WITHOUT holding GetSharedIconCacheMutex().
    // Swaps the request queue out under a brief lock (clears in_flight_reads_
    // for the swapped entries), performs ReadFromDisk() entirely off-lock for
    // each request, then re-acquires the mutex to insert the decoded pixels into
    // the LRU (or silently drops if the entry was evicted by another writer).
    // Safe to call from the prewarm/background thread alongside DrainWriteQueue().
    void DrainReadQueue();

    // Tick counter — incremented every frame by QdDesktopIconsElement.
    void AdvanceTick();

    // v1.8.20 Change 4 — Bulk BGRA persistence.
    // LoadFromDisk: called from constructor.  Reads the blob at `path` (format:
    //   CacheFileHeader + N * (CacheEntryHeader + bgra_bytes)).
    //   Returns false if file absent, corrupt, version-mismatch, or > 7 days old.
    //   On success, populates entries_ and hash_index_ from the blob; tick_counter_
    //   for loaded entries is set to 1 (distinct from 0 = invalid, but pre-dating
    //   any frame-advance ticks from this session).
    // SaveToDisk: called from destructor.  Writes all valid entries_ to `path`
    //   using kernel IPC (fsFsOpenFile + fsFileWrite).  Silently ignores errors.
    bool LoadFromDisk(const char *path);
    void SaveToDisk(const char *path);

protected:
    // ── Pure-logic helpers exposed to test shim via inheritance ───────────────
    // DJB2 path hash — u64 accumulator, wrapping mul as in icon_cache.rs.
    static u64 PathHash(const char *path);

    // Nearest-neighbor resize src (src_w×src_h RGBA) → dst (64×64) + RGBA→BGRA swap.
    // dst must point to CACHE_ENTRY_BYTES bytes.
    static void ScaleToBgra64(u8 *dst,
                               const u8 *src, s32 src_w, s32 src_h);

    // Find the LRU slot index (slot with minimum tick, invalid slots have tick=0).
    size_t LruSlot() const;

    // Find an existing slot by hash, returns MEM_CACHE_CAP if not found.
    // v1.8.19: O(1) via hash_index_ unordered_map (was O(N) linear scan).
    size_t FindSlot(u64 hash) const;

private:
    std::array<IconCacheEntry, MEM_CACHE_CAP> entries_;
    u64 tick_counter_;
    // v1.8.19: parallel index maps DJB2 hash → entries_ slot index.
    // Maintained by Put() (insert/update) and eviction in LruSlot() (erase stale key).
    // Enables O(1) FindSlot() replacing the prior O(N) linear scan.
    std::unordered_map<u64, size_t> hash_index_;
    // W6-LEDGER: per-slot ledger handle (0 = not tracked).
    uint64_t ledger_handles_[MEM_CACHE_CAP] = {};

    // ── v2.0.3-A8 hit-rate observability ─────────────────────────────────
    // Counters serialised by GetSharedIconCacheMutex() (held by every caller
    // of Get / Put per the contract above).  Plain u64 is sufficient under
    // that lock; emitted via UL_LOG_INFO from MaybeLogHitRate() and dtor.
    u64 mem_hits_      = 0;
    u64 disk_hits_     = 0;
    u64 misses_        = 0;
    u64 puts_          = 0;
    u64 last_log_tick_ = 0;

    // BUG-2 fix: one entry per deferred disk write.
    // pixels is a full 16 KB BGRA copy owned by this struct.
    struct PendingWrite {
        u64 hash;
        u8  pixels[CACHE_ENTRY_BYTES];
    };
    // Deferred-write queue: populated by Put() under the shared mutex;
    // drained by DrainWriteQueue() OFF the mutex on the prewarm thread.
    std::vector<PendingWrite> write_queue_;

    // BUG-7 fix: deferred disk-read queue.
    // The render thread enqueues a hash here on a memory-miss instead of
    // calling ReadFromDisk() synchronously.  The prewarm/background thread
    // drains the queue via DrainReadQueue(): reads + decodes pixels from disk
    // off-lock, then re-acquires the mutex only to insert the result into the
    // LRU (same pattern as DrainWriteQueue / BUG-2).
    //
    // in_flight_reads_: set of hashes that are already queued or being read,
    // so that N consecutive render-thread misses for the same icon enqueue only
    // one read request (no duplicate disk I/O, no double-slot eviction race).
    // Erased when DrainReadQueue() swaps the queue out.
    struct PendingRead {
        u64 hash;
    };
    std::vector<PendingRead>   read_request_queue_;
    std::unordered_set<u64>    in_flight_reads_;

    // Build the on-disk filename for a hash.
    static std::string DiskPath(u64 hash);

    // Read 64×64 BGRA from disk → fill entry pixels. Returns false if file missing/corrupt.
    bool ReadFromDisk(u64 hash, u8 *dst_bgra);

    // Write 64×64 BGRA to disk. Silently ignores write errors.
    void WriteToDisk(u64 hash, const u8 *bgra);

    // v2.0.3-A8: emit hit-rate summary every ~5 minutes of wall clock so the
    // log captures session-long trend without spamming.  Called from each
    // Get-path return; throttled by tick interval (~18 000 ticks @ 60 fps).
    void MaybeLogHitRate();
};

// ── Process-wide shared singleton (v1.8.18) ────────────────────────────────
// Both QdDesktopIconsElement and QdLaunchpadElement share the same QdIconCache
// and the same std::mutex via these two accessors.  All Get/Put callers MUST
// hold GetSharedIconCacheMutex() for the duration of the call.
QdIconCache& GetSharedIconCache();
std::mutex&  GetSharedIconCacheMutex();

// ── Negative-extract cache accessor (v1.8.19) ──────────────────────────────
// Per-session set of NRO paths for which ExtractNroIcon() returned invalid.
// QdDesktopIconsElement::LoadNroIconToCache() checks this at entry; if the
// path is present, it returns false immediately (no disk I/O, no ASET parse).
// On extraction failure the path is inserted.  Lifetime: static storage
// duration, same as GetSharedIconCache().
std::unordered_set<std::string>& GetFailedExtractPaths();

} // namespace ul::menu::qdesktop
