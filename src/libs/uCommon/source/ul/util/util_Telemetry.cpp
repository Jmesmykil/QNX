#include <ul/util/util_Telemetry.hpp>
#include <ul/util/util_RingFile.hpp>
#include <switch.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Compile-time constants
// ---------------------------------------------------------------------------
namespace {

    constexpr size_t SlotSize      = 512;          // bytes per SPSC slot
    constexpr u32    QueueCapacity = 4096;          // slots in the SPSC ring
    constexpr size_t RingSegBytes  = 512 * 1024;   // 512 KB per segment
    constexpr u8     RingSegCount  = 4;

    constexpr size_t TailLines     = 16;
    constexpr size_t TailLineLen   = 256;

    constexpr s64    DrainSleepNs  = 8'000'000LL;  // 8 ms

    constexpr size_t DrainStackSz  = 64 * 1024;    // 64 KB drain thread stack

    constexpr const char LogBaseDir[] = "sdmc:/qos-shell/logs";

}  // anonymous namespace

// ---------------------------------------------------------------------------
// SPSC bounded queue
// ---------------------------------------------------------------------------
namespace {

    // One message slot in the ring.
    struct Slot {
        char data[SlotSize];
        u32  len;   // bytes used in data (not including the trailing '\n')
    };

    // Storage for the queue — 4096 × 512 bytes = 2 MB.  Static so it lives
    // in BSS (not the stack).
    static Slot        g_queue[QueueCapacity];
    static std::atomic<u32> g_head{0};   // producer writes here
    static std::atomic<u32> g_tail{0};   // consumer reads here

    // Returns true if the slot was enqueued, false if the ring was full.
    bool QueuePush(const char *data, u32 len) {
        const u32 h    = g_head.load(std::memory_order_relaxed);
        const u32 next = (h + 1) % QueueCapacity;
        if(next == g_tail.load(std::memory_order_acquire)) {
            return false;  // full
        }
        Slot &slot = g_queue[h];
        if(len >= SlotSize) {
            len = SlotSize - 1;
        }
        memcpy(slot.data, data, len);
        slot.len = len;
        g_head.store(next, std::memory_order_release);
        return true;
    }

    // Returns true if a slot was consumed and copied into out_data/out_len.
    bool QueuePop(char *out_data, u32 &out_len) {
        const u32 t = g_tail.load(std::memory_order_relaxed);
        if(t == g_head.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        const Slot &slot = g_queue[t];
        out_len = slot.len;
        if(out_len >= SlotSize) {
            out_len = SlotSize - 1;
        }
        memcpy(out_data, slot.data, out_len);
        g_tail.store((t + 1) % QueueCapacity, std::memory_order_release);
        return true;
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Tail buffer — 16 × 256 bytes, written by both Emit and EmitSync
// ---------------------------------------------------------------------------
namespace {

    static char      g_tail_buf[TailLines][TailLineLen];
    static std::atomic<u32> g_tail_head{0};  // next slot to write (wraps)

    void TailPush(const char *line, size_t len) {
        const u32 slot = g_tail_head.fetch_add(1, std::memory_order_relaxed) % TailLines;
        if(len >= TailLineLen) {
            len = TailLineLen - 1;
        }
        memcpy(g_tail_buf[slot], line, len);
        g_tail_buf[slot][len] = '\0';
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
namespace {

    static ul::util::RingFile g_ring;
    static Mutex              g_ring_lock;

    static std::atomic<bool>  g_initialized{false};
    static std::atomic<bool>  g_shutdown{false};
    static std::atomic<u64>   g_dropped{0};

    static Thread g_drain_thread;
    static u8 g_drain_stack[DrainStackSz] __attribute__((aligned(4096)));

    // Filled in on Init
    static char g_proc_name[64];

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {

    // Map Cat → short ASCII tag (fixed 4 chars, NUL-terminated)
    const char *CatTag(ul::tel::Cat c) {
        switch(c) {
            case ul::tel::Cat::Generic:  return "GENR";
            case ul::tel::Cat::Qdesktop: return "QDSK";
            case ul::tel::Cat::Hid:      return "HID ";
            case ul::tel::Cat::Alloc:    return "ALOC";
            case ul::tel::Cat::Render:   return "RNDR";
            case ul::tel::Cat::Stress:   return "STRS";
            case ul::tel::Cat::Boot:     return "BOOT";
            default:                     return "????";
        }
    }

    const char *SevTag(ul::tel::Sev s) {
        switch(s) {
            case ul::tel::Sev::Trace: return "TRACE";
            case ul::tel::Sev::Info:  return "INFO ";
            case ul::tel::Sev::Warn:  return "WARN ";
            case ul::tel::Sev::Crit:  return "CRIT ";
            default:                  return "???? ";
        }
    }

    // Format a complete log line into out_buf (max out_cap bytes).
    // Returns bytes written, NOT including a trailing '\n'.
    u32 FormatLine(char *out_buf, size_t out_cap,
                   ul::tel::Cat c, ul::tel::Sev s,
                   const char *fmt, va_list args) {
        // Timestamp: try time service; fall back to 0
        u64 posix_ts = 0;
        timeGetCurrentTime(TimeType_UserSystemClock, &posix_ts);

        char msg[480];
        vsnprintf(msg, sizeof(msg), fmt, args);
        msg[sizeof(msg) - 1] = '\0';

        const int written = snprintf(out_buf, out_cap,
            "[%llu][%s][%s] %s",
            static_cast<unsigned long long>(posix_ts),
            SevTag(s), CatTag(c), msg);

        if(written <= 0) {
            return 0;
        }
        const u32 len = static_cast<u32>(written);
        return (len < static_cast<u32>(out_cap)) ? len : static_cast<u32>(out_cap) - 1;
    }

    // Write a line directly to g_ring (must be called with g_ring_lock held
    // or from Init before the drain thread starts).
    void WriteLineToRing(const char *line, u32 len) {
        // Append newline
        char nl_buf[SlotSize + 2];
        if(len >= SlotSize) {
            len = SlotSize - 1;
        }
        memcpy(nl_buf, line, len);
        nl_buf[len]     = '\n';
        nl_buf[len + 1] = '\0';
        g_ring.Write(nl_buf, len + 1);
    }

    // Read or create the boot sequence counter stored in
    // /qos-shell/logs/<proc>.bootseq (single decimal line).
    u32 ReadIncrementBootSeq(const char *proc) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.bootseq", LogBaseDir, proc);

        u32 seq = 0;
        FILE *f = fopen(path, "r");
        if(f != nullptr) {
            fscanf(f, "%u", &seq);
            fclose(f);
        }
        seq++;

        f = fopen(path, "w");
        if(f != nullptr) {
            fprintf(f, "%u\n", seq);
            fclose(f);
        }
        return seq;
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Drain thread
// ---------------------------------------------------------------------------
namespace {

    void DrainThreadFunc(void * /*arg*/) {
        char slot_data[SlotSize];
        u32  slot_len = 0;

        while(!g_shutdown.load(std::memory_order_acquire)) {
            bool did_work = false;
            while(QueuePop(slot_data, slot_len)) {
                mutexLock(&g_ring_lock);
                g_ring.Write(slot_data, slot_len);
                // write the newline that was stripped for the tail buffer
                g_ring.Write("\n", 1);
                mutexUnlock(&g_ring_lock);
                did_work = true;
            }
            if(!did_work) {
                svcSleepThread(DrainSleepNs);
            }
        }

        // Final drain after shutdown flag is set
        while(QueuePop(slot_data, slot_len)) {
            mutexLock(&g_ring_lock);
            g_ring.Write(slot_data, slot_len);
            g_ring.Write("\n", 1);
            mutexUnlock(&g_ring_lock);
        }

        threadExit();
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace ul::tel {

    bool Init(const char *proc_name) {
        if(g_initialized.load(std::memory_order_acquire)) {
            return true;
        }

        strncpy(g_proc_name, proc_name, sizeof(g_proc_name) - 1);
        g_proc_name[sizeof(g_proc_name) - 1] = '\0';

        // Create log directory.  We try even if the FS may not be mounted
        // yet — the ring file Open() will fail gracefully in that case.
        {
            // Ensure parent sdmc:/qos-shell exists
            struct stat st;
            if(stat("sdmc:/qos-shell", &st) != 0) {
                mkdir("sdmc:/qos-shell", 0777);
            }
            if(stat(LogBaseDir, &st) != 0) {
                mkdir(LogBaseDir, 0777);
            }
        }

        g_ring.Configure(LogBaseDir, proc_name, RingSegBytes, RingSegCount);
        if(!g_ring.Open()) {
            // Ring file unavailable — mark as disabled so Emit is a no-op
            // rather than crashing.  g_initialized stays false.
            return false;
        }

        mutexInit(&g_ring_lock);

        // Boot sequence marker (synchronous, before drain thread)
        const u32 boot_seq = ReadIncrementBootSeq(proc_name);

        {
            char boot_line[256];
            u64 ts = 0;
            timeGetCurrentTime(TimeType_UserSystemClock, &ts);
            snprintf(boot_line, sizeof(boot_line),
                "[%llu][BOOT ][BOOT] seq=%u proc=%s\n",
                static_cast<unsigned long long>(ts),
                boot_seq, proc_name);
            g_ring.Write(boot_line, strlen(boot_line));
            g_ring.Flush();

            // Also seed tail buffer
            TailPush(boot_line, strlen(boot_line));
        }

        // Start drain thread at priority 0x30 (below main 0x2C so we don't
        // starve the app; higher than idle so we drain quickly).
        g_shutdown.store(false, std::memory_order_release);
        const Result thread_rc = threadCreate(&g_drain_thread,
            DrainThreadFunc, nullptr,
            g_drain_stack, sizeof(g_drain_stack),
            0x30, -2);
        if(R_SUCCEEDED(thread_rc)) {
            threadStart(&g_drain_thread);
        }
        // If thread creation fails we still continue — Emit will just queue
        // messages that never drain.  Flush() drains synchronously if called.

        g_initialized.store(true, std::memory_order_release);
        return true;
    }

    bool IsInitialized() {
        return g_initialized.load(std::memory_order_acquire);
    }

    void Emit(Cat c, Sev s, const char *fmt, ...) {
        if(!g_initialized.load(std::memory_order_acquire)) {
            return;
        }

        char line[SlotSize];
        va_list args;
        va_start(args, fmt);
        const u32 len = FormatLine(line, sizeof(line), c, s, fmt, args);
        va_end(args);

        if(len == 0) {
            return;
        }

        // Feed tail buffer (without newline)
        TailPush(line, len);

        // Push to SPSC queue (drain thread writes to ring file)
        if(!QueuePush(line, len)) {
            g_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void EmitSync(Cat c, Sev s, const char *fmt, ...) {
        if(!g_initialized.load(std::memory_order_acquire)) {
            return;
        }

        char line[SlotSize];
        va_list args;
        va_start(args, fmt);
        const u32 len = FormatLine(line, sizeof(line), c, s, fmt, args);
        va_end(args);

        if(len == 0) {
            return;
        }

        TailPush(line, len);

        mutexLock(&g_ring_lock);
        WriteLineToRing(line, len);
        g_ring.Flush();
        mutexUnlock(&g_ring_lock);
    }

    void Flush() {
        if(!g_initialized.load(std::memory_order_acquire)) {
            return;
        }

        // Drain the async queue into the ring file synchronously
        char slot_data[SlotSize];
        u32  slot_len = 0;
        while(QueuePop(slot_data, slot_len)) {
            mutexLock(&g_ring_lock);
            g_ring.Write(slot_data, slot_len);
            g_ring.Write("\n", 1);
            mutexUnlock(&g_ring_lock);
        }

        mutexLock(&g_ring_lock);
        g_ring.Flush();
        mutexUnlock(&g_ring_lock);
    }

    void Shutdown() {
        if(!g_initialized.load(std::memory_order_acquire)) {
            return;
        }

        g_shutdown.store(true, std::memory_order_release);
        // Give drain thread a chance to finish naturally
        threadWaitForExit(&g_drain_thread);
        threadClose(&g_drain_thread);

        // Final sync flush
        Flush();

        mutexLock(&g_ring_lock);
        g_ring.Close();
        mutexUnlock(&g_ring_lock);

        g_initialized.store(false, std::memory_order_release);
    }

    size_t TailCopy(char *dst, size_t cap, u32 n_lines) {
        if(dst == nullptr || cap == 0) {
            return 0;
        }

        if(n_lines == 0) {
            dst[0] = '\0';
            return 0;
        }
        if(n_lines > static_cast<u32>(TailLines)) {
            n_lines = static_cast<u32>(TailLines);
        }

        // The tail buffer is a circular array; g_tail_head points to the NEXT
        // slot to be written (i.e., the oldest is at (head - n_lines) mod 16).
        const u32 head = g_tail_head.load(std::memory_order_relaxed);

        size_t written = 0;
        for(u32 i = 0; i < n_lines; i++) {
            const u32 slot = (head - n_lines + i) % TailLines;
            const char *line = g_tail_buf[slot];
            const size_t line_len = strnlen(line, TailLineLen - 1);
            if(line_len == 0) {
                continue;
            }
            if(written + line_len + 2 >= cap) {
                break;  // no space left
            }
            memcpy(dst + written, line, line_len);
            written += line_len;
            dst[written++] = '\n';
        }

        dst[written] = '\0';
        return written;
    }

}  // namespace ul::tel
