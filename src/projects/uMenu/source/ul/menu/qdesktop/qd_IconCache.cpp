// qd_IconCache.cpp — 128-entry LRU icon cache for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
//
// On-disk format (individual files): 64×64 BGRA raw bytes (CACHE_ENTRY_BYTES = 16384).
// Filename: "<hash16hex>.rgba" in ICON_CACHE_DIR.
// Memory LRU: 128 entries, evict by minimum tick (oldest).
// Disk (individual files): read on memory miss; written on Put().
//
// v1.8.18: File-scope singleton so Desktop and Launchpad share one cache object.
// v1.8.19: O(1) FindSlot via hash_index_.
// v1.8.20 Change 4: LoadFromDisk / SaveToDisk persist all valid entries as a single
//   binary blob at sdmc:/ulaunch/cache/icons.bgra using kernel IPC (fsFsOpenFile /
//   fsFileRead / fsFileWrite).  7-day staleness check on the blob file via
//   fsGetLastAccessTimeStamp (or fallback to always-accept when unavailable).
//
// Blob binary format:
//   CacheFileHeader  (16 bytes):  u32 magic=0x43415143 ('QCAC' LE), u32 version=1,
//                                 u32 entry_count, u32 reserved=0
//   Per valid entry  (40 + CACHE_ENTRY_BYTES bytes):
//     u64 key_hash; u32 width; u32 height; u32 bgra_size; u8 bgra[bgra_size]
//   (width/height are stored but ignored on load — always 64×64 for v1.)

#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <time.h>

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

// Blob path constant — must be consistent between constructor and destructor.
static constexpr const char BULK_BLOB_PATH[] = "sdmc:/ulaunch/cache/icons.bgra";

QdIconCache::QdIconCache() : tick_counter_(0) {
    // Zero-initialise all slots.
    for (auto &e : entries_) {
        e.path_hash = 0;
        e.tick      = 0;
        e.valid     = false;
        // Pixel buffer is inside the struct — no heap allocation for the slots.
    }
    // W6-LEDGER: zero all per-slot handles.
    for (auto &h : ledger_handles_) { h = 0; }
    // v1.8.19: pre-reserve the hash→index map to avoid rehashes across MEM_CACHE_CAP inserts.
    hash_index_.reserve(MEM_CACHE_CAP);
    // v1.8.20 Change 4: attempt to warm the LRU from the previous session's blob.
    // LoadFromDisk returns false quietly on first boot (file absent) or stale blob.
    LoadFromDisk(BULK_BLOB_PATH);
}

// v2.0.3-A8: emit hit-rate summary every ~5 min of wall clock.  Throttled by
// tick interval (18 000 ticks ≈ 5 min @ 60 fps) so the log line appears once
// per long-running session window instead of every Get call.  Caller already
// holds GetSharedIconCacheMutex(), so plain u64 reads are race-free here.
void QdIconCache::MaybeLogHitRate() {
    constexpr u64 kLogIntervalTicks = 18000ULL;
    if (tick_counter_ < last_log_tick_ + kLogIntervalTicks) {
        return;
    }
    last_log_tick_ = tick_counter_;
    const u64 total = mem_hits_ + disk_hits_ + misses_;
    if (total == 0) {
        return;
    }
    const u64 mem_pct  = (mem_hits_  * 100) / total;
    const u64 disk_pct = (disk_hits_ * 100) / total;
    const u64 miss_pct = (misses_    * 100) / total;
    UL_LOG_INFO("qdesktop: A8 IconCache hit-rate [periodic]: "
                "mem=%llu (%llu%%) disk=%llu (%llu%%) miss=%llu (%llu%%) puts=%llu total=%llu",
                (unsigned long long)mem_hits_,  (unsigned long long)mem_pct,
                (unsigned long long)disk_hits_, (unsigned long long)disk_pct,
                (unsigned long long)misses_,    (unsigned long long)miss_pct,
                (unsigned long long)puts_,
                (unsigned long long)total);
}

QdIconCache::~QdIconCache() {
    // v2.0.3-A8: emit final hit-rate summary at process exit so the session
    // log captures terminal counters even if the periodic emission window
    // hasn't elapsed.
    const u64 total = mem_hits_ + disk_hits_ + misses_;
    if (total > 0) {
        const u64 mem_pct  = (mem_hits_  * 100) / total;
        const u64 disk_pct = (disk_hits_ * 100) / total;
        const u64 miss_pct = (misses_    * 100) / total;
        UL_LOG_INFO("qdesktop: A8 IconCache hit-rate [final]: "
                    "mem=%llu (%llu%%) disk=%llu (%llu%%) miss=%llu (%llu%%) puts=%llu total=%llu",
                    (unsigned long long)mem_hits_,  (unsigned long long)mem_pct,
                    (unsigned long long)disk_hits_, (unsigned long long)disk_pct,
                    (unsigned long long)misses_,    (unsigned long long)miss_pct,
                    (unsigned long long)puts_,
                    (unsigned long long)total);
    }
    // v1.8.20 Change 4: persist all valid entries to the bulk blob so the next
    // session can load them without re-extracting NROs.
    SaveToDisk(BULK_BLOB_PATH);
}

// ── EnsureDir ─────────────────────────────────────────────────────────────────

bool QdIconCache::EnsureDir() {
    // Try opening a read handle on the directory; if that fails, create it.
    // Horizon FsService create_directory returns 0 on success and
    // 0x402 (already-exists) on re-call — both are acceptable.
    // sdmc: is mounted by uMenu via fsdevMountSdmc() at boot (see uMenu main.cpp).
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc == nullptr) {
        UL_LOG_WARN("qdesktop: IconCache EnsureDir: fsdevGetDeviceFileSystem(sdmc) NULL");
        return false;
    }
    // Directory name MUST stay in lockstep with ICON_CACHE_DIR in qd_IconCache.hpp.
    // F5 (stabilize-5): RC-B4 — bumped to qos-icon-cache-v3.
    // v1.8.22g B66 — bumped to qos-icon-cache-v4 to invalidate v1.8.21/22b grays.
    Result rc = fsFsCreateDirectory(sdmc, "/switch/qos-icon-cache-v4");
    const bool ok = R_SUCCEEDED(rc) || (rc == 0x402);
    UL_LOG_INFO("qdesktop: IconCache EnsureDir rc=0x%X ok=%d", static_cast<unsigned>(rc), ok ? 1 : 0);
    if (!ok) {
        return false;
    }
    // Write generation.txt sentinel so future builds can detect stale caches.
    // Using raw fopen on the sdmc: devoptab path (the device is already mounted).
    const std::string gen_path = std::string(ICON_CACHE_DIR) + "generation.txt";
    FILE *gf = fopen(gen_path.c_str(), "w");
    if (gf) {
        fputs(ICON_CACHE_GENERATION, gf);
        fclose(gf);
    }
    return true;
}

// ── AdvanceTick ───────────────────────────────────────────────────────────────

void QdIconCache::AdvanceTick() {
    ++tick_counter_;
}

// ── PathHash (u64 DJB2, wrapping) ─────────────────────────────────────────────

u64 QdIconCache::PathHash(const char *path) {
    u64 h = 5381ULL;
    for (const u8 *p = reinterpret_cast<const u8 *>(path); *p != '\0'; ++p) {
        // Wrapping mul-33 then add byte — mirrors Rust h.wrapping_mul(33).wrapping_add(b).
        h = h * 33ULL + static_cast<u64>(*p);
    }
    return h;
}

// ── DiskPath ──────────────────────────────────────────────────────────────────

// Returns: "sdmc:/switch/qos-icon-cache-v3/<hash16hex>.rgba"
std::string QdIconCache::DiskPath(u64 hash) {
    static constexpr const char hex[] = "0123456789abcdef";
    // 16 hex digits + ".rgba" suffix
    char stem[17 + 5 + 1]; // 16 digits + ".rgba" + NUL
    for (int i = 15; i >= 0; --i) {
        stem[i] = hex[hash & 0xFu];
        hash >>= 4;
    }
    stem[16] = '.';
    stem[17] = 'r';
    stem[18] = 'g';
    stem[19] = 'b';
    stem[20] = 'a';
    stem[21] = '\0';

    std::string result;
    result.reserve(sizeof(ICON_CACHE_DIR) + 22);
    result += ICON_CACHE_DIR;
    result += stem;
    return result;
}

// ── LruSlot ───────────────────────────────────────────────────────────────────

size_t QdIconCache::LruSlot() const {
    size_t best_idx  = 0;
    u64    best_tick = UINT64_MAX;
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        // Invalid entries have tick == 0, so they are always evicted first.
        const u64 t = entries_[i].valid ? entries_[i].tick : 0ULL;
        if (t < best_tick) {
            best_tick = t;
            best_idx  = i;
        }
    }
    return best_idx;
}

// ── FindSlot ──────────────────────────────────────────────────────────────────
// v1.8.19: O(1) lookup via hash_index_ map (was O(N) linear scan over entries_).
// hash_index_ is maintained by Put() on insert/update and by LruSlot() on eviction.

size_t QdIconCache::FindSlot(u64 hash) const {
    const auto it = hash_index_.find(hash);
    if (it == hash_index_.end()) {
        return MEM_CACHE_CAP; // sentinel: not found
    }
    // Defensive: verify the slot is still valid and carries the expected hash.
    // This guard catches any residual stale-index edge case during eviction.
    const size_t idx = it->second;
    if (idx < MEM_CACHE_CAP && entries_[idx].valid && entries_[idx].path_hash == hash) {
        return idx;
    }
    return MEM_CACHE_CAP; // stale index entry — caller will fall through to disk
}

// ── ScaleToBgra64 ─────────────────────────────────────────────────────────────

// Nearest-neighbour resize src (src_w × src_h, RGBA) → dst (64×64 BGRA).
// dst must point to CACHE_ENTRY_BYTES (16384) bytes.
// Swaps R and B channels on write (RGBA → BGRA).
void QdIconCache::ScaleToBgra64(u8 *dst, const u8 *src, s32 src_w, s32 src_h) {
    const s32 dw = static_cast<s32>(CACHE_ICON_W);
    const s32 dh = static_cast<s32>(CACHE_ICON_H);

    for (s32 dy = 0; dy < dh; ++dy) {
        for (s32 dx = 0; dx < dw; ++dx) {
            // Nearest-neighbour: floor(dst_coord * src_dim / dst_dim).
            const s32 sx = (dx * src_w) / dw;
            const s32 sy = (dy * src_h) / dh;
            // Clamp to src bounds (defensive — src_w/src_h must be > 0).
            const s32 sx_c = (sx < src_w - 1) ? sx : src_w - 1;
            const s32 sy_c = (sy < src_h - 1) ? sy : src_h - 1;

            const size_t si = (static_cast<size_t>(sy_c) * static_cast<size_t>(src_w)
                               + static_cast<size_t>(sx_c)) * 4u;
            const size_t di = (static_cast<size_t>(dy) * static_cast<size_t>(dw)
                               + static_cast<size_t>(dx)) * 4u;

            // RGBA → BGRA channel swap.
            dst[di    ] = src[si + 2]; // B ← src.R
            dst[di + 1] = src[si + 1]; // G
            dst[di + 2] = src[si    ]; // R ← src.B
            dst[di + 3] = src[si + 3]; // A
        }
    }
}

// ── ReadFromDisk ──────────────────────────────────────────────────────────────

// Read 64×64 BGRA (16384 bytes) from the on-disk cache file.
// Returns true on success, false if the file is absent or has wrong size.
bool QdIconCache::ReadFromDisk(u64 hash, u8 *dst_bgra) {
    const std::string path = DiskPath(hash);
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    const size_t n = fread(dst_bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    return n == CACHE_ENTRY_BYTES;
}

// ── WriteToDisk ───────────────────────────────────────────────────────────────

// Write 64×64 BGRA (16384 bytes) to the on-disk cache file.
// Silently ignores write errors (per spec).
void QdIconCache::WriteToDisk(u64 hash, const u8 *bgra) {
    const std::string path = DiskPath(hash);
    // Write to a .tmp sibling first for atomicity.
    std::string tmp = path + ".tmp";

    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) {
        return; // SD not writable; silently ignore.
    }
    const size_t written = fwrite(bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    if (written != CACHE_ENTRY_BYTES) {
        // Partial write — clean up the temp file and abort.
        remove(tmp.c_str());
        return;
    }
    // Best-effort pseudo-atomic rename: remove old, rename .tmp → canonical.
    // FAT32 does not support atomic rename (EXDEV on cross-dir, no fsync guarantee).
    // A power loss between remove() and rename() leaves no cache file (safe: cache
    // miss on next boot re-extracts the icon from the NRO).  This is acceptable.
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

// ── LoadFromDisk / SaveToDisk (v1.8.20 Change 4) ──────────────────────────────
//
// Binary blob format (all fields little-endian, matching AArch64 native byte order):
//
//   struct CacheFileHeader {
//       u32 magic;        // 0x43415143 = 'QCAC' in LE memory bytes ['Q','C','A','C']
//       u32 version;      // 1
//       u32 entry_count;  // number of entries that follow
//       u32 reserved;     // 0 — reserved for future use
//   };
//   // Followed by entry_count copies of:
//   struct CacheEntryHeader {
//       u64 key_hash;     // DJB2 u64 path hash (same as entries_.path_hash)
//       u32 width;        // 64 (stored for future compatibility)
//       u32 height;       // 64
//       u32 bgra_size;    // CACHE_ENTRY_BYTES = 16384
//       u32 _pad;         // explicit padding to keep header 24 bytes, 8-byte aligned
//   };
//   // Immediately after each CacheEntryHeader: bgra_size bytes of BGRA pixel data.
//
// All kernel IPC via fsFsOpenFile / fsFileRead / fsFileWrite — no POSIX shim.
// The blob is written atomically by creating sdmc:/ulaunch/cache/icons.bgra.tmp,
// writing all data, then renaming to icons.bgra (best-effort on FAT32).
//
// 7-day staleness check: if fsdevGetMtime on the blob file returns a timestamp
// older than 7*24*3600 seconds relative to the current RTC time, the blob is
// rejected (returns false) so icons are re-extracted fresh.  On platforms where
// the RTC is unavailable (time() == (time_t)-1) the staleness check is skipped
// and the blob is always accepted.

namespace {
    // CacheFileHeader on-wire layout.
    struct CacheFileHeader {
        u32 magic;
        u32 version;
        u32 entry_count;
        u32 reserved;
    };
    static_assert(sizeof(CacheFileHeader) == 16, "CacheFileHeader must be 16 bytes");

    // CacheEntryHeader on-wire layout.
    struct CacheEntryHeader {
        u64 key_hash;
        u32 width;
        u32 height;
        u32 bgra_size;
        u32 pad;
    };
    static_assert(sizeof(CacheEntryHeader) == 24, "CacheEntryHeader must be 24 bytes");

    static constexpr u32 CACHE_BLOB_MAGIC   = 0x43415143u; // 'QCAC' LE
    // v1.8.22c Edit 3: bumped 1 → 2 to invalidate any persisted gray fallbacks
    // written under "app:<hex>" keys by pre-v1.8.22c builds (LoadNsIconToCache
    // and LoadJpegIconToCache no longer Put gray for app: keys).  Old blobs are
    // refused on LoadFromDisk; SaveToDisk overwrites them with v=2 contents.
    static constexpr u32 CACHE_BLOB_VERSION = 2u;
    static constexpr s64 STALE_SECONDS      = 7LL * 24LL * 3600LL; // 7 days

    // Strip "sdmc:" prefix from a path for kernel IPC calls.
    // Returns a pointer into path itself (not a copy).
    inline const char *BlobStripPrefix(const char *path) {
        if (path[0]=='s' && path[1]=='d' && path[2]=='m' && path[3]=='c' && path[4]==':') {
            return path + 5;
        }
        return path;
    }

    // Write exactly `len` bytes from `data` into `f` at absolute offset `*off`,
    // advancing *off by len on success.  Returns true on success.
    bool BlobWrite(FsFile *f, s64 *off, const void *data, u64 len) {
        Result rc = fsFileWrite(f, *off, data, len, FsWriteOption_None);
        if (R_FAILED(rc)) {
            return false;
        }
        *off += static_cast<s64>(len);
        return true;
    }

    // Read exactly `len` bytes from `f` at absolute offset `*off` into `dst`,
    // advancing *off by len on success.  Returns true on success.
    bool BlobRead(FsFile *f, s64 *off, void *dst, u64 len) {
        u64 bytes_read = 0;
        Result rc = fsFileRead(f, *off, dst, len, 0, &bytes_read);
        if (R_FAILED(rc) || bytes_read != len) {
            return false;
        }
        *off += static_cast<s64>(len);
        return true;
    }
} // anonymous namespace

bool QdIconCache::LoadFromDisk(const char *path) {
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc == nullptr) {
        UL_LOG_WARN("qdesktop: IconCache LoadFromDisk: sdmc not mounted");
        return false;
    }

    const char *fs_path = BlobStripPrefix(path);

    // ── 7-day staleness check ─────────────────────────────────────────────────
    // Use fsFsGetFileTimeStampRaw to retrieve the blob's modification time.
    // On failure (file absent, or FS does not support mtime), we continue:
    // if the file exists we load it; if absent we return false silently.
    {
        FsTimeStampRaw ts = {};
        Result ts_rc = fsFsGetFileTimeStampRaw(sdmc, fs_path, &ts);
        if (R_SUCCEEDED(ts_rc) && ts.is_valid) {
            // ts.modified is a POSIX timestamp (seconds since 1970-01-01T00:00:00Z).
            time_t now_posix_val = static_cast<time_t>(-1);
            ::time(&now_posix_val);
            const time_t now_posix = now_posix_val;
            if (now_posix != static_cast<time_t>(-1)) {
                const s64 age_seconds = static_cast<s64>(now_posix)
                                      - static_cast<s64>(ts.modified);
                if (age_seconds > STALE_SECONDS) {
                    UL_LOG_INFO("qdesktop: IconCache LoadFromDisk: blob is %lld s old (>7d), skipping",
                                (long long)age_seconds);
                    return false;
                }
            }
        }
    }

    // ── Open the blob file ────────────────────────────────────────────────────
    FsFile fsf;
    Result rc = fsFsOpenFile(sdmc, fs_path, FsOpenMode_Read, &fsf);
    if (R_FAILED(rc)) {
        // File absent on first boot — silent.
        return false;
    }

    // ── Read and validate the file header ─────────────────────────────────────
    s64 read_off = 0;
    CacheFileHeader hdr = {};
    if (!BlobRead(&fsf, &read_off, &hdr, sizeof(hdr))) {
        fsFileClose(&fsf);
        UL_LOG_WARN("qdesktop: IconCache LoadFromDisk: header read failed");
        return false;
    }
    if (hdr.magic != CACHE_BLOB_MAGIC || hdr.version != CACHE_BLOB_VERSION) {
        fsFileClose(&fsf);
        UL_LOG_WARN("qdesktop: IconCache LoadFromDisk: bad magic/version magic=0x%X ver=%u",
                    (unsigned)hdr.magic, (unsigned)hdr.version);
        return false;
    }
    if (hdr.entry_count > MEM_CACHE_CAP) {
        // More entries than our LRU supports — cap to MEM_CACHE_CAP.
        hdr.entry_count = static_cast<u32>(MEM_CACHE_CAP);
    }

    // ── Read entries ──────────────────────────────────────────────────────────
    u32 loaded = 0;
    for (u32 i = 0; i < hdr.entry_count; ++i) {
        CacheEntryHeader eh = {};
        if (!BlobRead(&fsf, &read_off, &eh, sizeof(eh))) {
            UL_LOG_WARN("qdesktop: IconCache LoadFromDisk: entry %u header read failed", i);
            break;
        }
        if (eh.bgra_size != CACHE_ENTRY_BYTES) {
            // Unexpected size — skip this entry's payload.
            read_off += static_cast<s64>(eh.bgra_size);
            continue;
        }
        // Find a free or LRU slot for this entry.
        size_t slot_idx = FindSlot(eh.key_hash);
        if (slot_idx >= MEM_CACHE_CAP) {
            slot_idx = LruSlot();
        }
        IconCacheEntry &slot = entries_[slot_idx];
        // Evict stale map key if necessary.
        if (slot.valid && slot.path_hash != eh.key_hash) {
            hash_index_.erase(slot.path_hash);
        }
        if (!BlobRead(&fsf, &read_off, slot.pixels, CACHE_ENTRY_BYTES)) {
            UL_LOG_WARN("qdesktop: IconCache LoadFromDisk: entry %u pixels read failed", i);
            break;
        }
        slot.path_hash = eh.key_hash;
        slot.tick      = 1; // non-zero: valid; lower than any frame-advanced tick
        slot.valid     = true;
        hash_index_[eh.key_hash] = slot_idx;
        ++loaded;
    }

    fsFileClose(&fsf);
    UL_LOG_INFO("qdesktop: IconCache LoadFromDisk: loaded %u/%u entries from %s",
                loaded, hdr.entry_count, path);
    return loaded > 0;
}

void QdIconCache::SaveToDisk(const char *path) {
    FsFileSystem *sdmc = fsdevGetDeviceFileSystem("sdmc");
    if (sdmc == nullptr) {
        return; // sdmc not mounted — nothing we can do.
    }

    // Count valid entries.
    u32 valid_count = 0;
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (entries_[i].valid) { ++valid_count; }
    }
    if (valid_count == 0) {
        return; // Nothing to save.
    }

    // Ensure the parent directory exists: sdmc:/ulaunch/cache
    // We attempt to create both directories in sequence; 0x402 = already-exists, fine.
    fsFsCreateDirectory(sdmc, "/ulaunch");
    fsFsCreateDirectory(sdmc, "/ulaunch/cache");

    // Write to .tmp first; rename on success (best-effort FAT32 atomicity).
    std::string tmp_str = std::string(path) + ".tmp";
    const char *fs_path_tmp = BlobStripPrefix(tmp_str.c_str());
    const char *fs_path     = BlobStripPrefix(path);

    // Remove any stale .tmp from a prior crash.
    fsFsDeleteFile(sdmc, fs_path_tmp);

    // Compute total file size for pre-allocation.
    const u64 total_size = sizeof(CacheFileHeader)
                         + static_cast<u64>(valid_count)
                           * (sizeof(CacheEntryHeader) + CACHE_ENTRY_BYTES);

    // Create the .tmp file.
    Result rc = fsFsCreateFile(sdmc, fs_path_tmp, static_cast<s64>(total_size), 0);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: IconCache SaveToDisk: fsFsCreateFile failed rc=0x%X", (unsigned)rc);
        return;
    }

    FsFile fsf;
    rc = fsFsOpenFile(sdmc, fs_path_tmp, FsOpenMode_Write | FsOpenMode_Append, &fsf);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: IconCache SaveToDisk: fsFsOpenFile(.tmp) failed rc=0x%X", (unsigned)rc);
        fsFsDeleteFile(sdmc, fs_path_tmp);
        return;
    }

    s64 write_off = 0;
    bool ok = true;

    // Write file header.
    CacheFileHeader hdr;
    hdr.magic       = CACHE_BLOB_MAGIC;
    hdr.version     = CACHE_BLOB_VERSION;
    hdr.entry_count = valid_count;
    hdr.reserved    = 0;
    ok = ok && BlobWrite(&fsf, &write_off, &hdr, sizeof(hdr));

    // Write each valid entry.
    for (size_t i = 0; i < MEM_CACHE_CAP && ok; ++i) {
        const IconCacheEntry &slot = entries_[i];
        if (!slot.valid) { continue; }

        CacheEntryHeader eh;
        eh.key_hash  = slot.path_hash;
        eh.width     = CACHE_ICON_W;
        eh.height    = CACHE_ICON_H;
        eh.bgra_size = static_cast<u32>(CACHE_ENTRY_BYTES);
        eh.pad       = 0;
        ok = ok && BlobWrite(&fsf, &write_off, &eh, sizeof(eh));
        ok = ok && BlobWrite(&fsf, &write_off, slot.pixels, CACHE_ENTRY_BYTES);
    }

    // Flush before close to ensure data is committed to the FAT32 chain.
    if (ok) {
        fsFileFlush(&fsf);
    }
    fsFileClose(&fsf);

    if (!ok) {
        UL_LOG_WARN("qdesktop: IconCache SaveToDisk: write error — deleting .tmp");
        fsFsDeleteFile(sdmc, fs_path_tmp);
        return;
    }

    // Rename .tmp → final.  Delete the old blob first (FAT32 has no atomic rename).
    fsFsDeleteFile(sdmc, fs_path);
    rc = fsFsRenameFile(sdmc, fs_path_tmp, fs_path);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: IconCache SaveToDisk: fsFsRenameFile failed rc=0x%X", (unsigned)rc);
        fsFsDeleteFile(sdmc, fs_path_tmp);
        return;
    }

    UL_LOG_INFO("qdesktop: IconCache SaveToDisk: saved %u entries to %s (%llu bytes)",
                valid_count, path, (unsigned long long)total_size);
}

// ── Get ───────────────────────────────────────────────────────────────────────

// Look up an icon by NRO path.
// 1. Memory LRU hit → update tick, return pixel pointer.
// 2. On-disk hit     → load into LRU, return pixel pointer.
// 3. No hit          → return nullptr.
const u8 *QdIconCache::Get(const char *nro_path) {
    // ── v1.8.22g B66 defense-in-depth: never serve cached BGRAs for romfs:/ ──
    // romfs:/ paths reference assets bundled in the uMenu romfs.bin and are
    // loaded via pu::ui::render::LoadImageFromFile (IMG_Load → libnx fsdev).
    // They MUST NOT touch this cache: (a) historical bug — v1.8.21/22b's
    // LoadJpegIconToCache::do_fallback Put gray BGRAs under romfs keys, and
    // those grays persisted on disk (qos-icon-cache-v3/<hash>.rgba) across
    // process boundaries, displacing the v1.8.22d 2a-romfs lazy load; (b) the
    // path is satisfied directly from romfs at zero cost (no IPC fan-out).
    // Pair with the symmetric guard in Put().
    if (nro_path != nullptr
            && nro_path[0] == 'r' && nro_path[1] == 'o'
            && nro_path[2] == 'm' && nro_path[3] == 'f'
            && nro_path[4] == 's' && nro_path[5] == ':') {
        return nullptr;
    }

    const u64 hash = PathHash(nro_path);

    // ── 1. Memory LRU hit ─────────────────────────────────────────────────
    const size_t mem_idx = FindSlot(hash);
    if (mem_idx < MEM_CACHE_CAP) {
        // Update LRU timestamp.
        entries_[mem_idx].tick = tick_counter_;
        // v2.0.3-A8: count memory hits.
        ++mem_hits_;
        MaybeLogHitRate();
        return entries_[mem_idx].pixels;
    }

    // ── 2. Memory miss — enqueue an async disk read (BUG-7 fix) ─────────────
    // Do NOT call ReadFromDisk() here: this is the render thread and
    // ReadFromDisk() is a blocking fopen/fread on the SD card (~5-20ms on
    // FAT32 / libnx, identical stall to the BUG-2 write-path).
    //
    // Instead, enqueue the hash for the prewarm/background thread to service
    // via DrainReadQueue().  The decoded pixels will appear in the LRU on the
    // next frame after DrainReadQueue() completes — same one-frame lag as the
    // BUG-2 write deferral, acceptable per spec.
    //
    // Dedup guard: in_flight_reads_ is a set of hashes already queued so
    // repeated render-thread misses for the same icon only produce one read.
    // The set is cleared (per-entry) when DrainReadQueue() swaps the queue out.
    if (in_flight_reads_.find(hash) == in_flight_reads_.end()) {
        PendingRead pr;
        pr.hash = hash;
        read_request_queue_.push_back(pr);
        in_flight_reads_.insert(hash);
    }

    // ── 3. Not cached yet ────────────────────────────────────────────────────
    // v2.0.3-A8: count misses.
    ++misses_;
    MaybeLogHitRate();
    return nullptr;
}

// ── Put ───────────────────────────────────────────────────────────────────────

// Store an icon from a raw RGBA buffer (any size).
// Resizes to 64×64 and swaps to BGRA, evicts LRU slot if full,
// writes the BGRA bytes to disk.
void QdIconCache::Put(const char *nro_path, const u8 *rgba_pixels,
                      s32 src_w, s32 src_h) {
    // ── v1.8.22g B66 defense-in-depth: never cache BGRAs for romfs:/ keys ──
    // Symmetric to the Get() guard above — see rationale there. Closes the
    // bug class: even if a future caller bypasses LoadJpegIconToCache's root
    // rejection, romfs paths cannot land in the LRU or on disk.
    if (nro_path != nullptr
            && nro_path[0] == 'r' && nro_path[1] == 'o'
            && nro_path[2] == 'm' && nro_path[3] == 'f'
            && nro_path[4] == 's' && nro_path[5] == ':') {
        return;
    }

    // v2.0.3-A8: count Put calls (write-side observability).
    ++puts_;

    const u64  hash = PathHash(nro_path);

    // Reuse existing slot if this path is already cached.
    size_t idx = FindSlot(hash);
    if (idx >= MEM_CACHE_CAP) {
        idx = LruSlot();
    }

    IconCacheEntry &slot = entries_[idx];

    // v1.8.19: if the LRU slot was occupied by a different hash, remove the
    // stale map entry so the evicted key no longer aliases this index.
    if (slot.valid && slot.path_hash != hash) {
        hash_index_.erase(slot.path_hash);
    }

    // W6-LEDGER: untrack the evicted slot (if any) before overwriting.
    UL_LEDGER_UNTRACK(ledger_handles_[idx]);
    ledger_handles_[idx] = 0;

    // Scale + channel-swap into the slot's pixel buffer.
    ScaleToBgra64(slot.pixels, rgba_pixels, src_w, src_h);
    slot.path_hash = hash;
    slot.tick      = tick_counter_;
    slot.valid     = true;

    // W6-LEDGER: track this newly populated slot.
    char ledger_tag[32];
    snprintf(ledger_tag, sizeof(ledger_tag), "%016llx", (unsigned long long)hash);
    ledger_handles_[idx] = UL_LEDGER_TRACK(
        QdResKind::IconCache, ledger_tag, CACHE_ENTRY_BYTES);

    // v1.8.19: keep the hash→index map in sync.
    hash_index_[hash] = idx;

    // BUG-2 fix: do NOT call WriteToDisk() here under the caller's lock.
    // SD I/O (fopen/fwrite/rename, ~5-20ms on FAT32) inside Put() while
    // GetSharedIconCacheMutex() is held blocked the render thread for up to
    // 3.9 seconds during prewarm.  Instead, copy the scaled BGRA pixels into
    // the deferred write-queue; the prewarm thread drains the queue via
    // DrainWriteQueue() after releasing the shared mutex.
    // The pixel copy is 16 KB — cheap compared to the 5-20ms SD write it defers.
    {
        PendingWrite pw;
        pw.hash = hash;
        memcpy(pw.pixels, slot.pixels, CACHE_ENTRY_BYTES);
        write_queue_.push_back(std::move(pw));
    }
}

// ── DrainWriteQueue (BUG-2 fix) ──────────────────────────────────────────────
//
// Drains the deferred write-queue populated by Put() and persists each entry
// to its per-icon SD file via WriteToDisk().
//
// Threading contract:
//   - MUST be called WITHOUT holding GetSharedIconCacheMutex().
//   - Performs a brief re-acquisition of the mutex only to swap the queue out;
//     the actual SD I/O (fopen/fwrite/rename, ~5-20ms per entry) runs entirely
//     outside the lock so the render thread is never blocked.
//   - Only the prewarm thread calls this (one consumer) — no concurrent drain race.
//   - Double-write is impossible: each Put() appends exactly one entry; Drain
//     swaps the entire vector out atomically, so a second Drain call on an empty
//     queue is a no-op.
//
// Call pattern in the prewarm thread (LoadNroIconToCache / LoadNsIconToCache /
// LoadJpegIconToCache all follow the same pattern):
//
//   {
//       std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
//       GetSharedIconCache().Put(cache_key, pixels, w, h);
//   }                               // ← mutex released here
//   GetSharedIconCache().DrainWriteQueue();   // ← SD I/O happens here, off-lock
void QdIconCache::DrainWriteQueue() {
    // Swap the current queue out under the lock (O(1) — just a pointer/size swap).
    std::vector<PendingWrite> local_queue;
    {
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        if (write_queue_.empty()) {
            return;
        }
        local_queue.swap(write_queue_);
    }

    // Ensure the on-disk directory exists before the first write.
    // EnsureDir() is idempotent; it stat-creates the directory via fsFsCreateDirectory
    // and is fast on subsequent calls (0x402 already-exists → immediate return).
    // Call it here rather than inside WriteToDisk() so the logic stays in one place.
    // (EnsureDir() itself does NOT hold the shared mutex.)
    EnsureDir();

    // Write each entry to SD entirely outside the shared mutex.
    for (const PendingWrite &pw : local_queue) {
        WriteToDisk(pw.hash, pw.pixels);
    }
}

// ── DrainReadQueue (BUG-7 fix) ────────────────────────────────────────────────
//
// Services the deferred read-request queue populated by Get() on memory miss.
// Performs ReadFromDisk() for each pending hash ENTIRELY off the shared mutex
// (the blocking I/O never touches the render thread), then re-acquires the
// mutex to insert the decoded pixels into the LRU.
//
// Threading contract (mirrors DrainWriteQueue / BUG-2):
//   - MUST be called WITHOUT holding GetSharedIconCacheMutex().
//   - Swaps the request queue out under a brief lock (O(1) pointer swap).
//     Also clears in_flight_reads_ for the swapped entries so subsequent
//     render-thread misses for the same hash will re-enqueue after this drain
//     completes (they won't: the LRU will hold the entry; but if eviction
//     later displaces it the queue is open for a new request).
//   - ReadFromDisk() (fopen/fread ~5-20ms on FAT32) runs off-lock.
//   - A second brief lock re-acquisition inserts the filled slot into LRU.
//     Slot insertion uses LruSlot() to pick the eviction target, same as
//     Get()'s old synchronous path — no change in eviction semantics.
//   - If ReadFromDisk() returns false (file absent / wrong size) the request
//     is silently dropped: the render thread will continue to get nullptr
//     from Get() and show the placeholder fallback.  No in_flight re-insert
//     is done so the render thread will re-enqueue on the next miss, giving
//     a single retry per frame.  This is intentional: a missing individual
//     .rgba file is a non-fatal cache miss, not a persistent error.
//   - Only the prewarm thread calls this (one consumer); no concurrent drain race.
//   - Safe to call alongside DrainWriteQueue() in the same prewarm loop iteration.
void QdIconCache::DrainReadQueue() {
    // ── Step 1: swap the request queue out under a brief lock ────────────────
    std::vector<PendingRead> local_queue;
    {
        std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());
        if (read_request_queue_.empty()) {
            return;
        }
        local_queue.swap(read_request_queue_);
        // Clear the in-flight dedup set for these entries.  New misses for
        // the same hashes can re-enqueue after we release the lock — that is
        // correct: either this drain populates the LRU (so the next Get()
        // returns from memory) or ReadFromDisk fails (so a retry is desired).
        for (const PendingRead &pr : local_queue) {
            in_flight_reads_.erase(pr.hash);
        }
    }

    // ── Step 2: read each entry from disk — entirely off the lock ────────────
    // We use a small stack buffer so no heap allocation is needed per entry.
    // CACHE_ENTRY_BYTES = 16384.
    for (const PendingRead &pr : local_queue) {
        // Temporary pixel buffer — lives on the stack for the duration of the
        // fread, then is copied into the LRU slot under the lock below.
        // 16 KB is fine on the prewarm thread's stack (Switch default stack is 1MB).
        u8 tmp_pixels[CACHE_ENTRY_BYTES];

        const bool ok = ReadFromDisk(pr.hash, tmp_pixels);
        if (!ok) {
            // File absent or corrupt — drop this request silently.
            // The render thread will get nullptr on the next frame and, if it
            // still needs the icon, re-enqueue a fresh PendingRead.
            UL_LOG_INFO("qdesktop: BUG-7 DrainReadQueue: disk miss for hash=%016llx (absent or corrupt)",
                        (unsigned long long)pr.hash);
            continue;
        }

        // ── Step 3: insert decoded pixels into LRU under the lock ────────────
        {
            std::lock_guard<std::mutex> lock(GetSharedIconCacheMutex());

            // Check if another path (prewarm Put) already populated this hash
            // while we were doing the disk read off-lock.  If so, just update
            // the tick and skip the redundant copy.
            const size_t existing = FindSlot(pr.hash);
            if (existing < MEM_CACHE_CAP) {
                entries_[existing].tick = tick_counter_;
                UL_LOG_INFO("qdesktop: BUG-7 DrainReadQueue: hash=%016llx already in LRU after disk read — tick updated",
                            (unsigned long long)pr.hash);
                // v2.0.3-A8: count as disk hit (we did do the I/O).
                ++disk_hits_;
                continue;
            }

            // Evict the LRU slot and populate it.
            const size_t lru_idx = LruSlot();
            IconCacheEntry &slot  = entries_[lru_idx];

            // Evict stale map entry for the displaced slot.
            if (slot.valid && slot.path_hash != pr.hash) {
                hash_index_.erase(slot.path_hash);
            }

            memcpy(slot.pixels, tmp_pixels, CACHE_ENTRY_BYTES);
            slot.path_hash = pr.hash;
            slot.tick      = tick_counter_;
            slot.valid     = true;
            hash_index_[pr.hash] = lru_idx;

            // v2.0.3-A8: count disk hits.
            ++disk_hits_;
            UL_LOG_INFO("qdesktop: BUG-7 DrainReadQueue: loaded hash=%016llx into slot=%zu",
                        (unsigned long long)pr.hash, lru_idx);
        }
    }
}

// ── Process-wide shared singleton (v1.8.18 / A1-OPT-1) ──────────────────────
// Desktop and Launchpad both call GetSharedIconCache() / GetSharedIconCacheMutex()
// so they always operate on the same QdIconCache object and the same std::mutex.
//
// A1-OPT-1: converted from a static-storage object (which ran QdIconCache()
// before main() and did a ~393 KB SD blob read on the boot critical path) to a
// lazy heap singleton created on first GetSharedIconCache() call — deferred until
// after sdmc: is mounted and libnx services are ready.  SaveToDisk() at exit is
// guaranteed by atexit() registered once at first construction.  The mutex is
// still a plain static so it is always ready (it has a trivial constructor).

static std::mutex g_shared_icon_cache_mutex;

// Raw pointer — intentionally never freed (process is about to exit; the OS
// reclaims all memory).  Using a raw pointer avoids static-destruction-order
// issues that could fire after sdmc: or logging services are torn down.
static QdIconCache *g_shared_icon_cache_ptr = nullptr;

// Atexit handler: flush the icon cache blob to SD at process exit (RF-3).
// Registered once in GetSharedIconCache() on first creation.
// std::atexit fires while sdmc: is still mounted and logging is still live.
static void SharedIconCacheAtExit() {
    if (g_shared_icon_cache_ptr) {
        g_shared_icon_cache_ptr->SaveToDisk(BULK_BLOB_PATH);
    }
}

QdIconCache& GetSharedIconCache() {
    if (g_shared_icon_cache_ptr == nullptr) {
        g_shared_icon_cache_ptr = new QdIconCache();
        // Register SaveToDisk at process exit (RF-3: icon persistence).
        // std::atexit fires before static destructors, after main() returns,
        // while sdmc: is still mounted and logging is still live.
        std::atexit(SharedIconCacheAtExit);
    }
    return *g_shared_icon_cache_ptr;
}

std::mutex& GetSharedIconCacheMutex() {
    return g_shared_icon_cache_mutex;
}

// ── Negative-extract cache (v1.8.19) ─────────────────────────────────────────
// Per-session set of NRO paths for which ExtractNroIcon() returned valid==false.
// LoadNroIconToCache() checks this set at entry; if the path is present it returns
// false immediately, skipping the disk I/O and ASET parse entirely.  The set is
// never cleared across prewarm passes — if a file wasn't extractable on the first
// attempt it won't be on subsequent attempts (the file doesn't change while uMenu
// is running).  This eliminates the redundant re-read cost across the background
// prewarm thread's repeated passes.
static std::unordered_set<std::string> g_failed_extract_paths;

std::unordered_set<std::string>& GetFailedExtractPaths() {
    return g_failed_extract_paths;
}

} // namespace ul::menu::qdesktop
