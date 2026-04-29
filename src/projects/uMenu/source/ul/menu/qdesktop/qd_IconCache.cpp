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
    // v1.8.19: pre-reserve the hash→index map to avoid rehashes across MEM_CACHE_CAP inserts.
    hash_index_.reserve(MEM_CACHE_CAP);
    // v1.8.20 Change 4: attempt to warm the LRU from the previous session's blob.
    // LoadFromDisk returns false quietly on first boot (file absent) or stale blob.
    LoadFromDisk(BULK_BLOB_PATH);
}

QdIconCache::~QdIconCache() {
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
        return entries_[mem_idx].pixels;
    }

    // ── 2. On-disk hit ────────────────────────────────────────────────────
    // Evict the LRU slot and try to fill it from disk.
    const size_t lru_idx = LruSlot();
    IconCacheEntry &slot = entries_[lru_idx];

    // Attempt to read 16384 BGRA bytes from disk.
    if (ReadFromDisk(hash, slot.pixels)) {
        // v1.8.19: evict stale map entry before overwriting slot.
        if (slot.valid && slot.path_hash != hash) {
            hash_index_.erase(slot.path_hash);
        }
        slot.path_hash = hash;
        slot.tick      = tick_counter_;
        slot.valid     = true;
        // v1.8.19: keep the hash→index map in sync for this disk-loaded entry.
        hash_index_[hash] = lru_idx;
        return slot.pixels;
    }

    // ── 3. Not cached ─────────────────────────────────────────────────────
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

    // Scale + channel-swap into the slot's pixel buffer.
    ScaleToBgra64(slot.pixels, rgba_pixels, src_w, src_h);
    slot.path_hash = hash;
    slot.tick      = tick_counter_;
    slot.valid     = true;

    // v1.8.19: keep the hash→index map in sync.
    hash_index_[hash] = idx;

    // Persist the BGRA bytes to SD.
    WriteToDisk(hash, slot.pixels);
}

// ── Process-wide shared singleton (v1.8.18) ───────────────────────────────────
// Desktop and Launchpad both call GetSharedIconCache() / GetSharedIconCacheMutex()
// so they always operate on the same QdIconCache object and the same std::mutex.
// Lifetime: static storage duration — constructed before main() via the normal
// C++ static initialisation order and destroyed after main() returns.

static QdIconCache  g_shared_icon_cache;
static std::mutex   g_shared_icon_cache_mutex;

QdIconCache& GetSharedIconCache() {
    return g_shared_icon_cache;
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
