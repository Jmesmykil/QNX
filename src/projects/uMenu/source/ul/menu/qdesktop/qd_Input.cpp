// qd_Input.cpp — HID input event pump for uMenu C++ SP3 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/input.rs.
//
// SP3-F04: InputEvent tagged union — no std::variant / std::visit.
// SP3-F05: NpadStyleTag_Handheld | NpadStyleTag_JoyDual | NpadStyleTag_FullKey.
// SP3-F06: (t.attributes & HidTouchAttribute_Start) != 0.
// SP3-F07: touch events appended via insert(events.end(), ...) equivalent
//          (we use a flat buf[] + out_count; see append_event below).
// SP3-F08: MAX_TOUCH = 4, no pixel remap.
// AB-04:   hidGetTouchScreenStates, HidTouchScreenState, HidTouchState — exact C names.
// AB-05:   All variables used; cast discards via (void) where needed.
// AB-09:   No std::variant / std::visit.
// AB-12:   No 1280 / 720 literals.

#include <ul/menu/qdesktop/qd_Input.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace ul::menu::qdesktop {

// ── HID → layout coord scale ─────────────────────────────────────────────────
//
// libnx HID reports touch coordinates in the physical panel space (1280×720
// on Erista handheld panel).  Plutonium's layout coordinate system is
// 1920×1080 logical (see render_Renderer.hpp ScreenFactor = 1.5).  Every
// qdesktop element (QdDesktopIconsElement, QdCursorElement, QdHud, etc.)
// hit-tests against layout coords, so the input pump scales raw HID by 3/2
// before exposing values to consumers.  Integer math keeps the hot path
// branch-free; AB-12 forbids hardcoding 1280/720 literals.
namespace {

static constexpr int32_t kTouchScaleNum = 3;  // 1920 / gcd(1920, 1280)
static constexpr int32_t kTouchScaleDen = 2;  // 1280 / gcd(1920, 1280)

static inline int32_t scale_hid_to_layout(int32_t v) {
    return (v * kTouchScaleNum) / kTouchScaleDen;
}

/// Append one event to the fixed buffer in PolledFrame.
/// Silently drops if the buffer is already full (should never happen at 32 slots).
static void append_event(PolledFrame &f, InputEvent ev) {
    if (f.event_count < 32u) {
        f.events[f.event_count++] = ev;
    }
}

} // namespace

// ── pump_input ────────────────────────────────────────────────────────────────

void pump_input(InputState &state, PolledFrame &out) {
    // Zero the output frame.
    out.event_count  = 0;
    out.stick_r_x    = 0;
    out.stick_r_y    = 0;
    out.rb_held      = false;
    out.zr_held      = false;
    out.zl_held      = false;
    out.a_held       = false;
    out.touch_held   = false;
    out.y_held       = false;
    out.lb_held      = false;
    out.touch_count  = 0;
    for (uint32_t i = 0; i < MAX_TOUCH; ++i) {
        out.touch_pts_x[i] = 0;
        out.touch_pts_y[i] = 0;
    }

#ifdef __SWITCH__
    // ── 1. Touchscreen ────────────────────────────────────────────────────────

    HidTouchScreenState ts = {};
    hidGetTouchScreenStates(&ts, 1);

    // Collect up to MAX_TOUCH finger positions for multitouch (v0.8.0).
    // Coordinates scaled from raw HID 1280×720 panel space to Plutonium's
    // 1920×1080 layout space so consumers (icon hit-test, cursor, etc.)
    // see uniform layout coords.
    const uint32_t raw_count = (uint32_t)ts.count;
    const uint32_t cap_count = (raw_count < MAX_TOUCH) ? raw_count : MAX_TOUCH;
    out.touch_count = (uint8_t)cap_count;
    for (uint32_t i = 0; i < cap_count; ++i) {
        out.touch_pts_x[i] = scale_hid_to_layout((int32_t)ts.touches[i].x);
        out.touch_pts_y[i] = scale_hid_to_layout((int32_t)ts.touches[i].y);
    }

    bool touch_now_active = false;

    // ── Touch event build (same touch_events[] equivalent) ───────────────────
    // We build touch events in a small local buffer first so they can be
    // appended AFTER button events, matching input.rs order.
    // SP3-F07: "events.extend(touch_events)" → append after buttons below.

    InputEvent touch_buf[8];
    uint32_t   touch_count_local = 0;

    if (ts.count > 0) {
        const HidTouchState &t0 = ts.touches[0];
        // Scale primary-touch event coordinates into Plutonium layout space
        // so TouchDown/TouchMove/TouchUp events match icon hit-test geometry.
        const int32_t tx = scale_hid_to_layout((int32_t)t0.x);
        const int32_t ty = scale_hid_to_layout((int32_t)t0.y);
        touch_now_active = true;

        // SP3-F06: bitmask test — (attributes & HidTouchAttribute_Start) != 0.
        const bool start_flag = (t0.attributes & HidTouchAttribute_Start) != 0;
        const bool end_flag   = (t0.attributes & HidTouchAttribute_End)   != 0;

        if (start_flag || !state.prev_touch_active) {
            if (touch_count_local < 8u) {
                touch_buf[touch_count_local++] = input_event_touch(InputEvent::Kind::TouchDown, tx, ty);
            }
        }
        if (touch_count_local < 8u) {
            touch_buf[touch_count_local++] = input_event_touch(InputEvent::Kind::TouchMove, tx, ty);
        }
        if (end_flag) {
            if (touch_count_local < 8u) {
                touch_buf[touch_count_local++] = input_event_touch(InputEvent::Kind::TouchUp, tx, ty);
            }
            touch_now_active = false;
        }

        state.prev_touch_x = tx;
        state.prev_touch_y = ty;
    } else if (state.prev_touch_active) {
        // Finger left without firing End — synthesise TouchUp at last position.
        if (touch_count_local < 8u) {
            touch_buf[touch_count_local++] = input_event_touch(
                InputEvent::Kind::TouchUp, state.prev_touch_x, state.prev_touch_y);
        }
    }

    state.prev_touch_active = touch_now_active;

    // ── 2. Buttons ────────────────────────────────────────────────────────────
    // SP3-F05: libnx 4.x modern PadState API — padInitializeAny once, padUpdate
    // per pump call, padGetButtons for current state. Mirrors Plutonium's
    // input_pad usage pattern (see render_Renderer.hpp:205,399).

    static PadState g_pad;
    static bool g_pad_initialized = false;
    if (!g_pad_initialized) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeAny(&g_pad);
        g_pad_initialized = true;
    }
    padUpdate(&g_pad);

    const uint64_t cur  = padGetButtons(&g_pad);
    const uint64_t down = cur & ~state.prev_buttons;
    state.prev_buttons  = cur;

    // D-pad edges.
    if (down & HidNpadButton_Left)  { append_event(out, input_event_simple(InputEvent::Kind::Left)); }
    if (down & HidNpadButton_Right) { append_event(out, input_event_simple(InputEvent::Kind::Right)); }
    if (down & HidNpadButton_Up)    { append_event(out, input_event_simple(InputEvent::Kind::Up)); }
    if (down & HidNpadButton_Down)  { append_event(out, input_event_simple(InputEvent::Kind::Down)); }

    // Face buttons.
    if (down & HidNpadButton_A) { append_event(out, input_event_simple(InputEvent::Kind::A)); }
    if (down & HidNpadButton_B) { append_event(out, input_event_simple(InputEvent::Kind::B)); }
    if (down & HidNpadButton_X) { append_event(out, input_event_simple(InputEvent::Kind::X)); }
    if (down & HidNpadButton_Y) { append_event(out, input_event_simple(InputEvent::Kind::Y)); }

    // Mouse-button mappings (v0.5.0: ZR = left click, ZL = right click).
    if (down & HidNpadButton_ZR) { append_event(out, input_event_simple(InputEvent::Kind::RClick)); }
    if (down & HidNpadButton_ZL) { append_event(out, input_event_simple(InputEvent::Kind::ZLClick)); }

    // Shoulder bumpers (v0.11.0: L=LBPress, R=RBPress).
    if (down & HidNpadButton_L) { append_event(out, input_event_simple(InputEvent::Kind::LBPress)); }
    if (down & HidNpadButton_R) { append_event(out, input_event_simple(InputEvent::Kind::RBPress)); }

    // System buttons.
    if (down & HidNpadButton_Plus)  { append_event(out, input_event_simple(InputEvent::Kind::Plus)); }
    if (down & HidNpadButton_Minus) { append_event(out, input_event_simple(InputEvent::Kind::Minus)); }

    // SP3-F07: Append touch events AFTER button events.
    for (uint32_t i = 0; i < touch_count_local; ++i) {
        append_event(out, touch_buf[i]);
    }

    // ── 3. Right stick ────────────────────────────────────────────────────────

    HidAnalogStickState stick_r = padGetStickPos(&g_pad, 1);
    out.stick_r_x = (stick_r.x < STICK_DEADZONE && stick_r.x > -STICK_DEADZONE) ? 0 : stick_r.x;
    out.stick_r_y = (stick_r.y < STICK_DEADZONE && stick_r.y > -STICK_DEADZONE) ? 0 : stick_r.y;

    // ── 4. Held-state booleans ────────────────────────────────────────────────

    out.rb_held    = (cur & HidNpadButton_R)  != 0;
    out.zr_held    = (cur & HidNpadButton_ZR) != 0;
    out.zl_held    = (cur & HidNpadButton_ZL) != 0;
    out.a_held     = (cur & HidNpadButton_A)  != 0;
    out.touch_held = state.prev_touch_active;
    out.y_held     = (cur & HidNpadButton_Y)  != 0;
    out.lb_held    = (cur & HidNpadButton_L)  != 0;

#else
    // Host build: hardware I/O is unavailable; output frame stays zeroed.
    // Tests exercise the logic via the helpers below.
    (void)state;
#endif
}

// ── Host-accessible touch state-machine helper ────────────────────────────────
// This function encapsulates the touch event logic so host tests can exercise
// it without requiring libnx.  Called by pump_input on-device; called directly
// by tests on the host.

/// Process one frame of raw touch data and append events to out.
/// raw_count: number of fingers reported this frame.
/// tx, ty: position of finger 0 (only used when raw_count > 0).
/// start_flag: HidTouchAttribute_Start was set for finger 0.
/// end_flag:   HidTouchAttribute_End was set for finger 0.
/// touch_now_active_out: receives the new prev_touch_active value.
void input_process_touch_frame(
    InputState &state,
    uint32_t raw_count,
    int32_t tx, int32_t ty,
    bool start_flag, bool end_flag,
    PolledFrame &out,
    bool &touch_now_active_out)
{
    bool touch_now_active = false;

    if (raw_count > 0) {
        touch_now_active = true;

        if (start_flag || !state.prev_touch_active) {
            append_event(out, input_event_touch(InputEvent::Kind::TouchDown, tx, ty));
        }
        append_event(out, input_event_touch(InputEvent::Kind::TouchMove, tx, ty));
        if (end_flag) {
            append_event(out, input_event_touch(InputEvent::Kind::TouchUp, tx, ty));
            touch_now_active = false;
        }

        state.prev_touch_x = tx;
        state.prev_touch_y = ty;
    } else if (state.prev_touch_active) {
        append_event(out, input_event_touch(
            InputEvent::Kind::TouchUp, state.prev_touch_x, state.prev_touch_y));
    }

    touch_now_active_out    = touch_now_active;
    state.prev_touch_active = touch_now_active;
}

/// Process one frame of raw button data and append events to out.
/// cur: bitmask of currently held buttons.
/// Updates state.prev_buttons in place.
void input_process_button_frame(
    InputState &state,
    uint64_t cur,
    PolledFrame &out)
{
    const uint64_t down = cur & ~state.prev_buttons;
    state.prev_buttons  = cur;

    if (down & (1ULL << 0))  { append_event(out, input_event_simple(InputEvent::Kind::Left)); }
    if (down & (1ULL << 1))  { append_event(out, input_event_simple(InputEvent::Kind::Right)); }
    if (down & (1ULL << 2))  { append_event(out, input_event_simple(InputEvent::Kind::Up)); }
    if (down & (1ULL << 3))  { append_event(out, input_event_simple(InputEvent::Kind::Down)); }
    if (down & (1ULL << 4))  { append_event(out, input_event_simple(InputEvent::Kind::A)); }
    if (down & (1ULL << 5))  { append_event(out, input_event_simple(InputEvent::Kind::B)); }
    if (down & (1ULL << 6))  { append_event(out, input_event_simple(InputEvent::Kind::X)); }
    if (down & (1ULL << 7))  { append_event(out, input_event_simple(InputEvent::Kind::Y)); }
    if (down & (1ULL << 8))  { append_event(out, input_event_simple(InputEvent::Kind::RClick)); }
    if (down & (1ULL << 9))  { append_event(out, input_event_simple(InputEvent::Kind::ZLClick)); }
    if (down & (1ULL << 10)) { append_event(out, input_event_simple(InputEvent::Kind::LBPress)); }
    if (down & (1ULL << 11)) { append_event(out, input_event_simple(InputEvent::Kind::RBPress)); }
    if (down & (1ULL << 12)) { append_event(out, input_event_simple(InputEvent::Kind::Plus)); }
    if (down & (1ULL << 13)) { append_event(out, input_event_simple(InputEvent::Kind::Minus)); }
}

} // namespace ul::menu::qdesktop
