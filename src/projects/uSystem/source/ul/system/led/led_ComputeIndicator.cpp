// ---------------------------------------------------------------------------
// led_ComputeIndicator.cpp — JoyCon heartbeat LED for uSystem
//
// hidsys API used:
//   hidsysInitialize()                   — open hid:sys session
//   hidsysGetUniquePadIds(...)           — enumerate all connected pads
//   hidsysSetNotificationLedPattern(...) — push pattern to one pad
//   hidsysExit()                         — close session
//
// The notification LED is the player-indicator ring on each JoyCon and the
// pro controller.  hidsysSetNotificationLedPattern takes a
// HidsysNotificationLedPattern struct (see libnx hidsys.h) plus a
// HidsysUniquePadId obtained from hidsysGetUniquePadIds.
//
// Timing: armGetSystemTick() / 19.2 MHz = seconds.  We rate-limit IPC to
// one call per UL_LED_IPC_INTERVAL_MS (250 ms) so the hot ~10 ms MainLoop
// is not dominated by hidsys IPC round-trips.
// ---------------------------------------------------------------------------

#include <ul/system/led/led_ComputeIndicator.hpp>

#if UL_COMPUTE_LED

#include <switch.h>

namespace ul::system::led {

namespace {

    // -----------------------------------------------------------------------
    // Timing constants — 19.2 MHz system tick on Erista.
    // -----------------------------------------------------------------------
    constexpr u64 kTicksPerMs         = 19'200ul;          // 19 200 ticks = 1 ms
    constexpr u64 kIpcIntervalMs      = 250ul;             // push LED at most every 250 ms
    constexpr u64 kIpcIntervalTicks   = kIpcIntervalMs * kTicksPerMs;

    // Re-enumerate pads every 5 seconds so we pick up newly attached JoyCons.
    constexpr u64 kReenumIntervalMs   = 5'000ul;
    constexpr u64 kReenumIntervalTicks = kReenumIntervalMs * kTicksPerMs;

    // -----------------------------------------------------------------------
    // Module state — all pod types, no allocation.
    // -----------------------------------------------------------------------
    static bool      s_initialized         = false;
    static u64       s_last_ipc_tick        = 0;
    static u64       s_last_renum_tick      = 0;
    static u8        s_heartbeat_phase      = 0;     // 0 or 1, toggled each IPC push

    static HidsysUniquePadId s_pad_ids[8]  = {};
    static s32               s_pad_count   = 0;

    // -----------------------------------------------------------------------
    // BuildPattern — fill a HidsysNotificationLedPattern struct.
    //
    // HidsysNotificationLedPattern layout (see libnx hidsys.h):
    //   baseMiniCycleDuration  u8  — 0x0=OFF, 0x1-0xF: 12.5ms .. 187.5ms per step
    //   totalMiniCycles        u8  — number of mini-cycles - 1 (0x0 = 1 cycle)
    //   totalFullCycles        u8  — 0x0 = repeat forever
    //   startIntensity         u8  — 0x0-0xF
    //   miniCycles[16]:
    //     ledIntensity         u8  — target intensity for this mini-cycle
    //     transitionSteps      u8  — PWM fade steps (0x0 = instant)
    //     finalStepDuration    u8  — multiplier on baseMiniCycleDuration for hold
    //     pad                  u8
    //
    // Idle pattern:  two-mini-cycle slow blink: ON(dim) → OFF
    //   baseMiniCycleDuration = 0x8  (8 × 12.5ms = 100ms per step)
    //   totalMiniCycles       = 0x1  (2 mini-cycles)
    //   totalFullCycles       = 0x0  (repeat forever)
    //   startIntensity        = 0x0
    //   mc[0]: intensity=0x3, transition=0x4 (fade up), hold=0x5 (×100ms = 500ms on)
    //   mc[1]: intensity=0x0, transition=0x4 (fade down), hold=0x8 (×100ms = 800ms off)
    //   → ~1.4 Hz dim pulse: visible but calm.
    //
    // Busy pattern:  two-mini-cycle fast bright flash: BRIGHT → OFF
    //   baseMiniCycleDuration = 0x4  (4 × 12.5ms = 50ms per step)
    //   totalMiniCycles       = 0x1  (2 mini-cycles)
    //   totalFullCycles       = 0x0  (repeat forever)
    //   startIntensity        = 0x0
    //   mc[0]: intensity=0xF, transition=0x1 (near-instant), hold=0x2 (×50ms = 100ms on)
    //   mc[1]: intensity=0x0, transition=0x1, hold=0x2 (100ms off)
    //   → ~5 Hz bright strobe: unmistakably "busy".
    //
    // Off pattern:   baseMiniCycleDuration=0x0 → LED stays off.
    // -----------------------------------------------------------------------

    static HidsysNotificationLedPattern BuildIdlePattern() {
        HidsysNotificationLedPattern p = {};
        p.baseMiniCycleDuration = 0x8;   // 100 ms per step
        p.totalMiniCycles       = 0x1;   // 2 mini-cycles (index 0 and 1)
        p.totalFullCycles       = 0x0;   // loop forever
        p.startIntensity        = 0x0;

        // Mini-cycle 0: fade up to dim
        p.miniCycles[0].ledIntensity     = 0x3;   // 20% brightness
        p.miniCycles[0].transitionSteps  = 0x4;   // 4-step PWM fade
        p.miniCycles[0].finalStepDuration = 0x5;  // hold 5×100ms = 500ms

        // Mini-cycle 1: fade down to off
        p.miniCycles[1].ledIntensity     = 0x0;
        p.miniCycles[1].transitionSteps  = 0x4;
        p.miniCycles[1].finalStepDuration = 0x8;  // hold 8×100ms = 800ms
        return p;
    }

    static HidsysNotificationLedPattern BuildBusyPattern() {
        HidsysNotificationLedPattern p = {};
        p.baseMiniCycleDuration = 0x4;   // 50 ms per step
        p.totalMiniCycles       = 0x1;   // 2 mini-cycles
        p.totalFullCycles       = 0x0;   // loop forever
        p.startIntensity        = 0x0;

        // Mini-cycle 0: instant-on at full brightness
        p.miniCycles[0].ledIntensity     = 0xF;   // 100% brightness
        p.miniCycles[0].transitionSteps  = 0x1;   // near-instant
        p.miniCycles[0].finalStepDuration = 0x2;  // hold 2×50ms = 100ms

        // Mini-cycle 1: instant-off
        p.miniCycles[1].ledIntensity     = 0x0;
        p.miniCycles[1].transitionSteps  = 0x1;
        p.miniCycles[1].finalStepDuration = 0x2;  // 100ms off → ~5 Hz
        return p;
    }

    static HidsysNotificationLedPattern BuildOffPattern() {
        // baseMiniCycleDuration=0 → LED off immediately and stays off.
        HidsysNotificationLedPattern p = {};
        return p;
    }

    // Push a pattern to every enumerated pad.  Silently ignores IPC errors
    // (pads may disconnect mid-session; the next re-enum will fix the list).
    static void PushPattern(const HidsysNotificationLedPattern &pattern) {
        for(s32 i = 0; i < s_pad_count; i++) {
            hidsysSetNotificationLedPattern(&pattern, s_pad_ids[i]);
        }
    }

    // Refresh the pad ID list.
    static void ReenumPads() {
        s32 total = 0;
        const Result rc = hidsysGetUniquePadIds(s_pad_ids, 8, &total);
        if(R_SUCCEEDED(rc)) {
            s_pad_count = total;
        }
        // On failure keep the stale list so we don't go dark unexpectedly.
        s_last_renum_tick = armGetSystemTick();
    }

}   // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Initialize() {
    const Result rc = hidsysInitialize();
    if(R_FAILED(rc)) {
        // hidsys is not critical; degrade gracefully.
        s_initialized = false;
        return;
    }
    s_initialized = true;
    s_pad_count   = 0;
    s_pad_ids[0]  = {};
    s_last_ipc_tick   = 0;
    s_last_renum_tick = 0;
    s_heartbeat_phase = 0;

    ReenumPads();

    // Start with the idle pattern immediately.
    const auto idle = BuildIdlePattern();
    PushPattern(idle);
}

void Finalize() {
    if(!s_initialized) { return; }
    // Send an explicit OFF pattern so the LED doesn't stay stuck in a
    // busy-flash state after uSystem exits.
    const auto off = BuildOffPattern();
    PushPattern(off);
    hidsysExit();
    s_initialized = false;
    s_pad_count   = 0;
}

void Tick(bool busy) {
    if(!s_initialized) { return; }

    const u64 now = armGetSystemTick();

    // Re-enumerate pads periodically (picks up newly connected JoyCons).
    if((now - s_last_renum_tick) >= kReenumIntervalTicks) {
        ReenumPads();
    }

    // Rate-limit the hidsys IPC to kIpcIntervalMs.
    if((now - s_last_ipc_tick) < kIpcIntervalTicks) {
        return;
    }
    s_last_ipc_tick = now;

    // Toggle the heartbeat phase so each IPC push sends a FRESH pattern
    // command.  The pattern itself is looping on the JoyCon firmware, but
    // re-issuing the command proves to an observer that this code path ran —
    // if the MainLoop freezes, no new command ever arrives and the phase
    // stops changing (detectable in logs, and the LED will eventually stop
    // restarting the looping pattern, becoming static).
    s_heartbeat_phase ^= 1;

    if(busy) {
        const auto p = BuildBusyPattern();
        PushPattern(p);
    } else {
        const auto p = BuildIdlePattern();
        PushPattern(p);
    }
}

}  // namespace ul::system::led

#endif  // UL_COMPUTE_LED
