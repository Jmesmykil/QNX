// qd_IconCache.cpp — 24-entry LRU icon cache for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/icon_cache.rs.
//
// On-disk format: 64×64 BGRA raw bytes (CACHE_ENTRY_BYTES = 16384) per entry.
// Filename: "<hash16hex>.rgba" in ICON_CACHE_DIR.
// Memory LRU: 24 entries, evict by minimum tick (oldest).
// Disk: persisted across power cycles; read on memory miss; written on Put().
// No mtime check in C++ SP1 — full mtime support deferred to SP2.

#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdIconCache::QdIconCache() : tick_counter_(0) {
    // Zero-initialise all slots.
    for (auto &e : entries_) {
        e.path_hash = 0;
        e.tick      = 0;
        e.valid     = false;
        // Pixel buffer is inside the struct — no heap allocation for the slots.
    }
}

QdIconCache::~QdIconCache() {
    // All resources are value-typed inside the entries array — nothing to free.
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
    Result rc = fsFsCreateDirectory(sdmc, "/switch/qos-icon-cache-v3");
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

size_t QdIconCache::FindSlot(u64 hash) const {
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (entries_[i].valid && entries_[i].path_hash == hash) {
            return i;
        }
    }
    return MEM_CACHE_CAP; // sentinel: not found
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

// ── Get ───────────────────────────────────────────────────────────────────────

// Look up an icon by NRO path.
// 1. Memory LRU hit → update tick, return pixel pointer.
// 2. On-disk hit     → load into LRU, return pixel pointer.
// 3. No hit          → return nullptr.
const u8 *QdIconCache::Get(const char *nro_path) {
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
        slot.path_hash = hash;
        slot.tick      = tick_counter_;
        slot.valid     = true;
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
    const u64  hash = PathHash(nro_path);

    // Reuse existing slot if this path is already cached.
    size_t idx = FindSlot(hash);
    if (idx >= MEM_CACHE_CAP) {
        idx = LruSlot();
    }

    IconCacheEntry &slot = entries_[idx];

    // Scale + channel-swap into the slot's pixel buffer.
    ScaleToBgra64(slot.pixels, rgba_pixels, src_w, src_h);
    slot.path_hash = hash;
    slot.tick      = tick_counter_;
    slot.valid     = true;

    // Persist the BGRA bytes to SD.
    WriteToDisk(hash, slot.pixels);
}

} // namespace ul::menu::qdesktop
