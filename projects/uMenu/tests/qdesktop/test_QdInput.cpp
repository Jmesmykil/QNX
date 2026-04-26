// test_QdInput.cpp — host unit tests for SP3 HID input pump helpers.
// SP3-F04: InputEvent tagged union.
// SP3-F06: HidTouchAttribute_Start / _End bitmask semantics.
// SP3-F08: MAX_TOUCH = 4 (count constant, not remapped).
// AB-09:   No std::variant / std::visit.
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
    return 0;
}
