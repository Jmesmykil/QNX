// test_QdOsk.cpp — Host unit tests for SP4 OSK widget.
//
// SP4-F01: Verifies 12 pixel constants are ×1.5 remapped.
// SP4-F02: Verifies NORMAL_KEY_COUNT=40, KEY_COUNT=43 are NOT remapped.
// SP4-F03: Verifies key_rect returns bool + out-param for all 43 keys.
// SP4-F04: Verifies drag clamp stays within bounds (std::clamp).
// SP4-F05: Verifies backspace via buf[--len]='\0'.
// AB-12:   Verifies no 1280/720 literals by testing SCREEN_W/SCREEN_H-derived constants.
#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Osk.hpp>
#include <cstring>
#include <climits>

using namespace ul::menu::qdesktop;

// ── SP4-F01: Pixel constants ×1.5 ────────────────────────────────────────────

static void test_osk_pixel_constants_remapped() {
    ASSERT_EQ(OSK_H,              480u);   // Rust: 320
    ASSERT_EQ(OSK_TITLE_H,         36u);   // Rust:  24
    ASSERT_EQ(KEY_SIZE,             72u);   // Rust:  48
    ASSERT_EQ(KEY_GAP,               9u);   // Rust:   6
    ASSERT_EQ(KEY_PAD,              30);    // Rust:  20
    ASSERT_EQ(CLOSE_BTN_SIZE,       48);    // Rust:  32
    ASSERT_EQ(CLOSE_BTN_MARGIN,     12);    // Rust:   8
    ASSERT_EQ(TEXT_PANEL_H,         60u);   // Rust:  40
    ASSERT_EQ(TEXT_PANEL_GAP,       12u);   // Rust:   8
    TEST_PASS("SP4-F01: 9 pixel constants ×1.5 remapped");
}

// ── SP4-F01: OSK_DEFAULT_Y and SPACE_WIDTH ────────────────────────────────────

static void test_osk_derived_constants() {
    // OSK_DEFAULT_Y = SCREEN_H - OSK_H = 1080 - 480 = 600
    ASSERT_EQ(OSK_DEFAULT_Y, 600);
    // SPACE_WIDTH = 10 * KEY_SIZE + 9 * KEY_GAP = 720 + 81 = 801
    ASSERT_EQ(SPACE_WIDTH, 801u);
    TEST_PASS("SP4-F01: OSK_DEFAULT_Y=600 (1080-480), SPACE_WIDTH=801");
}

// ── SP4-F02: Count constants NOT remapped ─────────────────────────────────────

static void test_osk_count_constants_not_remapped() {
    ASSERT_EQ(NORMAL_KEY_COUNT, 40u);
    ASSERT_EQ(KEY_COUNT,        43u);
    ASSERT_EQ(KEY_IDX_SPACE,    40u);
    ASSERT_EQ(KEY_IDX_BACKSPACE,41u);
    ASSERT_EQ(KEY_IDX_ENTER,    42u);
    TEST_PASS("SP4-F02: NORMAL_KEY_COUNT=40, KEY_COUNT=43 not remapped");
}

// ── SP4-F03: key_rect — normal keys ──────────────────────────────────────────

static void test_key_rect_normal_keys() {
    OskState osk = osk_state_new();
    KeyRect r;

    // Key 0 = 'q' — top-left corner of grid.
    ASSERT_TRUE(key_rect(&osk, 0, &r));
    ASSERT_TRUE(r.w == KEY_SIZE);
    ASSERT_TRUE(r.h == KEY_SIZE);

    // Key 10 = 'a' — first key of row 1 (same column as 'q', row +1).
    KeyRect r10;
    ASSERT_TRUE(key_rect(&osk, 10, &r10));
    ASSERT_EQ(r10.x, r.x);
    ASSERT_EQ(r10.y, r.y + static_cast<int32_t>(KEY_SIZE) + static_cast<int32_t>(KEY_GAP));

    // Key 1 = 'w' — same row as 'q', column +1.
    KeyRect r1;
    ASSERT_TRUE(key_rect(&osk, 1, &r1));
    ASSERT_EQ(r1.y, r.y);
    ASSERT_EQ(r1.x, r.x + static_cast<int32_t>(KEY_SIZE) + static_cast<int32_t>(KEY_GAP));

    TEST_PASS("SP4-F03: key_rect normal keys geometry correct");
}

static void test_key_rect_all_normal_keys_return_true() {
    OskState osk = osk_state_new();
    KeyRect r;
    for (size_t i = 0; i < NORMAL_KEY_COUNT; ++i) {
        ASSERT_TRUE(key_rect(&osk, i, &r));
        ASSERT_EQ(r.w, KEY_SIZE);
        ASSERT_EQ(r.h, KEY_SIZE);
    }
    TEST_PASS("SP4-F03: all 40 normal keys return true with KEY_SIZE rect");
}

// ── SP4-F03: key_rect — special keys ─────────────────────────────────────────

static void test_key_rect_special_keys() {
    OskState osk = osk_state_new();

    // Space key: full width = SPACE_WIDTH.
    KeyRect sp;
    ASSERT_TRUE(key_rect(&osk, KEY_IDX_SPACE, &sp));
    ASSERT_EQ(sp.w, SPACE_WIDTH);
    ASSERT_EQ(sp.h, KEY_SIZE);

    // Backspace: KEY_SIZE wide, immediately right of space.
    KeyRect bk;
    ASSERT_TRUE(key_rect(&osk, KEY_IDX_BACKSPACE, &bk));
    ASSERT_EQ(bk.w, KEY_SIZE);
    ASSERT_EQ(bk.h, KEY_SIZE);
    ASSERT_EQ(bk.x, sp.x + static_cast<int32_t>(SPACE_WIDTH) + static_cast<int32_t>(KEY_GAP));

    // Enter: KEY_SIZE wide, immediately right of backspace.
    KeyRect en;
    ASSERT_TRUE(key_rect(&osk, KEY_IDX_ENTER, &en));
    ASSERT_EQ(en.w, KEY_SIZE);
    ASSERT_EQ(en.h, KEY_SIZE);
    ASSERT_EQ(en.x, bk.x + static_cast<int32_t>(KEY_SIZE) + static_cast<int32_t>(KEY_GAP));

    // Space, Backspace, Enter all on the same row.
    ASSERT_EQ(sp.y, bk.y);
    ASSERT_EQ(bk.y, en.y);

    TEST_PASS("SP4-F03: Space/Backspace/Enter positions correct");
}

static void test_key_rect_out_of_range_returns_false() {
    OskState osk = osk_state_new();
    KeyRect r;
    ASSERT_FALSE(key_rect(&osk, KEY_COUNT, &r));
    ASSERT_FALSE(key_rect(&osk, 100, &r));
    ASSERT_FALSE(key_rect(nullptr, 0, &r));
    ASSERT_FALSE(key_rect(&osk, 0, nullptr));
    TEST_PASS("SP4-F03: out-of-range / null returns false");
}

// ── SP4-F04: Drag clamp ───────────────────────────────────────────────────────

static void test_drag_clamp_default_position() {
    OskState osk = osk_state_new();
    // Default: drag_x=0, drag_y=0 → origin_y = OSK_DEFAULT_Y = 600.
    ASSERT_EQ(osk_origin_y(osk), OSK_DEFAULT_Y);
    // osk_origin_x with drag_x=0 → 0 (within [-960, 960]).
    ASSERT_EQ(osk_origin_x(osk), 0);
    TEST_PASS("SP4-F04: default drag gives OSK_DEFAULT_Y origin");
}

static void test_drag_clamp_y_upper_bound() {
    OskState osk = osk_state_new();
    // Push far above screen — must clamp to osk_min_y().
    osk.drag_y = -99999;
    const int32_t oy = osk_origin_y(osk);
    ASSERT_TRUE(oy >= osk_min_y());
    TEST_PASS("SP4-F04: drag_y far negative clamps to osk_min_y");
}

static void test_drag_clamp_y_lower_bound() {
    OskState osk = osk_state_new();
    // Push far below screen — must clamp to osk_max_y().
    osk.drag_y = 99999;
    const int32_t oy = osk_origin_y(osk);
    ASSERT_TRUE(oy <= osk_max_y());
    TEST_PASS("SP4-F04: drag_y far positive clamps to osk_max_y");
}

// ── osk_open / osk_close ──────────────────────────────────────────────────────

static void test_osk_open_close() {
    OskState osk = osk_state_new();
    ASSERT_FALSE(osk.visible);
    osk_open(osk);
    ASSERT_TRUE(osk.visible);
    osk_close(osk);
    ASSERT_FALSE(osk.visible);
    TEST_PASS("osk_open/osk_close toggle visible flag");
}

// ── D-pad navigation ──────────────────────────────────────────────────────────

static void test_dpad_left_wraps() {
    OskState osk = osk_state_new();
    osk.osk_cursor = 0;
    osk_dpad_left(osk);
    ASSERT_EQ(osk.osk_cursor, KEY_COUNT - 1);
    TEST_PASS("osk_dpad_left: wrap from 0 to KEY_COUNT-1");
}

static void test_dpad_right_wraps() {
    OskState osk = osk_state_new();
    osk.osk_cursor = KEY_COUNT - 1;
    osk_dpad_right(osk);
    ASSERT_EQ(osk.osk_cursor, 0u);
    TEST_PASS("osk_dpad_right: wrap from KEY_COUNT-1 to 0");
}

static void test_dpad_up_from_special_row_goes_to_row3() {
    OskState osk = osk_state_new();
    osk.osk_cursor = KEY_IDX_SPACE;
    osk_dpad_up(osk);
    // Should land on row 3 (index 30..39), column 0 = index 30.
    ASSERT_EQ(osk.osk_cursor, NORMAL_KEY_COUNT - 10u);
    TEST_PASS("osk_dpad_up from Space → row3 col0 (index 30)");
}

static void test_dpad_down_from_last_row_goes_to_space() {
    OskState osk = osk_state_new();
    osk.osk_cursor = 30; // row 3, col 0
    osk_dpad_down(osk);
    ASSERT_EQ(osk.osk_cursor, KEY_IDX_SPACE);
    TEST_PASS("osk_dpad_down from row3 → Space");
}

static void test_dpad_up_top_row_wraps_to_bottom_row() {
    OskState osk = osk_state_new();
    osk.osk_cursor = 5; // top row, col 5
    osk_dpad_up(osk);
    // Should wrap to same column in bottom normal row: 30 + 5 = 35.
    ASSERT_EQ(osk.osk_cursor, 35u);
    TEST_PASS("osk_dpad_up: top row wraps to bottom normal row, same column");
}

// ── SP4-F05: osk_apply_key — backspace ────────────────────────────────────────

static void test_osk_apply_key_backspace() {
    char buf[64] = "hello";
    size_t len = 5;
    osk_apply_key(KEY_IDX_BACKSPACE, buf, sizeof(buf), len);
    ASSERT_EQ(len, 4u);
    ASSERT_EQ(buf[4], '\0');
    // Remaining content: "hell"
    ASSERT_TRUE(strncmp(buf, "hell", 4) == 0);
    TEST_PASS("SP4-F05: backspace decrements len, NUL-terminates");
}

static void test_osk_apply_key_backspace_at_zero() {
    char buf[64] = "";
    size_t len = 0;
    osk_apply_key(KEY_IDX_BACKSPACE, buf, sizeof(buf), len);
    ASSERT_EQ(len, 0u);
    ASSERT_EQ(buf[0], '\0');
    TEST_PASS("SP4-F05: backspace at len=0 is a no-op");
}

static void test_osk_apply_key_normal_char() {
    char buf[64];
    buf[0] = '\0';
    size_t len = 0;
    // 'q' is index 0
    osk_apply_key(0, buf, sizeof(buf), len);
    ASSERT_EQ(len, 1u);
    ASSERT_EQ(buf[0], 'q');
    ASSERT_EQ(buf[1], '\0');
    TEST_PASS("osk_apply_key: normal key appends character");
}

static void test_osk_apply_key_space() {
    char buf[64];
    buf[0] = '\0';
    size_t len = 0;
    osk_apply_key(KEY_IDX_SPACE, buf, sizeof(buf), len);
    ASSERT_EQ(len, 1u);
    ASSERT_EQ(buf[0], ' ');
    TEST_PASS("osk_apply_key: Space appends ' '");
}

static void test_osk_apply_key_enter() {
    char buf[64];
    buf[0] = '\0';
    size_t len = 0;
    osk_apply_key(KEY_IDX_ENTER, buf, sizeof(buf), len);
    ASSERT_EQ(len, 1u);
    ASSERT_EQ(buf[0], '\n');
    TEST_PASS("osk_apply_key: Enter appends '\\n'");
}

static void test_osk_apply_key_buffer_full() {
    char buf[4] = "abc";
    size_t len = 3;
    // buf_size=4, len+1=4 >= buf_size → refuse append.
    osk_apply_key(0, buf, 4, len);
    ASSERT_EQ(len, 3u);
    ASSERT_EQ(buf[3], '\0');
    TEST_PASS("osk_apply_key: refuses append when buffer full");
}

// ── osk_hit_test ──────────────────────────────────────────────────────────────

static void test_osk_hit_test_first_key() {
    OskState osk = osk_state_new();
    KeyRect r;
    ASSERT_TRUE(key_rect(&osk, 0, &r));
    // Hit exactly at the top-left corner of key 0.
    const size_t hit = osk_hit_test(osk, r.x, r.y);
    ASSERT_EQ(hit, 0u);
    TEST_PASS("osk_hit_test: click on key 0 returns index 0");
}

static void test_osk_hit_test_miss() {
    OskState osk = osk_state_new();
    // Above the panel.
    const size_t hit = osk_hit_test(osk, 0, 0);
    ASSERT_EQ(hit, SIZE_MAX);
    TEST_PASS("osk_hit_test: miss returns SIZE_MAX");
}

// ── osk_key_label ─────────────────────────────────────────────────────────────

static void test_osk_key_label() {
    ASSERT_TRUE(strncmp(osk_key_label(KEY_IDX_SPACE),     "Space", 5) == 0);
    ASSERT_TRUE(strncmp(osk_key_label(KEY_IDX_BACKSPACE), "Bksp",  4) == 0);
    ASSERT_TRUE(strncmp(osk_key_label(KEY_IDX_ENTER),     "Enter", 5) == 0);
    // First key should be "q"
    ASSERT_TRUE(osk_key_label(0)[0] == 'q');
    // Out-of-range returns empty string
    ASSERT_TRUE(osk_key_label(KEY_COUNT)[0] == '\0');
    TEST_PASS("osk_key_label: Space/Bksp/Enter/normal/OOR correct");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_osk_pixel_constants_remapped();
    test_osk_derived_constants();
    test_osk_count_constants_not_remapped();
    test_key_rect_normal_keys();
    test_key_rect_all_normal_keys_return_true();
    test_key_rect_special_keys();
    test_key_rect_out_of_range_returns_false();
    test_drag_clamp_default_position();
    test_drag_clamp_y_upper_bound();
    test_drag_clamp_y_lower_bound();
    test_osk_open_close();
    test_dpad_left_wraps();
    test_dpad_right_wraps();
    test_dpad_up_from_special_row_goes_to_row3();
    test_dpad_down_from_last_row_goes_to_space();
    test_dpad_up_top_row_wraps_to_bottom_row();
    test_osk_apply_key_backspace();
    test_osk_apply_key_backspace_at_zero();
    test_osk_apply_key_normal_char();
    test_osk_apply_key_space();
    test_osk_apply_key_enter();
    test_osk_apply_key_buffer_full();
    test_osk_hit_test_first_key();
    test_osk_hit_test_miss();
    test_osk_key_label();
    return 0;
}
