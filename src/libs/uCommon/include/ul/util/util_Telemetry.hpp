
#pragma once
#include <switch.h>
#include <cstddef>

// Structured telemetry pipeline for Q OS uMenu.
// v0.21 "bedrock" — ring file + SPSC async drain + tail buffer.
// No RTTI, no exceptions.  Safe to include everywhere.

namespace ul::tel {

    // Subsystem category tag
    enum class Cat : u8 {
        Generic  = 0,
        Qdesktop = 1,
        Hid      = 2,
        Alloc    = 3,
        Render   = 4,
        Stress   = 5,
        Boot     = 6,
    };

    // Severity level
    enum class Sev : u8 {
        Trace = 0,
        Info  = 1,
        Warn  = 2,
        Crit  = 3,
    };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Must be called once per process before any Emit call.
    // Safe to call before full stdlib init; idempotent.
    // Creates /qos-shell/logs/, opens ring file, writes BOOT marker.
    bool Init(const char *proc_name);

    // Returns true once Init() has completed successfully.
    bool IsInitialized();

    // Drain async queue, flush ring file.
    void Flush();

    // Flush, close ring file, join drain thread.
    void Shutdown();

    // -----------------------------------------------------------------------
    // Emit path
    // -----------------------------------------------------------------------

    // Async path: format, timestamp, push to SPSC ring.
    // If ring is full, drops and increments dropped_count.
    void Emit(Cat c, Sev s, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // Synchronous path: format + write + flush immediately.
    // Used for Warn/Crit so the message survives an immediate crash.
    void EmitSync(Cat c, Sev s, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // -----------------------------------------------------------------------
    // Tail buffer (for HUD — v0.22)
    // -----------------------------------------------------------------------

    // Copy the most recent n_lines complete log lines into dst (NUL-terminated).
    // n_lines is clamped to 16.  Returns bytes written (excluding final NUL).
    size_t TailCopy(char *dst, size_t cap, u32 n_lines);

}  // namespace ul::tel

// ---------------------------------------------------------------------------
// Convenience macros — identical call-site syntax to UL_LOG_*
// ---------------------------------------------------------------------------
#define UL_TEL_TRACE(cat, fmt, ...) \
    ::ul::tel::Emit(::ul::tel::Cat::cat, ::ul::tel::Sev::Trace, fmt, ##__VA_ARGS__)

#define UL_TEL_INFO(cat, fmt, ...) \
    ::ul::tel::Emit(::ul::tel::Cat::cat, ::ul::tel::Sev::Info, fmt, ##__VA_ARGS__)

#define UL_TEL_WARN(cat, fmt, ...) \
    ::ul::tel::EmitSync(::ul::tel::Cat::cat, ::ul::tel::Sev::Warn, fmt, ##__VA_ARGS__)

#define UL_TEL_CRIT(cat, fmt, ...) \
    ::ul::tel::EmitSync(::ul::tel::Cat::cat, ::ul::tel::Sev::Crit, fmt, ##__VA_ARGS__)
