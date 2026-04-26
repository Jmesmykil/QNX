// qd_IconCache.hpp — 24-entry LRU icon cache for uMenu C++ SP1 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
//
// L-cycle optimisations applied (v1.2.0):
//   - CacheStats struct + per-operation accounting behind QD_ICON_CACHE_STATS.
//   - Lazy disk promotion: ReadFromDisk promoted on Get; avoids blocking the
//     render loop on disk I/O when the memory LRU is cold but disk is warm.
//   - EvictCount() exposes eviction rate for profiling.
//   - LruSlot() short-circuits on the first invalid (tick==0) slot found,
//     avoiding a full scan when slots are only partially filled.
//   - DiskPath() is a pure char-array function; no std::string heap allocation
//     in the hot path (disk reads during render frames).
#pragma once
#include <pu/Plutonium>
#include <array>
#include <string>

namespace ul::menu::qdesktop {

// ── Cache constants ────────────────────────────────────────────────────────
static constexpr u32    CACHE_ICON_W       = 64;     // from icon_cache.rs CACHE_ICON_W
static constexpr u32    CACHE_ICON_H       = 64;     // from icon_cache.rs CACHE_ICON_H
static constexpr size_t CACHE_ENTRY_BYTES  = CACHE_ICON_W * CACHE_ICON_H * 4;  // 16384
static constexpr size_t MEM_CACHE_CAP      = 24;     // from icon_cache.rs MEM_CACHE_CAP
// On-disk directory.  Created by EnsureIconCacheDir() on first use.
// v2: directory bumped from "qos-icon-cache" to "qos-icon-cache-v2" because the
// v1 ring contains channel-scrambled NRO icons (qd_NroAsset RGBA8888 instead of
// ABGR8888). Old files are ignored, not migrated; user can delete the v1 dir
// from the SD root at leisure.
static constexpr const char ICON_CACHE_DIR[] = "sdmc:/switch/qos-icon-cache-v2/";

// ── Debug stats (gated behind QD_ICON_CACHE_STATS) ────────────────────────
// Define QD_ICON_CACHE_STATS=1 at compile time to enable hit/miss/eviction
// counters and periodic UL_LOG_INFO dumps.  Off by default to keep the
// production build lean.
struct CacheStats {
    u64 mem_hits;      // Get() returned from memory LRU
    u64 disk_hits;     // Get() promoted an entry from disk into memory LRU
    u64 misses;        // Get() found nothing in memory or on disk
    u64 evictions;     // LRU slot reused, displacing a valid entry
    u64 puts;          // Put() calls (icon stored or refreshed)
};

// Single LRU slot.
struct IconCacheEntry {
    u64  path_hash;     // DJB2 u64 of the NRO path
    u64  tick;          // frame counter at last access — LRU eviction key
    u8   pixels[CACHE_ENTRY_BYTES]; // 64x64 BGRA — disk format
    bool valid;         // true if the slot holds a real icon
};

// 24-entry in-memory LRU + transparent on-disk persistence.
// Disk format per entry: 64x64 BGRA (16384 bytes).
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
    // - Falls back to on-disk read if present (promotes into memory LRU).
    // - Returns nullptr if not cached.
    // - Returned pointer is into the entry's pixel buffer; valid until next Put or eviction.
    const u8 *Get(const char *nro_path);

    // Store an icon from a raw RGBA buffer (any size -- will be resized to 64x64).
    // Evicts LRU slot if memory is full.
    // Writes 64x64 BGRA to disk.
    void Put(const char *nro_path,
             const u8 *rgba_pixels, s32 src_w, s32 src_h);

    // Tick counter -- incremented every frame by QdDesktopIconsElement.
    void AdvanceTick();

    // Returns a snapshot of the current stats counters.
    // All counters are zero when QD_ICON_CACHE_STATS is not defined.
    CacheStats GetStats() const;

    // Logs stats via UL_LOG_INFO and resets all counters to zero.
    // No-op when QD_ICON_CACHE_STATS is not defined.
    void LogAndResetStats();

    // Returns the number of entries currently occupied in the memory LRU.
    size_t OccupiedCount() const;

protected:
    // ── Pure-logic helpers exposed to test shim via inheritance ───────────────
    // DJB2 path hash -- u64 accumulator, wrapping mul as in icon_cache.rs.
    static u64 PathHash(const char *path);

    // Nearest-neighbor resize src (src_w x src_h RGBA) -> dst (64x64) + RGBA->BGRA swap.
    // dst must point to CACHE_ENTRY_BYTES bytes.
    static void ScaleToBgra64(u8 *dst,
                               const u8 *src, s32 src_w, s32 src_h);

    // Find the LRU slot index (earliest invalid slot, then slot with minimum tick).
    // Short-circuits on the first invalid slot to avoid full scan during fill.
    size_t LruSlot() const;

    // Find an existing slot by hash, returns MEM_CACHE_CAP if not found.
    size_t FindSlot(u64 hash) const;

private:
    std::array<IconCacheEntry, MEM_CACHE_CAP> entries_;
    u64 tick_counter_;

    // Stats counters.  Unconditionally present (zero-cost when unused).
    CacheStats stats_;

    // Build the on-disk filename for a hash into buf (must be >= 64 bytes).
    // Returns the number of characters written (excluding NUL).
    // Does NOT allocate heap memory -- buf is caller-supplied.
    static size_t DiskPathBuf(u64 hash, char *buf, size_t buf_len);

    // Build the on-disk filename for a hash (heap std::string -- used only in
    // WriteToDisk to construct the .tmp sibling path).
    static std::string DiskPath(u64 hash);

    // Read 64x64 BGRA from disk -> fill entry pixels. Returns false if file missing/corrupt.
    bool ReadFromDisk(u64 hash, u8 *dst_bgra);

    // Write 64x64 BGRA to disk. Silently ignores write errors.
    void WriteToDisk(u64 hash, const u8 *bgra);
};

} // namespace ul::menu::qdesktop
