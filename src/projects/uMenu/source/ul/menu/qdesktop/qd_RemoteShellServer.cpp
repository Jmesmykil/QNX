// qd_RemoteShellServer.cpp — Phase 2 of HBmenu absorption (target: v2.3.0).
//
// TCP remote shell on port 9999.
// Threading template: bt_Manager.cpp:106-150 (same as qd_NxlinkServer.cpp).

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_RemoteShellServer.hpp>
#include <ul/menu/qdesktop/qd_ShellCommands.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <atomic>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>

// v3.0.x: PIN auth requires randomness.  libnx exposes randomGet() on Switch;
// on host builds we fall back to rand().
// <ctime> is unconditionally included for time() (used by the auth lockout ring
// buffer); on Switch, newlib provides the same time() via its POSIX layer.
#include <ctime>
#ifdef __SWITCH__
// randomGet is part of switch.h — already included via the extern "C" block above.
#endif

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

// ── Singleton ─────────────────────────────────────────────────────────────────
QdRemoteShellServer g_RemoteShellServer;

// ── Internal state ────────────────────────────────────────────────────────────
namespace {

static constexpr int kShellPort      = 9999;
static constexpr int kPollTimeoutMs  = 100;
static constexpr int kRecvTimeoutSec = 300; // 5-minute idle timeout

#ifdef __SWITCH__
Thread g_ShellServerThread;
alignas(0x1000) constinit u8 g_ShellServerStack[64 * 1024]; // 64 KB
#endif

std::atomic_bool g_running { false };
std::atomic<int> g_server_fd { -1 };
std::atomic<QdRemoteShellServer::State> g_state { QdRemoteShellServer::State::Stopped };

// Process-lifetime BSD socket subsystem guard (mirrors qd_NxlinkServer.cpp).
// If both servers are enabled simultaneously each checks independently — both
// are safe because socketInitializeDefault is idempotent after first call.
std::atomic_bool g_socket_init_done { false };

// v3.0.x Fix 3 — PIN authentication.
// Per-server-start 6-digit PIN (100000–999999).  Generated in Start() before
// the thread is spawned so it is visible in the hot-corner dropdown immediately.
// Regenerated each Start(); NOT each connection.  Connections that send the wrong
// PIN are rejected before any command loop.
// The atomic is read from the UI thread (dropdown render) and written once by the
// main thread in Start().  Relaxed ordering is sufficient — the thread fence in
// threadCreate() provides the necessary happens-before for the server thread read.
std::atomic<int> g_auth_pin { 0 };

// v3.5.0 Fix 3 — 3-strikes-in-60-second auth lockout ring buffer.
// Protects against brute-force PIN guessing over the LAN.
// Implemented as a fixed-size ring of POSIX timestamps (time_t seconds).
// On each failed auth attempt: push the current timestamp into the ring,
// then count how many entries are within the last 60 seconds.  If >= 3,
// disable the listen socket for the rest of the session.
//
// Only failed attempts increment the counter.  Successful auths do not
// reset it (an attacker who guesses correctly still does not clear prior
// strikes that happened within the window — this is intentional).
//
// Access model: only the server thread reads/writes these; no mutex needed.
// The server thread is serialised internally (one client at a time).
static constexpr int kAuthStrikeMax      = 3;     // max fails in window
static constexpr time_t kAuthStrikeWindowSec = 60; // sliding window, seconds
static constexpr int kAuthRingSize       = 8;     // ring slots (> kAuthStrikeMax)
static time_t g_auth_fail_ring[kAuthRingSize] = {}; // zero-init = epoch (no hits)
static int    g_auth_fail_ring_head = 0;            // index of next write slot

// Push a failure timestamp and return the count of failures in the window.
static int AuthRingPush() {
    const time_t now = ::time(nullptr);
    g_auth_fail_ring[g_auth_fail_ring_head % kAuthRingSize] = now;
    g_auth_fail_ring_head = (g_auth_fail_ring_head + 1) % kAuthRingSize;
    // Count entries within the sliding window.
    int count = 0;
    for (int i = 0; i < kAuthRingSize; ++i) {
        if (g_auth_fail_ring[i] != 0 &&
            (now - g_auth_fail_ring[i]) < kAuthStrikeWindowSec) {
            ++count;
        }
    }
    return count;
}

// Reset the ring (called on each Start() so a re-enable wipes old strikes).
static void AuthRingReset() {
    for (int i = 0; i < kAuthRingSize; ++i) {
        g_auth_fail_ring[i] = 0;
    }
    g_auth_fail_ring_head = 0;
}

bool EnsureSocketInitialized() {
#ifdef __SWITCH__
    if (g_socket_init_done.load(std::memory_order_acquire)) {
        return true;
    }
    // If qd_NxlinkServer (or any other consumer) already called
    // socketInitializeDefault, this second call returns a non-zero rc that
    // would otherwise look like failure.  The subsystem is still usable, so
    // log the rc and proceed.  Any genuine failure surfaces in the next
    // socket() call, which is gated by its own retry loop in Run().
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        UL_LOG_INFO("qdesktop: shell-server socketInitializeDefault rc=0x%08X "
                    "(already initialized by another subsystem; proceeding)",
                    static_cast<unsigned>(rc));
    } else {
        UL_LOG_INFO("qdesktop: shell-server socket subsystem up");
    }
    g_socket_init_done.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

} // anonymous namespace

// ── ThreadEntry ───────────────────────────────────────────────────────────────

void QdRemoteShellServer::ThreadEntry(void *self_v) {
    auto *self = static_cast<QdRemoteShellServer *>(self_v);
    if (self != nullptr) {
        self->Run();
    }
}

// ── Run ───────────────────────────────────────────────────────────────────────

void QdRemoteShellServer::Run() {
#ifdef __SWITCH__
    UL_LOG_INFO("qdesktop: shell-server thread alive — port %d", kShellPort);

    // Process-wide SIGPIPE → ignore.  Libnx's BSD socket layer does NOT
    // honour MSG_NOSIGNAL on send(); when a client closes the connection
    // mid-response, default SIGPIPE delivery kills the whole uMenu process
    // (visible as "Undefined System Call svc 0x6f" in atmosphere crash
    // reports).  Ignoring SIGPIPE makes failed sends return EPIPE instead.
    signal(SIGPIPE, SIG_IGN);

    // Small delay: Wi-Fi / nifm settles before socket init (same rationale as
    // qd_NxlinkServer.cpp — avoids audio-skip on boot).
    svcSleepThread(2'000'000'000LL); // 2 s

    if (!EnsureSocketInitialized()) {
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    // Create TCP socket with retry (Wi-Fi may still be settling).
    int srv = -1;
    for (int attempt = 0; attempt < 10 && g_running.load(std::memory_order_acquire); ++attempt) {
        srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv >= 0) break;
        UL_LOG_WARN("qdesktop: shell-server socket() attempt %d errno=%d", attempt, errno);
        svcSleepThread(1'000'000'000LL); // 1 s
    }
    if (srv < 0) {
        UL_LOG_WARN("qdesktop: shell-server socket() exhausted retries");
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    {
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&opt), sizeof(opt));
    }

    // v3.0.x Fix 2 — Bind to the Switch's current Wi-Fi/Ethernet IP instead of
    // INADDR_ANY.  INADDR_ANY accepts connections on ALL interfaces (Wi-Fi, USB-
    // RNDIS, loopback) — any host on any attached network can reach the shell.
    // Binding to nifmGetCurrentIpAddress() restricts acceptance to the single
    // active network interface that the user is actually using.
    //
    // If nifm returns 0.0.0.0 or fails (e.g. network not yet ready), we refuse
    // to listen rather than fall back to INADDR_ANY.  Better to be silently
    // off than open on an unexpected interface.  The user can re-enable via the
    // dropdown once the network is confirmed up (the label will show the IP).
    u32 bind_ip = 0;
    {
        // Give nifm up to 30 s (same window as the old bind retry loop) to
        // surface a usable IP before giving up.
        bool ip_ready = false;
        for (int attempt = 0; attempt < 30 && g_running.load(std::memory_order_acquire); ++attempt) {
            const Result ip_rc = nifmGetCurrentIpAddress(&bind_ip);
            if (R_SUCCEEDED(ip_rc) && bind_ip != 0) {
                ip_ready = true;
                UL_LOG_INFO("qdesktop: shell-server got bind IP %u.%u.%u.%u on attempt %d",
                            static_cast<unsigned>(bind_ip & 0xFF),
                            static_cast<unsigned>((bind_ip >> 8)  & 0xFF),
                            static_cast<unsigned>((bind_ip >> 16) & 0xFF),
                            static_cast<unsigned>((bind_ip >> 24) & 0xFF),
                            attempt);
                break;
            }
            UL_LOG_WARN("qdesktop: shell-server nifmGetCurrentIpAddress attempt %d rc=0x%08X ip=0x%X",
                        attempt, R_FAILED(ip_rc) ? static_cast<unsigned>(ip_rc) : 0u,
                        static_cast<unsigned>(bind_ip));
            svcSleepThread(1'000'000'000LL); // 1 s
        }
        if (!ip_ready) {
            UL_LOG_WARN("qdesktop: shell-server refusing to bind — no usable IP from nifm "
                        "(better off than open on INADDR_ANY)");
            close(srv);
            g_state.store(State::SocketInitFailed, std::memory_order_release);
            g_running.store(false, std::memory_order_release);
            return;
        }

        // v3.5.0 Fix 2 — Reject loopback addresses (127.0.0.0/8).
        // nifm returning a loopback address means the Switch is not connected
        // to a real network (only the loopback interface is up).  Binding telnet
        // to 127.x.x.x would be unreachable from any host PC anyway, but more
        // importantly it signals an unexpected network state — don't bind at all.
        // The IP from nifm is in network byte order (LSB = first octet).
        const u8 first_octet = static_cast<u8>(bind_ip & 0xFFu);
        if (first_octet == 127u) {
            UL_LOG_WARN("qdesktop: shell-server refusing to bind — nifm returned loopback "
                        "IP %u.%u.%u.%u (Switch not connected to real network)",
                        static_cast<unsigned>(bind_ip & 0xFF),
                        static_cast<unsigned>((bind_ip >> 8) & 0xFF),
                        static_cast<unsigned>((bind_ip >> 16) & 0xFF),
                        static_cast<unsigned>((bind_ip >> 24) & 0xFF));
            close(srv);
            g_state.store(State::SocketInitFailed, std::memory_order_release);
            g_running.store(false, std::memory_order_release);
            return;
        }
    }

    // Bind with retry (network stack may still be setting up even after IP is known).
    bool bound = false;
    for (int attempt = 0; attempt < 10 && g_running.load(std::memory_order_acquire); ++attempt) {
        struct sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = bind_ip;   // network-byte-order from nifm — do NOT htonl
        addr.sin_port        = htons(static_cast<uint16_t>(kShellPort));
        if (bind(srv, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            bound = true;
            UL_LOG_INFO("qdesktop: shell-server bind() succeeded on attempt %d", attempt);
            break;
        }
        UL_LOG_WARN("qdesktop: shell-server bind() attempt %d errno=%d", attempt, errno);
        svcSleepThread(1'000'000'000LL); // 1 s
    }
    if (!bound) {
        close(srv);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    if (listen(srv, 1) != 0) {
        UL_LOG_WARN("qdesktop: shell-server listen() errno=%d", errno);
        close(srv);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    g_server_fd.store(srv, std::memory_order_release);
    g_state.store(State::Listening, std::memory_order_release);
    // v3.5.0 Fix 3 — log PIN + IP at INFO so the operator can confirm it matches
    // the hot-corner dropdown.  Format: "telnet: PIN=123456 listening on <ip>:9999".
    {
        const int pin = g_auth_pin.load(std::memory_order_acquire);
        UL_LOG_INFO("telnet: PIN=%06d listening on %u.%u.%u.%u:9999",
                    pin,
                    static_cast<unsigned>(bind_ip & 0xFF),
                    static_cast<unsigned>((bind_ip >> 8) & 0xFF),
                    static_cast<unsigned>((bind_ip >> 16) & 0xFF),
                    static_cast<unsigned>((bind_ip >> 24) & 0xFF));
    }
    UL_LOG_INFO("qdesktop: shell-server listening on port %d", kShellPort);

    // Accept loop — one client at a time.
    while (g_running.load(std::memory_order_acquire)) {
        struct pollfd pfd {};
        pfd.fd     = srv;
        pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, kPollTimeoutMs);
        if (pr < 0) {
            if (errno == EINTR) continue;
            UL_LOG_WARN("qdesktop: shell-server poll() errno=%d", errno);
            break;
        }
        if (pr == 0) continue; // timeout — re-check g_running

        if (!(pfd.revents & POLLIN)) continue;

        struct sockaddr_in remote {};
        socklen_t rlen = sizeof(remote);
        const int client = accept(srv,
                                  reinterpret_cast<struct sockaddr *>(&remote),
                                  &rlen);
        if (client < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) continue;
            // v2.8.8 — SELF-STOP ON HARD ERROR.  Replaces v2.8.2's 1 s backoff.
            //
            // HW telemetry (v2.8.7 diagnostic build 2026-05-18) confirmed
            // that appletGetFocusState() does NOT transition for uMenu when
            // the Switch sleeps — uMenu is the qlaunch replacement (the home
            // applet itself), so the kernel never sends FocusStateChanged
            // to it.  Heartbeat showed `focus=1` for the entire session,
            // including during sleep.  This rules out v2.8.4's appletHook
            // approach AND v2.8.6's direct-poll approach as fundamentally
            // unable to detect sleep for our applet type.
            //
            // Pivot to the canonical hbmenu netloader pattern (Agent 5
            // research, /nx-hbmenu/common/netloader.c::netloader_loop):
            // "on any error close the socket and DO NOT REOPEN — user must
            // re-arm from menu."  The server thread sees its own broken
            // socket via ECONNABORTED/EHOSTUNREACH/whatever; treats that
            // as the sleep signal and self-stops.  IsRunning() then returns
            // false, dropdown row 9 label flips to "Remote Shell: OFF",
            // user manually re-enables when ready.
            //
            // Memory cost zero.  No focus-state dependency.  Matches every
            // other Switch homebrew TCP server (sys-ftpd, ftpd, hbmenu,
            // sphaira) — none auto-restart, all default-off-after-failure.
            UL_LOG_WARN("qdesktop: shell-server accept() errno=%d — self-stopping (broken socket)", errno);

            // Close the listen socket if still open (defensive — kernel may
            // have already closed it, but exchange-then-close is idempotent).
            const int fd_to_close = g_server_fd.exchange(-1, std::memory_order_acq_rel);
            if (fd_to_close >= 0) {
                close(fd_to_close);
            }

            // Flip running flag BEFORE state so dropdown label updates
            // atomically when IsRunning() is read between these two stores.
            g_running.store(false, std::memory_order_release);
            g_state.store(State::Stopped, std::memory_order_release);

            UL_LOG_INFO("qdesktop: shell-server thread exiting (self-stop); user can re-enable via dropdown row 9");
            // Break out of the accept loop.  Thread exits naturally below.
            // The thread handle stays allocated until next Start() call,
            // which calls threadClose() before threadCreate().
            break;
        }

        UL_LOG_INFO("qdesktop: shell-server client connected fd=%d", client);
        g_state.store(State::Connected, std::memory_order_release);
        ServeClient(client);
        close(client);

        if (g_running.load(std::memory_order_acquire)) {
            g_state.store(State::Listening, std::memory_order_release);
        }
    }

    // Cleanup.
    {
        const int fd = g_server_fd.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) close(fd);
    }
    g_state.store(State::Stopped, std::memory_order_release);
    UL_LOG_INFO("qdesktop: shell-server thread exiting");
#endif // __SWITCH__
}

// ── ServeClient ───────────────────────────────────────────────────────────────
// Line-oriented read loop: read until '\n', dispatch to DispatchCommand,
// repeat until "quit" or the client disconnects.

void QdRemoteShellServer::ServeClient(int client_fd) {
#ifdef __SWITCH__
    // Set receive timeout to catch idle clients.
    {
        struct timeval tv {};
        tv.tv_sec  = kRecvTimeoutSec;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&tv), sizeof(tv));
    }

    // v3.0.x Fix 3 — PIN authentication.
    // The first line the client sends must be exactly the 6-digit PIN displayed
    // on the Switch screen (hot-corner dropdown row 9).  A 5-second timeout
    // applies — clients that don't respond in time are dropped.
    // The PIN is per-server-start; it is regenerated each time the server is
    // enabled, NOT each connection.
    {
        // Override the 300-second idle timeout with a tight 5-second auth window.
        struct timeval auth_tv {};
        auth_tv.tv_sec  = 5;
        auth_tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&auth_tv), sizeof(auth_tv));

        // Send the auth prompt (no PIN hint — user must look at the Switch screen).
        static const char kAuthPrompt[] = "PIN: ";
        send(client_fd, kAuthPrompt, sizeof(kAuthPrompt) - 1, MSG_NOSIGNAL);

        // Read the first line from the client (up to 16 chars including newline).
        char auth_buf[16];
        int  auth_len = 0;
        bool auth_got_newline = false;
        while (auth_len < static_cast<int>(sizeof(auth_buf)) - 1) {
            char ch = '\0';
            const ssize_t n = recv(client_fd, &ch, 1, 0);
            if (n <= 0) break;  // timeout or disconnect
            if (ch == '\r') continue;
            if (ch == '\n') { auth_got_newline = true; break; }
            auth_buf[auth_len++] = ch;
        }
        auth_buf[auth_len] = '\0';

        // Compare against the current PIN.
        const int expected_pin = g_auth_pin.load(std::memory_order_acquire);
        char expected_str[8];
        snprintf(expected_str, sizeof(expected_str), "%06d", expected_pin);

        if (!auth_got_newline || strcmp(auth_buf, expected_str) != 0) {
            UL_LOG_WARN("qdesktop: shell-server PIN mismatch fd=%d (client sent '%s')",
                        client_fd, auth_buf);
            static const char kAuthFail[] = "ERR: auth\r\n";
            send(client_fd, kAuthFail, sizeof(kAuthFail) - 1, MSG_NOSIGNAL);

            // v3.5.0 Fix 3 — lockout: push failure into ring buffer.
            // If 3 failures have occurred within 60 seconds, disable the listener
            // for this session.  Log a security alert at WARN level.
            const int recent_fails = AuthRingPush();
            if (recent_fails >= kAuthStrikeMax) {
                UL_LOG_WARN("qdesktop: shell-server SECURITY ALERT — %d auth failures "
                            "within %lld seconds; disabling telnet listener for this session",
                            recent_fails,
                            static_cast<long long>(kAuthStrikeWindowSec));
                // Force-close the listen socket.  The accept loop's poll() will
                // return an error and the thread will self-stop (same as the
                // existing hard-error path at accept() errno != EINTR).
                const int listen_fd = g_server_fd.exchange(-1, std::memory_order_acq_rel);
                if (listen_fd >= 0) {
                    close(listen_fd);
                }
                g_running.store(false, std::memory_order_release);
                g_state.store(State::Stopped, std::memory_order_release);
            }
            return;  // close client socket — no command loop
        }

        UL_LOG_INFO("qdesktop: shell-server PIN accepted fd=%d", client_fd);

        // Restore the full 300-second idle timeout for the command loop.
        struct timeval cmd_tv {};
        cmd_tv.tv_sec  = kRecvTimeoutSec;
        cmd_tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&cmd_tv), sizeof(cmd_tv));
    }

    // Welcome banner.
    static const char kBanner[] =
        "Q OS uMenu remote shell — type 'help' for commands, 'quit' to disconnect\r\n> ";
    send(client_fd, kBanner, sizeof(kBanner) - 1, MSG_NOSIGNAL);

    char line_buf[256];
    int  line_len = 0;

    while (g_running.load(std::memory_order_acquire)) {
        char ch = '\0';
        const ssize_t n = recv(client_fd, &ch, 1, 0);
        if (n <= 0) {
            // EOF or timeout — client disconnected.
            UL_LOG_INFO("qdesktop: shell-server client disconnected (recv=%zd errno=%d)",
                        n, errno);
            break;
        }

        // Accumulate characters until newline.
        if (ch == '\r') continue; // telnet sends \r\n — skip the \r
        if (ch == '\n' || line_len >= static_cast<int>(sizeof(line_buf)) - 1) {
            line_buf[line_len] = '\0';
            line_len = 0;

            // Dispatch — returns false on "quit".
            const bool cont = shell::DispatchCommand(client_fd, line_buf);
            if (!cont) break;

            // Send prompt for next command.
            static const char kPrompt[] = "> ";
            send(client_fd, kPrompt, sizeof(kPrompt) - 1, MSG_NOSIGNAL);
        } else {
            line_buf[line_len++] = ch;
        }
    }
#else
    (void)client_fd;
#endif
}

// ── Public API ────────────────────────────────────────────────────────────────

QdRemoteShellServer::~QdRemoteShellServer() {
    Stop();
}

bool QdRemoteShellServer::Start() {
#ifdef __SWITCH__
    if (g_running.load(std::memory_order_acquire)) {
        UL_LOG_INFO("qdesktop: shell-server Start() already running — no-op");
        return true;
    }

    // v3.0.x Fix 3 — generate a new PIN before spawning the thread.
    // PIN is visible in the hot-corner dropdown row 9 immediately after Start()
    // returns, so the user can read it before any connection attempt.
    // randomGet() (libnx crypto/random.h) fills a buffer with CSPRNG bytes;
    // we mod into [100000, 999999] to guarantee exactly 6 digits.
    {
        u64 rnd = 0;
        randomGet(&rnd, sizeof(rnd));
        const int pin = static_cast<int>(rnd % 900000u) + 100000;
        g_auth_pin.store(pin, std::memory_order_release);
        UL_LOG_INFO("qdesktop: shell-server new PIN generated (display on screen only)");
    }

    // v3.5.0 Fix 3 — reset the auth-failure ring buffer so a re-enable after a
    // lockout starts with a clean slate (prior failed attempts are forgotten).
    AuthRingReset();

    g_running.store(true, std::memory_order_release);
    g_state.store(State::Listening, std::memory_order_release);

    const Result rc_create = threadCreate(&g_ShellServerThread,
                                          &QdRemoteShellServer::ThreadEntry,
                                          this,
                                          g_ShellServerStack,
                                          sizeof(g_ShellServerStack),
                                          /*priority=*/38,
                                          /*core=*/-2);
    if (R_FAILED(rc_create)) {
        UL_LOG_WARN("qdesktop: shell-server threadCreate rc=0x%08X",
                    static_cast<unsigned>(rc_create));
        g_running.store(false, std::memory_order_release);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    const Result rc_start = threadStart(&g_ShellServerThread);
    if (R_FAILED(rc_start)) {
        UL_LOG_WARN("qdesktop: shell-server threadStart rc=0x%08X",
                    static_cast<unsigned>(rc_start));
        threadClose(&g_ShellServerThread);
        g_running.store(false, std::memory_order_release);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    UL_LOG_INFO("qdesktop: shell-server thread spawned (priority=38, core=-2)");
    return true;
#else
    return false;
#endif
}

void QdRemoteShellServer::Stop() {
#ifdef __SWITCH__
    if (!g_running.load(std::memory_order_acquire)) {
        return;
    }
    g_running.store(false, std::memory_order_release);

    // Force-close the listen socket so poll() / accept() unblocks immediately.
    const int fd = g_server_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        close(fd);
    }

    threadWaitForExit(&g_ShellServerThread);
    threadClose(&g_ShellServerThread);
    g_state.store(State::Stopped, std::memory_order_release);
    UL_LOG_INFO("qdesktop: shell-server stopped");
#endif
}

bool QdRemoteShellServer::IsRunning() const {
    return g_running.load(std::memory_order_acquire);
}

QdRemoteShellServer::State QdRemoteShellServer::GetState() const {
    return g_state.load(std::memory_order_acquire);
}

int QdRemoteShellServer::GetAuthPin() const {
    return g_auth_pin.load(std::memory_order_acquire);
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
