// qd_InputInjector.hpp — Thread-safe synthetic input injection for uMenu.
//
// A socket/command thread calls EnqueuePress() or EnqueueTouch() to queue a
// synthetic event.  The UI/render thread calls DrainSynth() exactly once per
// frame at the top of the input phase.  DrainSynth advances one active event
// at a time, returning the down/held button bits for that frame and (for touch
// events) calling lyt->SimulateTouchPosition() so Plutonium's
// ConsumeSimulatedTouchPosition() picks it up in the same frame.
//
// Queue is bounded to 8 entries; overflow is silently dropped (never blocks).
//
// Free functions DebugInjectPress() / DebugInjectTouch() forward to the
// g_InputInjector singleton and are safe to call from any thread.

#pragma once

#ifdef QDESKTOP_MODE

#include <switch.h>
#include <pu/ui/ui_Layout.hpp>
#include <mutex>
#include <deque>

namespace ul::menu::qdesktop {

// ── Public data types ─────────────────────────────────────────────────────────

/**
 * @brief Synthetic button/held state returned by DrainSynth() for one frame.
 *
 * Matches the (keys_down, keys_held) pair that Plutonium passes to OnInput
 * callbacks, so callers can OR these bits directly into the live input state.
 */
struct SynthInput {
    u64  down  = 0;   ///< Buttons that became pressed THIS frame (first frame of the event).
    u64  held  = 0;   ///< Buttons held this frame (every active frame, incl. the first).
    bool touch = false; ///< true if a synthetic touch is active this frame (drive HDLS SetTouch).
    s32  tx    = 0;   ///< Touch X (layout space, 0..1919) when touch==true.
    s32  ty    = 0;   ///< Touch Y (layout space, 0..1079) when touch==true.
};

// ── Injector class ────────────────────────────────────────────────────────────

class QdInputInjector {
public:
    QdInputInjector() = default;
    ~QdInputInjector() = default;

    QdInputInjector(const QdInputInjector &)            = delete;
    QdInputInjector &operator=(const QdInputInjector &) = delete;

    /**
     * @brief Queue a synthetic button-press event (socket/command thread).
     *
     * The event spans @p hold_frames frames.  On the first active frame the
     * button appears in SynthInput::down; on every active frame (including the
     * first) it appears in SynthInput::held.  If the queue already has 8
     * pending events the new event is silently dropped.
     *
     * @param btn_mask   HidNpadButton bitmask to assert.
     * @param hold_frames Number of frames to hold the button (>= 1).
     */
    void EnqueuePress(u64 btn_mask, int hold_frames);

    /**
     * @brief Queue a synthetic touch event (socket/command thread).
     *
     * While the event is active, DrainSynth() calls
     * lyt->SimulateTouchPosition({touch_x, touch_y}) each frame.  Lift is
     * implicit: once touch_frames expires the injector stops calling
     * SimulateTouchPosition and Plutonium's ConsumeSimulatedTouchPosition
     * returns an empty TouchPoint.  Coordinates are in 1920x1080 layout space.
     * If the queue already has 8 pending events the new event is silently
     * dropped.
     *
     * @param x           X coordinate in layout space (0..1919).
     * @param y           Y coordinate in layout space (0..1079).
     * @param touch_frames Number of frames to hold the touch (>= 1).
     */
    void EnqueueTouch(s32 x, s32 y, int touch_frames);

    /**
     * @brief Advance the injector by one frame and return synthetic input.
     *
     * Must be called exactly once per frame on the UI/render thread, at the
     * top of the input phase (before Plutonium's own input dispatch).
     *
     * If a touch event is active and @p lyt is non-null,
     * lyt->SimulateTouchPosition() is called so Plutonium's
     * ConsumeSimulatedTouchPosition() returns the position this frame.
     *
     * @param lyt  The active Layout, or nullptr (touch injection skipped).
     * @return     SynthInput with down/held bits for this frame.
     */
    SynthInput DrainSynth(pu::ui::Layout *lyt);

private:
    // ── Internal event record ─────────────────────────────────────────────────
    enum class EventKind : u8 { Press, Touch };

    struct InjectionEvent {
        EventKind kind        = EventKind::Press;
        u64       btn_mask    = 0;
        s32       touch_x     = 0;
        s32       touch_y     = 0;
        int       hold_frames = 1;  ///< Original duration — needed to detect "first" frame.
    };

    static constexpr std::size_t kMaxQueue = 8;

    std::mutex                  mtx_;
    std::deque<InjectionEvent>  queue_;     ///< Pending events (bounded to kMaxQueue).

    // One active event at a time.
    bool           active_          = false;
    InjectionEvent active_event_    = {};
    int            frames_remaining_ = 0;
};

// ── Singleton ─────────────────────────────────────────────────────────────────

/// Global injector instance — defined in qd_InputInjector.cpp.
extern QdInputInjector g_InputInjector;

// ── Convenience helpers ───────────────────────────────────────────────────────

/**
 * @brief Forward a synthetic press to g_InputInjector (any thread).
 */
void DebugInjectPress(u64 btn_mask, int hold_frames = 1);

/**
 * @brief Forward a synthetic touch to g_InputInjector (any thread).
 */
void DebugInjectTouch(s32 x, s32 y, int touch_frames = 2);

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
