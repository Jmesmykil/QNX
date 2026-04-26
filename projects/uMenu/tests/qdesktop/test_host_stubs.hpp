// test_host_stubs.hpp — Minimal stubs for host-side (macOS x86_64) unit testing of
// qdesktop algorithms that use only Switch primitive types and no HW APIs.
//
// Included by every qdesktop test TU before any ul/menu/qdesktop header.
// Provides: u8/u16/u32/u64/s32/s64/Result/R_SUCCEEDED, and null-ops for Plutonium
// types that appear in qd_Theme.hpp.

#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>

// ── libnx primitive types ─────────────────────────────────────────────────────
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;
using s64 = int64_t;

using Result = u32;
static inline bool R_SUCCEEDED(Result rc) { return rc == 0u; }

// ── Stub pu::ui::Color ────────────────────────────────────────────────────────
// qd_Theme.hpp uses pu::ui::Color only as a struct member with value semantics.
// We need just enough to compile.
namespace pu {
namespace ui {
    struct Color {
        u8 r, g, b, a;
        Color() : r(0), g(0), b(0), a(0) {}
        Color(u8 rr, u8 gg, u8 bb, u8 aa) : r(rr), g(gg), b(bb), a(aa) {}
    };
} // ui
} // pu

// Make <pu/Plutonium.hpp> a no-op guard so qd_Theme.hpp doesn't try to pull in the real one.
#define PU_PLUTONIUM_HPP

// ── Minimal assertion macro ───────────────────────────────────────────────────
// Tests use ASSERT_EQ and ASSERT_TRUE to avoid external frameworks.
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL [%s:%d]: ASSERT_EQ(%s, %s) -> got %lld vs %lld\n", \
                __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        ::abort(); \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL [%s:%d]: ASSERT_TRUE(%s)\n", __FILE__, __LINE__, #expr); \
        ::abort(); \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define TEST_PASS(name) fprintf(stderr, "PASS  %s\n", name)
