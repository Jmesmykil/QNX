// qd_ResourceLedger.hpp — Central resource ledger for Q OS uMenu qdesktop.
// Process-singleton tracker for textures, surfaces, services, sessions, threads,
// file handles, windows, minimized snapshots, icon-cache entries, and SFX chunks.
//
// Usage:
//   uint64_t h = UL_LEDGER_TRACK(QdResKind::Texture, "my_tex", w*h*4);
//   // ... use resource ...
//   UL_LEDGER_UNTRACK(h);
//
// The ledger is INSTRUMENTATION ONLY — it does not own resources.  Every call
// site that does not add Track/Untrack continues to work unchanged.
//
// Thread-safety: a single libnx Mutex guards all internal state.  Track/Untrack
// are safe to call from the main thread, prewarm thread, audio thread, etc.
//
// Bounded storage: max QD_LEDGER_CAP_PER_KIND (1024) live entries per kind.
// Overflow logs a UL_LOG_WARN and returns handle=0; Untrack(0) is a no-op.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.
#pragma once
#include <switch.h>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ul::menu::qdesktop {

// ── Resource categories ────────────────────────────────────────────────────────

enum class QdResKind : uint8_t {
    Texture       = 0,  ///< SDL_Texture* (Plutonium + raw SDL)
    Surface       = 1,  ///< SDL_Surface*
    Service       = 2,  ///< libnx *Initialize / *Exit
    Session       = 3,  ///< libnx open*Session / *Close (ts/clkrst/applet/etc.)
    Thread        = 4,  ///< libnx threadCreate / threadClose
    FileHandle    = 5,  ///< fopen / fclose
    Window        = 6,  ///< QdWindow instances tracked by manager
    MinimizedSnap = 7,  ///< QdMinimizedDockEntry snapshots
    IconCache     = 8,  ///< QdIconCache / QdNsIconCache entries
    Sfx           = 9,  ///< pu::audio Mix_Chunk
    Count         = 10
};

// Maximum live tracked entries per kind before overflow warning + drop.
static constexpr size_t QD_LEDGER_CAP_PER_KIND = 1024;

// ── QdResourceLedger ──────────────────────────────────────────────────────────

class QdResourceLedger {
public:
    // ── Process-singleton ─────────────────────────────────────────────────────
    static QdResourceLedger& Instance();

    // ── Track / Untrack ───────────────────────────────────────────────────────

    /// Begin tracking a resource.  Returns an opaque handle (> 0 on success,
    /// 0 on overflow — caller must check before storing).
    /// `tag`  — short human label shown in Monitor (≤31 chars + NUL).
    /// `bytes` — best-effort size hint; 0 if unknown.
    /// Prefer the UL_LEDGER_TRACK macro; `file` and `line` are captured automatically.
    uint64_t Track(QdResKind kind, const char* tag, size_t bytes,
                   const char* file, int line);

    /// Stop tracking a previously returned handle.  Untrack(0) is a no-op.
    void Untrack(uint64_t handle);

    /// Update the byte hint after initial Track (e.g. texture size known post-creation).
    void UpdateBytes(uint64_t handle, size_t new_bytes);

    // ── Snapshot API (for Monitor rendering) ─────────────────────────────────

    struct CategoryStat {
        uint32_t count_live;        ///< currently tracked
        uint32_t count_total_ever;  ///< cumulative since boot
        size_t   bytes_live;        ///< sum of byte hints for live entries
        size_t   bytes_total_ever;  ///< cumulative
    };

    struct Snapshot {
        CategoryStat per_kind[static_cast<size_t>(QdResKind::Count)];
        uint32_t     total_live;
        size_t       total_bytes;
        uint64_t     uptime_sec;    ///< svcGetSystemTick / 19.2 MHz
    };

    /// Returns a by-value copy — safe to read outside the lock.
    Snapshot GetSnapshot();

    // ── Recent-entry API (for Monitor expanded view) ──────────────────────────

    struct Entry {
        uint64_t  handle;
        QdResKind kind;
        char      tag[32];
        size_t    bytes;
        uint64_t  alloc_tick;   ///< armGetSystemTick at Track() time
        const char* file;       ///< source file (string-literal pointer)
        int       line;
    };

    /// Fill `out` with up to `max` live entries for `kind`, newest-first.
    void GetRecent(QdResKind kind, std::vector<Entry>& out, size_t max);

    // ── Debug dump ────────────────────────────────────────────────────────────

    /// Dump all live entries to UL_LOG_INFO with "ledger" tag.
    void DumpLive() const;

private:
    QdResourceLedger();
    ~QdResourceLedger() = default;

    // Non-copyable, non-movable.
    QdResourceLedger(const QdResourceLedger&) = delete;
    QdResourceLedger& operator=(const QdResourceLedger&) = delete;

    // ── Internal types ────────────────────────────────────────────────────────

    struct InternalEntry {
        uint64_t  handle;
        QdResKind kind;
        char      tag[32];
        size_t    bytes;
        uint64_t  alloc_tick;
        const char* file;
        int       line;
    };

    // ── State ─────────────────────────────────────────────────────────────────

    Mutex    mutex_;
    uint64_t next_handle_;  ///< monotonically increasing; 0 is the null sentinel

    // Per-kind live-entry storage.  All entries in entries_[k] are for kind k.
    std::vector<InternalEntry> entries_[static_cast<size_t>(QdResKind::Count)];
    uint32_t count_live_[static_cast<size_t>(QdResKind::Count)];
    uint32_t count_ever_[static_cast<size_t>(QdResKind::Count)];
    size_t   bytes_live_[static_cast<size_t>(QdResKind::Count)];
    size_t   bytes_ever_[static_cast<size_t>(QdResKind::Count)];
};

// ── Convenience macros ─────────────────────────────────────────────────────────

#define UL_LEDGER_TRACK(kind, tag, bytes) \
    ::ul::menu::qdesktop::QdResourceLedger::Instance().Track( \
        (kind), (tag), (bytes), __FILE__, __LINE__)

#define UL_LEDGER_UNTRACK(handle) \
    ::ul::menu::qdesktop::QdResourceLedger::Instance().Untrack(handle)

} // namespace ul::menu::qdesktop
