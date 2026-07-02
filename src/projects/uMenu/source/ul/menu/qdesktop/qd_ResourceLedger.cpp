// qd_ResourceLedger.cpp — Central resource ledger implementation.
// See qd_ResourceLedger.hpp for design contract.
//
// Thread-safety: a single libnx Mutex (mutexLock / mutexUnlock) guards all
// internal state.  GetSnapshot() copies under the lock and returns by value
// so the render path never holds the lock during drawing.
//
// Bounded storage: max QD_LEDGER_CAP_PER_KIND live entries per kind.  Overflow
// logs UL_LOG_WARN and returns handle=0.  Untrack(0) is a no-op.
//
// GPL-2 — Q OS uMenu qdesktop subsystem.
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/ul_Result.hpp>    // UL_LOG_INFO, UL_LOG_WARN
#include <switch.h>            // Mutex, armGetSystemTick, svcGetSystemTick
#include <cstring>
#include <algorithm>

// Tegra X1 system-tick frequency (Hz) — matches QD_MONITOR_TICK_HZ.
static constexpr uint64_t kLedgerTickHz = 19200000ULL;

namespace ul::menu::qdesktop {

// ── Singleton ─────────────────────────────────────────────────────────────────

QdResourceLedger& QdResourceLedger::Instance() {
    static QdResourceLedger s_instance;
    return s_instance;
}

// ── Constructor ───────────────────────────────────────────────────────────────

QdResourceLedger::QdResourceLedger() : next_handle_(1) {
    mutexInit(&mutex_);
    for (size_t k = 0; k < static_cast<size_t>(QdResKind::Count); ++k) {
        count_live_[k] = 0;
        count_ever_[k] = 0;
        bytes_live_[k] = 0;
        bytes_ever_[k] = 0;
        // Pre-reserve a small block to amortize the first few insertions.
        entries_[k].reserve(16);
    }
}

// ── Track ─────────────────────────────────────────────────────────────────────

uint64_t QdResourceLedger::Track(QdResKind kind, const char* tag, size_t bytes,
                                  const char* file, int line) {
    const size_t ki = static_cast<size_t>(kind);
    if (ki >= static_cast<size_t>(QdResKind::Count)) {
        UL_LOG_WARN("ledger: Track: invalid kind %u", (unsigned)ki);
        return 0;
    }

    mutexLock(&mutex_);

    // Overflow guard.
    if (count_live_[ki] >= static_cast<uint32_t>(QD_LEDGER_CAP_PER_KIND)) {
        mutexUnlock(&mutex_);
        UL_LOG_WARN("ledger: overflow kind=%u cap=%zu tag=%s — entry dropped",
                    (unsigned)ki, QD_LEDGER_CAP_PER_KIND,
                    tag ? tag : "(null)");
        return 0;
    }

    const uint64_t handle = next_handle_++;

    InternalEntry e;
    e.handle     = handle;
    e.kind       = kind;
    e.bytes      = bytes;
    e.alloc_tick = armGetSystemTick();
    e.file       = file;
    e.line       = line;
    // Copy tag, always NUL-terminated.
    if (tag != nullptr) {
        strncpy(e.tag, tag, sizeof(e.tag) - 1);
        e.tag[sizeof(e.tag) - 1] = '\0';
    } else {
        e.tag[0] = '\0';
    }

    entries_[ki].push_back(e);
    ++count_live_[ki];
    ++count_ever_[ki];
    bytes_live_[ki] += bytes;
    bytes_ever_[ki] += bytes;

    mutexUnlock(&mutex_);
    return handle;
}

// ── Untrack ───────────────────────────────────────────────────────────────────

void QdResourceLedger::Untrack(uint64_t handle) {
    if (handle == 0) {
        return;  // null sentinel — no-op
    }

    mutexLock(&mutex_);

    for (size_t ki = 0; ki < static_cast<size_t>(QdResKind::Count); ++ki) {
        auto& vec = entries_[ki];
        for (size_t i = 0; i < vec.size(); ++i) {
            if (vec[i].handle == handle) {
                const size_t bytes = vec[i].bytes;
                // Swap with back and pop — O(1) removal.
                if (i != vec.size() - 1) {
                    vec[i] = vec.back();
                }
                vec.pop_back();
                // Update accounting.
                if (count_live_[ki] > 0) --count_live_[ki];
                if (bytes_live_[ki] >= bytes) {
                    bytes_live_[ki] -= bytes;
                } else {
                    bytes_live_[ki] = 0;
                }
                mutexUnlock(&mutex_);
                return;
            }
        }
    }

    mutexUnlock(&mutex_);
    // Handle not found — could be a double-untrack or untracked resource; silent.
}

// ── UpdateBytes ───────────────────────────────────────────────────────────────

void QdResourceLedger::UpdateBytes(uint64_t handle, size_t new_bytes) {
    if (handle == 0) return;

    mutexLock(&mutex_);

    for (size_t ki = 0; ki < static_cast<size_t>(QdResKind::Count); ++ki) {
        for (auto& e : entries_[ki]) {
            if (e.handle == handle) {
                const size_t old_bytes = e.bytes;
                e.bytes = new_bytes;
                // Adjust live byte totals.
                if (bytes_live_[ki] >= old_bytes) {
                    bytes_live_[ki] -= old_bytes;
                } else {
                    bytes_live_[ki] = 0;
                }
                bytes_live_[ki] += new_bytes;
                // Adjust ever-total by delta (can only grow).
                if (new_bytes > old_bytes) {
                    bytes_ever_[ki] += (new_bytes - old_bytes);
                }
                mutexUnlock(&mutex_);
                return;
            }
        }
    }

    mutexUnlock(&mutex_);
}

// ── GetSnapshot ───────────────────────────────────────────────────────────────

QdResourceLedger::Snapshot QdResourceLedger::GetSnapshot() {
    Snapshot snap = {};

    mutexLock(&mutex_);

    uint32_t total_live  = 0;
    size_t   total_bytes = 0;

    for (size_t ki = 0; ki < static_cast<size_t>(QdResKind::Count); ++ki) {
        snap.per_kind[ki].count_live        = count_live_[ki];
        snap.per_kind[ki].count_total_ever  = count_ever_[ki];
        snap.per_kind[ki].bytes_live        = bytes_live_[ki];
        snap.per_kind[ki].bytes_total_ever  = bytes_ever_[ki];
        total_live  += count_live_[ki];
        total_bytes += bytes_live_[ki];
    }

    snap.total_live  = total_live;
    snap.total_bytes = total_bytes;
    snap.uptime_sec  = svcGetSystemTick() / kLedgerTickHz;

    mutexUnlock(&mutex_);
    return snap;
}

// ── GetRecent ─────────────────────────────────────────────────────────────────

void QdResourceLedger::GetRecent(QdResKind kind, std::vector<Entry>& out, size_t max) {
    out.clear();
    const size_t ki = static_cast<size_t>(kind);
    if (ki >= static_cast<size_t>(QdResKind::Count)) return;

    mutexLock(&mutex_);

    const auto& vec = entries_[ki];
    // Copy into a local buffer under the lock, then sort outside.
    out.reserve(std::min(max, vec.size()));

    // Iterate from back (newest by insertion order due to swap-remove).
    // Actually swap-remove breaks insertion order; use alloc_tick for ordering.
    for (const auto& ie : vec) {
        Entry e;
        e.handle     = ie.handle;
        e.kind       = ie.kind;
        e.bytes      = ie.bytes;
        e.alloc_tick = ie.alloc_tick;
        e.file       = ie.file;
        e.line       = ie.line;
        memcpy(e.tag, ie.tag, sizeof(e.tag));
        out.push_back(e);
        if (out.size() >= max) break;
    }

    mutexUnlock(&mutex_);

    // Sort newest-first by alloc_tick.
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.alloc_tick > b.alloc_tick;
    });
}

// ── DumpLive ──────────────────────────────────────────────────────────────────

static const char* KindName(QdResKind kind) {
    switch (kind) {
        case QdResKind::Texture:       return "Texture";
        case QdResKind::Surface:       return "Surface";
        case QdResKind::Service:       return "Service";
        case QdResKind::Session:       return "Session";
        case QdResKind::Thread:        return "Thread";
        case QdResKind::FileHandle:    return "FileHandle";
        case QdResKind::Window:        return "Window";
        case QdResKind::MinimizedSnap: return "MinimizedSnap";
        case QdResKind::IconCache:     return "IconCache";
        case QdResKind::Sfx:           return "Sfx";
        default:                       return "Unknown";
    }
}

void QdResourceLedger::DumpLive() const {
    // const_cast to take lock — DumpLive is logically const but needs the mutex.
    QdResourceLedger* self = const_cast<QdResourceLedger*>(this);

    mutexLock(&self->mutex_);

    UL_LOG_INFO("ledger: === DumpLive ===");

    size_t total_live = 0;
    size_t total_bytes = 0;

    for (size_t ki = 0; ki < static_cast<size_t>(QdResKind::Count); ++ki) {
        const auto& vec = entries_[ki];
        if (vec.empty()) continue;

        const char* kn = KindName(static_cast<QdResKind>(ki));
        UL_LOG_INFO("ledger:  %s: live=%u ever=%u bytes_live=%zu",
                    kn,
                    (unsigned)count_live_[ki],
                    (unsigned)count_ever_[ki],
                    bytes_live_[ki]);

        for (const auto& e : vec) {
            UL_LOG_INFO("ledger:    h=%llu tag=%s bytes=%zu %s:%d",
                        (unsigned long long)e.handle,
                        e.tag,
                        e.bytes,
                        e.file ? e.file : "?",
                        e.line);
        }

        total_live  += count_live_[ki];
        total_bytes += bytes_live_[ki];
    }

    UL_LOG_INFO("ledger: === Total live=%zu bytes=%zu ===", total_live, total_bytes);

    mutexUnlock(&self->mutex_);
}

} // namespace ul::menu::qdesktop
