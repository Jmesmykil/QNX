// qd_Power.cpp — System power management for Q OS uMenu (v0.21+).
//
// Seven entry points exported by qd_Power.hpp:
//   Reboot()                    — clean reboot via bpcRebootSystem
//   Shutdown()                  — clean shutdown via bpcShutdownSystem
//   Sleep()                     — standby via appletStartSleepSequence(true)
//   RebootToHekate()            — stage reboot_payload.bin via bpc:ams, then reboot
//   IsRebootToHekateSupported() — probe bpc:ams once; cache result
//   RebootToHekateUms()         — stage hekate_ums.bin (fallback: reboot_payload.bin);
//                                  returns bool; false = payload absent, no reboot
//   IsRebootToHekateUmsSupported() — delegates to IsRebootToHekateSupported()
//
// Hardware semantics:
//   Reboot / Shutdown        — do not return; the applet is destroyed by the OS.
//   Sleep                    — returns when the console wakes; caller must treat
//                              the post-call frame as a fresh resume.
//   RebootToHekate           — does not return (successful path via bpcRebootSystem).
//                              Falls back to plain reboot on failure; also does
//                              not return on that path.
//
// Host build (no __SWITCH__):
//   All functions log and return immediately — the test harness uses this.

#include <ul/menu/qdesktop/qd_Power.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
#include <switch.h>           // bpc*, appletStartSleepSequence
#include <switch-ipcext.h>    // bpcamsInitialize, bpcamsExit, bpcamsSetRebootPayload
#include <cstdio>             // fopen / fread / fclose
#include <cstdlib>            // malloc / free
#include <cstring>            // memset (not actually used, but included for completeness)
#endif // __SWITCH__

namespace ul::menu::qdesktop::power {

// ── Reboot ────────────────────────────────────────────────────────────────────

void Reboot() {
#ifdef __SWITCH__
    // CFW-SAFETY (2026-06-20): a bare bpcRebootSystem() from the SystemApplet
    // context boots to STOCK OFW (no payload staged) — on Erista that strands the
    // unit at the OFW logo and forces a physical RCM re-inject. The power-menu
    // "Reboot" button calls this, so route it through the payload-staged path so a
    // reboot always returns to CFW via hekate autoboot. RebootToHekate() reads
    // sdmc:/atmosphere/reboot_payload.bin, stages it via bpc:ams, then reboots
    // (same proven path the hot-corner uses); it has its own internal fallback.
    RebootToHekate();
#else
    UL_LOG_INFO("qdesktop: Reboot called on host build (no-op)");
#endif // __SWITCH__
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void Shutdown() {
#ifdef __SWITCH__
    const Result rc = bpcInitialize();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: bpcInitialize failed rc=0x%X — cannot shut down", rc);
        return;
    }
    const Result rc2 = bpcShutdownSystem();
    // bpcShutdownSystem does not return on hardware when it succeeds.
    // We only reach here on failure.
    UL_LOG_WARN("qdesktop: bpcShutdownSystem failed rc=0x%X", rc2);
    bpcExit();
#else
    UL_LOG_INFO("qdesktop: Shutdown called on host build (no-op)");
#endif // __SWITCH__
}

// ── Sleep ─────────────────────────────────────────────────────────────────────
//
// appletStartSleepSequence(true) suspends the console.  The call blocks until
// the hardware wakes up — the caller's next statement executes on resume.
// No bpcInitialize needed: sleep is driven by the applet service, which is
// already open for the lifetime of the uMenu applet.

void Sleep() {
#ifdef __SWITCH__
    UL_LOG_INFO("qdesktop: Sleep entry — entering standby");
    const Result rc = appletStartSleepSequence(true);
    // Reaches here on wake-up (success) or on failure.
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: appletStartSleepSequence failed rc=0x%X", rc);
    } else {
        UL_LOG_INFO("qdesktop: Sleep exit — console resumed from standby");
    }
#else
    UL_LOG_INFO("qdesktop: Sleep called on host build (no-op)");
#endif // __SWITCH__
}

// ── RebootToHekate ────────────────────────────────────────────────────────────
//
// Strategy:
//   1. Read sdmc:/atmosphere/reboot_payload.bin into a heap buffer.
//   2. Call bpcamsInitialize() to connect to the Atmosphère bpc:ams port.
//   3. Call bpcamsSetRebootPayload(buf, size) to stage the payload.
//   4. Call bpcInitialize() + bpcRebootSystem() — the Atmosphère extension
//      intercepts the reboot and chains into the staged payload (Hekate).
//
// Fallback: if any step fails, attempt a plain reboot so the system reaches
// a known state.  The fallback reboot also does not return on hardware.

void RebootToHekate() {
#ifdef __SWITCH__
    static constexpr const char *kPayloadPath = "sdmc:/atmosphere/reboot_payload.bin";

    // ── 1. Open and size the payload file ────────────────────────────────────
    FILE *const fp = fopen(kPayloadPath, "rb");
    if (fp == nullptr) {
        UL_LOG_WARN("qdesktop: RebootToHekate — cannot open %s; falling back to plain reboot",
                    kPayloadPath);
        // Fall through to plain reboot.
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            // Reaches here only on failure.
            bpcExit();
        }
        UL_LOG_WARN("qdesktop: fallback bpcRebootSystem also failed");
        return;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        UL_LOG_WARN("qdesktop: RebootToHekate — fseek(SEEK_END) failed; falling back");
        fclose(fp);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }
    const long raw_size = ftell(fp);
    if (raw_size <= 0) {
        UL_LOG_WARN("qdesktop: RebootToHekate — payload file empty or ftell error (%ld); falling back",
                    raw_size);
        fclose(fp);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }
    rewind(fp);

    const size_t payload_size = static_cast<size_t>(raw_size);

    // ── 2. Read payload into heap buffer ─────────────────────────────────────
    void *const payload_buf = malloc(payload_size);
    if (payload_buf == nullptr) {
        UL_LOG_WARN("qdesktop: RebootToHekate — malloc(%zu) failed; falling back", payload_size);
        fclose(fp);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }

    const size_t bytes_read = fread(payload_buf, 1u, payload_size, fp);
    fclose(fp);

    if (bytes_read != payload_size) {
        UL_LOG_WARN("qdesktop: RebootToHekate — fread partial: got %zu of %zu bytes; falling back",
                    bytes_read, payload_size);
        free(payload_buf);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }

    // ── 3. Connect to bpc:ams and stage the payload ───────────────────────────
    const Result ams_rc = bpcamsInitialize();
    if (R_FAILED(ams_rc)) {
        UL_LOG_WARN("qdesktop: RebootToHekate — bpcamsInitialize failed rc=0x%X; falling back to plain reboot",
                    ams_rc);
        free(payload_buf);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }

    const Result set_rc = bpcamsSetRebootPayload(payload_buf, payload_size);
    bpcamsExit();
    free(payload_buf);

    if (R_FAILED(set_rc)) {
        UL_LOG_WARN("qdesktop: RebootToHekate — bpcamsSetRebootPayload failed rc=0x%X; falling back to plain reboot",
                    set_rc);
        const Result brc = bpcInitialize();
        if (R_SUCCEEDED(brc)) {
            bpcRebootSystem();
            bpcExit();
        }
        return;
    }

    UL_LOG_INFO("qdesktop: RebootToHekate — payload staged (%zu bytes); rebooting to Hekate",
                payload_size);

    // ── 4. Reboot — Atmosphère intercepts and chains to the staged payload ────
    const Result brc = bpcInitialize();
    if (R_FAILED(brc)) {
        UL_LOG_WARN("qdesktop: RebootToHekate — bpcInitialize failed rc=0x%X after payload staged",
                    brc);
        return;
    }
    const Result r = bpcRebootSystem();
    // Does not return on hardware success.
    UL_LOG_WARN("qdesktop: RebootToHekate — bpcRebootSystem failed rc=0x%X", r);
    bpcExit();
#else
    UL_LOG_INFO("qdesktop: RebootToHekate called on host build (no-op)");
#endif // __SWITCH__
}

// ── IsRebootToHekateSupported ─────────────────────────────────────────────────
//
// Probes bpcamsInitialize() once.  Caches the result in a static bool so
// subsequent calls skip the IPC round-trip.  On the host build always returns
// false — the login UI grays out the button.

bool IsRebootToHekateSupported() {
#ifdef __SWITCH__
    // One-time probe: initialise → cache result → exit.
    static bool s_probed   = false;
    static bool s_supported = false;

    if (!s_probed) {
        const Result rc = bpcamsInitialize();
        s_supported = R_SUCCEEDED(rc);
        if (s_supported) {
            bpcamsExit();
        }
        s_probed = true;
    }

    return s_supported;
#else
    return false;
#endif // __SWITCH__
}

// ── RebootToHekateUms ─────────────────────────────────────────────────────────
//
// Strategy:
//   1. Try to open sdmc:/bootloader/payloads/hekate_ums.bin.
//      If absent, fall back to sdmc:/atmosphere/reboot_payload.bin.
//      If both are absent, return false immediately — the caller shows a
//      "payload not found" notification and takes no further action.
//   2. Read the chosen payload into a heap buffer (same fopen/fseek/malloc/fread
//      pattern as RebootToHekate()).
//   3. Stage the payload via bpcamsSetRebootPayload.
//   4. Call bpcRebootSystem() — Atmosphère intercepts and chains to the payload.
//      On hardware success this call does not return; we return true only as a
//      compile-time formality.
//   5. Return false on any IPC failure so the caller can surface the error.

bool RebootToHekateUms() {
#ifdef __SWITCH__
    // ── 1. Resolve the payload path (UMS-specific first, generic Hekate second) ─
    static constexpr const char *kUmsPath     = "sdmc:/bootloader/payloads/hekate_ums.bin";
    static constexpr const char *kFallbackPath = "sdmc:/atmosphere/reboot_payload.bin";

    const char *payload_path = nullptr;
    {
        FILE *probe = fopen(kUmsPath, "rb");
        if (probe != nullptr) {
            fclose(probe);
            payload_path = kUmsPath;
        } else {
            FILE *fallback_probe = fopen(kFallbackPath, "rb");
            if (fallback_probe != nullptr) {
                fclose(fallback_probe);
                payload_path = kFallbackPath;
                UL_LOG_INFO("qdesktop: RebootToHekateUms — UMS payload absent; falling back to %s",
                            kFallbackPath);
            }
        }
    }

    if (payload_path == nullptr) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — neither payload found (%s nor %s); no reboot",
                    kUmsPath, kFallbackPath);
        return false;
    }

    // ── 2. Open and size the payload file ────────────────────────────────────
    FILE *const fp = fopen(payload_path, "rb");
    if (fp == nullptr) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — cannot open %s after probe; no reboot",
                    payload_path);
        return false;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — fseek(SEEK_END) failed on %s; no reboot",
                    payload_path);
        fclose(fp);
        return false;
    }
    const long raw_size = ftell(fp);
    if (raw_size <= 0) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — payload file empty or ftell error (%ld) on %s; no reboot",
                    raw_size, payload_path);
        fclose(fp);
        return false;
    }
    rewind(fp);

    const size_t payload_size = static_cast<size_t>(raw_size);

    // ── 3. Read payload into heap buffer ─────────────────────────────────────
    void *const payload_buf = malloc(payload_size);
    if (payload_buf == nullptr) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — malloc(%zu) failed; no reboot", payload_size);
        fclose(fp);
        return false;
    }

    const size_t bytes_read = fread(payload_buf, 1u, payload_size, fp);
    fclose(fp);

    if (bytes_read != payload_size) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — fread partial: got %zu of %zu bytes from %s; no reboot",
                    bytes_read, payload_size, payload_path);
        free(payload_buf);
        return false;
    }

    // ── 4. Connect to bpc:ams and stage the payload ───────────────────────────
    const Result ams_rc = bpcamsInitialize();
    if (R_FAILED(ams_rc)) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — bpcamsInitialize failed rc=0x%X; no reboot",
                    ams_rc);
        free(payload_buf);
        return false;
    }

    const Result set_rc = bpcamsSetRebootPayload(payload_buf, payload_size);
    bpcamsExit();
    free(payload_buf);

    if (R_FAILED(set_rc)) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — bpcamsSetRebootPayload failed rc=0x%X; no reboot",
                    set_rc);
        return false;
    }

    UL_LOG_INFO("qdesktop: RebootToHekateUms — payload staged (%zu bytes from %s); rebooting to Hekate UMS",
                payload_size, payload_path);

    // ── 5. Reboot — Atmosphère intercepts and chains to the staged UMS payload ─
    const Result brc = bpcInitialize();
    if (R_FAILED(brc)) {
        UL_LOG_WARN("qdesktop: RebootToHekateUms — bpcInitialize failed rc=0x%X after payload staged",
                    brc);
        return false;
    }
    const Result r = bpcRebootSystem();
    // Does not return on hardware success.
    UL_LOG_WARN("qdesktop: RebootToHekateUms — bpcRebootSystem failed rc=0x%X", r);
    bpcExit();
    return false;
#else
    UL_LOG_INFO("qdesktop: RebootToHekateUms called on host build (no-op)");
    return false;
#endif // __SWITCH__
}

// ── IsRebootToHekateUmsSupported ──────────────────────────────────────────────
//
// Delegates directly to IsRebootToHekateSupported(): UMS reboot requires the
// same Atmosphère bpc:ams IPC extension as the plain Hekate reboot.  The
// one-time static probe inside IsRebootToHekateSupported() is reused at zero
// additional IPC cost.

bool IsRebootToHekateUmsSupported() {
    return IsRebootToHekateSupported();
}

} // namespace ul::menu::qdesktop::power
