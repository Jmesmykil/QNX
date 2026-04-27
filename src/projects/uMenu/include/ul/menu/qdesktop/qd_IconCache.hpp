// qd_IconCache.hpp — LRU icon cache for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
#pragma once
#include <pu/Plutonium>
#include <array>
#include <string>

namespace ul::menu::qdesktop {

// ── Cache constants ────────────────────────────────────────────────────────
static constexpr u32    CACHE_ICON_W       = 64;     // from icon_cache.rs CACHE_ICON_W
static constexpr u32    CACHE_ICON_H       = 64;     // from icon_cache.rs CACHE_ICON_H
static constexpr size_t CACHE_ENTRY_BYTES  = CACHE_ICON_W * CACHE_ICON_H * 4;  // 16384
// F9 (stabilize-4): raised from 24 → 128 so a full Launchpad page of apps
// (up to ~80 icons per scroll page) can cache without LRU thrashing.  At
// 128 entries × 16 KB = 2 MB total — well within Switch heap headroom.
static constexpr size_t MEM_CACHE_CAP      = 128;
// On-disk directory.  Created by EnsureIconCacheDir() on first use.
// v2: directory bumped from "qos-icon-cache" → "qos-icon-cache-v2" because the
// v1 ring contains channel-scrambled NRO icons (qd_NroAsset RGBA8888 instead of
// ABGR8888). Old files are ignored, not migrated; user can delete the v1 dir
// from the SD root at leisure.
// F5 (stabilize-5): RC-B4 — directory bumped to "qos-icon-cache-v3" to invalidate
// any stale gray-block entries from builds where nsextGetApplicationControlData
// was returning 0x196002 (all NS icons landed as gray blobs in v2).
// EnsureDir() also writes a generation.txt sentinel (see qd_IconCache.cpp).
static constexpr const char ICON_CACHE_DIR[]        = "sdmc:/switch/qos-icon-cache-v3/";
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
    // Writes 64×64 BGRA to disk.
    void Put(const char *nro_path,
             const u8 *rgba_pixels, s32 src_w, s32 src_h);

    // Tick counter — incremented every frame by QdDesktopIconsElement.
    void AdvanceTick();

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
    size_t FindSlot(u64 hash) const;

private:
    std::array<IconCacheEntry, MEM_CACHE_CAP> entries_;
    u64 tick_counter_;

    // Build the on-disk filename for a hash.
    static std::string DiskPath(u64 hash);

    // Read 64×64 BGRA from disk → fill entry pixels. Returns false if file missing/corrupt.
    bool ReadFromDisk(u64 hash, u8 *dst_bgra);

    // Write 64×64 BGRA to disk. Silently ignores write errors.
    void WriteToDisk(u64 hash, const u8 *bgra);
};

} // namespace ul::menu::qdesktop
