// qd_DebugServer.hpp — Remote-test layer (Phase 2 of "AI can test uMenu itself").
//
// A tiny HTTP server inside uMenu so a host PC (or Claude) can drive + observe a
// live session over WiFi.  HTTP — not raw TCP — so plain `curl` is enough:
//
//   curl http://<switch-ip>:6010/ping            -> "pong"
//   curl http://<switch-ip>:6010/state           -> JSON snapshot
//   curl http://<switch-ip>:6010/screenshot -o f.png   (Increment 2)
//   curl "http://<switch-ip>:6010/press?btn=B"         (Increment 3)
//   curl "http://<switch-ip>:6010/touch?x=100&y=200"   (Increment 3)
//
// SECURITY / opt-in: the server only starts if  sdmc:/ulaunch/debug.flag  exists.
// No flag → zero overhead (one fopen at boot), server never listens.  Binds to the
// nifm IP (never INADDR_ANY), mirroring qd_RemoteShellServer.  Input-injection
// routes (Increment 3) will additionally require a token from the flag file.
//
// Threading: a worker thread (page-aligned stack, priority 38, core -2) runs the
// accept loop and NEVER touches SDL / pu::ui.  Anything needing the renderer
// (screenshots) is handed to OnRenderFrame(), which runs on the render thread.
#pragma once

#ifdef QDESKTOP_MODE

#include <atomic>
#include <switch.h>

namespace ul::menu::qdesktop {

class QdDebugServer {
public:
    enum class State { Stopped, SocketInitFailed, Listening, Connected };

    QdDebugServer() = default;
    ~QdDebugServer();

    QdDebugServer(const QdDebugServer &)            = delete;
    QdDebugServer &operator=(const QdDebugServer &) = delete;

    // Start the server IFF sdmc:/ulaunch/debug.flag exists.  Idempotent.
    // Returns false if the flag is absent or thread/socket setup fails.
    bool Start();
    void Stop();
    bool IsRunning() const;
    State GetState() const;

    // Called once per frame from PumpAppletMessages (RENDER THREAD).  First call
    // auto-starts the server (flag-gated); every call publishes a state snapshot
    // (frame, focus) for /state and — Increment 2 — services screenshot captures.
    void OnRenderFrame(u32 frame);
};

// Singleton — defined in qd_DebugServer.cpp.
extern QdDebugServer g_DebugServer;

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
