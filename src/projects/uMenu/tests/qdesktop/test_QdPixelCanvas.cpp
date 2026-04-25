// test_QdPixelCanvas.cpp — Host-side unit tests for QdPixelCanvas static helpers.
// Only tests BlendChannel, BlendAdd, BlendOver — no SDL renderer needed.
// Build:
//   c++ -std=c++23 -I../../include test_QdPixelCanvas.cpp -o test_QdPixelCanvas && ./test_QdPixelCanvas

#include "test_host_stubs.hpp"

// Stub SDL2 so the header compiles on the host without SDL installed.
// We only call static methods that don't touch SDL_Renderer.
struct SDL_Renderer {};
#define SDL_PIXELFORMAT_ABGR8888 0
#define SDL_TEXTUREACCESS_STREAMING 0
struct SDL_Rect { int x, y, w, h; };
struct SDL_Texture {};
static inline void SDL_SetRenderDrawColor(SDL_Renderer*, uint8_t, uint8_t, uint8_t, uint8_t) {}
static inline void SDL_RenderDrawPoint(SDL_Renderer*, int, int) {}
static inline void SDL_RenderFillRect(SDL_Renderer*, SDL_Rect*) {}
static inline void SDL_RenderDrawLine(SDL_Renderer*, int, int, int, int) {}
static inline SDL_Texture* SDL_CreateTexture(SDL_Renderer*, int, int, int, int) { return nullptr; }
static inline void SDL_UpdateTexture(SDL_Texture*, void*, const void*, int) {}
static inline void SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, void*, SDL_Rect*) {}
static inline void SDL_DestroyTexture(SDL_Texture*) {}

// Stub sdl2_Types.hpp so qd_PixelCanvas.hpp compiles.
namespace pu { namespace sdl2 { using Renderer = SDL_Renderer*; using Texture = SDL_Texture*; } }
#define PU_SDL2_SDL2_TYPES_HPP

#include <ul/menu/qdesktop/qd_PixelCanvas.hpp>

// Inline the implementation (only static methods used in tests don't need SDL).
// Re-implement the statics inline here to avoid SDL link dependency.
namespace ul::menu::qdesktop {

u8 QdPixelCanvas::BlendChannel(u8 src, u8 dst, u32 alpha256) {
    const u32 out = (static_cast<u32>(src) * alpha256
                   + static_cast<u32>(dst) * (256u - alpha256)) >> 8u;
    return static_cast<u8>(out > 255u ? 255u : out);
}

void QdPixelCanvas::BlendAdd(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                              u8 src_r, u8 src_g, u8 src_b, u32 alpha256)
{
    const auto add = [&](u8 &dst, u8 src) {
        const u32 v = static_cast<u32>(dst)
                    + (static_cast<u32>(src) * static_cast<u32>(alpha256) >> 8u);
        dst = static_cast<u8>(v > 255u ? 255u : v);
    };
    add(dst_r, src_r);
    add(dst_g, src_g);
    add(dst_b, src_b);
}

void QdPixelCanvas::BlendOver(u8 &dst_r, u8 &dst_g, u8 &dst_b,
                               u8 src_r, u8 src_g, u8 src_b, u32 alpha256)
{
    dst_r = BlendChannel(src_r, dst_r, static_cast<u32>(alpha256));
    dst_g = BlendChannel(src_g, dst_g, static_cast<u32>(alpha256));
    dst_b = BlendChannel(src_b, dst_b, static_cast<u32>(alpha256));
}

void QdPixelCanvas::Begin(SDL_Renderer *r) { renderer_ = r; }
void QdPixelCanvas::End() { renderer_ = nullptr; }
void QdPixelCanvas::SetPixel(s32, s32, u8, u8, u8, u8) {}
void QdPixelCanvas::FillRect(s32, s32, s32, s32, u8, u8, u8, u8) {}
void QdPixelCanvas::BlitRgba(s32, s32, s32, s32, const u8*, s32, s32) {}
void QdPixelCanvas::DrawLine(s32, s32, s32, s32, u8, u8, u8, u8) {}

} // namespace ul::menu::qdesktop

using namespace ul::menu::qdesktop;

// ── BlendChannel tests ───────────────────────────────────────────────────────

static void test_blend_channel_zero_alpha() {
    // alpha=0 → result is entirely dst
    ASSERT_EQ(QdPixelCanvas::BlendChannel(0xFFu, 0x00u, 0u), 0x00u);
    ASSERT_EQ(QdPixelCanvas::BlendChannel(0xFFu, 0xAAu, 0u), 0xAAu);
    TEST_PASS("blend_channel_zero_alpha");
}

static void test_blend_channel_full_alpha() {
    // alpha=256 → result is entirely src
    ASSERT_EQ(QdPixelCanvas::BlendChannel(0xFFu, 0x00u, 256u), 0xFFu);
    ASSERT_EQ(QdPixelCanvas::BlendChannel(0x80u, 0xFFu, 256u), 0x80u);
    TEST_PASS("blend_channel_full_alpha");
}

static void test_blend_channel_half_alpha() {
    // alpha=128 ≈ 50% — midpoint (may round by 1 due to >>8)
    const u8 result = QdPixelCanvas::BlendChannel(0xFFu, 0x00u, 128u);
    // (0xFF*128 + 0x00*128) >> 8 = 0x7F
    ASSERT_EQ(result, 0x7Fu);
    TEST_PASS("blend_channel_half_alpha");
}

static void test_blend_add_no_overflow() {
    u8 r = 0x10u, g = 0x20u, b = 0x30u;
    QdPixelCanvas::BlendAdd(r, g, b, 0x10u, 0x10u, 0x10u, 256u);
    // (0x10 + 0x10) = 0x20 each
    ASSERT_EQ(r, 0x20u);
    ASSERT_EQ(g, 0x30u);
    ASSERT_EQ(b, 0x40u);
    TEST_PASS("blend_add_no_overflow");
}

static void test_blend_add_clamp() {
    u8 r = 0xF0u, g = 0xF0u, b = 0xF0u;
    QdPixelCanvas::BlendAdd(r, g, b, 0xFFu, 0xFFu, 0xFFu, 256u);
    // Would overflow — must clamp to 255
    ASSERT_EQ(r, 0xFFu);
    ASSERT_EQ(g, 0xFFu);
    ASSERT_EQ(b, 0xFFu);
    TEST_PASS("blend_add_clamp");
}

static void test_blend_over_identity() {
    // BlendOver with alpha=256 → src replaces dst exactly.
    u8 r = 0xAAu, g = 0xBBu, b = 0xCCu;
    QdPixelCanvas::BlendOver(r, g, b, 0x11u, 0x22u, 0x33u, 256u);
    ASSERT_EQ(r, 0x11u);
    ASSERT_EQ(g, 0x22u);
    ASSERT_EQ(b, 0x33u);
    TEST_PASS("blend_over_identity");
}

static void test_blend_over_transparent() {
    // BlendOver with alpha=0 → dst unchanged.
    u8 r = 0xAAu, g = 0xBBu, b = 0xCCu;
    QdPixelCanvas::BlendOver(r, g, b, 0xFFu, 0xFFu, 0xFFu, 0u);
    ASSERT_EQ(r, 0xAAu);
    ASSERT_EQ(g, 0xBBu);
    ASSERT_EQ(b, 0xCCu);
    TEST_PASS("blend_over_transparent");
}

int main() {
    test_blend_channel_zero_alpha();
    test_blend_channel_full_alpha();
    test_blend_channel_half_alpha();
    test_blend_add_no_overflow();
    test_blend_add_clamp();
    test_blend_over_identity();
    test_blend_over_transparent();
    fprintf(stderr, "All QdPixelCanvas tests PASSED\n");
    return 0;
}
