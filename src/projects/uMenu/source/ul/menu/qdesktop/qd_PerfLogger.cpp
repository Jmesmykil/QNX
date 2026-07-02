// qd_PerfLogger.cpp — Frame-level CSV performance logger for Q OS uMenu.
// See qd_PerfLogger.hpp for the full design notes.
//
// Performance contract:
//   OnFrameTick(): two armGetSystemTick calls (~10 ns combined), no IPC.
//   WriteRow(): one QdResourceLedger::GetSnapshot() (mutex + memcpy, <1 µs),
//     one fwrite (~5 µs buffered), one conditional fflush.
//   No libnx service IPC in the hot path — hardware stats (cpu_mhz, temps,
//   battery) are zeroed rather than paid per row.  Only ledger counters,
//   frame timing, and RAM (via svcGetInfo — one syscall, ~2 µs) are live.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.

#include <ul/menu/qdesktop/qd_PerfLogger.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/ul_Result.hpp>
#include <switch/kernel/svc.h>   // svcGetInfo / InfoType_UsedMemorySize
#include <switch/arm/counter.h>  // armGetSystemTick
#include <cstdio>
#include <cstring>
#include <sys/stat.h>   // stat() / mkdir()
#include <cerrno>

namespace ul::menu::qdesktop {

// ── Tick constant ─────────────────────────────────────────────────────────────

static constexpr uint64_t kTickHz = 19200000ULL;

static inline double TicksToMs(uint64_t ticks) {
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(kTickHz);
}

// ── Singleton ─────────────────────────────────────────────────────────────────

QdPerfLogger& QdPerfLogger::Instance() {
    static QdPerfLogger s_instance;
    return s_instance;
}

QdPerfLogger::QdPerfLogger() {
    mutexInit(&mtx_);
    pending_event_[0] = '\0';
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────

void QdPerfLogger::Init() {
    mutexLock(&mtx_);
    if (inited_) {
        mutexUnlock(&mtx_);
        return;
    }
    // sdmc:/ is mounted by __appInit before Init() is called.
    // Ensure sdmc:/ulaunch/ exists (idempotent; EEXIST is not an error).
    mkdir("sdmc:/ulaunch", 0777);
    // W10-BUG3: pre-rotate at Init() if the existing file already meets or
    // exceeds the rotation threshold.  The old code initialised bytes_written_
    // from stat().st_size and then deferred rotation to the first WriteRow()
    // call that crossed kRotateBytes.  If a previous session left the file at
    // e.g. 5,190 KB, the threshold was crossed after only ~218 rows (~218 s at
    // 1 Hz), triggering 10 synchronous SD rename() calls on the UI thread and
    // producing a 500–5000 ms stall at exactly that elapsed time.  Rotating
    // eagerly at Init() ensures the session always starts with bytes_written_=0,
    // so mid-session rotation only fires once the CURRENT session has written a
    // full 5 MiB — far beyond any plausible session length.
    {
        struct stat st;
        const size_t existing = (stat(kLogPath, &st) == 0)
                                ? static_cast<size_t>(st.st_size) : 0;
        if (existing >= kRotateBytes) {
            // Perform the rotation synchronously now (once, at startup) rather
            // than deferring it to the hot render path.
            UL_LOG_INFO("qd_PerfLogger: pre-rotating %s (existing=%zu >= %zu)",
                        kLogPath, existing, kRotateBytes);
            for (int i = kRotateMax; i >= 1; --i) {
                char src[256], dst[256];
                if (i == 1) {
                    snprintf(src, sizeof(src), "%s", kLogPath);
                } else {
                    snprintf(src, sizeof(src), "%s.%d", kLogPath, i - 1);
                }
                snprintf(dst, sizeof(dst), "%s.%d", kLogPath, i);
                rename(src, dst);
            }
            fp_ = fopen(kLogPath, "w");
        } else {
            fp_ = fopen(kLogPath, "a");
        }
    }
    if (fp_ == nullptr) {
        UL_LOG_WARN("qd_PerfLogger: fopen(%s) failed — perf logging disabled",
                    kLogPath);
        mutexUnlock(&mtx_);
        return;
    }
    // bytes_written_ always starts from 0 for this session so that
    // RotateIfNeeded() in WriteRow() only fires after a full kRotateBytes of
    // NEW data — never due to a pre-existing large file from a prior session.
    bytes_written_    = 0;
    inited_           = true;
    frame_count_      = 0;
    rows_since_flush_ = 0;
    WriteHeader();
    mutexUnlock(&mtx_);
    UL_LOG_INFO("qd_PerfLogger: initialised -> %s", kLogPath);
}

void QdPerfLogger::Shutdown() {
    mutexLock(&mtx_);
    if (!inited_ || fp_ == nullptr) {
        mutexUnlock(&mtx_);
        return;
    }
    fflush(fp_);
    fclose(fp_);
    fp_     = nullptr;
    inited_ = false;
    UL_LOG_INFO("qd_PerfLogger: shutdown");
    mutexUnlock(&mtx_);
}

// ── Per-frame tick ────────────────────────────────────────────────────────────

void QdPerfLogger::OnFrameBegin() {
    prev_begin_tick_  = frame_begin_tick_;
    frame_begin_tick_ = armGetSystemTick();
    ++frame_count_;
}

void QdPerfLogger::OnFrameEnd() {
    frame_end_tick_ = armGetSystemTick();

    if (!inited_ || fp_ == nullptr) {
        return;
    }

    // W8-FIX Bug 1: verbose mode is capped to kVerboseRateFrames (~10 Hz)
    // to prevent OOM on Switch.  At 60 Hz × ~240 B/row the libc stdio
    // buffer fills in <0.5 s; without periodic fflush the buffer grows
    // until the process exhausts its ~128 MiB heap and panics.  10 Hz is
    // still 6× the normal 1 Hz rate and safe for SD writes (~2.4 KB/s).
    const bool should_write =
        (verbose_ && (frame_count_ % kVerboseRateFrames) == 0)
        || (!verbose_ && period_frames_ > 0
            && (frame_count_ % period_frames_) == 0);

    if (should_write) {
        WriteRow();
    }
}

// ── StampEvent ────────────────────────────────────────────────────────────────

void QdPerfLogger::StampEvent(const char* event) {
    mutexLock(&mtx_);
    if (event != nullptr) {
        strncpy(pending_event_, event, kEventBuf - 1);
        pending_event_[kEventBuf - 1] = '\0';
    } else {
        pending_event_[0] = '\0';
    }
    mutexUnlock(&mtx_);
}

// ── WriteHeader ───────────────────────────────────────────────────────────────

void QdPerfLogger::WriteHeader() {
    // Caller must hold mtx_.
    static const char kHeader[] =
        "frame_idx,uptime_ms,frame_ms,render_ms,input_ms,idle_ms,"
        "cpu_mhz,gpu_mhz,soc_c,pcb_c,ram_used_mib,ram_total_mib,battery_pct,"
        "tex_live,surf_live,svc_live,sess_live,thread_live,file_live,"
        "win_live,snap_live,iconcache_live,sfx_live,"
        "tex_bytes_mib,snap_bytes_mib,iconcache_bytes_mib,"
        "total_live,total_bytes_mib,"
        "event\n";

    const size_t n = sizeof(kHeader) - 1;
    if (fp_ != nullptr) {
        fwrite(kHeader, 1, n, fp_);
        bytes_written_ += n;
    }
}

// ── RotateIfNeeded ────────────────────────────────────────────────────────────

void QdPerfLogger::RotateIfNeeded() {
    // Caller must hold mtx_.
    if (bytes_written_ < kRotateBytes) {
        return;
    }
    if (fp_ != nullptr) {
        fflush(fp_);
        fclose(fp_);
        fp_ = nullptr;
    }
    // Shift: .9 evicted, .8->.9, ..., .1->.2, base->.1
    for (int i = kRotateMax; i >= 1; --i) {
        char src[256], dst[256];
        if (i == 1) {
            snprintf(src, sizeof(src), "%s", kLogPath);
        } else {
            snprintf(src, sizeof(src), "%s.%d", kLogPath, i - 1);
        }
        snprintf(dst, sizeof(dst), "%s.%d", kLogPath, i);
        rename(src, dst);  // best-effort; ignore failure
    }
    fp_ = fopen(kLogPath, "w");
    bytes_written_    = 0;
    rows_since_flush_ = 0;
    if (fp_ != nullptr) {
        WriteHeader();
        UL_LOG_INFO("qd_PerfLogger: log rotated -> %s", kLogPath);
    } else {
        UL_LOG_WARN("qd_PerfLogger: rotation fopen failed");
    }
}

// ── WriteRow ──────────────────────────────────────────────────────────────────

void QdPerfLogger::WriteRow() {
    // ── Timing ────────────────────────────────────────────────────────────────

    const uint64_t now_tick  = frame_end_tick_;
    const double   uptime_ms = static_cast<double>(now_tick) * 1000.0
                               / static_cast<double>(kTickHz);

    // frame_ms: time between consecutive OnFrameBegin calls.
    const double frame_ms = (prev_begin_tick_ != 0)
        ? TicksToMs(frame_begin_tick_ - prev_begin_tick_)
        : 0.0;

    // render_ms: OnFrameBegin to OnFrameEnd (covers render + input path).
    const double render_ms = TicksToMs(now_tick - frame_begin_tick_);

    // ── RAM (svcGetInfo — one syscall, ~2 µs, safe in hot path) ──────────────

    double ram_used_mib = 0.0, ram_total_mib = 0.0;
    {
        u64 used = 0, total = 0;
        if (R_SUCCEEDED(svcGetInfo(&used,  InfoType_UsedMemorySize,
                                   CUR_PROCESS_HANDLE, 0)) &&
            R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize,
                                   CUR_PROCESS_HANDLE, 0))) {
            ram_used_mib  = static_cast<double>(used)  / (1024.0 * 1024.0);
            ram_total_mib = static_cast<double>(total) / (1024.0 * 1024.0);
        }
    }

    // ── Ledger snapshot ───────────────────────────────────────────────────────

    const QdResourceLedger::Snapshot snap =
        QdResourceLedger::Instance().GetSnapshot();

    const auto& tex  = snap.per_kind[static_cast<size_t>(QdResKind::Texture)];
    const auto& surf = snap.per_kind[static_cast<size_t>(QdResKind::Surface)];
    const auto& svc  = snap.per_kind[static_cast<size_t>(QdResKind::Service)];
    const auto& sess = snap.per_kind[static_cast<size_t>(QdResKind::Session)];
    const auto& thr  = snap.per_kind[static_cast<size_t>(QdResKind::Thread)];
    const auto& fh   = snap.per_kind[static_cast<size_t>(QdResKind::FileHandle)];
    const auto& win  = snap.per_kind[static_cast<size_t>(QdResKind::Window)];
    const auto& snp  = snap.per_kind[static_cast<size_t>(QdResKind::MinimizedSnap)];
    const auto& ic   = snap.per_kind[static_cast<size_t>(QdResKind::IconCache)];
    const auto& sfx  = snap.per_kind[static_cast<size_t>(QdResKind::Sfx)];

    const double tex_bytes_mib = static_cast<double>(tex.bytes_live)
                                 / (1024.0 * 1024.0);
    const double snp_bytes_mib = static_cast<double>(snp.bytes_live)
                                 / (1024.0 * 1024.0);
    const double ic_bytes_mib  = static_cast<double>(ic.bytes_live)
                                 / (1024.0 * 1024.0);
    const double tot_bytes_mib = static_cast<double>(snap.total_bytes)
                                 / (1024.0 * 1024.0);

    // ── Acquire lock, consume event, rotate, write ────────────────────────────

    mutexLock(&mtx_);

    char event_col[kEventBuf];
    strncpy(event_col, pending_event_, kEventBuf - 1);
    event_col[kEventBuf - 1] = '\0';
    pending_event_[0] = '\0';

    RotateIfNeeded();

    if (fp_ == nullptr) {
        mutexUnlock(&mtx_);
        return;
    }

    // Row is at most ~240 chars; use a stack buffer.
    char row[256];
    const int n = snprintf(row, sizeof(row),
        "%u,%.3f,%.3f,%.3f,%.3f,%.3f,"
        "%u,%u,%.1f,%.1f,%.2f,%.2f,%u,"
        "%u,%u,%u,%u,%u,%u,"
        "%u,%u,%u,%u,"
        "%.2f,%.2f,%.2f,"
        "%u,%.2f,"
        "%s\n",
        /* timing  */ frame_count_, uptime_ms, frame_ms, render_ms, 0.0, 0.0,
        /* hw      */ 0u, 0u, 0.0, 0.0,
                      ram_used_mib, ram_total_mib, 0u,
        /* ledger  */ tex.count_live,  surf.count_live, svc.count_live,
                      sess.count_live, thr.count_live,  fh.count_live,
                      win.count_live,  snp.count_live,  ic.count_live,
                      sfx.count_live,
        /* bytes   */ tex_bytes_mib, snp_bytes_mib, ic_bytes_mib,
        /* totals  */ snap.total_live, tot_bytes_mib,
        /* event   */ event_col);

    if (n > 0 && static_cast<size_t>(n) < sizeof(row)) {
        // W8-FIX Bug 1: guard write failure — if fwrite returns short
        // (SD card full / removed) disable the logger so we don't loop
        // on a full-card error and accumulate buffer without flushing.
        const size_t written = fwrite(row, 1, static_cast<size_t>(n), fp_);
        if (written < static_cast<size_t>(n)) {
            UL_LOG_WARN("qd_PerfLogger: fwrite short (%zu/%d) — disabling logger",
                        written, n);
            fclose(fp_);
            fp_     = nullptr;
            inited_ = false;
            mutexUnlock(&mtx_);
            return;
        }
        bytes_written_ += written;
    }

    ++rows_since_flush_;
    if (rows_since_flush_ >= kFlushPeriod) {
        fflush(fp_);
        rows_since_flush_ = 0;
    }
    // W8-FIX Bug 1: in verbose mode force-flush every row so the libc
    // stdio buffer never accumulates more than one row's worth of data
    // (~240 bytes).  This is the primary OOM prevention: the buffer is
    // drained to SD immediately rather than growing until ENOMEM.
    if (verbose_) {
        fflush(fp_);
    }

    mutexUnlock(&mtx_);
}

} // namespace ul::menu::qdesktop
