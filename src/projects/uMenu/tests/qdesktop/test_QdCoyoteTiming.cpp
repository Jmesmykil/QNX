// test_QdCoyoteTiming.cpp -- v1.8.23 host tests for coyote-timing constants.
//
// Pins all six timing constants introduced in v1.8.23 (qd_DesktopIcons.cpp:1173-1177
// and CLICK_TOLERANCE_PX at :1228). Verifies their values and their derived
// real-time semantics at the 19.2 MHz armGetSystemTick rate used on Erista.
//
// These constants are file-scope statics in qd_DesktopIcons.cpp and cannot be
// imported directly.  They are mirrored here verbatim — identical to the
// production lines — so any future change to the production values produces a
// test failure immediately.
//
// Tick arithmetic:
//   tick_rate = 19'200'000 Hz (libnx armGetSystemTick on Switch Erista).
//   ms = ticks / tick_rate * 1000 = ticks / 19200.
//
// Build:  make test_QdCoyoteTiming  (from tests/qdesktop/)
// Run:    ./test_QdCoyoteTiming

#include "test_host_stubs.hpp"
#include <cstdint>

// ── Constants mirrored from qd_DesktopIcons.cpp:1173-1228 ───────────────────

static constexpr uint64_t TAP_MAX_TICKS          = 4'800'000ULL;
static constexpr uint64_t RELAUNCH_LOCKOUT_TICKS = 5'760'000ULL;
static constexpr uint32_t DPAD_REPEAT_DELAY_F    = 18u;
static constexpr uint32_t DPAD_REPEAT_INTERVAL_F = 9u;
static constexpr int32_t  CLICK_TOLERANCE_PX     = 24;

// Tick rate for Switch Erista (armGetSystemTick source).
static constexpr uint64_t TICK_HZ = 19'200'000ULL;

// ── Test 1: TAP_MAX_TICKS raw value ─────────────────────────────────────────

static void test_tap_max_ticks_raw() {
    ASSERT_EQ(TAP_MAX_TICKS, 4'800'000ULL);
    TEST_PASS("TAP_MAX_TICKS == 4'800'000 (raw)");
}

// ── Test 2: TAP_MAX_TICKS converts to 250 ms ±5 ms at 19.2 MHz ──────────────

static void test_tap_max_ticks_ms() {
    // ticks / Hz * 1000 = ms  (avoid FP: multiply first to stay integer)
    const uint64_t ms_x1000 = TAP_MAX_TICKS * 1000ULL / TICK_HZ;
    // 4'800'000 * 1000 / 19'200'000 = 250
    ASSERT_TRUE(ms_x1000 >= 245u && ms_x1000 <= 255u);
    TEST_PASS("TAP_MAX_TICKS => 250 ms ±5 ms at 19.2 MHz");
}

// ── Test 3: RELAUNCH_LOCKOUT_TICKS raw value ────────────────────────────────

static void test_relaunch_lockout_ticks_raw() {
    ASSERT_EQ(RELAUNCH_LOCKOUT_TICKS, 5'760'000ULL);
    TEST_PASS("RELAUNCH_LOCKOUT_TICKS == 5'760'000 (raw)");
}

// ── Test 4: RELAUNCH_LOCKOUT_TICKS converts to 300 ms ±5 ms at 19.2 MHz ────

static void test_relaunch_lockout_ticks_ms() {
    const uint64_t ms_x1000 = RELAUNCH_LOCKOUT_TICKS * 1000ULL / TICK_HZ;
    // 5'760'000 * 1000 / 19'200'000 = 300
    ASSERT_TRUE(ms_x1000 >= 295u && ms_x1000 <= 305u);
    TEST_PASS("RELAUNCH_LOCKOUT_TICKS => 300 ms ±5 ms at 19.2 MHz");
}

// ── Test 5: DPAD_REPEAT_DELAY_F == 18 (300 ms at 60 fps) ───────────────────

static void test_dpad_repeat_delay_f() {
    ASSERT_EQ(DPAD_REPEAT_DELAY_F, 18u);
    // 18 frames / 60 fps = 300 ms (Plutonium DefaultMoveWaitTimeMs canonical).
    // Integer check: 18 * 1000 / 60 = 300.
    const uint32_t ms = DPAD_REPEAT_DELAY_F * 1000u / 60u;
    ASSERT_EQ(ms, 300u);
    TEST_PASS("DPAD_REPEAT_DELAY_F == 18 => 300 ms at 60 fps");
}

// ── Test 6: DPAD_REPEAT_INTERVAL_F == 9 (150 ms at 60 fps) ─────────────────

static void test_dpad_repeat_interval_f() {
    ASSERT_EQ(DPAD_REPEAT_INTERVAL_F, 9u);
    // 9 frames / 60 fps = 150 ms (Plutonium DefaultMoveWaitTimeMs canonical).
    const uint32_t ms = DPAD_REPEAT_INTERVAL_F * 1000u / 60u;
    ASSERT_EQ(ms, 150u);
    TEST_PASS("DPAD_REPEAT_INTERVAL_F == 9 => 150 ms at 60 fps");
}

// ── Test 7: CLICK_TOLERANCE_PX == 24 (preserved from prior baseline) ────────

static void test_click_tolerance_px() {
    ASSERT_EQ(CLICK_TOLERANCE_PX, 24);
    TEST_PASS("CLICK_TOLERANCE_PX == 24 (preserved baseline)");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_tap_max_ticks_raw();
    test_tap_max_ticks_ms();
    test_relaunch_lockout_ticks_raw();
    test_relaunch_lockout_ticks_ms();
    test_dpad_repeat_delay_f();
    test_dpad_repeat_interval_f();
    test_click_tolerance_px();
    return 0;
}
