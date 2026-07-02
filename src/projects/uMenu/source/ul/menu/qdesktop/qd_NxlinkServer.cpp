// qd_NxlinkServer.cpp — Phase 1 of HBmenu absorption (target: v2.2.0).
//
// Implements the HBmenu netloader wire protocol on port 28771.
// Phase 1 includes full zlib decompression (protocol always uses it).
// Design doc: staging/v2.1-prep/HBMENU-ABSORPTION-AND-WINDOWED-NRO-IMPL-READY.md

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_NxlinkServer.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
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
#include <mutex>

// BSD socket headers (libnx wraps these)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
// libgen.h (POSIX basename) is not in newlib/devkitA64 — replaced with the
// inline strrchr fallback at the call site below.

// zlib — always linked via -lz (confirmed in uMenu/Makefile LIBS)
#include <zlib.h>

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...)  do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...)  do {} while (0)
#endif

namespace ul::menu::qdesktop {

// ── Singletons ────────────────────────────────────────────────────────────────
QdNxlinkServer g_NxlinkServer;
std::atomic_bool g_nxlink_scan_pending { false };

// ── Internal state (file-static; no header exposure needed) ──────────────────
namespace {

// Threading. Pattern from bt_Manager.cpp:106-150.
#ifdef __SWITCH__
Thread g_NxlinkServerThread;
alignas(0x1000) constinit u8 g_NxlinkServerStack[64 * 1024]; // 64 KB
#endif

std::atomic_bool g_running { false };

// Listen socket fd. -1 when not bound. The server thread owns this — Stop()
// reads it to close from the main thread (after threadWaitForExit) so we don't
// race the recv loop on close.
std::atomic<int> g_server_fd { -1 };

// Status snapshot. Atomic enum so DevConsole UI reads are lock-free.
std::atomic<QdNxlinkServer::State> g_state { QdNxlinkServer::State::Stopped };

// Last filename + progress. Mutex-guarded because std::string is not atomic.
// Reads (UI) and writes (server thread) are infrequent — coarse mutex is fine.
std::mutex g_status_mu;
std::string g_last_filename;
size_t g_bytes_received = 0;
size_t g_total_bytes    = 0;

// Process-lifetime socket-subsystem guard. Mirrors qd_DevTools.cpp's
// g_socket_initialized but lives in this TU's anonymous namespace so
// initialization order is local. The shared guard pattern (export
// EnsureSocketInitialized() from qd_DevTools) is a v2.2.0 cleanup item.
std::atomic_bool g_socket_init_done { false };

} // anonymous namespace

// ── EnsureSocketInitialized ──────────────────────────────────────────────────

bool QdNxlinkServer::EnsureSocketInitialized() {
#ifdef __SWITCH__
    if (g_socket_init_done.load(std::memory_order_acquire)) {
        return true;
    }
    // If qd_RemoteShellServer (or any other consumer) already called
    // socketInitializeDefault, this second call returns a non-zero rc that
    // would otherwise look like failure.  The subsystem is still usable, so
    // log the rc and proceed.  Any genuine failure surfaces in the next
    // socket() call, which is gated by its own retry loop in Run().
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        UL_LOG_INFO("qdesktop: nxlink-server socketInitializeDefault rc=0x%08X "
                    "(already initialized by another subsystem; proceeding)",
                    static_cast<unsigned>(rc));
    } else {
        UL_LOG_INFO("qdesktop: nxlink-server socket subsystem up");
    }
    g_socket_init_done.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

// ── ThreadEntry ──────────────────────────────────────────────────────────────

void QdNxlinkServer::ThreadEntry(void *self_v) {
    auto *self = static_cast<QdNxlinkServer*>(self_v);
    if (self != nullptr) {
        self->Run();
    }
}

// ── Internal helpers ─────────────────────────────────────────────────────────
namespace {

// v2.2.3 port-direction fix: libnx/include/switch/runtime/nxlink.h is
// authoritative — NXLINK_SERVER_PORT=28280 is the Switch (server) listen
// port; NXLINK_CLIENT_PORT=28771 is the HOST listen port (used when nxlink
// runs `-s` reverse-stdio mode). HBmenu netloader.c listens on 28280 and
// replies UDP "bootnx" to host_ip:28771. Earlier QOS builds had these
// inverted (we listened on 28771), so the standard `nxlink -a` host tool
// — which probes 28280 — never found us.
static constexpr int    kServerPort       = 28280;  // NXLINK_SERVER_PORT
static constexpr int    kClientReplyPort  = 28771;  // NXLINK_CLIENT_PORT
static constexpr int    kPollTimeoutMs    = 100;
static constexpr int    kRcvTimeoutSec    = 10;
static constexpr size_t kZlibChunk        = 16 * 1024;   // matches HBmenu ZLIB_CHUNK
static constexpr size_t kRecvBuf          = 32 * 1024;   // recv scratch on server stack

// Reliable recv: keeps calling recv() until exactly `size` bytes are received
// or an error/EOF occurs. Returns the number of bytes actually received;
// returns 0 on clean EOF, -1 on socket error.
static ssize_t recvall(int sock, void *buf, size_t size) {
    auto *p = static_cast<char *>(buf);
    size_t remaining = size;
    while (remaining > 0) {
        const ssize_t n = recv(sock, p, remaining, 0);
        if (n == 0) return 0;           // clean EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p         += n;
        remaining -= static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(size);
}

} // namespace

// ── Run ──────────────────────────────────────────────────────────────────────
// Phase 1 implementation: TCP server on port 28771, accept loop with poll().

void QdNxlinkServer::Run() {
#ifdef __SWITCH__
    UL_LOG_INFO("qdesktop: nxlink-server thread alive — initializing socket subsystem");

    // v2.2.0: small delay at thread start so Wi-Fi / nifm has time to settle
    // before we try socket init. Q OS launches the server during MainMenuLayout
    // init, which can run before nifm has finalized network state.
    svcSleepThread(2'000'000'000);  // 2 seconds

    // 0. Initialize BSD socket subsystem on the server thread (NOT the main
    //    thread — see Start() comment on the audio-skip fix).
    if (!EnsureSocketInitialized()) {
        UL_LOG_WARN("qdesktop: nxlink-server EnsureSocketInitialized failed");
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    // 1. Create TCP socket — retry up to 10 times at 1 s intervals if the
    //    socket subsystem reports temporary failure (network not up yet).
    int srv = -1;
    for (int attempt = 0; attempt < 10 && g_running.load(std::memory_order_acquire); ++attempt) {
        srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv >= 0) break;
        UL_LOG_WARN("qdesktop: nxlink-server socket() attempt %d errno=%d",
                    attempt, errno);
        svcSleepThread(1'000'000'000);  // 1 s
    }
    if (srv < 0) {
        UL_LOG_WARN("qdesktop: nxlink-server socket() exhausted retries");
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    // 2. SO_REUSEADDR so restart after a crash binds cleanly.
    {
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&opt), sizeof(opt));
    }

    // 3. Bind to INADDR_ANY:28771 — retry up to 30 times at 1 s intervals.
    //    The most common failure here is EADDRNOTAVAIL when nifm hasn't
    //    completed Wi-Fi negotiation yet.
    bool bound = false;
    for (int attempt = 0; attempt < 30 && g_running.load(std::memory_order_acquire); ++attempt) {
        struct sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(static_cast<uint16_t>(kServerPort));
        if (bind(srv, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            bound = true;
            UL_LOG_INFO("qdesktop: nxlink-server bind() succeeded on attempt %d", attempt);
            break;
        }
        UL_LOG_WARN("qdesktop: nxlink-server bind() attempt %d errno=%d", attempt, errno);
        svcSleepThread(1'000'000'000);  // 1 s
    }
    if (!bound) {
        close(srv);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    // 4. listen() — single client at a time, backlog 1.
    if (listen(srv, 1) != 0) {
        UL_LOG_WARN("qdesktop: nxlink-server listen() errno=%d", errno);
        close(srv);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    // 5. Publish the listen fd so Stop() can close it from the main thread.
    g_server_fd.store(srv, std::memory_order_release);

    // 5a. UDP discovery socket on the same port.  v2.2.2: nxlink host tool
    //     always does a UDP "nxboot" handshake before TCP connect — even with
    //     `-a <ip>` flag.  Without responding "bootnx" to the discovery probe,
    //     `nxlink` reports "Connection to <ip> failed" before it even tries
    //     TCP.  HBmenu netloader.c reference: same port (28771) for both UDP
    //     and TCP; respond with "bootnx" to any "nxboot" UDP packet.
    int udp_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd >= 0) {
        int opt = 1;
        setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&opt), sizeof(opt));
        struct sockaddr_in udp_addr {};
        udp_addr.sin_family      = AF_INET;
        udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        udp_addr.sin_port        = htons(static_cast<uint16_t>(kServerPort));
        if (bind(udp_fd, reinterpret_cast<struct sockaddr *>(&udp_addr), sizeof(udp_addr)) != 0) {
            UL_LOG_WARN("qdesktop: nxlink-server UDP bind() errno=%d (discovery disabled)", errno);
            close(udp_fd);
            udp_fd = -1;
        } else {
            UL_LOG_INFO("qdesktop: nxlink-server UDP discovery listening on port %d", kServerPort);
        }
    } else {
        UL_LOG_WARN("qdesktop: nxlink-server UDP socket() errno=%d (discovery disabled)", errno);
    }

    g_state.store(State::Listening, std::memory_order_release);
    UL_LOG_INFO("qdesktop: nxlink-server listening on port %d", kServerPort);

    // 6. Accept loop — poll BOTH the TCP listen socket and the UDP discovery
    //    socket. UDP packets get a synchronous "bootnx" response; TCP gets the
    //    full receive flow via ReceiveOne().
    while (g_running.load(std::memory_order_acquire)) {
        struct pollfd pfds[2] {};
        int nfds = 0;
        pfds[nfds].fd     = srv;
        pfds[nfds].events = POLLIN;
        ++nfds;
        const int udp_idx = (udp_fd >= 0) ? nfds : -1;
        if (udp_fd >= 0) {
            pfds[nfds].fd     = udp_fd;
            pfds[nfds].events = POLLIN;
            ++nfds;
        }
        const int pr = poll(pfds, nfds, kPollTimeoutMs);
        if (pr < 0) {
            if (errno == EINTR) continue;
            UL_LOG_WARN("qdesktop: nxlink-server poll() errno=%d", errno);
            break;
        }
        if (pr == 0) continue;  // timeout — re-check g_running

        // ── UDP discovery: respond "bootnx" to any "nxboot" probe ──────────
        if (udp_idx >= 0 && (pfds[udp_idx].revents & POLLIN)) {
            char udp_buf[64] {};
            struct sockaddr_in src {};
            socklen_t src_len = sizeof(src);
            const ssize_t got = recvfrom(udp_fd, udp_buf, sizeof(udp_buf) - 1, 0,
                                         reinterpret_cast<struct sockaddr *>(&src),
                                         &src_len);
            if (got > 0) {
                udp_buf[got] = '\0';
                // The host probe is the literal ASCII bytes "nxboot".  Match by
                // prefix to be liberal (some nxlink versions append zero-pad).
                if (got >= 6 && std::memcmp(udp_buf, "nxboot", 6) == 0) {
                    // v2.2.3: reply to host_ip:NXLINK_CLIENT_PORT (28771), NOT
                    // the source port — HBmenu netloader.c does the same.
                    // nxlink listens on 28771 for the bootnx response.
                    struct sockaddr_in reply_to = src;
                    reply_to.sin_port = htons(static_cast<uint16_t>(kClientReplyPort));
                    static const char kReply[] = "bootnx";
                    sendto(udp_fd, kReply, sizeof(kReply) - 1, 0,
                           reinterpret_cast<struct sockaddr *>(&reply_to),
                           sizeof(reply_to));
                    UL_LOG_INFO("qdesktop: nxlink-server UDP discovery — replied 'bootnx' to %u.%u.%u.%u:%d",
                                static_cast<unsigned>(src.sin_addr.s_addr & 0xFF),
                                static_cast<unsigned>((src.sin_addr.s_addr >> 8) & 0xFF),
                                static_cast<unsigned>((src.sin_addr.s_addr >> 16) & 0xFF),
                                static_cast<unsigned>((src.sin_addr.s_addr >> 24) & 0xFF),
                                kClientReplyPort);
                }
            }
        }

        // ── TCP file transfer ──────────────────────────────────────────────
        if (!(pfds[0].revents & POLLIN)) continue;

        struct sockaddr_in remote {};
        socklen_t rlen = sizeof(remote);
        const int client = accept(srv,
                                  reinterpret_cast<struct sockaddr *>(&remote),
                                  &rlen);
        if (client < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) continue;
            // v2.8.8 — self-stop on hard error.  See companion comment in
            // qd_RemoteShellServer.cpp for the full rationale (telemetry from
            // v2.8.7 confirmed appletGetFocusState doesn't transition for
            // qlaunch-replacement uMenu, so the focus-poll approach is dead).
            // Match canonical hbmenu pattern: server detects broken socket,
            // closes everything, exits, user manually re-enables from menu.
            UL_LOG_WARN("qdesktop: nxlink-server accept() errno=%d — self-stopping (broken socket)", errno);

            const int fd_to_close = g_server_fd.exchange(-1, std::memory_order_acq_rel);
            if (fd_to_close >= 0) {
                close(fd_to_close);
            }
            g_running.store(false, std::memory_order_release);
            g_state.store(State::Stopped, std::memory_order_release);

            UL_LOG_INFO("qdesktop: nxlink-server thread exiting (self-stop); user can re-enable via dropdown row 8");
            break;
        }

        UL_LOG_INFO("qdesktop: nxlink-server accepted client fd=%d", client);
        ReceiveOne(client);
        close(client);

        // Return to Listening after each transfer (success or failure).
        if (g_running.load(std::memory_order_acquire)) {
            g_state.store(State::Listening, std::memory_order_release);
        }
    }

    // 7. Cleanup: close server fd if Stop() hasn't already done it.
    {
        const int fd = g_server_fd.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) close(fd);
    }
    if (udp_fd >= 0) {
        close(udp_fd);
    }
    g_state.store(State::Stopped, std::memory_order_release);
    UL_LOG_INFO("qdesktop: nxlink-server thread exiting");
#endif // __SWITCH__
}

// ── ReceiveOne ────────────────────────────────────────────────────────────────
// HBmenu netloader wire protocol (from common/netloader.c::loadnro):
//   1. uint32_t namelen   (LE)
//   2. char     filename[namelen]
//   3. uint32_t filelen   (LE) — total COMPRESSED size
//   4. Server sends int response (0 = OK, negative = error)
//   5. Compressed payload as zlib chunks:
//        loop: uint32_t chunksize, then <chunksize> bytes of deflated data
//        until inflate returns Z_STREAM_END
//   6. Server sends int response (0 = OK) on success
//   7. uint32_t cmdlen, then cmdlen bytes of NUL-terminated argument strings

bool QdNxlinkServer::ReceiveOne(int client_fd) {
#ifdef __SWITCH__
    // Set receive timeout so a stalled host can't deadlock the server thread.
    {
        struct timeval tv {};
        tv.tv_sec  = kRcvTimeoutSec;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&tv), sizeof(tv));
    }

    // ── 1. Receive filename length ──────────────────────────────────────────
    uint32_t namelen = 0;
    if (recvall(client_fd, &namelen, sizeof(namelen)) != sizeof(namelen)) {
        UL_LOG_WARN("qdesktop: nxlink recv namelen failed errno=%d", errno);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }
    // namelen is LE on both Switch and x86 hosts — no swap needed on Switch.

    if (namelen == 0 || namelen >= PATH_MAX) {
        UL_LOG_WARN("qdesktop: nxlink bad namelen=%u", namelen);
        const int resp = -1;
        send(client_fd, &resp, sizeof(resp), 0);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }

    // ── 2. Receive filename ─────────────────────────────────────────────────
    char filename[PATH_MAX + 1] {};
    if (recvall(client_fd, filename, namelen) != static_cast<ssize_t>(namelen)) {
        UL_LOG_WARN("qdesktop: nxlink recv filename failed errno=%d", errno);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }
    filename[namelen] = '\0';

    // Extract just the basename to prevent path-traversal attacks.
    // newlib/devkitA64 doesn't expose POSIX basename(3), so do it inline:
    // take everything after the last '/' or '\' separator.  If neither is
    // present, the whole filename is the basename.
    const char *base = filename;
    const char *last_slash    = strrchr(filename, '/');
    const char *last_backslash = strrchr(filename, '\\');
    const char *last_sep = (last_slash > last_backslash) ? last_slash : last_backslash;
    if (last_sep != nullptr) {
        base = last_sep + 1;
    }
    if (*base == '\0') {
        UL_LOG_WARN("qdesktop: nxlink basename empty after stripping path");
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }

    // Build destination path: sdmc:/switch/<basename>.nro
    char dest_path[PATH_MAX + 1];
    snprintf(dest_path, sizeof(dest_path), "sdmc:/switch/%s", base);

    UL_LOG_INFO("qdesktop: nxlink incoming '%s' → '%s'", filename, dest_path);

    // Update UI filename under mutex.
    {
        std::lock_guard<std::mutex> lk(g_status_mu);
        g_last_filename   = base;
        g_bytes_received  = 0;
        g_total_bytes     = 0;
    }
    g_state.store(State::Receiving, std::memory_order_release);

    // ── 3. Receive compressed file length ──────────────────────────────────
    uint32_t filelen = 0;
    if (recvall(client_fd, &filelen, sizeof(filelen)) != sizeof(filelen)) {
        UL_LOG_WARN("qdesktop: nxlink recv filelen failed errno=%d", errno);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(g_status_mu);
        g_total_bytes = static_cast<size_t>(filelen);
    }

    // ── 4. Send OK response to unblock host ────────────────────────────────
    {
        const int resp = 0;
        if (send(client_fd, &resp, sizeof(resp), 0) < 0) {
            UL_LOG_WARN("qdesktop: nxlink send response errno=%d", errno);
            g_state.store(State::TransferFailed, std::memory_order_release);
            return false;
        }
    }

    // ── 5. Open destination file ────────────────────────────────────────────
    FILE *outfile = fopen(dest_path, "wb");
    if (outfile == nullptr) {
        UL_LOG_WARN("qdesktop: nxlink fopen '%s' failed errno=%d", dest_path, errno);
        const int resp = -1;
        send(client_fd, &resp, sizeof(resp), 0);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }

    // ── 6. Receive and decompress zlib chunks ──────────────────────────────
    // Protocol: loop reading (uint32 chunksize, <chunksize> bytes of deflate)
    // until inflate returns Z_STREAM_END.

    // Stack-allocated scratch buffers (server stack is 64 KB; two 16 KB bufs
    // = 32 KB — safe headroom).
    unsigned char in_buf[kZlibChunk];
    unsigned char out_buf[kZlibChunk];

    z_stream strm {};
    strm.zalloc  = Z_NULL;
    strm.zfree   = Z_NULL;
    strm.opaque  = Z_NULL;
    strm.next_in = Z_NULL;

    if (inflateInit(&strm) != Z_OK) {
        UL_LOG_WARN("qdesktop: nxlink inflateInit failed");
        fclose(outfile);
        unlink(dest_path);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }

    bool ok = true;
    size_t bytes_written = 0;
    int zret = Z_OK;

    do {
        // Receive chunk size prefix.
        uint32_t chunksize = 0;
        const ssize_t cn = recvall(client_fd, &chunksize, sizeof(chunksize));
        if (cn != sizeof(chunksize)) {
            UL_LOG_WARN("qdesktop: nxlink recv chunksize failed errno=%d n=%zd", errno, cn);
            ok = false;
            break;
        }
        if (chunksize == 0 || chunksize > sizeof(in_buf)) {
            UL_LOG_WARN("qdesktop: nxlink bad chunksize=%u", chunksize);
            ok = false;
            break;
        }

        // Receive compressed chunk.
        const ssize_t rn = recvall(client_fd, in_buf, chunksize);
        if (rn != static_cast<ssize_t>(chunksize)) {
            UL_LOG_WARN("qdesktop: nxlink recv chunk data failed errno=%d", errno);
            ok = false;
            break;
        }

        strm.avail_in = static_cast<uInt>(chunksize);
        strm.next_in  = in_buf;

        // Drain the inflate output.
        do {
            strm.avail_out = static_cast<uInt>(sizeof(out_buf));
            strm.next_out  = out_buf;
            zret = inflate(&strm, Z_NO_FLUSH);
            if (zret == Z_NEED_DICT || zret == Z_DATA_ERROR ||
                zret == Z_MEM_ERROR || zret == Z_STREAM_ERROR) {
                UL_LOG_WARN("qdesktop: nxlink inflate error %d", zret);
                ok = false;
                break;
            }
            const size_t have = sizeof(out_buf) - strm.avail_out;
            if (have > 0) {
                if (fwrite(out_buf, 1, have, outfile) != have) {
                    UL_LOG_WARN("qdesktop: nxlink fwrite failed errno=%d", errno);
                    ok = false;
                    break;
                }
                bytes_written += have;

                // Update progress under mutex.
                std::lock_guard<std::mutex> lk(g_status_mu);
                g_bytes_received = bytes_written;
            }
        } while (ok && strm.avail_out == 0);

    } while (ok && zret != Z_STREAM_END);

    inflateEnd(&strm);

    fflush(outfile);
    fclose(outfile);

    if (!ok) {
        UL_LOG_WARN("qdesktop: nxlink transfer failed — removing partial '%s'", dest_path);
        unlink(dest_path);
        g_state.store(State::TransferFailed, std::memory_order_release);
        return false;
    }

    UL_LOG_INFO("qdesktop: nxlink wrote %zu bytes → '%s'", bytes_written, dest_path);

    // ── 7. Send final OK + receive cmdline args ────────────────────────────
    {
        const int resp = 0;
        send(client_fd, &resp, sizeof(resp), 0);
    }

    // Capture cmdline args the host sends (length-prefixed buffer of
    // NUL-separated argv strings — hbmenu netloader.c::loadnro format).
    // We hold these in argv_buf for the auto-launch step below; the embedded
    // NULs are preserved by std::string(ptr, len) construction so hbloader
    // sees the same argv layout as if the host had launched directly.
    //
    // Safety cap: 1024 bytes (smaller than the 2048-byte NroArgvSize buffer
    // in TargetInput so CopyToStringBuffer never truncates the payload).
    static constexpr size_t kMaxCmdlineLen = 1024;
    std::string argv_buf;
    {
        uint32_t cmdlen = 0;
        if (recvall(client_fd, &cmdlen, sizeof(cmdlen)) == sizeof(cmdlen) &&
            cmdlen > 0 && cmdlen <= kMaxCmdlineLen) {
            argv_buf.resize(cmdlen);
            if (recvall(client_fd, argv_buf.data(), cmdlen) != static_cast<ssize_t>(cmdlen)) {
                UL_LOG_WARN("qdesktop: nxlink recv cmdline failed errno=%d — auto-launch skipped",
                            errno);
                argv_buf.clear();
            }
        } else if (cmdlen > kMaxCmdlineLen) {
            UL_LOG_WARN("qdesktop: nxlink cmdlen=%u exceeds %zu cap — draining and skipping auto-launch",
                        cmdlen, kMaxCmdlineLen);
            // Drain anyway so the host doesn't see a broken pipe.
            char drain[256];
            size_t remaining = cmdlen;
            while (remaining > 0) {
                const size_t take = remaining < sizeof(drain) ? remaining : sizeof(drain);
                if (recvall(client_fd, drain, take) != static_cast<ssize_t>(take)) break;
                remaining -= take;
            }
        }
    }

    // Signal main thread to rescan sdmc:/switch/.
    g_nxlink_scan_pending.store(true, std::memory_order_release);
    g_state.store(State::Done, std::memory_order_release);
    UL_LOG_INFO("qdesktop: nxlink transfer done — scan_pending set");

    // ── 8. Auto-launch the just-pushed NRO via uSystem ─────────────────────
    // Mirrors hbmenu netloader.c::loadnro — after a successful push, it fires
    // hbmenu's "launch this NRO" path with the received argv. Q OS routes
    // the same intent through uSystem via SMI: LaunchHomebrewLibraryApplet
    // takes (nro_path, nro_argv) and triggers hbloader. The argv buffer we
    // captured above (NUL-separated strings) flows verbatim through
    // util::CopyToStringBuffer (memcpy-based, embedded NULs preserved) into
    // TargetInput.nro_argv (2048 bytes), then hbloader unpacks it the same
    // way upstream hbmenu does.
    //
    // Hardcoded ALWAYS-launch — matches hbmenu's default behaviour. No
    // existing config flag for "auto-launch after netload"; if one is added
    // later (e.g. a "netload then return to menu" mode for archival pushes),
    // gate this block on it.
    {
        const Result rc_launch = smi::LaunchHomebrewLibraryApplet(
            std::string(dest_path), argv_buf);
        if (R_SUCCEEDED(rc_launch)) {
            UL_LOG_INFO("qdesktop: nxlink auto-launch dispatched '%s' (argv=%zu bytes)",
                        dest_path, argv_buf.size());
        } else {
            UL_LOG_WARN("qdesktop: nxlink auto-launch failed rc=0x%08X (NRO saved; user can launch manually)",
                        static_cast<unsigned>(rc_launch));
        }
    }
    return true;

#else // !__SWITCH__
    (void)client_fd;
    return false;
#endif
}

// ── Public API ───────────────────────────────────────────────────────────────

QdNxlinkServer::~QdNxlinkServer() {
    Stop();
}

bool QdNxlinkServer::Start() {
#ifdef __SWITCH__
    if (g_running.load(std::memory_order_acquire)) {
        UL_LOG_INFO("qdesktop: nxlink-server Start() already running — no-op");
        return true;
    }
    // v2.2.0 audio-skip fix: socketInitializeDefault() can take a noticeable
    // amount of time on first call and was being run on the MAIN thread during
    // MainMenuLayout::Initialize(), which blocked SDL_mixer's audio callback
    // long enough to cause BGM skipping.  Defer it to the server thread (the
    // first thing Run() does) so the main thread returns from Initialize()
    // immediately and the audio thread stays fed.  If socket init fails on
    // the server thread, Run() sets State::SocketInitFailed and exits cleanly.
    g_running.store(true, std::memory_order_release);
    g_state.store(State::Listening, std::memory_order_release);

    const Result rc_create = threadCreate(&g_NxlinkServerThread,
                                          &QdNxlinkServer::ThreadEntry,
                                          this,
                                          g_NxlinkServerStack,
                                          sizeof(g_NxlinkServerStack),
                                          /*priority=*/38,
                                          /*core=*/-2);
    if (R_FAILED(rc_create)) {
        UL_LOG_WARN("qdesktop: nxlink-server threadCreate rc=0x%08X",
                    static_cast<unsigned>(rc_create));
        g_running.store(false, std::memory_order_release);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    const Result rc_start = threadStart(&g_NxlinkServerThread);
    if (R_FAILED(rc_start)) {
        UL_LOG_WARN("qdesktop: nxlink-server threadStart rc=0x%08X",
                    static_cast<unsigned>(rc_start));
        threadClose(&g_NxlinkServerThread);
        g_running.store(false, std::memory_order_release);
        g_state.store(State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    UL_LOG_INFO("qdesktop: nxlink-server thread spawned (priority=38, core=-2)");
    return true;
#else
    return false;
#endif
}

void QdNxlinkServer::Stop() {
#ifdef __SWITCH__
    if (!g_running.load(std::memory_order_acquire)) {
        return;
    }
    g_running.store(false, std::memory_order_release);
    threadWaitForExit(&g_NxlinkServerThread);
    threadClose(&g_NxlinkServerThread);

    const int fd = g_server_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        close(fd);
    }
    g_state.store(State::Stopped, std::memory_order_release);
    UL_LOG_INFO("qdesktop: nxlink-server stopped");
#endif
}

bool QdNxlinkServer::IsRunning() const {
    return g_running.load(std::memory_order_acquire);
}

QdNxlinkServer::State QdNxlinkServer::GetState() const {
    return g_state.load(std::memory_order_acquire);
}

std::string QdNxlinkServer::GetLastFilename() const {
    std::lock_guard<std::mutex> lk(g_status_mu);
    return g_last_filename;
}

void QdNxlinkServer::GetProgress(size_t *bytes_received, size_t *total_bytes) const {
    std::lock_guard<std::mutex> lk(g_status_mu);
    if (bytes_received != nullptr) *bytes_received = g_bytes_received;
    if (total_bytes    != nullptr) *total_bytes    = g_total_bytes;
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
