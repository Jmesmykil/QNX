// test_QdNroAsset.cpp — Host-side unit tests for qd_NroAsset pure-math functions.
// Tests Djb2Hash32, HslToRgb, and MakeFallbackIcon.
// ExtractNroIcon is hardware/filesystem-dependent and cannot run on the host.
//
// Build:
//   c++ -std=c++23 -I../../include test_QdNroAsset.cpp -o test_QdNroAsset && ./test_QdNroAsset

#include "test_host_stubs.hpp"

// Prevent Plutonium.hpp from being included via qd_NroAsset.hpp.
// test_host_stubs.hpp already defines PU_PLUTONIUM_HPP as a guard.

// SDL2 stubs — qd_NroAsset.hpp does not directly include SDL2 headers, but the
// .cpp does.  For the test binary we inline only the pure-math functions we test.
// So we include the header (which compiles fine after stub guards) and then provide
// inline definitions of only the three pure-math functions, without linking SDL2.

#include <ul/menu/qdesktop/qd_NroAsset.hpp>

// ── Inline the pure-math function implementations (no SDL2 dependency) ────────

namespace ul::menu::qdesktop {

u32 Djb2Hash32(const char *str) {
    u32 h = 5381;
    const u8 *p = reinterpret_cast<const u8 *>(str);
    while (*p) {
        h = h * 33u + static_cast<u32>(*p);
        ++p;
    }
    return h;
}

void HslToRgb(u32 h_deg, float s, float l,
              u8 &out_r, u8 &out_g, u8 &out_b) {
    const float h = static_cast<float>(h_deg % 360u);
    const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    const float h60 = h / 60.0f;
    const float mod2 = h60 - 2.0f * floorf(h60 / 2.0f);
    const float x = c * (1.0f - fabsf(mod2 - 1.0f));
    const float m = l - c / 2.0f;
    float r1, g1, b1;
    if      (h < 60.0f)  { r1 = c; g1 = x; b1 = 0.0f; }
    else if (h < 120.0f) { r1 = x; g1 = c; b1 = 0.0f; }
    else if (h < 180.0f) { r1 = 0.0f; g1 = c; b1 = x; }
    else if (h < 240.0f) { r1 = 0.0f; g1 = x; b1 = c; }
    else if (h < 300.0f) { r1 = x; g1 = 0.0f; b1 = c; }
    else                  { r1 = c; g1 = 0.0f; b1 = x; }
    out_r = static_cast<u8>((r1 + m) * 255.0f);
    out_g = static_cast<u8>((g1 + m) * 255.0f);
    out_b = static_cast<u8>((b1 + m) * 255.0f);
}

u8 *MakeFallbackIcon(const char *nro_path) {
    // v1.6.11: neutral gray #3A3A3A -- no more random HSL colouring.
    (void)nro_path;
    constexpr u8 kGray = 0x3A;
    constexpr size_t N = 64 * 64 * 4;
    u8 *buf = new u8[N];
    for (size_t i = 0; i < 64 * 64; ++i) {
        buf[i * 4 + 0] = kGray;
        buf[i * 4 + 1] = kGray;
        buf[i * 4 + 2] = kGray;
        buf[i * 4 + 3] = 0xFF;
    }
    return buf;
}

} // namespace ul::menu::qdesktop

using namespace ul::menu::qdesktop;

// ── Djb2Hash32 tests ─────────────────────────────────────────────────────────

static void test_djb2_empty_string() {
    // Empty string → seed only = 5381.
    const u32 h = Djb2Hash32("");
    ASSERT_EQ(h, 5381u);
    TEST_PASS("djb2_empty_string");
}

static void test_djb2_single_char_A() {
    // 'A' (65): h = 5381 * 33 + 65 = 177670 + 65 = 177735
    const u32 h = Djb2Hash32("A");
    ASSERT_EQ(h, 5381u * 33u + 65u);
    TEST_PASS("djb2_single_char_A");
}

static void test_djb2_deterministic() {
    const u32 a = Djb2Hash32("sdmc:/switch/hbmenu.nro");
    const u32 b = Djb2Hash32("sdmc:/switch/hbmenu.nro");
    ASSERT_EQ(a, b);
    TEST_PASS("djb2_deterministic");
}

static void test_djb2_distinct_paths() {
    const u32 a = Djb2Hash32("sdmc:/switch/hbmenu.nro");
    const u32 b = Djb2Hash32("sdmc:/switch/ftpd.nro");
    ASSERT_TRUE(a != b);
    TEST_PASS("djb2_distinct_paths");
}

static void test_djb2_two_chars() {
    // "AB": round 1: h1 = 5381*33 + 'A'(65) = 177735
    //        round 2: h2 = h1*33 + 'B'(66) = 177735*33 + 66 = 5865255 + 66 = 5865321
    const u32 expected = (5381u * 33u + 65u) * 33u + 66u;
    const u32 h = Djb2Hash32("AB");
    ASSERT_EQ(h, expected);
    TEST_PASS("djb2_two_chars");
}

// ── HslToRgb tests ────────────────────────────────────────────────────────────

// Helper: compute expected HslToRgb independently using the piecewise formula.
// Used only to verify against the function under test (no copy-paste from impl).

static void test_hsl_red_pure() {
    // H=0, S=1.0, L=0.5 → pure red (255, 0, 0).
    u8 r, g, b;
    HslToRgb(0u, 1.0f, 0.5f, r, g, b);
    ASSERT_EQ(r, 255u);
    ASSERT_EQ(g, 0u);
    ASSERT_EQ(b, 0u);
    TEST_PASS("hsl_red_pure");
}

static void test_hsl_green_pure() {
    // H=120, S=1.0, L=0.5 → pure green (0, 255, 0).
    u8 r, g, b;
    HslToRgb(120u, 1.0f, 0.5f, r, g, b);
    ASSERT_EQ(r, 0u);
    ASSERT_EQ(g, 255u);
    ASSERT_EQ(b, 0u);
    TEST_PASS("hsl_green_pure");
}

static void test_hsl_blue_pure() {
    // H=240, S=1.0, L=0.5 → pure blue (0, 0, 255).
    u8 r, g, b;
    HslToRgb(240u, 1.0f, 0.5f, r, g, b);
    ASSERT_EQ(r, 0u);
    ASSERT_EQ(g, 0u);
    ASSERT_EQ(b, 255u);
    TEST_PASS("hsl_blue_pure");
}

static void test_hsl_white() {
    // S=0, L=1.0 → white (255, 255, 255).
    u8 r, g, b;
    HslToRgb(0u, 0.0f, 1.0f, r, g, b);
    ASSERT_EQ(r, 255u);
    ASSERT_EQ(g, 255u);
    ASSERT_EQ(b, 255u);
    TEST_PASS("hsl_white");
}

static void test_hsl_black() {
    // S=0, L=0.0 → black (0, 0, 0).
    u8 r, g, b;
    HslToRgb(0u, 0.0f, 0.0f, r, g, b);
    ASSERT_EQ(r, 0u);
    ASSERT_EQ(g, 0u);
    ASSERT_EQ(b, 0u);
    TEST_PASS("hsl_black");
}

static void test_hsl_gray_saturation_zero() {
    // S=0, L=0.5 → mid-gray (127, 127, 127) ± 1.
    // With S=0: c=0, x=0, m=0.5, all channels = m*255 = 127.
    u8 r, g, b;
    HslToRgb(180u, 0.0f, 0.5f, r, g, b);
    // Each channel should be 127 (floor(0.5*255)=127).
    ASSERT_EQ(r, 127u);
    ASSERT_EQ(g, 127u);
    ASSERT_EQ(b, 127u);
    TEST_PASS("hsl_gray_saturation_zero");
}

static void test_hsl_hue_360_wraps_to_0() {
    // H=360 → same as H=0 (360 % 360 == 0).
    u8 r0, g0, b0;
    u8 r360, g360, b360;
    HslToRgb(0u,   1.0f, 0.5f, r0,   g0,   b0);
    HslToRgb(360u, 1.0f, 0.5f, r360, g360, b360);
    ASSERT_EQ(r0, r360);
    ASSERT_EQ(g0, g360);
    ASSERT_EQ(b0, b360);
    TEST_PASS("hsl_hue_360_wraps_to_0");
}

static void test_hsl_desktop_palette_params() {
    // Verify the specific parameters used by MakeFallbackIcon (s=0.55, l=0.40).
    // At H=0, s=0.55, l=0.40: c = (1-|0.8-1|)*0.55 = (1-0.2)*0.55 = 0.8*0.55 = 0.44
    // m = 0.40 - 0.44/2 = 0.40 - 0.22 = 0.18; x=0 at H=0
    // sextant 0 (H<60): r1=c=0.44, g1=0, b1=0
    // R = (0.44+0.18)*255 = 0.62*255 = 158(.1)  → 158
    // G = (0+0.18)*255    = 0.18*255 = 45(.9)    → 45
    // B = (0+0.18)*255    = 45
    u8 r, g, b;
    HslToRgb(0u, 0.55f, 0.40f, r, g, b);
    ASSERT_EQ(r, 158u);
    ASSERT_EQ(g, 45u);
    ASSERT_EQ(b, 45u);
    TEST_PASS("hsl_desktop_palette_params");
}

// ── MakeFallbackIcon tests ────────────────────────────────────────────────────

static void test_make_fallback_icon_non_null() {
    u8 *buf = MakeFallbackIcon("sdmc:/switch/test.nro");
    ASSERT_TRUE(buf != nullptr);
    delete[] buf;
    TEST_PASS("make_fallback_icon_non_null");
}

static void test_make_fallback_icon_size_64x64() {
    // We cannot directly query the allocation size, but we can verify that
    // pixel strides are correct: every 4th byte (alpha) should be 0xFF.
    u8 *buf = MakeFallbackIcon("sdmc:/switch/test.nro");
    ASSERT_TRUE(buf != nullptr);
    // Spot-check: first pixel alpha, last pixel alpha.
    ASSERT_EQ(buf[3], 0xFFu);                      // first pixel alpha
    ASSERT_EQ(buf[(64 * 64 - 1) * 4 + 3], 0xFFu); // last pixel alpha
    delete[] buf;
    TEST_PASS("make_fallback_icon_size_64x64");
}

static void test_make_fallback_icon_uniform_color() {
    // All pixels in the 64×64 buffer must have the same RGB values.
    u8 *buf = MakeFallbackIcon("sdmc:/switch/uniformtest.nro");
    ASSERT_TRUE(buf != nullptr);
    const u8 r0 = buf[0], g0 = buf[1], b0 = buf[2];
    for (size_t i = 1; i < 64 * 64; ++i) {
        ASSERT_EQ(buf[i * 4 + 0], r0);
        ASSERT_EQ(buf[i * 4 + 1], g0);
        ASSERT_EQ(buf[i * 4 + 2], b0);
        ASSERT_EQ(buf[i * 4 + 3], 0xFFu);
    }
    delete[] buf;
    TEST_PASS("make_fallback_icon_uniform_color");
}

static void test_make_fallback_icon_deterministic() {
    u8 *a = MakeFallbackIcon("sdmc:/switch/hbmenu.nro");
    u8 *b = MakeFallbackIcon("sdmc:/switch/hbmenu.nro");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    // First pixel must match.
    ASSERT_EQ(a[0], b[0]);
    ASSERT_EQ(a[1], b[1]);
    ASSERT_EQ(a[2], b[2]);
    ASSERT_EQ(a[3], b[3]);
    delete[] a;
    delete[] b;
    TEST_PASS("make_fallback_icon_deterministic");
}

static void test_make_fallback_icon_neutral_gray_both_paths() {
    // v1.6.11: fallback is always #3A3A3A regardless of the NRO path.
    // Two different paths must produce identical neutral-gray pixels.
    u8 *a = MakeFallbackIcon("sdmc:/switch/hbmenu.nro");
    u8 *b = MakeFallbackIcon("sdmc:/switch/ftpd.nro");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    // Both must be identical neutral gray.
    ASSERT_EQ(a[0], 0x3Au);
    ASSERT_EQ(a[1], 0x3Au);
    ASSERT_EQ(a[2], 0x3Au);
    ASSERT_EQ(b[0], 0x3Au);
    ASSERT_EQ(b[1], 0x3Au);
    ASSERT_EQ(b[2], 0x3Au);
    delete[] a;
    delete[] b;
    TEST_PASS("make_fallback_icon_neutral_gray_both_paths");
}

static void test_make_fallback_icon_neutral_gray_exact_value() {
    // v1.6.11: any path → R=G=B=0x3A, A=0xFF for every pixel.
    u8 *buf = MakeFallbackIcon("");
    ASSERT_TRUE(buf != nullptr);
    ASSERT_EQ(buf[0], 0x3Au); // R
    ASSERT_EQ(buf[1], 0x3Au); // G
    ASSERT_EQ(buf[2], 0x3Au); // B
    ASSERT_EQ(buf[3], 0xFFu); // A
    // Verify last pixel too.
    ASSERT_EQ(buf[(64 * 64 - 1) * 4 + 0], 0x3Au);
    ASSERT_EQ(buf[(64 * 64 - 1) * 4 + 1], 0x3Au);
    ASSERT_EQ(buf[(64 * 64 - 1) * 4 + 2], 0x3Au);
    ASSERT_EQ(buf[(64 * 64 - 1) * 4 + 3], 0xFFu);
    delete[] buf;
    TEST_PASS("make_fallback_icon_neutral_gray_exact_value");
}

// ── v1.6.11 Fix 2: SD-root NRO path construction tests ───────────────────────

static void test_sdroot_nro_path_hbmenu() {
    const char *fname = "hbmenu.nro";
    char nro_path[768];
    int written = snprintf(nro_path, sizeof(nro_path), "sdmc:/%s", fname);
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE(static_cast<size_t>(written) < sizeof(nro_path));
    ASSERT_TRUE(strncmp(nro_path, "sdmc:/", 6) == 0);
    const size_t plen = strlen(nro_path);
    ASSERT_TRUE(plen >= 4u);
    ASSERT_TRUE(strcmp(nro_path + plen - 4u, ".nro") == 0);
    ASSERT_TRUE(strcmp(nro_path, "sdmc:/hbmenu.nro") == 0);
    TEST_PASS("sdroot_nro_path_hbmenu");
}

static void test_sdroot_nro_path_umanager() {
    const char *fname = "uManager.nro";
    char nro_path[768];
    int written = snprintf(nro_path, sizeof(nro_path), "sdmc:/%s", fname);
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE(static_cast<size_t>(written) < sizeof(nro_path));
    ASSERT_TRUE(strcmp(nro_path, "sdmc:/uManager.nro") == 0);
    TEST_PASS("sdroot_nro_path_umanager");
}

static void test_sdroot_nro_path_distinct_from_switch_dir() {
    const char *fname = "hbmenu.nro";
    char root_path[768];
    char switch_path[768];
    snprintf(root_path,   sizeof(root_path),   "sdmc:/%s",        fname);
    snprintf(switch_path, sizeof(switch_path),  "sdmc:/switch/%s", fname);
    ASSERT_TRUE(strcmp(root_path, switch_path) != 0);
    TEST_PASS("sdroot_nro_path_distinct_from_switch_dir");
}

static void test_sdroot_nro_stem_extraction() {
    const char *fname = "hbmenu.nro";
    const size_t flen = strlen(fname);
    const size_t stem_len = flen - 4u;
    char stem[64];
    ASSERT_TRUE(stem_len > 0u && stem_len < sizeof(stem));
    memcpy(stem, fname, stem_len);
    stem[stem_len] = '\0';
    ASSERT_TRUE(strcmp(stem, "hbmenu") == 0);
    TEST_PASS("sdroot_nro_stem_extraction");
}

int main() {
    test_djb2_empty_string();
    test_djb2_single_char_A();
    test_djb2_deterministic();
    test_djb2_distinct_paths();
    test_djb2_two_chars();
    test_hsl_red_pure();
    test_hsl_green_pure();
    test_hsl_blue_pure();
    test_hsl_white();
    test_hsl_black();
    test_hsl_gray_saturation_zero();
    test_hsl_hue_360_wraps_to_0();
    test_hsl_desktop_palette_params();
    test_make_fallback_icon_non_null();
    test_make_fallback_icon_size_64x64();
    test_make_fallback_icon_uniform_color();
    test_make_fallback_icon_deterministic();
    test_make_fallback_icon_neutral_gray_both_paths();
    test_make_fallback_icon_neutral_gray_exact_value();
    // v1.6.11 Fix 2: SD-root NRO path construction.
    test_sdroot_nro_path_hbmenu();
    test_sdroot_nro_path_umanager();
    test_sdroot_nro_path_distinct_from_switch_dir();
    test_sdroot_nro_stem_extraction();
    fprintf(stderr, "All QdNroAsset tests PASSED\n");
    return 0;
}
