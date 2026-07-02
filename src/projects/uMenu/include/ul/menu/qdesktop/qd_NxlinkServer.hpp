// qd_NxlinkServer.hpp — Phase 1 of HBmenu absorption (target: v2.2.0).
//
// SCAFFOLD ONLY (v2.1.0 commit). The thread lifecycle, public API surface, and
// inter-thread signalling are in place; the actual TCP `accept()` + NRO
// receive loop body is a stub that immediately exits. Next session fills in
// the server logic per the design at:
//   staging/v2.1-prep/HBMENU-ABSORPTION-AND-WINDOWED-NRO-IMPL-READY.md (Phase 1)
//
// Goal:
//   uMenu becomes a TCP server listening on port 28771 for incoming NRO file
//   transfers from the standard `nxlink -s <ip> <file.nro>` host tool. Replaces
//   the need for HBmenu's own netloader (HBmenu `common/netloader.c`).
//
// Threading model — follow `bt_Manager.cpp:106-150` exactly:
//   - 64 KB aligned stack (larger than BT's 32 KB because the recv loop will
//     buffer NRO chunks; sized once at scaffold-time so v2.2.0 fill-in doesn't
//     have to migrate stack size).
//   - Thread priority 38, preferred core -2 (low-priority background, scheduler
//     decides core).
//   - `std::atomic_bool g_NxlinkServerRunning` stop flag.
//   - select()/poll() with ~100 ms timeout in the accept loop so the stop flag
//     is checked promptly on shutdown.
//
// Socket lifecycle:
//   `socketInitializeDefault()` is already called by `qd_DevTools.cpp::TryEnableNxlink()`
//   — guarded by `g_socket_initialized` static. The Phase 1 implementer must
//   share that guard (do NOT duplicate). Recommended: export `EnsureSocketInitialized()`
//   from qd_DevTools or a new qd_NetworkUtils. `socketExit()` is never called —
//   process-lifetime singleton.
//
// Inter-thread integration with the main thread:
//   - On NRO write complete, set `std::atomic_bool g_nxlink_scan_pending = true`
//     (declared in this header, defined in qd_NxlinkServer.cpp).
//   - Main thread checks the flag in its update loop (qd_DesktopIcons update),
//     re-scans `sdmc:/switch/`, refreshes the icon grid. SDL texture creation
//     happens on the main thread only — the server thread MUST NOT touch SDL.
//   - menu_Cache.cpp::CacheHomebrewEntry() was verified SDL-clean on
//     2026-05-04 (pure file I/O, zero SDL/RenderText/Texture calls). Calling
//     it from the server thread is safe.
//
// HBmenu wire protocol (port 28771, from HBmenu common/netloader.c):
//   1. Host sends 4-byte little-endian filesize.
//   2. Host sends `size` bytes of NRO data (optionally zlib — Phase 1 v2.2.0
//      ships uncompressed-only per OQ-2; zlib is v2.2.1 polish).
//   3. Server writes to `sdmc:/switch/<basename>.nro` then sets scan_pending.
//
// OQ-1 deferred: dual-port semantics (28280 probe vs 28771 server) — the
// existing DC_NXLINK_PORT=28280 in qd_DevConsoleElement.hpp:60 stays as a
// separate "is a remote nxlink process running on localhost" probe; this server
// adds DC_NXLINK_SERVER_PORT=28771 alongside it. Decision pushed to v2.2.0
// implementation phase.
#pragma once

#ifdef QDESKTOP_MODE

#include <atomic>
#include <string>
#include <switch.h>

namespace ul::menu::qdesktop {

class QdNxlinkServer {
public:
    // Server status enum — drives DevConsole Panel A row 4 ("Status").
    enum class State {
        Stopped,             // Not running.
        SocketInitFailed,    // socket()/bind()/listen() failed; see GetLastError().
        Listening,           // Idle, waiting for a host to connect.
        Receiving,           // Active transfer in progress.
        Done,                // Last transfer succeeded; back to Listening on next accept.
        TransferFailed,      // Last transfer errored; back to Listening on next accept.
    };

    QdNxlinkServer() = default;
    ~QdNxlinkServer();

    // Spawn the server thread. Idempotent — calling again while running is a
    // logged no-op that returns true. Returns false on socket-init failure.
    bool Start();

    // Set the stop flag, join the thread, close the listen socket. Idempotent.
    void Stop();

    // True iff the server thread is alive AND the listen socket is bound.
    bool IsRunning() const;

    // Current server state for UI display. Atomic load.
    State GetState() const;

    // Most recent transfer's filename (or empty). For DevConsole "Receiving X"
    // and "Done — X.nro saved" displays. Returned by value (copy of std::string)
    // to avoid use-after-free during the background thread's writes.
    std::string GetLastFilename() const;

    // Most recent transfer's progress: bytes received / total bytes. Both 0
    // when no transfer is in flight or when the most recent transfer completed.
    void GetProgress(size_t *bytes_received, size_t *total_bytes) const;

private:
    // Server-thread entry point. Static + void* signature for threadCreate.
    static void ThreadEntry(void *self);

    // Server thread body — fills server_fd_, listens, accepts, calls
    // ReceiveOne() for each connection.  Currently a stub (Phase 1 fill-in
    // pending).
    void Run();

    // Receive one NRO transfer on the given client_fd. Closes client_fd when
    // done. Returns true on successful receive + write. Stub in v2.1.0 scaffold.
    bool ReceiveOne(int client_fd);

    // Process-lifetime BSD socket subsystem guard. Calls socketInitializeDefault
    // exactly once; safe to call from any thread context. Returns true if the
    // subsystem is up after the call.
    static bool EnsureSocketInitialized();
};

// Singleton instance — analogous to g_BluetoothThread in bt_Manager.cpp.
// Defined in qd_NxlinkServer.cpp.
extern QdNxlinkServer g_NxlinkServer;

// Main-thread signal: server thread sets to true after a successful NRO write.
// Main thread (qd_DesktopIcons update loop) tests, triggers an NRO scan, then
// resets to false. Use std::memory_order_acq_rel for the set-and-test pair.
extern std::atomic_bool g_nxlink_scan_pending;

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
