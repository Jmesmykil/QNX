#include <ul/audio/audio_SystemVolume.hpp>
#include <switch/services/audctl.h>
#include <ul/ul_Result.hpp>

namespace ul::audio {

    namespace {

        bool g_audctl_open = false;

        // Cache: last polled volume value and the tick when it was polled.
        // 250 ms = 250'000'000 ns; armGetSystemTick returns 19.2 MHz ticks
        // on Switch, so 250 ms ≈ 4'800'000 ticks.
        static constexpr u64 CacheTtlTicks = 4'800'000ULL;

        float g_cached_vol   = 1.0f;
        u64   g_cache_stamp  = 0;

    }

    bool InitializeSystemVolume() {
        if(g_audctl_open) {
            return true;
        }
        const Result rc = audctlInitialize();
        if(R_SUCCEEDED(rc)) {
            g_audctl_open  = true;
            g_cached_vol   = 1.0f;
            g_cache_stamp  = 0;
            UL_LOG_INFO("[SystemVolume] audctl opened successfully");
        }
        else {
            UL_LOG_WARN("[SystemVolume] audctlInitialize failed: 0x%X — falling back to full volume", rc);
        }
        return g_audctl_open;
    }

    void FinalizeSystemVolume() {
        if(g_audctl_open) {
            audctlExit();
            g_audctl_open = false;
            UL_LOG_INFO("[SystemVolume] audctl closed");
        }
    }

    float GetSystemVolume() {
        if(!g_audctl_open) {
            return 1.0f;
        }

        const u64 now = armGetSystemTick();
        if((now - g_cache_stamp) < CacheTtlTicks) {
            return g_cached_vol;
        }

        float vol = 1.0f;
        const Result rc = audctlGetSystemOutputMasterVolume(&vol);
        if(R_SUCCEEDED(rc)) {
            // Clamp defensively; the service returns values in [0.0, 1.0]
            // but saturate against unexpected firmware behaviour.
            if(vol < 0.0f) { vol = 0.0f; }
            if(vol > 1.0f) { vol = 1.0f; }
            g_cached_vol  = vol;
            g_cache_stamp = now;
        }
        else {
            UL_LOG_WARN("[SystemVolume] audctlGetSystemOutputMasterVolume failed: 0x%X — returning cached value %.3f", rc, g_cached_vol);
            // Keep the last good value; reset stamp so we retry next poll.
            g_cache_stamp = 0;
        }

        return g_cached_vol;
    }

}
