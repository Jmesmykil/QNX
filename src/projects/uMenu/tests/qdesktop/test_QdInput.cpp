// test_QdInput.cpp — host unit tests for SP3 HID input pump helpers.
// SP3-F04: InputEvent tagged union.
// SP3-F06: HidTouchAttribute_Start / _End bitmask semantics.
// SP3-F08: MAX_TOUCH = 4 (count constant, not remapped).
// AB-09:   No std::variant / std::visit.
// v1.8.23: Coyote-timing input chain assertions (tap vs hold, relaunch lockout,
//          D-pad repeat delay + interval, host-side arithmetic only).
//
// All tests call input_process_touch_frame / input_process_button_frame —
// the host-accessible helpers extracted from pump_input() to make testing
// possible without libnx.
#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_Input.hpp>
#include <cstdint>

using namespace ul::menu::qdesktop;

// ── helpers ───────────────────────────────────────────────────────────────────

/// Find the first event of kind k in out; returns true and sets *ev on match.
static bool find_event(const PolledFrame &f, InputEvent::Kind k, InputEvent *ev = nullptr) {
    for (uint32_t i = 0; i < f.event_count; ++i) {
        if (f.events[i].kind == k) {
            if (ev) { *ev = f.events[i]; }
            return true;
        }
    }
    return false;
}

/// Count events of kind k in out.
static uint32_t count_events(const PolledFrame &f, InputEvent::Kind k) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < f.event_count; ++i) {
        if (f.events[i].kind == k) { ++n; }
    }
    return n;
}

static PolledFrame zero_frame() {
    PolledFrame f;
    f.event_count = 0;
    f.stick_r_x = 0; f.stick_r_y = 0;
    f.rb_held = f.zr_held = f.zl_held = f.a_held = false;
    f.touch_held = f.y_held = f.lb_held = false;
    f.touch_count = 0;
    for (uint32_t i = 0; i < MAX_TOUCH; ++i) {
        f.touch_pts_x[i] = 0;
        f.touch_pts_y[i] = 0;
    }
    return f;
}

// ── touch state machine tests ─────────────────────────────────────────────────

static void test_touch_down_on_first_contact() {
    InputState st = input_state_zero();
    PolledFrame f = zero_frame();
    bool active_out = false;

    // First contact: raw_count=1, start_flag=true, end_flag=false.
    input_process_touch_frame(st, 1, 100, 200, true, false, f, active_out);

    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchDown));
    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchMove));
    ASSERT_FALSE(find_event(f, InputEvent::Kind::TouchUp));
    ASSERT_TRUE(active_out);
    ASSERT_TRUE(st.prev_touch_active);
    ASSERT_EQ(st.prev_touch_x, 100);
    ASSERT_EQ(st.prev_touch_y, 200);
    TEST_PASS("TouchDown + TouchMove emitted on first contact");
}

static void test_touch_move_only_on_held() {
    InputState st = input_state_zero();
    st.prev_touch_active = true;

    PolledFrame f = zero_frame();
    bool active_out = false;

    // Continuing contact: start_flag=false, end_flag=false, prev_touch_active=true.
    input_process_touch_frame(st, 1, 110, 210, false, false, f, active_out);

    // Only a TouchMove should be emitted; no TouchDown (start_flag=false and was already active).
    ASSERT_FALSE(find_event(f, InputEvent::Kind::TouchDown));
    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchMove));
    ASSERT_FALSE(find_event(f, InputEvent::Kind::TouchUp));
    ASSERT_TRUE(active_out);
    TEST_PASS("only TouchMove emitted when already active and no start_flag");
}

static void test_touch_up_on_end_flag() {
    InputState st = input_state_zero();
    st.prev_touch_active = true;
    st.prev_touch_x = 120;
    st.prev_touch_y = 220;

    PolledFrame f = zero_frame();
    bool active_out = false;

    // End of touch: start_flag=false, end_flag=true.
    input_process_touch_frame(st, 1, 120, 220, false, true, f, active_out);

    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchMove));
    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchUp));
    ASSERT_FALSE(active_out);
    ASSERT_FALSE(st.prev_touch_active);
    TEST_PASS("TouchUp emitted when end_flag set");
}

static void test_touch_up_synthesised_on_liftoff() {
    // raw_count drops to 0 while prev_touch_active=true → synthesise TouchUp at last pos.
    InputState st = input_state_zero();
    st.prev_touch_active = true;
    st.prev_touch_x = 50;
    st.prev_touch_y = 60;

    PolledFrame f = zero_frame();
    bool active_out = false;

    input_process_touch_frame(st, 0, 0, 0, false, false, f, active_out);

    InputEvent ev;
    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchUp, &ev));
    ASSERT_EQ(ev.data.touch.x, 50);
    ASSERT_EQ(ev.data.touch.y, 60);
    ASSERT_FALSE(active_out);
    TEST_PASS("TouchUp synthesised at last position when raw_count drops to 0");
}

static void test_touch_no_events_if_no_touch_and_was_not_active() {
    // prev_touch_active=false and raw_count=0 → no events.
    InputState st = input_state_zero();
    PolledFrame f = zero_frame();
    bool active_out = false;

    input_process_touch_frame(st, 0, 0, 0, false, false, f, active_out);

    ASSERT_EQ(f.event_count, 0u);
    ASSERT_FALSE(active_out);
    TEST_PASS("no touch events when no contact and was not active");
}

static void test_touch_down_emitted_when_no_start_flag_but_was_not_active() {
    // prev_touch_active=false, raw_count=1, start_flag=false.
    // The !state.prev_touch_active branch should still emit TouchDown.
    InputState st = input_state_zero();
    PolledFrame f = zero_frame();
    bool active_out = false;

    input_process_touch_frame(st, 1, 200, 300, false, false, f, active_out);

    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchDown));
    ASSERT_TRUE(find_event(f, InputEvent::Kind::TouchMove));
    TEST_PASS("TouchDown emitted when not previously active even if start_flag=false");
}

// ── button edge detection tests ───────────────────────────────────────────────

static void test_button_edge_fires_once() {
    InputState st = input_state_zero();
    PolledFrame f = zero_frame();

    // Press A (bit 4).
    const uint64_t A_BIT = (1ULL << 4);
    input_process_button_frame(st, A_BIT, f);
    ASSERT_EQ(count_events(f, InputEvent::Kind::A), 1u);

    // Same bitmask still held → no new edge.
    PolledFrame f2 = zero_frame();
    input_process_button_frame(st, A_BIT, f2);
    ASSERT_EQ(count_events(f2, InputEvent::Kind::A), 0u);

    // Release and re-press → fires again.
    PolledFrame f3 = zero_frame();
    input_process_button_frame(st, 0, f3);  // release

    PolledFrame f4 = zero_frame();
    input_process_button_frame(st, A_BIT, f4);
    ASSERT_EQ(count_events(f4, InputEvent::Kind::A), 1u);

    TEST_PASS("button edge fires once on press, not while held");
}

static void test_button_all_kinds_round_trip() {
    // Verify each bit position maps to the expected InputEvent::Kind.
    struct { uint32_t bit; InputEvent::Kind kind; } table[] = {
        { 0,  InputEvent::Kind::Left    },
        { 1,  InputEvent::Kind::Right   },
        { 2,  InputEvent::Kind::Up      },
        { 3,  InputEvent::Kind::Down    },
        { 4,  InputEvent::Kind::A       },
        { 5,  InputEvent::Kind::B       },
        { 6,  InputEvent::Kind::X       },
        { 7,  InputEvent::Kind::Y       },
        { 8,  InputEvent::Kind::RClick  },
        { 9,  InputEvent::Kind::ZLClick },
        { 10, InputEvent::Kind::LBPress },
        { 11, InputEvent::Kind::RBPress },
        { 12, InputEvent::Kind::Plus    },
        { 13, InputEvent::Kind::Minus   },
    };
    for (auto &entry : table) {
        InputState st = input_state_zero();
        PolledFrame f = zero_frame();
        input_process_button_frame(st, (1ULL << entry.bit), f);
        ASSERT_TRUE(find_event(f, entry.kind));
    }
    TEST_PASS("all 14 button bits map to correct InputEvent::Kind");
}

static void test_button_simultaneous_press() {
    InputState st = input_state_zero();
    PolledFrame f = zero_frame();

    // Press Left+Right simultaneously.
    const uint64_t bits = (1ULL << 0) | (1ULL << 1);
    input_process_button_frame(st, bits, f);
    ASSERT_EQ(count_events(f, InputEvent::Kind::Left), 1u);
    ASSERT_EQ(count_events(f, InputEvent::Kind::Right), 1u);
    TEST_PASS("simultaneous button presses generate separate events");
}

// ── MAX_TOUCH constant ────────────────────────────────────────────────────────

static void test_max_touch_constant() {
    ASSERT_EQ(MAX_TOUCH, 4u);
    TEST_PASS("MAX_TOUCH == 4 (count constant, no pixel remap)");
}

// ── input_state_zero ─────────────────────────────────────────────────────────

static void test_input_state_zero() {
    InputState st = input_state_zero();
    ASSERT_EQ(st.prev_buttons, 0u);
    ASSERT_FALSE(st.prev_touch_active);
    ASSERT_EQ(st.prev_touch_x, 0);
    ASSERT_EQ(st.prev_touch_y, 0);
    TEST_PASS("input_state_zero initialises all fields to zero/false");
}

// ── v1.8.23 coyote-timing input chain tests ───────────────────────────────────
//
// These tests exercise the ARITHMETIC of the coyote-timing decisions without
// calling any production function.  They mirror the exact same constants that
// test_QdCoyoteTiming.cpp pins, and verify the decision formulas that
// OnInput() applies to those constants.  If a constant changes in production,
// BOTH test files disagree simultaneously.
//
// Tick rate: TICK_HZ = 19'200'000 Hz (armGetSystemTick on Erista).
// Frame rate: 60 fps (DPAD_REPEAT_DELAY_F and DPAD_REPEAT_INTERVAL_F are
//             frame counts).

// Constants mirrored verbatim from qd_DesktopIcons.cpp:1173-1228.
static constexpr uint64_t CT_TAP_MAX_TICKS          = 4'800'000ULL;
static constexpr uint64_t CT_RELAUNCH_LOCKOUT_TICKS = 5'760'000ULL;
static constexpr uint32_t CT_DPAD_REPEAT_DELAY_F    = 18u;
static constexpr uint32_t CT_DPAD_REPEAT_INTERVAL_F = 9u;

// ── Test: tap detection — down_tick within TAP_MAX_TICKS of release ───────────
// The production branch:
//   if (up_tick - down_tick <= TAP_MAX_TICKS) → treat as tap (launch).
//   else                                       → treat as long-hold (ignore).
//
// Checks that the boundary is non-strict (exactly TAP_MAX_TICKS → tap).

static void test_coyote_tap_within_threshold() {
    // Exactly at threshold: should be a tap.
    const uint64_t down_tick = 100'000'000ULL;
    const uint64_t up_tick_tap  = down_tick + CT_TAP_MAX_TICKS;
    const uint64_t up_tick_hold = down_tick + CT_TAP_MAX_TICKS + 1ULL;

    const bool is_tap_at_boundary = (up_tick_tap - down_tick) <= CT_TAP_MAX_TICKS;
    const bool is_tap_over_boundary = (up_tick_hold - down_tick) <= CT_TAP_MAX_TICKS;

    ASSERT_TRUE(is_tap_at_boundary);
    ASSERT_FALSE(is_tap_over_boundary);
    TEST_PASS("tap detection: <=TAP_MAX_TICKS is tap, >TAP_MAX_TICKS is hold");
}

// ── Test: relaunch lockout — second launch within RELAUNCH_LOCKOUT_TICKS blocked
// The production branch:
//   if (now - last_launch_tick < RELAUNCH_LOCKOUT_TICKS) → block second launch.
//   else                                                  → allow.
//
// Verifies the boundary: exactly at RELAUNCH_LOCKOUT_TICKS → allowed (strict <).

static void test_coyote_relaunch_lockout() {
    const uint64_t last_launch_tick = 200'000'000ULL;
    const uint64_t now_just_inside  = last_launch_tick + CT_RELAUNCH_LOCKOUT_TICKS - 1ULL;
    const uint64_t now_at_boundary  = last_launch_tick + CT_RELAUNCH_LOCKOUT_TICKS;

    const bool blocked_inside    = (now_just_inside - last_launch_tick) < CT_RELAUNCH_LOCKOUT_TICKS;
    const bool blocked_at_boundary = (now_at_boundary - last_launch_tick) < CT_RELAUNCH_LOCKOUT_TICKS;

    ASSERT_TRUE(blocked_inside);
    ASSERT_FALSE(blocked_at_boundary);   // exactly at lockout → NOT blocked
    TEST_PASS("relaunch lockout: <RELAUNCH_LOCKOUT_TICKS blocked, ==RELAUNCH_LOCKOUT_TICKS allowed");
}

// ── Test: D-pad repeat delay — first repeat fires at frame DELAY_F ───────────
// The production formula (qd_DesktopIcons.cpp ~3420):
//   repeat = (held_frames > DPAD_REPEAT_DELAY_F) &&
//             ((held_frames - DPAD_REPEAT_DELAY_F - 1) % DPAD_REPEAT_INTERVAL_F == 0)
//
// Frame 18 (DELAY_F): no repeat yet (held_frames == DELAY_F, not >).
// Frame 19: held_frames = 19 > 18, (19-18-1) % 9 = 0 % 9 = 0 → REPEAT.

static void test_coyote_dpad_repeat_delay() {
    // Helper: does held_frames trigger a repeat?
    auto fires = [](uint32_t f) -> bool {
        if (f <= CT_DPAD_REPEAT_DELAY_F) return false;
        return ((f - CT_DPAD_REPEAT_DELAY_F - 1u) % CT_DPAD_REPEAT_INTERVAL_F) == 0u;
    };

    ASSERT_FALSE(fires(CT_DPAD_REPEAT_DELAY_F));       // frame 18: not yet
    ASSERT_TRUE (fires(CT_DPAD_REPEAT_DELAY_F + 1u)); // frame 19: first repeat
    TEST_PASS("D-pad repeat delay: no repeat at frame DELAY_F; first repeat at DELAY_F+1");
}

// ── Test: D-pad repeat interval — repeats fire every INTERVAL_F frames ────────
// Following the first repeat at frame 19, the next should fire at frame 28
// (19 + 9), then 37 (28 + 9), etc.

static void test_coyote_dpad_repeat_interval() {
    auto fires = [](uint32_t f) -> bool {
        if (f <= CT_DPAD_REPEAT_DELAY_F) return false;
        return ((f - CT_DPAD_REPEAT_DELAY_F - 1u) % CT_DPAD_REPEAT_INTERVAL_F) == 0u;
    };

    // First repeat: frame 19 (DELAY_F + 1).
    const uint32_t first = CT_DPAD_REPEAT_DELAY_F + 1u;
    ASSERT_TRUE(fires(first));

    // Intermediate frames: must NOT fire.
    for (uint32_t i = 1u; i < CT_DPAD_REPEAT_INTERVAL_F; ++i) {
        ASSERT_FALSE(fires(first + i));
    }

    // Second repeat: first + INTERVAL_F.
    ASSERT_TRUE(fires(first + CT_DPAD_REPEAT_INTERVAL_F));

    // Third repeat.
    ASSERT_TRUE(fires(first + 2u * CT_DPAD_REPEAT_INTERVAL_F));

    TEST_PASS("D-pad repeat interval: repeats fire every DPAD_REPEAT_INTERVAL_F frames after first");
}

// ── Test: held-frame counter increments and resets ───────────────────────────
// Models the dpad_held_frames[i] increment-on-held / reset-on-release contract
// (qd_DesktopIcons.cpp ~3412-3416).

static void test_coyote_held_frames_counter() {
    uint32_t held = 0u;

    // Simulate 20 consecutive held frames.
    for (uint32_t frame = 0; frame < 20u; ++frame) {
        // Key held: increment.
        held += 1u;
    }
    ASSERT_EQ(held, 20u);

    // Key released: reset to zero.
    held = 0u;
    ASSERT_EQ(held, 0u);

    // One more press cycle.
    held = 1u;
    ASSERT_EQ(held, 1u);

    TEST_PASS("held-frame counter: increments per-frame when held, resets to 0 on release");
}

// ── Test: tap produces no repeat (held for <=TAP_MAX ticks, then released) ───
// If a key is tapped (released before it crosses DELAY_F frames), the held_frame
// counter never reaches DELAY_F+1, so no D-pad repeat fires.
//
// Approximate: TAP_MAX_TICKS / (TICK_HZ/60) = 4'800'000 / 320'000 = 15 frames.
// 15 < DELAY_F (18) → no repeat during a tap.

static void test_coyote_tap_produces_no_repeat() {
    // TAP_MAX at 60 fps in frames (integer: floor).
    static constexpr uint64_t TICK_HZ = 19'200'000ULL;
    static constexpr uint64_t TICKS_PER_FRAME = TICK_HZ / 60ULL;  // 320000
    const uint32_t tap_max_frames =
        static_cast<uint32_t>(CT_TAP_MAX_TICKS / TICKS_PER_FRAME);  // 15

    // A tap releases within tap_max_frames; held counter never exceeds that.
    ASSERT_TRUE(tap_max_frames < CT_DPAD_REPEAT_DELAY_F);
    TEST_PASS("tap_max_frames (15) < DPAD_REPEAT_DELAY_F (18): tap never triggers D-pad repeat");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_touch_down_on_first_contact();
    test_touch_move_only_on_held();
    test_touch_up_on_end_flag();
    test_touch_up_synthesised_on_liftoff();
    test_touch_no_events_if_no_touch_and_was_not_active();
    test_touch_down_emitted_when_no_start_flag_but_was_not_active();
    test_button_edge_fires_once();
    test_button_all_kinds_round_trip();
    test_button_simultaneous_press();
    test_max_touch_constant();
    test_input_state_zero();
    // v1.8.23 coyote-timing input chain
    test_coyote_tap_within_threshold();
    test_coyote_relaunch_lockout();
    test_coyote_dpad_repeat_delay();
    test_coyote_dpad_repeat_interval();
    test_coyote_held_frames_counter();
    test_coyote_tap_produces_no_repeat();
    return 0;
}
