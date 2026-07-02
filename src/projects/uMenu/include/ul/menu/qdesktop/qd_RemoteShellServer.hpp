// qd_RemoteShellServer.hpp — Phase 2 of HBmenu absorption (target: v2.3.0).
//
// Parallel TCP server on port 9999 — a developer on a host PC connects with
//   telnet 192.168.1.x 9999   or   nc 192.168.1.x 9999
// and runs shell-style commands against a live uMenu session.
//
// Threading model: identical to QdNxlinkServer (bt_Manager.cpp:106-150 template).
//   - 64 KB aligned stack, priority 38, core -2.
//   - std::atomic_bool stop flag, poll() with 100 ms timeout.
//   - Server thread NEVER touches SDL or pu::ui.
//
// Socket lifecycle: shares EnsureSocketInitialized() from qd_DevTools.cpp /
// qd_NxlinkServer.cpp (process-lifetime singleton guard).
//
// Protocol: line-oriented.
//   Host sends: <command>\n
//   Server replies: <response>\n  (may be multiple lines, ends at next prompt)
//   Server sends "> " prompt after each reply.
//   "quit" or disconnect closes the client connection; server returns to listen.
//
// Auth: none for v2.3.0 (local home-network use; documented trade-off).
//       Pre-shared-key auth planned for v2.3.1.
#pragma once

#ifdef QDESKTOP_MODE

#include <atomic>
#include <string>
#include <switch.h>

namespace ul::menu::qdesktop {

class QdRemoteShellServer {
public:
    enum class State {
        Stopped,
        SocketInitFailed,
        Listening,
        Connected,
    };

    QdRemoteShellServer() = default;
    ~QdRemoteShellServer();

    // Non-copyable, non-movable (owns OS thread handle).
    QdRemoteShellServer(const QdRemoteShellServer &)            = delete;
    QdRemoteShellServer &operator=(const QdRemoteShellServer &) = delete;
    QdRemoteShellServer(QdRemoteShellServer &&)                 = delete;
    QdRemoteShellServer &operator=(QdRemoteShellServer &&)      = delete;

    // Spawn the server thread. Idempotent — returns true if already running.
    // Returns false on thread-create or socket-init failure.
    bool Start();

    // Set the stop flag, join the thread, close the listen socket. Idempotent.
    void Stop();

    // True iff the server thread is alive AND the listen socket is bound.
    bool IsRunning() const;

    // Current state (atomic load — lock-free UI reads).
    State GetState() const;

    // v3.0.x Fix 3 — return the current auth PIN (0 if server has never started).
    // The PIN is generated once per Start() call and displayed in the hot-corner
    // dropdown row 9 so only the person standing at the Switch can connect.
    // Lock-free — safe to call from the UI thread at any time.
    int GetAuthPin() const;

private:
    static void ThreadEntry(void *self);
    void Run();

    // Serve one connected client until "quit" or disconnect.
    // All I/O is via the supplied client_fd; returns when the session ends.
    void ServeClient(int client_fd);
};

// Singleton instance — analogous to g_NxlinkServer.
// Defined in qd_RemoteShellServer.cpp.
extern QdRemoteShellServer g_RemoteShellServer;

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
