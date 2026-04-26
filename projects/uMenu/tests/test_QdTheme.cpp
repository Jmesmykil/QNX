// test_QdTheme.cpp — Host-side unit tests for QdTheme.
// Tests: DarkLiquidGlass token values.
// Build: c++ -std=c++23 -I../../include test_QdTheme.cpp -o test_QdTheme && ./test_QdTheme

// Stub Switch types BEFORE including any ul headers.
#include "test_host_stubs.hpp"

// QdTheme is entirely inline in the header — no linkage dependency on qd_Theme.cpp.
#include <ul/menu/qdesktop/qd_Theme.hpp>

using namespace ul::menu::qdesktop;

static void test_dark_liquid_glass_token_values() {
    const QdTheme t = QdTheme::DarkLiquidGlass();

    // desktop_bg = (0x0A, 0x0A, 0x14)
    ASSERT_EQ(t.desktop_bg.r, 0x0Au);
    ASSERT_EQ(t.desktop_bg.g, 0x0Au);
    ASSERT_EQ(t.desktop_bg.b, 0x14u);
    ASSERT_EQ(t.desktop_bg.a, 0xFFu);

    // accent = (0x7D, 0xD3, 0xFC)
    ASSERT_EQ(t.accent.r, 0x7Du);
    ASSERT_EQ(t.accent.g, 0xD3u);
    ASSERT_EQ(t.accent.b, 0xFCu);
    ASSERT_EQ(t.accent.a, 0xFFu);

    // focus_ring = (0x7C, 0xC5, 0xFF)
    ASSERT_EQ(t.focus_ring.r, 0x7Cu);
    ASSERT_EQ(t.focus_ring.g, 0xC5u);
    ASSERT_EQ(t.focus_ring.b, 0xFFu);

    // button_maximize = button_restore = (0x4A, 0xDE, 0x80)
    ASSERT_EQ(t.button_maximize.r, 0x4Au);
    ASSERT_EQ(t.button_maximize.g, 0xDEu);
    ASSERT_EQ(t.button_maximize.b, 0x80u);
    ASSERT_EQ(t.button_restore.r,  0x4Au);
    ASSERT_EQ(t.button_restore.g,  0xDEu);
    ASSERT_EQ(t.button_restore.b,  0x80u);

    // button_close = (0xF8, 0x71, 0x71)
    ASSERT_EQ(t.button_close.r, 0xF8u);
    ASSERT_EQ(t.button_close.g, 0x71u);
    ASSERT_EQ(t.button_close.b, 0x71u);

    // button_minimize = (0xFB, 0xBF, 0x24)
    ASSERT_EQ(t.button_minimize.r, 0xFBu);
    ASSERT_EQ(t.button_minimize.g, 0xBFu);
    ASSERT_EQ(t.button_minimize.b, 0x24u);

    // grid_line = (0x18, 0x18, 0x32)
    ASSERT_EQ(t.grid_line.r, 0x18u);
    ASSERT_EQ(t.grid_line.g, 0x18u);
    ASSERT_EQ(t.grid_line.b, 0x32u);

    TEST_PASS("dark_liquid_glass_token_values");
}

static void test_dark_liquid_glass_is_deterministic() {
    // Two independent factory calls must produce byte-identical theme structs.
    const QdTheme a = QdTheme::DarkLiquidGlass();
    const QdTheme b = QdTheme::DarkLiquidGlass();

    ASSERT_EQ(a.desktop_bg.r,         b.desktop_bg.r);
    ASSERT_EQ(a.desktop_bg.g,         b.desktop_bg.g);
    ASSERT_EQ(a.desktop_bg.b,         b.desktop_bg.b);
    ASSERT_EQ(a.accent.r,             b.accent.r);
    ASSERT_EQ(a.accent.g,             b.accent.g);
    ASSERT_EQ(a.accent.b,             b.accent.b);
    ASSERT_EQ(a.focus_ring.r,         b.focus_ring.r);
    ASSERT_EQ(a.button_close.r,       b.button_close.r);
    ASSERT_EQ(a.button_minimize.r,    b.button_minimize.r);
    ASSERT_EQ(a.button_maximize.r,    b.button_maximize.r);
    ASSERT_EQ(a.cursor_fill.r,        b.cursor_fill.r);
    ASSERT_EQ(a.cursor_outline.r,     b.cursor_outline.r);
    ASSERT_EQ(a.cursor_right_click.r, b.cursor_right_click.r);
    ASSERT_EQ(a.titlebar_inactive.r,  b.titlebar_inactive.r);
    ASSERT_EQ(a.button_restore.r,     b.button_restore.r);
    ASSERT_EQ(a.grid_line.r,          b.grid_line.r);
    ASSERT_EQ(a.text_primary.r,       b.text_primary.r);
    ASSERT_EQ(a.text_secondary.r,     b.text_secondary.r);

    TEST_PASS("dark_liquid_glass_is_deterministic");
}

int main() {
    test_dark_liquid_glass_token_values();
    test_dark_liquid_glass_is_deterministic();
    fprintf(stderr, "All QdTheme tests PASSED\n");
    return 0;
}
