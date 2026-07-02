// qd_Input.hpp — HID input event pump for uMenu C++ SP3 (v1.2.0).
// Ported from tools/mock-nro-desktop-gui/src/input.rs.
//
// SP3-F04: InputEvent tagged union — std::variant / std::visit BANNED.
// SP3-F05: NpadStyleTag C names: NpadStyleTag_Handheld, NpadStyleTag_JoyDual,
//          NpadStyleTag_FullKey — no () call syntax.
// SP3-F06: HidTouchAttribute_Start bitmask test — no .contains() method.
// SP3-F07: events.extend(touch_events) → insert(events.end(), ...).
// SP3-F08: MAX_TOUCH = 4 — count constant, NOT remapped.
// AB-04:   hidGetTouchScreenStates / HidTouchScreenState / HidTouchState — exact C names.
// AB-09:   No std::variant / std::visit.
// AB-12:   No 1280 or 720 literals.
#pragma once
#include <pu/Plutonium>
#include <cstdint>

// On the Switch target these are provided by libnx; on the host test target
// they are provided by test_host_stubs.hpp.
#ifndef HID_TOUCH_MAX_TOUCHES
#define HID_TOUCH_MAX_TOUCHES 16
#endif

namespace ul::menu::qdesktop {

// ── MAX_TOUCH — SP3-F08: count constant, no ×1.5 remap ──────────────────────

static constexpr uint32_t MAX_TOUCH = 4;  // not a pixel — no remap

// ── InputEvent — SP3-F04 tagged union ────────────────────────────────────────

/// Edge-triggered input event for one frame.
/// One InputEvent is produced per button edge or per touch phase transition.
/// TouchMove is produced every frame a finger is down.
/// SP3-F04: explicit tagged union — std::variant / std::visit are banned.
struct InputEvent {
    enum class Kind : uint8_t {
        // D-pad directional edges.
        Left,
        Right,
        Up,
        Down,
        // Face buttons.
        A,
        B,
        X,
        Y,
        // ZR = left click edge; ZL = right click edge.
        RClick,
        ZLClick,
        // Shoulder bumpers.
        LBPress,
        RBPress,
        // Touch phases.
        TouchDown,   ///< First frame a finger is down.
        TouchMove,   ///< Every frame a finger is down.
        TouchUp,     ///< Frame the finger lifts.
        // System.
        Plus,
        Minus,
    } kind;

    union {
        struct { int32_t x, y; } touch;    ///< valid for TouchDown/TouchMove/TouchUp
        uint64_t                 button;   ///< reserved (unused by this pump but kept for extension)
        struct { int32_t x, y; } stick;    ///< reserved
    } data;
};

// Convenience constructors.
inline InputEvent input_event_simple(InputEvent::Kind k) {
    InputEvent e;
    e.kind       = k;
    e.data.touch = { 0, 0 };
    return e;
}

inline InputEvent input_event_touch(InputEvent::Kind k, int32_t x, int32_t y) {
    InputEvent e;
    e.kind         = k;
    e.data.touch.x = x;
    e.data.touch.y = y;
    return e;
}

// ── InputState — persistent across frames ────────────────────────────────────

/// State that persists between pump_input calls.
/// Caller owns one of these in UiState and passes it by reference each frame.
struct InputState {
    uint64_t prev_buttons;       ///< Button bitmask from the previous frame.
    bool     prev_touch_active;  ///< True if a finger was down last frame.
    int32_t  prev_touch_x;       ///< Last known finger X (for synthesised TouchUp).
    int32_t  prev_touch_y;       ///< Last known finger Y.
};

inline InputState input_state_zero() {
    InputState s;
    s.prev_buttons      = 0;
    s.prev_touch_active = false;
    s.prev_touch_x      = 0;
    s.prev_touch_y      = 0;
    return s;
}

// ── Polled frame ─────────────────────────────────────────────────────────────

/// One frame of polled input: event list + continuous held-state booleans.
/// Events are written into a caller-supplied fixed buffer to avoid heap allocation.
struct PolledFrame {
    uint32_t event_count;                    ///< Number of valid entries in events[].
    InputEvent events[32];                   ///< Fixed-size event buffer (>= max per frame).

    int32_t  stick_r_x;   ///< Right-stick X after deadzone, raw HID scale.
    int32_t  stick_r_y;   ///< Right-stick Y after deadzone (positive = up per HID).

    bool rb_held;          ///< RB (R bumper) currently depressed.
    bool zr_held;          ///< ZR (right trigger) currently depressed.
    bool zl_held;          ///< ZL (left trigger) currently depressed.
    bool a_held;           ///< A currently depressed.
    bool touch_held;       ///< Any finger currently touching the screen.
    bool y_held;           ///< Y currently depressed.
    bool lb_held;          ///< LB (L bumper) currently depressed.

    // v0.8.0 multi-touch.
    uint8_t  touch_count;                        ///< Fingers on screen (0..MAX_TOUCH).
    int32_t  touch_pts_x[MAX_TOUCH];             ///< Finger X positions.
    int32_t  touch_pts_y[MAX_TOUCH];             ///< Finger Y positions.
};

// ── Public API ────────────────────────────────────────────────────────────────

/// Deadzone threshold — raw HID stick values below this magnitude are zeroed.
static constexpr int32_t STICK_DEADZONE = 4500;

/// Pump one frame of input from libnx HID into out_frame.
/// state is updated in place.
///
/// On the Switch target: reads hidGetTouchScreenStates and hidGetNpadStates.
/// SP3-F05: uses NpadStyleTag_Handheld | NpadStyleTag_JoyDual | NpadStyleTag_FullKey.
/// SP3-F06: (t.attributes & HidTouchAttribute_Start) != 0.
/// SP3-F07: touch events appended via insert(events.end(), ...).
void pump_input(InputState &state, PolledFrame &out_frame);

// ── Host-testable helpers (also called internally by pump_input) ──────────────

/// Process one frame of raw touch data and append events to out.
/// Exposed so host unit tests can exercise the state machine without libnx.
///
/// raw_count: number of fingers on screen this frame (0 = no touch).
/// tx, ty: position of finger 0 (ignored when raw_count == 0).
/// start_flag: HidTouchAttribute_Start was set for finger 0.
/// end_flag:   HidTouchAttribute_End was set for finger 0.
/// touch_now_active_out: receives the updated prev_touch_active value.
void input_process_touch_frame(
    InputState &state,
    uint32_t raw_count,
    int32_t tx, int32_t ty,
    bool start_flag, bool end_flag,
    PolledFrame &out,
    bool &touch_now_active_out);

/// Process one frame of raw button data and append edge events to out.
/// Exposed so host unit tests can exercise edge detection without libnx.
///
/// cur: bitmask of currently held buttons (test-assigned bit positions).
/// Bit layout for tests (same order as InputEvent::Kind):
///   bit 0=Left, 1=Right, 2=Up, 3=Down, 4=A, 5=B, 6=X, 7=Y,
///   8=RClick, 9=ZLClick, 10=LBPress, 11=RBPress, 12=Plus, 13=Minus.
void input_process_button_frame(
    InputState &state,
    uint64_t cur,
    PolledFrame &out);

} // namespace ul::menu::qdesktop
