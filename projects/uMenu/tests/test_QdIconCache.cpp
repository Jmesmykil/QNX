// test_QdIconCache.cpp — Host-side unit tests for QdIconCache pure-logic functions.
// Tests PathHash (DJB2 u64), ScaleToBgra64 (nearest-neighbour + channel swap),
// LruSlot, FindSlot — no SD, no fopen.
//
// Build:
//   c++ -std=c++23 -I. -I../../include test_QdIconCache.cpp -o test_QdIconCache && ./test_QdIconCache

#include "test_host_stubs.hpp"

#include <ul/menu/qdesktop/qd_IconCache.hpp>
#include <cstdio>
#include <cstring>

// ── Inline QdIconCache implementation (without Switch-SDK EnsureDir) ─────────
// All methods except EnsureDir use only standard C POSIX I/O which works on host.

namespace ul::menu::qdesktop {

QdIconCache::QdIconCache() : tick_counter_(0) {
    for (auto &e : entries_) {
        e.path_hash = 0;
        e.tick      = 0;
        e.valid     = false;
    }
}
QdIconCache::~QdIconCache() {}

bool QdIconCache::EnsureDir() {
    // No-op on host — directory creation is Switch-SDK-specific.
    return true;
}

void QdIconCache::AdvanceTick() { ++tick_counter_; }

u64 QdIconCache::PathHash(const char *path) {
    u64 h = 5381u;
    for (const u8 *p = reinterpret_cast<const u8 *>(path); *p != '\0'; ++p) {
        h = h * 33u + static_cast<u64>(*p);
    }
    return h;
}

std::string QdIconCache::DiskPath(u64 hash) {
    static constexpr const char hex[] = "0123456789abcdef";
    char stem[17 + 5 + 1];
    for (int i = 15; i >= 0; --i) { stem[i] = hex[hash & 0xFu]; hash >>= 4; }
    stem[16]='.'; stem[17]='r'; stem[18]='g'; stem[19]='b'; stem[20]='a'; stem[21]='\0';
    std::string result;
    result.reserve(sizeof(ICON_CACHE_DIR) + 22);
    result += ICON_CACHE_DIR;
    result += stem;
    return result;
}

size_t QdIconCache::LruSlot() const {
    size_t best_idx = 0;
    u64 best_tick = UINT64_MAX;
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        const u64 t = entries_[i].valid ? entries_[i].tick : 0u;
        if (t < best_tick) { best_tick = t; best_idx = i; }
    }
    return best_idx;
}

size_t QdIconCache::FindSlot(u64 hash) const {
    for (size_t i = 0; i < MEM_CACHE_CAP; ++i) {
        if (entries_[i].valid && entries_[i].path_hash == hash) return i;
    }
    return MEM_CACHE_CAP;
}

void QdIconCache::ScaleToBgra64(u8 *dst, const u8 *src, s32 src_w, s32 src_h) {
    const s32 dw = static_cast<s32>(CACHE_ICON_W);
    const s32 dh = static_cast<s32>(CACHE_ICON_H);
    for (s32 dy = 0; dy < dh; ++dy) {
        for (s32 dx = 0; dx < dw; ++dx) {
            const s32 sx = (dx * src_w) / dw;
            const s32 sy = (dy * src_h) / dh;
            const s32 sx_c = (sx < src_w - 1) ? sx : src_w - 1;
            const s32 sy_c = (sy < src_h - 1) ? sy : src_h - 1;
            const size_t si = (static_cast<size_t>(sy_c) * static_cast<size_t>(src_w)
                               + static_cast<size_t>(sx_c)) * 4u;
            const size_t di = (static_cast<size_t>(dy) * static_cast<size_t>(dw)
                               + static_cast<size_t>(dx)) * 4u;
            dst[di    ] = src[si + 2]; // B ← src.R
            dst[di + 1] = src[si + 1]; // G
            dst[di + 2] = src[si    ]; // R ← src.B
            dst[di + 3] = src[si + 3]; // A
        }
    }
}

bool QdIconCache::ReadFromDisk(u64 hash, u8 *dst_bgra) {
    const std::string path = DiskPath(hash);
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t n = fread(dst_bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    return n == CACHE_ENTRY_BYTES;
}

void QdIconCache::WriteToDisk(u64 hash, const u8 *bgra) {
    const std::string path = DiskPath(hash);
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    const size_t written = fwrite(bgra, 1u, CACHE_ENTRY_BYTES, f);
    fclose(f);
    if (written != CACHE_ENTRY_BYTES) { remove(tmp.c_str()); return; }
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

const u8 *QdIconCache::Get(const char *nro_path) {
    const u64 hash = PathHash(nro_path);
    const size_t mem_idx = FindSlot(hash);
    if (mem_idx < MEM_CACHE_CAP) {
        entries_[mem_idx].tick = tick_counter_;
        return entries_[mem_idx].pixels;
    }
    const size_t lru_idx = LruSlot();
    IconCacheEntry &slot = entries_[lru_idx];
    if (ReadFromDisk(hash, slot.pixels)) {
        slot.path_hash = hash;
        slot.tick      = tick_counter_;
        slot.valid     = true;
        return slot.pixels;
    }
    return nullptr;
}

void QdIconCache::Put(const char *nro_path, const u8 *rgba_pixels,
                      s32 src_w, s32 src_h) {
    const u64 hash = PathHash(nro_path);
    size_t idx = FindSlot(hash);
    if (idx >= MEM_CACHE_CAP) idx = LruSlot();
    IconCacheEntry &slot = entries_[idx];
    ScaleToBgra64(slot.pixels, rgba_pixels, src_w, src_h);
    slot.path_hash = hash;
    slot.tick      = tick_counter_;
    slot.valid     = true;
    WriteToDisk(hash, slot.pixels);
}

} // namespace ul::menu::qdesktop

// ── PathHash ─────────────────────────────────────────────────────────────────

// PathHash is private; expose via a thin test shim class.
struct IconCacheTestShim : public ul::menu::qdesktop::QdIconCache {
    static u64 TestPathHash(const char *path) {
        return PathHash(path);
    }
    static void TestScaleToBgra64(u8 *dst, const u8 *src, s32 sw, s32 sh) {
        ScaleToBgra64(dst, src, sw, sh);
    }
    size_t TestFindSlot(u64 hash) const { return FindSlot(hash); }
    size_t TestLruSlot() const { return LruSlot(); }
};

using namespace ul::menu::qdesktop;

// ── PathHash tests ────────────────────────────────────────────────────────────

static void test_path_hash_empty_string() {
    // Empty string — hash should be the seed 5381.
    const u64 h = IconCacheTestShim::TestPathHash("");
    ASSERT_EQ(h, 5381u);
    TEST_PASS("path_hash_empty_string");
}

static void test_path_hash_deterministic() {
    // Same input → same output.
    const u64 a = IconCacheTestShim::TestPathHash("sdmc:/switch/hbmenu.nro");
    const u64 b = IconCacheTestShim::TestPathHash("sdmc:/switch/hbmenu.nro");
    ASSERT_EQ(a, b);
    TEST_PASS("path_hash_deterministic");
}

static void test_path_hash_distinct_paths() {
    const u64 a = IconCacheTestShim::TestPathHash("sdmc:/switch/hbmenu.nro");
    const u64 b = IconCacheTestShim::TestPathHash("sdmc:/switch/ftpd.nro");
    ASSERT_TRUE(a != b);
    TEST_PASS("path_hash_distinct_paths");
}

static void test_path_hash_known_single_char() {
    // DJB2: h = 5381; h = h*33 + 'A' = 177670 + 65 = 177735 = 0x2B657
    const u64 h = IconCacheTestShim::TestPathHash("A");
    ASSERT_EQ(h, 5381u * 33u + 65u); // 177735
    TEST_PASS("path_hash_known_single_char");
}

// ── ScaleToBgra64 tests ───────────────────────────────────────────────────────

static void test_scale_to_bgra64_channel_swap() {
    // 1×1 RGBA source: R=0x11 G=0x22 B=0x33 A=0x44
    // Expected BGRA output (all 64×64 pixels): B=0x33 G=0x22 R=0x11 A=0x44
    const u8 src[4] = { 0x11u, 0x22u, 0x33u, 0x44u };
    static u8 dst[64 * 64 * 4];
    IconCacheTestShim::TestScaleToBgra64(dst, src, 1, 1);
    // Check first pixel.
    ASSERT_EQ(dst[0], 0x33u); // B ← src.R? No: B ← src[si+2] = src.B = 0x33
    ASSERT_EQ(dst[1], 0x22u); // G ← src[si+1] = 0x22
    ASSERT_EQ(dst[2], 0x11u); // R ← src[si+0] = 0x11
    ASSERT_EQ(dst[3], 0x44u); // A
    // Check last pixel (all should be the same for a 1×1 source).
    const size_t last = (64u * 64u - 1u) * 4u;
    ASSERT_EQ(dst[last + 0], 0x33u);
    ASSERT_EQ(dst[last + 1], 0x22u);
    ASSERT_EQ(dst[last + 2], 0x11u);
    ASSERT_EQ(dst[last + 3], 0x44u);
    TEST_PASS("scale_to_bgra64_channel_swap");
}

static void test_scale_to_bgra64_2x2_nearest_neighbour() {
    // 2×2 source: TL=red, TR=green, BL=blue, BR=white (RGBA)
    const u8 src[4 * 4] = {
        0xFFu, 0x00u, 0x00u, 0xFFu,  // TL: red
        0x00u, 0xFFu, 0x00u, 0xFFu,  // TR: green
        0x00u, 0x00u, 0xFFu, 0xFFu,  // BL: blue
        0xFFu, 0xFFu, 0xFFu, 0xFFu,  // BR: white
    };
    static u8 dst[64 * 64 * 4];
    IconCacheTestShim::TestScaleToBgra64(dst, src, 2, 2);

    // Top-left 32×32 of output → nearest neighbour maps to TL pixel (red).
    // BGRA of red = B=0x00, G=0x00, R=0xFF, A=0xFF.
    ASSERT_EQ(dst[0], 0x00u); // B
    ASSERT_EQ(dst[1], 0x00u); // G
    ASSERT_EQ(dst[2], 0xFFu); // R
    ASSERT_EQ(dst[3], 0xFFu); // A

    // Top-right corner (pixel at 63, 0) → TR (green): BGRA B=0x00, G=0xFF, R=0x00.
    const size_t tr = (0u * 64u + 63u) * 4u;
    ASSERT_EQ(dst[tr + 0], 0x00u); // B
    ASSERT_EQ(dst[tr + 1], 0xFFu); // G
    ASSERT_EQ(dst[tr + 2], 0x00u); // R

    TEST_PASS("scale_to_bgra64_2x2_nearest_neighbour");
}

// ── FindSlot / LruSlot tests ─────────────────────────────────────────────────

static void test_find_slot_empty_cache() {
    // Fresh cache: all slots invalid, FindSlot returns MEM_CACHE_CAP.
    IconCacheTestShim cache;
    ASSERT_EQ(cache.TestFindSlot(0xDEADBEEFu), MEM_CACHE_CAP);
    TEST_PASS("find_slot_empty_cache");
}

static void test_lru_slot_empty_cache() {
    // All invalid (tick=0): any slot is fine; implementation returns slot 0 first.
    IconCacheTestShim cache;
    const size_t idx = cache.TestLruSlot();
    ASSERT_TRUE(idx < MEM_CACHE_CAP);
    TEST_PASS("lru_slot_empty_cache");
}

// ── Integration: Put then Get (in-memory path only) ──────────────────────────

static void test_put_get_in_memory() {
    // Put a 1×1 RGBA pixel, then Get should return non-null (memory hit).
    // We stub file I/O by providing a path that cannot exist on the host;
    // ReadFromDisk will return false (fopen fails), WriteToDisk silently ignores.
    // The memory LRU path is fully exercised.
    QdIconCache cache;

    const u8 pixel[4] = { 0xAAu, 0xBBu, 0xCCu, 0xDDu };
    // Using a path that will not exist on the host filesystem.
    const char *path = "/nonexistent/sdmc/switch/qtest.nro";

    cache.Put(path, pixel, 1, 1);

    const u8 *got = cache.Get(path);
    ASSERT_TRUE(got != nullptr);

    // The stored data is BGRA (channel-swapped): B=0xCC G=0xBB R=0xAA A=0xDD.
    ASSERT_EQ(got[0], 0xCCu); // B
    ASSERT_EQ(got[1], 0xBBu); // G
    ASSERT_EQ(got[2], 0xAAu); // R
    ASSERT_EQ(got[3], 0xDDu); // A

    TEST_PASS("put_get_in_memory");
}

int main() {
    test_path_hash_empty_string();
    test_path_hash_deterministic();
    test_path_hash_distinct_paths();
    test_path_hash_known_single_char();
    test_scale_to_bgra64_channel_swap();
    test_scale_to_bgra64_2x2_nearest_neighbour();
    test_find_slot_empty_cache();
    test_lru_slot_empty_cache();
    test_put_get_in_memory();
    fprintf(stderr, "All QdIconCache tests PASSED\n");
    return 0;
}
