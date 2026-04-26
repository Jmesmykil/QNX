// qd_IconCache.cpp — 24-entry LRU icon cache for uMenu C++ SP1 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
//
// On-disk format: 64x64 BGRA raw bytes (CACHE_ENTRY_BYTES = 16384) per entry.
// Filename: "<hash16hex>.rgba" in ICON_CACHE_DIR.
// Memory LRU: 24 entries, evict by minimum tick (oldest).
// Disk: persisted across power cycles; read on memory miss; written on Put().
// No mtime check in C++ SP1 -- full mtime support deferred to SP2.
//
// L-cycle optimisations (v1.2.0):
//   - DiskPathBuf: stack-only hot path for Get() -- no heap allocation.
//   - LruSlot: short-circuits on first invalid slot to avoid full linear scan
//     while the ring is still being populated (first 24 loads).
//   - CacheStats accounting behind QD_ICON_CACHE_STATS compile flag.
//   - OccupiedCount, GetStats, LogAndResetStats added.

#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdIconCache::QdIconCache() : tick_counter_(0), stats_{} {
    // Zero-initialise all slots.
    for (auto &e : entries_) {
        e.path_hash = 0;
        e.tick      = 0;
        e.valid     = false;
        // Pixel buffer is inside the struct -- no heap allocation for the slots.
    }
}

QdIconCache::~QdIconCache() {
    // All resources are value-typed inside the entries array -- nothing to free.
}

// ── EnsureDir ─────────────────────────────────────────────────────────────────

bool QdIconCache::EnsureDir() {
    // Try opening a read handle on the directory; if that fails, create it.
    // Horizon FsService create_directory returns 0 on success and
    // 0x402 (already-exists) on re-call -- both are acceptable.
    // sdmc: is mounted by uMenu via fsdevMountSdmc() at boot (see uMenu main.cpp).
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc == nullptr) {
        UL_LOG_WARN("qdesktop: IconCache EnsureDir: fsdevGetDeviceFileSystem(sdmc) NULL");
        return false;
    }
    // Directory name MUST stay in lockstep with ICON_CACHE_DIR in qd_IconCache.hpp.
    Result rc = fsFsCreateDirectory(sdmc, "/switch/qos-icon-cache-v2");
    const bool ok = R_SUCCEEDED(rc) || (rc == 0x402);
    UL_LOG_INFO("qdesktop: IconCache EnsureDir rc=0x%X ok=%d", static_cast<unsigned>(rc), ok ? 1 : 0);
    return ok;
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdIconCache::AdvanceTick() {
    ++tick_counter_;

#if defined(QD_ICON_CACHE_STATS)
    // Emit stats every 3600 ticks (~60 seconds at 60 fps) so the log is not
    // flooded but still captures long-run behaviour.
    if ((tick_counter_ % 3600ULL) == 0ULL) {
        LogAndResetStats();
    }
#endif
}

// ── PathHash (u64 DJB2, wrapping) ─────────────────────────────────────────────

u64 QdIconCache::PathHash(const char *path) {
    u64 h = 5381ULL;
    for (const u8 *p = reinterpret_cast<const u8 *>(path); *p != '\0'; ++p) {
        // Wrapping mul-33 then add byte -- mirrors Rust h.wrapping_mul(33).wrapping_add(b).
        h = h * 33ULL + static_cast<u64>(*p);
    }
    return h;
}

// ── DiskPathBuf (stack-only hot path) ────────────────────────────────────────

// Writes "sdmc:/switch/qos-icon-cache-v2/<hash16hex>.rgba\0" into buf.
// buf_len must be >= 64.  Returns the number of characters written (excl. NUL).
// Called from Get() to avoid a heap allocation in the render loop.
size_t QdIconCache::DiskPathBuf(u64 hash, char *buf, size_t buf_len) {
    static constexpr const char hex[] = "0123456789abcdef";

    // ICON_CACHE_DIR = "sdmc:/switch/qos-icon-cache-v2/"  (31 chars + NUL)
    static constexpr size_t prefix_len = sizeof(ICON_CACHE_DIR) - 1u; // 31
    static constexpr size_t stem_len   = 16u + 5u; // 16 hex + ".rgba"
    static constexpr size_t total_len  = prefix_len + stem_len; // 52

    if (buf_len < total_len + 1u) {
        // Buffer too small -- return 0 to signal failure.
        return 0u;
    }

    // Copy prefix (including trailing '/').
    memcpy(buf, ICON_CACHE_DIR, prefix_len);

    // Write 16 hex digits.
    for (int i = 15; i >= 0; --i) {
        buf[prefix_len + static_cast<size_t>(i)] = hex[hash & 0xFu];
        hash >>= 4;
    }

    // Append ".rgba\0".
    buf[prefix_len + 16u] = '.';
    buf[prefix_len + 17u] = 'r';
    buf[prefix_len + 18u] = 'g';
    buf[prefix_len + 19u] = 'b';
    buf[prefix_len + 20u] = 'a';
    buf[prefix_len + 21u] = '\0';

    return total_len;
}

// ── DiskPath (heap path, used only in WriteToDisk) ────────────────────────────

// Returns: "sdmc:/switch/qos-icon-cache-v2/<hash16hex>.rgba"
std::string QdIconCache::DiskPath(u64 hash) {
    char buf[64];
    const size_t n = DiskPathBuf(hash, buf, sizeof(buf));
    if (n == 0u) {
        return {};
    }
    return std::string(buf, n);
}

// ── LruSlot ───────────────────────────────────────────────────────────────────
// Short-circuits on the first invalid slot: while the ring is filling (fewer
// than MEM_CACHE_CAP entries loaded) the first scan finds an empty slot in O(1)
// instead of always walking all 24 entries.  Once full, falls through to the
// standard min-tick search.

size_t QdIconCache::LruSlot() const {
    size_t best_idx  = 0;
    u64    best_tick = UINT64_MAX;
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (!entries_[i].valid) {
            // First empty slot -- use it immediately.
            return i;
        }
        if (entries_[i].tick < best_tick) {
            best_tick = entries_[i].tick;
            best_idx  = i;
        }
    }
    return best_idx;
}

// ── FindSlot ──────────────────────────────────────────────────────────────────

size_t QdIconCache::FindSlot(u64 hash) const {
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (entries_[i].valid && entries_[i].path_hash == hash) {
            return i;
        }
    }
    return MEM_CACHE_CAP; // sentinel: not found
}

// ── ScaleToBgra64 ─────────────────────────────────────────────────────────────

// Nearest-neighbour resize src (src_w x src_h, RGBA) -> dst (64x64 BGRA).
// dst must point to CACHE_ENTRY_BYTES (16384) bytes.
// Swaps R and B channels on write (RGBA -> BGRA).
void QdIconCache::ScaleToBgra64(u8 *dst, const u8 *src, s32 src_w, s32 src_h) {
    const s32 dw = static_cast<s32>(CACHE_ICON_W);
    const s32 dh = static_cast<s32>(CACHE_ICON_H);

    for (s32 dy = 0; dy < dh; ++dy) {
        for (s32 dx = 0; dx < dw; ++dx) {
            // Nearest-neighbour: floor(dst_coord * src_dim / dst_dim).
            const s32 sx = (dx * src_w) / dw;
            const s32 sy = (dy * src_h) / dh;
            // Clamp to src bounds (defensive -- src_w/src_h must be > 0).
            const s32 sx_c = (sx < src_w - 1) ? sx : src_w - 1;
            const s32 sy_c = (sy < src_h - 1) ? sy : src_h - 1;

            const size_t si = (static_cast<size_t>(sy_c) * static_cast<size_t>(src_w)
                               + static_cast<size_t>(sx_c)) * 4u;
            const size_t di = (static_cast<size_t>(dy) * static_cast<size_t>(dw)
                               + static_cast<size_t>(dx)) * 4u;

            // RGBA -> BGRA channel swap.
            dst[di    ] = src[si + 2]; // B <- src.R
            dst[di + 1] = src[si + 1]; // G
            dst[di + 2] = src[si    ]; // R <- src.B
            dst[di + 3] = src[si + 3]; // A
        }
    }
}

// ── ReadFromDisk ──────────────────────────────────────────────────────────────

// Read 64x64 BGRA (16384 bytes) from the on-disk cache file.
// Returns true on success, false if the file is absent or has wrong size.
// Uses DiskPathBuf (stack-only) to avoid a heap allocation in the render loop.
bool QdIconCache::ReadFromDisk(u64 hash, u8 *dst_bgra) {
    char path_buf[64];
    if (DiskPathBuf(hash, path_buf, sizeof(path_buf)) == 0u) {
        return false;
    }
    FILE *f = fopen(path_buf, "rb");
    if (!f) {
        return false;
    }
    const size_t n = fread(dst_bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    return n == CACHE_ENTRY_BYTES;
}

// ── WriteToDisk ───────────────────────────────────────────────────────────────

// Write 64x64 BGRA (16384 bytes) to the on-disk cache file.
// Silently ignores write errors (per spec).
void QdIconCache::WriteToDisk(u64 hash, const u8 *bgra) {
    const std::string path = DiskPath(hash);
    if (path.empty()) {
        return;
    }
    // Write to a .tmp sibling first for atomicity.
    std::string tmp = path + ".tmp";

    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) {
        return; // SD not writable; silently ignore.
    }
    const size_t written = fwrite(bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    if (written != CACHE_ENTRY_BYTES) {
        // Partial write -- clean up the temp file and abort.
        remove(tmp.c_str());
        return;
    }
    // Best-effort pseudo-atomic rename: remove old, rename .tmp -> canonical.
    // FAT32 does not support atomic rename (EXDEV on cross-dir, no fsync guarantee).
    // A power loss between remove() and rename() leaves no cache file (safe: cache
    // miss on next boot re-extracts the icon from the NRO).  This is acceptable.
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

// ── Get ───────────────────────────────────────────────────────────────────────

// Look up an icon by NRO path.
// 1. Memory LRU hit  -> update tick, return pixel pointer.
// 2. On-disk hit     -> load into LRU (evicting LRU slot), return pixel pointer.
// 3. No hit          -> return nullptr.
const u8 *QdIconCache::Get(const char *nro_path) {
    const u64 hash = PathHash(nro_path);

    // ── 1. Memory LRU hit ─────────────────────────────────────────────────
    const size_t mem_idx = FindSlot(hash);
    if (mem_idx < MEM_CACHE_CAP) {
        // Update LRU timestamp.
        entries_[mem_idx].tick = tick_counter_;
#if defined(QD_ICON_CACHE_STATS)
        ++stats_.mem_hits;
#endif
        return entries_[mem_idx].pixels;
    }

    // ── 2. On-disk hit ────────────────────────────────────────────────────
    // Select the eviction candidate before reading from disk so we do not
    // touch entries_ until we know the disk read will succeed.
    const size_t lru_idx = LruSlot();
    IconCacheEntry &slot = entries_[lru_idx];

#if defined(QD_ICON_CACHE_STATS)
    // Count eviction only when we are displacing a valid entry.
    const bool evicting_valid = slot.valid;
#endif

    // Attempt to read 16384 BGRA bytes from disk into the eviction slot.
    if (ReadFromDisk(hash, slot.pixels)) {
        slot.path_hash = hash;
        slot.tick      = tick_counter_;
        slot.valid     = true;
#if defined(QD_ICON_CACHE_STATS)
        if (evicting_valid) { ++stats_.evictions; }
        ++stats_.disk_hits;
#endif
        return slot.pixels;
    }

    // ── 3. Not cached ─────────────────────────────────────────────────────
#if defined(QD_ICON_CACHE_STATS)
    ++stats_.misses;
#endif
    return nullptr;
}

// ── Put ───────────────────────────────────────────────────────────────────────

// Store an icon from a raw RGBA buffer (any size).
// Resizes to 64x64 and swaps to BGRA, evicts LRU slot if full,
// writes the BGRA bytes to disk.
void QdIconCache::Put(const char *nro_path, const u8 *rgba_pixels,
                      s32 src_w, s32 src_h) {
    const u64  hash = PathHash(nro_path);

    // Reuse existing slot if this path is already cached.
    size_t idx = FindSlot(hash);
    if (idx >= MEM_CACHE_CAP) {
        idx = LruSlot();
#if defined(QD_ICON_CACHE_STATS)
        if (entries_[idx].valid) { ++stats_.evictions; }
#endif
    }

    IconCacheEntry &slot = entries_[idx];

    // Scale + channel-swap into the slot's pixel buffer.
    ScaleToBgra64(slot.pixels, rgba_pixels, src_w, src_h);
    slot.path_hash = hash;
    slot.tick      = tick_counter_;
    slot.valid     = true;

#if defined(QD_ICON_CACHE_STATS)
    ++stats_.puts;
#endif

    // Persist the BGRA bytes to SD.
    WriteToDisk(hash, slot.pixels);
}

// ── OccupiedCount ─────────────────────────────────────────────────────────────

size_t QdIconCache::OccupiedCount() const {
    size_t count = 0u;
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (entries_[i].valid) { ++count; }
    }
    return count;
}

// ── GetStats ──────────────────────────────────────────────────────────────────

CacheStats QdIconCache::GetStats() const {
    return stats_;
}

// ── LogAndResetStats ──────────────────────────────────────────────────────────

void QdIconCache::LogAndResetStats() {
#if defined(QD_ICON_CACHE_STATS)
    UL_LOG_INFO(
        "qdesktop: IconCache stats -- mem_hits=%" PRIu64
        " disk_hits=%" PRIu64
        " misses=%" PRIu64
        " evictions=%" PRIu64
        " puts=%" PRIu64
        " occupied=%zu/%zu",
        stats_.mem_hits,
        stats_.disk_hits,
        stats_.misses,
        stats_.evictions,
        stats_.puts,
        OccupiedCount(),
        MEM_CACHE_CAP);
    stats_ = {};
#else
    // Stats not compiled in; log occupied count only.
    UL_LOG_INFO("qdesktop: IconCache occupied=%zu/%zu (compile with QD_ICON_CACHE_STATS for full stats)",
                OccupiedCount(), MEM_CACHE_CAP);
#endif
}

} // namespace ul::menu::qdesktop
