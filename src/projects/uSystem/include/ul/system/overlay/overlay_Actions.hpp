// overlay_Actions.hpp — bridge between the overlay's render-thread input
// polling and uSystem's main loop applet/app actions.
//
// Why a bridge instead of direct calls:
//   • The render thread runs on its own thread (spawned by overlay_TestLayer)
//     and is forbidden from touching am/pm IPC state, because:
//       - uSystem's main thread already serializes all am IPC (LaunchMenu,
//         la::Terminate, app::Terminate, etc.) through the action queue.
//       - Two threads racing on am-side service handles risks the same
//         2011-0102 OutOfSessionMemory we already burned on.
//   • The render thread therefore SETS a flag; the main loop READS + clears
//     the flag and dispatches to the canonical HandleHomeButton path.
//
// Single-producer (render thread) + single-consumer (main loop) so a plain
// std::atomic<bool> is sufficient — no need for a queue or mutex.

#pragma once
#include <atomic>

namespace ul::system::overlay {

    // Set by the overlay render thread on close-X press-edge.
    // Read+cleared by uSystem main loop; calls HandleHomeButton() for now
    // (T3.0).  T3.1 will differentiate: this becomes "terminate foreground".
    extern std::atomic<bool> g_RequestClose;

    // Set by the overlay render thread on minimize press-edge.
    // Read+cleared by uSystem main loop; same HandleHomeButton() path for
    // T3.0.  T3.1 will differentiate: this becomes "suspend / hand to home".
    extern std::atomic<bool> g_RequestMinimize;

}  // namespace ul::system::overlay
