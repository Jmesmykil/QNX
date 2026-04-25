#pragma once
#include <switch.h>

namespace ul::audio {

    // Open the audctl service handle.  Safe to call multiple times; the
    // second and later calls are no-ops that return true immediately.
    bool InitializeSystemVolume();

    // Close the audctl service handle.  Must be called during __appExit.
    void FinalizeSystemVolume();

    // Return the current system master volume in [0.0, 1.0].
    // Internally caches the result for ~250 ms so the render loop can call
    // this freely without hammering IPC every frame.
    // If audctl is unavailable the function returns 1.0 (full volume) so
    // BGM/SFX always play at the intended per-menu level regardless.
    float GetSystemVolume();

}
