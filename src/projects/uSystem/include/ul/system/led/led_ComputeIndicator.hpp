#pragma once

// ---------------------------------------------------------------------------
// UL_COMPUTE_LED — JoyCon notification-LED heartbeat for uSystem.
//
// When enabled (UL_COMPUTE_LED=1, the default), every MainLoop iteration
// calls led::Tick().  The LED pattern changes once per iteration while
// uSystem is alive, giving the creator a physical "alive/computing" signal
// on the JoyCon player lights.  A frozen LED = uSystem's MainLoop has hung.
//
// Pattern design:
//   Idle/normal:  slow dim pulse (1 Hz blink, intensity 0x4)
//   Busy:         faster bright flash (4 Hz, intensity 0xF)
// The pattern is pushed to hidsys at most every UL_LED_IPC_INTERVAL_MS
// (default 250 ms) to keep IPC overhead negligible on the ~10 ms loop.
//
// To disable entirely, define UL_COMPUTE_LED=0 before including this header,
// or remove the three hook lines from main.cpp.
// ---------------------------------------------------------------------------

#ifndef UL_COMPUTE_LED
#define UL_COMPUTE_LED 1
#endif

namespace ul::system::led {

#if UL_COMPUTE_LED

    /// Call once at uSystem startup (after hidsys is initializable).
    /// Enumerates JoyCon pad IDs and sets the initial pattern.
    void Initialize();

    /// Call once at uSystem exit (before service teardown).
    /// Sends an OFF pattern to all pads and closes hidsys.
    void Finalize();

    /// Call once per MainLoop iteration.
    /// @param busy  true while a launch/IPC/heavy path is in progress;
    ///              false during normal idle polling.
    /// The function rate-limits the actual hidsys IPC to every
    /// UL_LED_IPC_INTERVAL_MS milliseconds.
    void Tick(bool busy);

#else   // UL_COMPUTE_LED == 0 — compile to nothing

    inline void Initialize() {}
    inline void Finalize()   {}
    inline void Tick(bool)   {}

#endif  // UL_COMPUTE_LED

}  // namespace ul::system::led
