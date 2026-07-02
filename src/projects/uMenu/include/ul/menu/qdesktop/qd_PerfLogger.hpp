// qd_PerfLogger.hpp — Frame-level CSV performance logger for Q OS uMenu.
//
// Writes one CSV row every `period_frames_` frames (default 60, ~1 s at 60 fps)
// to sdmc:/ulaunch/perf-log.csv.  In verbose mode a row is written every frame.
//
// CSV schema (header written on Init or after rotation):
//   frame_idx, uptime_ms, frame_ms, render_ms, input_ms, idle_ms,
//   cpu_mhz, gpu_mhz, soc_c, pcb_c, ram_used_mib, ram_total_mib, battery_pct,
//   tex_live, surf_live, svc_live, sess_live, thread_live, file_live,
//   win_live, snap_live, iconcache_live, sfx_live,
//   tex_bytes_mib, snap_bytes_mib, iconcache_bytes_mib,
//   total_live, total_bytes_mib,
//   event
//
// Rotation: when the file exceeds kRotateBytes (5 MiB), it is moved to
//   perf-log.csv.1 and a fresh file is opened.  .1 through .9 rotate in a
//   chain; .9 is evicted.
//
// Performance contract:
//   OnFrameBegin / OnFrameEnd each call armGetSystemTick() once (~5 ns).
//   WriteRow() is called at most once per period_frames_ frames outside verbose
//   mode; the write is a single fwrite to the stdio buffer + fflush every 60
//   rows.  Mutex hold is < 1 µs on the uncontended main-thread path.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.

#pragma once

#include <switch.h>
#include <cstdio>
#include <cstdint>
#include <cstddef>

namespace ul::menu::qdesktop {

// ── QdPerfLogger ──────────────────────────────────────────────────────────────

class QdPerfLogger {
public:
    // ── Process-singleton ──────────────────────────────────────────────────────
    static QdPerfLogger& Instance();

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Open the CSV file and write the header line.
    /// Safe to call multiple times; no-op if already initialised.
    /// Call after fsdevMountSdmc() and before the main render loop.
    void Init();

    /// Flush and close the CSV file.  Safe to call even if Init() was never
    /// called.  Called from __appExit.
    void Shutdown();

    // ── Per-frame hooks ────────────────────────────────────────────────────────

    /// Call at the START of each frame (before input + render).
    /// Records the begin tick and increments the frame counter.
    void OnFrameBegin();

    /// Call at the END of each frame (after render, before swap).
    /// Records the end tick; writes a CSV row if the sampling period has
    /// elapsed (or every frame in verbose mode).
    void OnFrameEnd();

    // ── Verbose mode ──────────────────────────────────────────────────────────

    /// When verbose mode is on, a row is written every frame.
    void SetVerbose(bool on) { verbose_ = on; }
    bool IsVerbose() const   { return verbose_; }

    // ── Event stamping ────────────────────────────────────────────────────────

    /// Stamp an event label into the `event` column of the NEXT written row.
    /// The string is copied (≤30 chars + NUL); excess is silently truncated.
    void StampEvent(const char* event);

private:
    QdPerfLogger();
    ~QdPerfLogger() = default;

    // Non-copyable, non-movable.
    QdPerfLogger(const QdPerfLogger&)            = delete;
    QdPerfLogger& operator=(const QdPerfLogger&) = delete;

    // ── Helpers ────────────────────────────────────────────────────────────────

    void WriteRow();
    void WriteHeader();
    void RotateIfNeeded();

    // ── File ──────────────────────────────────────────────────────────────────

    static constexpr size_t kRotateBytes = 5u * 1024u * 1024u;  // 5 MiB
    static constexpr int    kRotateMax   = 9;                    // .1 through .9
    static constexpr const char* kLogPath = "sdmc:/ulaunch/perf-log.csv";

    FILE*  fp_            = nullptr;
    bool   inited_        = false;
    size_t bytes_written_ = 0;
    Mutex  mtx_;

    // ── Sampling state ────────────────────────────────────────────────────────

    uint64_t frame_begin_tick_   = 0;
    uint64_t frame_end_tick_     = 0;
    uint64_t prev_begin_tick_    = 0;  ///< begin tick of the previous frame
    uint32_t frame_count_        = 0;  ///< monotonically increasing frame index
    uint32_t period_frames_      = 60; ///< write a row every N frames
    bool     verbose_            = false;

    // Flush counter — fflush every kFlushPeriod rows to amortise syscall cost.
    static constexpr uint32_t kFlushPeriod = 60;
    uint32_t rows_since_flush_   = 0;

    // W8-FIX Bug 1: verbose mode is throttled to 1-in-N frames to prevent
    // OOM.  6 frames → ~10 Hz (still 6× the normal 1 Hz rate).  An
    // unconditional per-row fflush is also added in WriteRow() so the libc
    // buffer never grows beyond one row's worth of data.
    static constexpr uint32_t kVerboseRateFrames = 6;

    // ── Pending event ─────────────────────────────────────────────────────────

    static constexpr size_t kEventBuf = 32;
    char pending_event_[kEventBuf] = {};
};

} // namespace ul::menu::qdesktop
