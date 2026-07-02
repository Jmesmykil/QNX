// qd_InputInjector.cpp — Thread-safe synthetic input injection for uMenu.
//
// See qd_InputInjector.hpp for the full design notes.
//
// Threading model:
//   • EnqueuePress / EnqueueTouch — called from the socket/command thread.
//   • DrainSynth                  — called from the UI/render thread.
//   Both paths lock mtx_ for the minimum critical section; there is no blocking.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_InputInjector.hpp>
#include <ul/ul_Result.hpp>

// UL_LOG_INFO may not be defined in all build configs (e.g. release builds that
// strip logging).  The same no-op fallback used by qd_DebugServer.cpp keeps
// compilation clean without duplicating a real logger.
#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

// ── Singleton ─────────────────────────────────────────────────────────────────

QdInputInjector g_InputInjector;

// ── EnqueuePress ──────────────────────────────────────────────────────────────

void QdInputInjector::EnqueuePress(u64 btn_mask, int hold_frames) {
    if (hold_frames < 1) hold_frames = 1;

    std::lock_guard<std::mutex> lk(mtx_);
    if (queue_.size() >= kMaxQueue) {
        UL_LOG_WARN("input-injector: queue full — dropping press 0x%lx",
                    static_cast<unsigned long>(btn_mask));
        return;
    }

    InjectionEvent ev;
    ev.kind        = EventKind::Press;
    ev.btn_mask    = btn_mask;
    ev.touch_x     = 0;
    ev.touch_y     = 0;
    ev.hold_frames = hold_frames;
    queue_.push_back(ev);

    UL_LOG_INFO("input-injector: queued press 0x%lx frames=%d",
                static_cast<unsigned long>(btn_mask), hold_frames);
}

// ── EnqueueTouch ─────────────────────────────────────────────────────────────

void QdInputInjector::EnqueueTouch(s32 x, s32 y, int touch_frames) {
    if (touch_frames < 1) touch_frames = 1;

    std::lock_guard<std::mutex> lk(mtx_);
    if (queue_.size() >= kMaxQueue) {
        UL_LOG_WARN("input-injector: queue full — dropping touch (%d,%d)", x, y);
        return;
    }

    InjectionEvent ev;
    ev.kind        = EventKind::Touch;
    ev.btn_mask    = 0;
    ev.touch_x     = x;
    ev.touch_y     = y;
    ev.hold_frames = touch_frames;
    queue_.push_back(ev);

    UL_LOG_INFO("input-injector: queued touch (%d,%d) frames=%d", x, y, touch_frames);
}

// ── DrainSynth ───────────────────────────────────────────────────────────────

SynthInput QdInputInjector::DrainSynth(pu::ui::Layout *lyt) {
    SynthInput out;  // down=0, held=0

    std::lock_guard<std::mutex> lk(mtx_);

    // Promote the next queued event if no event is currently active.
    if (!active_ && !queue_.empty()) {
        active_event_     = queue_.front();
        queue_.pop_front();
        frames_remaining_ = active_event_.hold_frames;
        active_           = true;
    }

    if (!active_) {
        // Nothing to do this frame.
        return out;
    }

    if (active_event_.kind == EventKind::Press) {
        // First frame: button transitions down.
        if (frames_remaining_ == active_event_.hold_frames) {
            out.down |= active_event_.btn_mask;
        }
        // Every active frame: button is held.
        out.held |= active_event_.btn_mask;

    } else {
        // Touch event.  Report the coords so the caller drives HDLS SetTouch this
        // frame (lift is implicit — caller ClearTouch's once touch goes false).
        out.touch = true;
        out.tx = active_event_.touch_x;
        out.ty = active_event_.touch_y;
        // Also drive Plutonium's sim-touch when a layout is supplied (legacy path;
        // skipped when caller passes nullptr + uses HDLS).
        if (lyt != nullptr) {
            // Sim-touch coords feed Plutonium's tch_pos WITHOUT the ScreenFactor
            // scaling hidGetTouchScreenStates applies, but layout elements live in
            // 1920x1080 space — so scale our 1280x720 screen coords by ScreenFactor
            // (the HDLS path above stays raw; the kernel scales that one).
            lyt->SimulateTouchPosition(
                pu::ui::TouchPoint(
                    static_cast<u32>(active_event_.touch_x * pu::ui::render::ScreenFactor),
                    static_cast<u32>(active_event_.touch_y * pu::ui::render::ScreenFactor)));
        }
    }

    // Advance the frame counter; clear the active slot when done.
    --frames_remaining_;
    if (frames_remaining_ <= 0) {
        active_ = false;
    }

    return out;
}

// ── Free-function helpers ─────────────────────────────────────────────────────

void DebugInjectPress(u64 btn_mask, int hold_frames) {
    g_InputInjector.EnqueuePress(btn_mask, hold_frames);
}

void DebugInjectTouch(s32 x, s32 y, int touch_frames) {
    g_InputInjector.EnqueueTouch(x, y, touch_frames);
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
