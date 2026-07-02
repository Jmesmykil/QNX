// qd_DebugServer.cpp — Remote-test layer: a tiny HTTP server inside uMenu.
//
// Socket / thread scaffold mirrors qd_RemoteShellServer.cpp (proven on HW):
// page-aligned stack, priority 38, core -2; 2 s WiFi settle; nifm IP bind (never
// INADDR_ANY); SIGPIPE ignored; self-stop on hard socket error.
//
// Protocol is HTTP/1.1 (one request per connection, Connection: close) so a host
// can drive it with plain curl.  Gated by sdmc:/ulaunch/debug.flag (opt-in).
//
// FIX-1 (2026-06-19): eliminate desktop double-tap.
//   /touch/<x>/<y> now feeds DrainSynth(nullptr) when the Main desktop is the
//   active layout, so only the HDLS raw tap fires.  Plutonium sim-touch
//   (DrainSynth with the layout ptr) is only fed when the active menu is NOT
//   MenuType::Main (login / lockscreen / menus that have no raw-HID pump).
//
// FIX-2 (2026-06-19): safe autonomous window-open route.
//   GET /openwin/<name>  opens a built-in desktop window directly via
//   QdDesktopIconsElement::Open*Window(), deferred to OnRenderFrame (UI thread).
//   GET /closeallwin     closes every open + minimized window (for clean
//   re-profiling).  Both use the /reboot flag-deferred pattern.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_DebugServer.hpp>
#include <ul/menu/qdesktop/qd_DebugCapsScreen.hpp>   // caps:sc live-screen JPEG
#include <ul/menu/qdesktop/qd_DebugObserve.hpp>       // safe observe-tier JSON
#include <ul/menu/qdesktop/qd_DebugHdls.hpp>          // hid:dbg HDLS input inject
#include <ul/menu/qdesktop/qd_InputInjector.hpp>      // synthetic-input queue
#include <ul/menu/smi/smi_Commands.hpp>               // reboot via reboot_to_hekate.nro
#include <ul/menu/ui/ui_MenuApplication.hpp>          // g_MenuApplication (reboot + menu-type query)
#include <ul/menu/ui/ui_MainMenuLayout.hpp>           // GetQdesktopIcons() (FIX-2 /openwin)
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include <mutex>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>

#include <vector>

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

// ⚠ DEV ONLY — MUST be 0 (or stripped) for any public/QNX release.  Gates the
// debug-server AUTO-START (creator mandate 2026-06-19: auto-on cannot ship).
// The manual hot-corner toggle + the /routes still ship; only auto-start is gated.
#define UL_DEBUG_SERVER_DEV 1

// Global menu application (defined in main.cpp) — used by /reboot to fade out +
// finalize before reboot_to_hekate.nro takes over.  Declared at global scope so
// the symbol resolves to ::g_MenuApplication (NOT ul::menu::qdesktop::).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

QdDebugServer g_DebugServer;

namespace {

static constexpr int kDebugPort     = 6010;
static constexpr int kPollTimeoutMs = 100;
static constexpr int kRecvTimeoutSec = 15;

#ifdef __SWITCH__
Thread g_DbgThread;
alignas(0x1000) constinit u8 g_DbgStack[64 * 1024]; // 64 KB, page-aligned (svcMapMemory)
#endif

std::atomic_bool g_running { false };
std::atomic<int> g_server_fd { -1 };
std::atomic<QdDebugServer::State> g_state { QdDebugServer::State::Stopped };
std::atomic_bool g_socket_init_done { false };

// Render-thread snapshot for /state (published by OnRenderFrame, read by server).
std::atomic<u32> g_snap_frame { 0 };
std::atomic<int> g_snap_focus { 0 };

// Frame-time profiler — set in OnRenderFrame, read by /state.  Lets the host
// measure the real on-device cost of optimizations (boot/100-window/frame).
std::atomic<u32> g_snap_ft_us     { 0 };  // avg frame time over last window (microseconds)
std::atomic<u32> g_snap_ft_max_us { 0 };  // max frame time over last window (microseconds)

// Dimensions of the last caps:sc frame (w/h populated by CaptureScreenJpeg via
// qd_DebugCapsScreen; reported in /state).
std::atomic<int> g_shot_w { 0 };
std::atomic<int> g_shot_h { 0 };

// /reboot — set by the server thread, serviced on the UI thread (OnRenderFrame),
// which launches reboot_to_hekate.nro (a lifecycle op that must run on the UI
// thread).  Reboots into Hekate → (autoboot) back into uMenu = the full loop.
std::atomic_bool g_reboot_requested { false };

// FIX-2: /openwin/<name> + /closeallwin — deferred to OnRenderFrame (UI thread),
// same flag pattern as /reboot.
//
// g_pending_open_win encodes which built-in window to open:
//   0 = none, 1 = Files/Vault, 2 = Monitor, 3 = Settings,
//   4 = About, 5 = Tasks, 6 = Nintendo, 7 = SaveEditor, 8 = Cheats
// g_closeallwin_requested tears down every open + minimized window.
std::atomic<int>  g_pending_open_win   { 0 };
std::atomic_bool  g_closeallwin_requested { false };

// /icons — request/response handshake between the server thread and the render
// thread.  The server thread sets g_icons_snap_requested; OnRenderFrame fills
// g_icons_snap_json (a pre-built JSON string) and sets g_icons_snap_ready.
// The server thread spin-polls g_icons_snap_ready for up to 500 ms (well
// within the 15 s recv timeout) then sends the pre-built body.
// A std::mutex serialises concurrent /icons requests (one at a time).
std::atomic_bool g_icons_snap_requested { false };
std::atomic_bool g_icons_snap_ready     { false };
std::mutex       g_icons_snap_mutex;          // guards g_icons_snap_json + handshake
std::string      g_icons_snap_json;

// /launchnro/<idx> — fire-and-forget, same pattern as /openwin.
// g_pending_launch_idx == -1 → nothing pending.
// Positive value → index into the icon grid; render thread calls LaunchIcon(idx).
std::atomic<int>  g_pending_launch_idx { -1 };

bool EnsureSocketInitialized() {
#ifdef __SWITCH__
    if (g_socket_init_done.load(std::memory_order_acquire)) return true;
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        UL_LOG_INFO("qdesktop: debug-server socketInitializeDefault rc=0x%08X "
                    "(already up elsewhere; proceeding)", static_cast<unsigned>(rc));
    } else {
        UL_LOG_INFO("qdesktop: debug-server socket subsystem up");
    }
    g_socket_init_done.store(true, std::memory_order_release);
    return true;
#else
    return false;
#endif
}

// ── HTTP response helper ────────────────────────────────────────────────────────
void SendHttp(int fd, const char *status, const char *ctype,
              const char *body, size_t body_len) {
    char hdr[256];
    const int hn = std::snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n", status, ctype, body_len);
    if (hn > 0) send(fd, hdr, static_cast<size_t>(std::min(hn, static_cast<int>(sizeof(hdr)) - 1)), MSG_NOSIGNAL);
    if (body_len > 0) send(fd, body, body_len, MSG_NOSIGNAL);
}

// ── Route one request (path already extracted, query stripped) ──────────────────
void HandleRequest(int fd, const char *path) {
    if (std::strcmp(path, "/ping") == 0) {
        static const char kPong[] = "pong\n";
        SendHttp(fd, "200 OK", "text/plain", kPong, sizeof(kPong) - 1);
        return;
    }
    if (std::strcmp(path, "/state") == 0) {
        char body[256];
        const int n = std::snprintf(body, sizeof(body),
            "{\"version\":\"%s\",\"running\":true,\"port\":%d,"
            "\"frame\":%u,\"focus\":%d,\"w\":%d,\"h\":%d,"
            "\"ft_us\":%u,\"ft_max_us\":%u}\n",
            UL_VERSION, kDebugPort,
            g_snap_frame.load(std::memory_order_relaxed),
            g_snap_focus.load(std::memory_order_relaxed),
            g_shot_w.load(std::memory_order_relaxed),
            g_shot_h.load(std::memory_order_relaxed),
            g_snap_ft_us.load(std::memory_order_relaxed),
            g_snap_ft_max_us.load(std::memory_order_relaxed));
        SendHttp(fd, "200 OK", "application/json", body,
                 n > 0 ? static_cast<size_t>(n) : 0);
        return;
    }
    if (std::strcmp(path, "/screenshot") == 0) {
        // caps:sc — system-service JPEG of the live composed screen.  Thread-safe
        // IPC, captured directly on the server thread (no render-thread handoff,
        // no SDL readback — this is the black-frame fix).
        std::vector<u8> out;
        if (CaptureScreenJpeg(out) && !out.empty()) {
            SendHttp(fd, "200 OK", "image/jpeg",
                     reinterpret_cast<const char *>(out.data()), out.size());
        } else {
            static const char kErr[] = "screenshot failed (caps:sc — check NPDM grant)\n";
            SendHttp(fd, "503 Service Unavailable", "text/plain", kErr, sizeof(kErr) - 1);
        }
        return;
    }
    if (std::strcmp(path, "/observe") == 0) {
        // Safe read-only system snapshot (temp/batt/wifi/fw/titles/fingerprint/…).
        const std::string j = BuildObserveJson();
        SendHttp(fd, "200 OK", "application/json", j.c_str(), j.size());
        return;
    }
    if (std::strncmp(path, "/press/", 7) == 0) {
        // /press/<BTN> — queue a synthetic button (held ~6 frames), injected via
        // HDLS next frame.  Drives uMenu AND any running game.
        const char *bn = path + 7;
        static const struct { const char *n; u64 m; } kBtn[] = {
            {"A",HidNpadButton_A},{"B",HidNpadButton_B},{"X",HidNpadButton_X},{"Y",HidNpadButton_Y},
            {"Up",HidNpadButton_Up},{"Down",HidNpadButton_Down},{"Left",HidNpadButton_Left},{"Right",HidNpadButton_Right},
            {"L",HidNpadButton_L},{"R",HidNpadButton_R},{"ZL",HidNpadButton_ZL},{"ZR",HidNpadButton_ZR},
            {"Plus",HidNpadButton_Plus},{"Minus",HidNpadButton_Minus},
            {"StickL",HidNpadButton_StickL},{"StickR",HidNpadButton_StickR},
        };
        u64 mask = 0;
        for (const auto &e : kBtn) if (std::strcmp(bn, e.n) == 0) { mask = e.m; break; }
        if (mask != 0) {
            g_InputInjector.EnqueuePress(mask, 6);
            static const char kOk[] = "queued\n";
            SendHttp(fd, "200 OK", "text/plain", kOk, sizeof(kOk) - 1);
        } else {
            static const char kBad[] = "unknown button (A B X Y Up Down Left Right L R ZL ZR Plus Minus StickL StickR)\n";
            SendHttp(fd, "400 Bad Request", "text/plain", kBad, sizeof(kBad) - 1);
        }
        return;
    }
    if (std::strncmp(path, "/touch/", 7) == 0) {
        // /touch/<x>/<y> — tap the touchscreen (coords in 1280x720 screen space,
        // matching the caps:sc screenshot pixels).  Held ~6 frames then lifted.
        const char *p = path + 7;
        const char *slash = std::strchr(p, '/');
        if (slash != nullptr) {
            const int x = std::atoi(p);
            const int y = std::atoi(slash + 1);
            if (x >= 0 && x < 1280 && y >= 0 && y < 720) {
                g_InputInjector.EnqueueTouch(x, y, 6);
                static const char kOk[] = "queued\n";
                SendHttp(fd, "200 OK", "text/plain", kOk, sizeof(kOk) - 1);
                return;
            }
        }
        static const char kBad[] = "usage: /touch/<x>/<y>  (x 0-1279, y 0-719)\n";
        SendHttp(fd, "400 Bad Request", "text/plain", kBad, sizeof(kBad) - 1);
        return;
    }
    if (std::strcmp(path, "/reboot") == 0) {
        // Reboot into Hekate (which, with autoboot, chains back into uMenu = the
        // full self-driving loop).  Deferred to the UI thread — the NRO launch +
        // app finalize cannot run on the server thread.
        g_reboot_requested.store(true, std::memory_order_release);
        static const char kOk[] = "rebooting into Hekate (autoboot -> uMenu)...\n";
        SendHttp(fd, "200 OK", "text/plain", kOk, sizeof(kOk) - 1);
        return;
    }
    if (std::strncmp(path, "/openwin/", 9) == 0) {
        // FIX-2: /openwin/<name> — open a named built-in desktop window without any
        // coordinate tap.  Deferred to the UI thread (OnRenderFrame) like /reboot.
        // Must be on the Main desktop (MenuType::Main) for the icons ref to be valid;
        // the UI thread checks this before dispatching.
        //
        // Recognised window names:
        //   files | vault    → Files (Vault)
        //   monitor          → Monitor
        //   settings         → Settings
        //   about            → About Q OS
        //   tasks            → Task Manager
        //   nintendo         → Nintendo Apps
        //   saveeditor | saves → Save Editor
        //   cheats           → Cheats
        const char *wn = path + 9;
        int which = 0;
        if (std::strcmp(wn, "files") == 0   || std::strcmp(wn, "vault") == 0)    which = 1;
        else if (std::strcmp(wn, "monitor") == 0)                                 which = 2;
        else if (std::strcmp(wn, "settings") == 0)                                which = 3;
        else if (std::strcmp(wn, "about") == 0)                                   which = 4;
        else if (std::strcmp(wn, "tasks") == 0)                                   which = 5;
        else if (std::strcmp(wn, "nintendo") == 0)                                which = 6;
        else if (std::strcmp(wn, "saveeditor") == 0 || std::strcmp(wn, "saves") == 0) which = 7;
        else if (std::strcmp(wn, "cheats") == 0)                                  which = 8;

        if (which != 0) {
            g_pending_open_win.store(which, std::memory_order_release);
            static const char kOk[] = "queued window open (UI thread)\n";
            SendHttp(fd, "200 OK", "text/plain", kOk, sizeof(kOk) - 1);
        } else {
            static const char kBad[] =
                "unknown window — names: files vault monitor settings about tasks nintendo saveeditor saves cheats\n";
            SendHttp(fd, "400 Bad Request", "text/plain", kBad, sizeof(kBad) - 1);
        }
        return;
    }
    if (std::strcmp(path, "/closeallwin") == 0) {
        // FIX-2: /closeallwin — close every open + minimized window on the UI thread.
        // Use between profiling runs so the desktop starts clean.
        g_closeallwin_requested.store(true, std::memory_order_release);
        static const char kOk[] = "queued close-all-windows (UI thread)\n";
        SendHttp(fd, "200 OK", "text/plain", kOk, sizeof(kOk) - 1);
        return;
    }
    if (std::strcmp(path, "/icons") == 0) {
        // /icons — read-only JSON array of every desktop icon.
        // Per-entry fields: idx, name, nro_path (empty if non-NRO), kind
        // (ClassifyKind int: 0=Unknown 1=NintendoGame 2=ThirdPartyGame
        // 3=HomebrewTool 4=Emulator 5=SystemUtil 6=Payload 7=Builtin),
        // icon_loaded (bool).
        //
        // Thread-safety: icons_[] is render-thread-owned.  Handshake:
        //   1. Serialise concurrent /icons calls with g_icons_snap_mutex —
        //      acquired NOW and held only across the JSON copy at the end
        //      (NOT across the spin-wait; holding it during the wait was the
        //      original deadlock: the render thread's try_lock always failed).
        //   2. Set g_icons_snap_requested (NO lock held); clear ready.
        //   3. Spin-poll g_icons_snap_ready for up to 500 ms (≈30 frames)
        //      with NO lock held — the render thread can now set it freely.
        //   4. Once ready, briefly lock to copy g_icons_snap_json into a
        //      local string, then release and send.
        //   5. Reset both atomics for the next call.
        std::unique_lock<std::mutex> lk(g_icons_snap_mutex);  // serialise callers
        g_icons_snap_ready.store(false, std::memory_order_release);
        g_icons_snap_requested.store(true, std::memory_order_release);
        lk.unlock();  // MUST release before spin-poll — render thread needs a clear path

        // Spin-poll up to ~500 ms (5000 × 100 µs).  No lock held.
        bool got_snap = false;
#ifdef __SWITCH__
        for (int i = 0; i < 5000; ++i) {
            if (g_icons_snap_ready.load(std::memory_order_acquire)) {
                got_snap = true;
                break;
            }
            svcSleepThread(100'000LL);  // 100 µs
        }
#endif
        // Re-acquire briefly to copy the JSON string (render thread is sole
        // writer; it already set ready, so the write is complete).
        std::string local_json;
        if (got_snap) {
            lk.lock();
            local_json = g_icons_snap_json;
            lk.unlock();
        }

        // Reset for next call (handler side; render thread resets requested).
        g_icons_snap_ready.store(false, std::memory_order_release);

        if (got_snap && !local_json.empty()) {
            SendHttp(fd, "200 OK", "application/json",
                     local_json.c_str(), local_json.size());
        } else {
            static const char kErr[] = "icons snapshot timed out (render thread busy)\n";
            SendHttp(fd, "503 Service Unavailable", "text/plain", kErr, sizeof(kErr) - 1);
        }
        return;
    }
    if (std::strncmp(path, "/launchnro/", 11) == 0) {
        // /launchnro/<idx> — launch the icon at the given grid index via the same
        // code path as a human tap (LaunchIconForDebug → LaunchIcon).
        // Deferred to OnRenderFrame exactly like /openwin — the render thread
        // calls LaunchIcon which triggers FadeOut+Finalize, finalizing uMenu;
        // the ACK response MUST be sent HERE before the defer fires.
        //
        // After the ACK the caller should expect the connection to drop because
        // LaunchIcon calls g_MenuApplication->Finalize() for NRO/Application
        // entries, which terminates the uMenu process and kills the server.
        const char *idx_str = path + 11;
        const int idx = std::atoi(idx_str);
        // Upper bound: MAX_ICONS = 512 (qd_DesktopIcons.hpp).  Render-thread
        // LaunchIcon() also guards via icon_count_, but we reject here first so
        // the HTTP caller gets a 400 rather than a silent no-op.
        static constexpr int kMaxIcons = 512;
        if (idx >= 0 && idx < kMaxIcons) {
            // Peek at the icons snapshot to build the ack name field.
            // We read g_icons_snap_json if available; if not, we send a minimal ack.
            // The deferred launch uses the live icons_ on the render thread so we
            // store the raw index, not a name.
            g_pending_launch_idx.store(idx, std::memory_order_release);
            // Build a lightweight ack JSON (name unknown at server-thread time).
            char ack[80];
            const int an = std::snprintf(ack, sizeof(ack),
                "{\"queued\":%d}\n", idx);
            SendHttp(fd, "200 OK", "application/json",
                     ack, an > 0 ? static_cast<size_t>(an) : 0);
        } else {
            static const char kBad[] = "usage: /launchnro/<idx>  (0 <= idx < 512)\n";
            SendHttp(fd, "400 Bad Request", "text/plain", kBad, sizeof(kBad) - 1);
        }
        return;
    }
#if UL_DEBUG_SERVER_DEV
    if (std::strcmp(path, "/crash") == 0) {
        // DEV ONLY: deliberately fatal uMenu to test crash auto-recovery
        // (Atmosphère fatal_auto_reboot_interval -> reboot-to-payload ->
        // autoboot -> uMenu).  Ack the request + flush before we die.
        static const char kCr[] = "crashing uMenu (fatalThrow 0xCAFE) to test auto-recovery...\n";
        SendHttp(fd, "200 OK", "text/plain", kCr, sizeof(kCr) - 1);
        svcSleepThread(250000000ULL);  // 250ms: let the socket flush first
        fatalThrow(0xCAFE);
    }
#endif
    // Unknown route.
    static const char kNF[] =
        "not found — routes: /ping /state /screenshot /observe"
        " /press/<BTN> /touch/<x>/<y> /reboot"
        " /openwin/<name> /closeallwin /icons /launchnro/<idx>\n";
    SendHttp(fd, "404 Not Found", "text/plain", kNF, sizeof(kNF) - 1);
}

// ── Serve one HTTP client: read request line, route, close ──────────────────────
void ServeClient(int client_fd) {
#ifdef __SWITCH__
    {
        struct timeval tv {};
        tv.tv_sec = kRecvTimeoutSec;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&tv), sizeof(tv));
    }

    // Read request headers until CRLFCRLF (or buffer full / timeout).
    char req[1024];
    int  rlen = 0;
    while (rlen < static_cast<int>(sizeof(req)) - 1) {
        char ch = '\0';
        const ssize_t n = recv(client_fd, &ch, 1, 0);
        if (n <= 0) break;
        req[rlen++] = ch;
        if (rlen >= 4 && req[rlen-4] == '\r' && req[rlen-3] == '\n' &&
            req[rlen-2] == '\r' && req[rlen-1] == '\n') break;
    }
    req[rlen] = '\0';

    // Parse the request line: "GET /path?query HTTP/1.1".
    char path[256] = "/";
    const char *sp1 = std::strchr(req, ' ');
    if (sp1 != nullptr) {
        const char *p = sp1 + 1;
        const char *sp2 = std::strchr(p, ' ');
        size_t len = sp2 ? static_cast<size_t>(sp2 - p) : std::strlen(p);
        // Strip the query string for routing (kept for Increment 3 parsing later).
        const char *q = static_cast<const char *>(std::memchr(p, '?', len));
        if (q != nullptr) len = static_cast<size_t>(q - p);
        if (len >= sizeof(path)) len = sizeof(path) - 1;
        std::memcpy(path, p, len);
        path[len] = '\0';
    }

    HandleRequest(client_fd, path);
#else
    (void)client_fd;
#endif
}

// ── Server thread ───────────────────────────────────────────────────────────────
void DbgThreadEntry(void *) {
#ifdef __SWITCH__
    UL_LOG_INFO("qdesktop: debug-server thread alive — port %d", kDebugPort);
    signal(SIGPIPE, SIG_IGN);
    svcSleepThread(2'000'000'000LL); // 2 s WiFi settle

    if (!EnsureSocketInitialized()) {
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    int srv = -1;
    for (int a = 0; a < 10 && g_running.load(std::memory_order_acquire); ++a) {
        srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv >= 0) break;
        svcSleepThread(1'000'000'000LL);
    }
    if (srv < 0) {
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }
    { int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR,
                              reinterpret_cast<const char *>(&opt), sizeof(opt)); }

    // Bind to the nifm IP (never INADDR_ANY).  Refuse loopback / no-IP.
    u32 bind_ip = 0;
    bool ip_ready = false;
    for (int a = 0; a < 30 && g_running.load(std::memory_order_acquire); ++a) {
        const Result rc = nifmGetCurrentIpAddress(&bind_ip);
        if (R_SUCCEEDED(rc) && bind_ip != 0 && (bind_ip & 0xFFu) != 127u) {
            ip_ready = true; break;
        }
        svcSleepThread(1'000'000'000LL);
    }
    if (!ip_ready) {
        UL_LOG_WARN("qdesktop: debug-server no usable nifm IP — refusing to bind");
        close(srv);
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    bool bound = false;
    for (int a = 0; a < 10 && g_running.load(std::memory_order_acquire); ++a) {
        struct sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = bind_ip;   // network byte order from nifm
        addr.sin_port        = htons(static_cast<uint16_t>(kDebugPort));
        if (bind(srv, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0) {
            bound = true; break;
        }
        svcSleepThread(1'000'000'000LL);
    }
    if (!bound || listen(srv, 1) != 0) {
        UL_LOG_WARN("qdesktop: debug-server bind/listen failed errno=%d", errno);
        close(srv);
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        g_running.store(false, std::memory_order_release);
        return;
    }

    g_server_fd.store(srv, std::memory_order_release);
    g_state.store(QdDebugServer::State::Listening, std::memory_order_release);
    UL_LOG_INFO("debug-server: listening on %u.%u.%u.%u:%d (curl /ping /state)",
                static_cast<unsigned>(bind_ip & 0xFF),
                static_cast<unsigned>((bind_ip >> 8) & 0xFF),
                static_cast<unsigned>((bind_ip >> 16) & 0xFF),
                static_cast<unsigned>((bind_ip >> 24) & 0xFF), kDebugPort);

    while (g_running.load(std::memory_order_acquire)) {
        struct pollfd pfd {};
        pfd.fd = srv; pfd.events = POLLIN;
        const int pr = poll(&pfd, 1, kPollTimeoutMs);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0 || !(pfd.revents & POLLIN)) continue;

        struct sockaddr_in remote {};
        socklen_t rlen = sizeof(remote);
        const int client = accept(srv, reinterpret_cast<struct sockaddr *>(&remote), &rlen);
        if (client < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) continue;
            UL_LOG_WARN("qdesktop: debug-server accept() errno=%d — self-stopping", errno);
            const int f = g_server_fd.exchange(-1, std::memory_order_acq_rel);
            if (f >= 0) close(f);
            g_running.store(false, std::memory_order_release);
            g_state.store(QdDebugServer::State::Stopped, std::memory_order_release);
            break;
        }
        g_state.store(QdDebugServer::State::Connected, std::memory_order_release);
        ServeClient(client);
        close(client);
        if (g_running.load(std::memory_order_acquire))
            g_state.store(QdDebugServer::State::Listening, std::memory_order_release);
    }

    { const int f = g_server_fd.exchange(-1, std::memory_order_acq_rel); if (f >= 0) close(f); }
    g_state.store(QdDebugServer::State::Stopped, std::memory_order_release);
    UL_LOG_INFO("qdesktop: debug-server thread exiting");
#endif // __SWITCH__
}

} // anonymous namespace

// ── Public API ──────────────────────────────────────────────────────────────────

QdDebugServer::~QdDebugServer() { Stop(); }

bool QdDebugServer::Start() {
#ifdef __SWITCH__
    if (g_running.load(std::memory_order_acquire)) return true;
    // Manually toggled from the right hot-corner dropdown (off by default — like
    // the nxlink and remote-shell servers).  No always-on flag.
    g_running.store(true, std::memory_order_release);
    g_state.store(QdDebugServer::State::Listening, std::memory_order_release);
    const Result rc = threadCreate(&g_DbgThread, &DbgThreadEntry, this,
                                   g_DbgStack, sizeof(g_DbgStack), 38, -2);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qdesktop: debug-server threadCreate rc=0x%08X", static_cast<unsigned>(rc));
        g_running.store(false, std::memory_order_release);
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    if (R_FAILED(threadStart(&g_DbgThread))) {
        threadClose(&g_DbgThread);
        g_running.store(false, std::memory_order_release);
        g_state.store(QdDebugServer::State::SocketInitFailed, std::memory_order_release);
        return false;
    }
    // Keep the Switch awake while remote-testing: auto-sleep drops WiFi and
    // truncates FTP transfers / screenshots (creator: "it keeps going to sleep").
    appletSetAutoSleepDisabled(true);
    UL_LOG_INFO("qdesktop: debug-server thread spawned (manual toggle, port %d, auto-sleep OFF)", kDebugPort);
    return true;
#else
    return false;
#endif
}

void QdDebugServer::Stop() {
#ifdef __SWITCH__
    if (!g_running.load(std::memory_order_acquire)) return;
    g_running.store(false, std::memory_order_release);
    appletSetAutoSleepDisabled(false);  // restore normal auto-sleep when not testing
    const int f = g_server_fd.exchange(-1, std::memory_order_acq_rel);
    if (f >= 0) close(f);
    threadWaitForExit(&g_DbgThread);
    threadClose(&g_DbgThread);
    g_state.store(QdDebugServer::State::Stopped, std::memory_order_release);
#endif
}

bool QdDebugServer::IsRunning() const {
    return g_running.load(std::memory_order_acquire);
}

QdDebugServer::State QdDebugServer::GetState() const {
    return g_state.load(std::memory_order_acquire);
}

void QdDebugServer::OnRenderFrame(u32 frame) {
#ifdef __SWITCH__
#if UL_DEBUG_SERVER_DEV
    // DEV ONLY (compiled out of release): auto-start IFF sdmc:/ulaunch/debug.flag
    // exists, so the server returns by itself after a /reboot (hands-off
    // deploy→reboot→test loop).  No flag = stays manual-toggle / off by default.
    static bool s_autostart_checked = false;
    if (!s_autostart_checked) {
        s_autostart_checked = true;
        FILE *ff = std::fopen("sdmc:/ulaunch/debug.flag", "rb");
        if (ff != nullptr) { std::fclose(ff); Start(); }
    }
#endif

    if (!g_running.load(std::memory_order_acquire)) return;  // off: zero per-frame cost

    // Serviced /reboot request: reboot-to-payload via reboot_to_hekate.nro ->
    // Hekate -> autoboot=4 -> CFW(EMUMMC) -> uMenu.  PROVEN to reach CFW.  Shows
    // the NRO's "press +" prompt but NEVER reboots to stock.
    //
    // !! DO NOT use plain bpcRebootSystem() here: it does a NORMAL reboot ->
    // STOCK OFW (needs RCM re-injection), NOT reboot-to-payload.  It cost an
    // injection on 2026-06-19.  The fatal-auto-reboot reaches CFW because the
    // *fatal* reboots TO PAYLOAD (reboot_payload.bin=fusee); a bare bpc reboot
    // does not.  Clean prompt-free path = amsBpcSetRebootPayload()+bpc (what
    // this NRO does internally) — TODO, test deliberately with VOL- ready.
    // Proven prompt-free path today = the /crash fatal auto-reboot.
    if (g_reboot_requested.exchange(false, std::memory_order_acq_rel)) {
        UL_LOG_INFO("debug-server: /reboot -> reboot_to_hekate.nro (proven -> CFW)");
        smi::LaunchHomebrewLibraryApplet(
            std::string("sdmc:/switch/reboot_to_hekate.nro"), std::string(""));
        if (::g_MenuApplication) {
            ::g_MenuApplication->FadeOutToNonLibraryApplet();
            ::g_MenuApplication->Finalize();
        }
        return;
    }

    // Publish a snapshot for /state (lock-free; server thread reads these).
    g_snap_frame.store(frame, std::memory_order_relaxed);
    g_snap_focus.store(static_cast<int>(appletGetFocusState()), std::memory_order_relaxed);

    // Frame-time profiler: wall-time between OnRenderFrame calls (UI thread),
    // rolling avg + max over a 32-frame window.  System tick is 19.2 MHz.
    {
        static u64 s_last_tick = 0;
        static u32 s_sum_us = 0, s_cnt = 0, s_max_us = 0;
        const u64 now = armGetSystemTick();
        if (s_last_tick != 0) {
            const u32 dt_us = static_cast<u32>((now - s_last_tick) * 10ULL / 192ULL);
            s_sum_us += dt_us; ++s_cnt;
            if (dt_us > s_max_us) s_max_us = dt_us;
            if (s_cnt >= 32) {
                g_snap_ft_us.store(s_sum_us / s_cnt, std::memory_order_relaxed);
                g_snap_ft_max_us.store(s_max_us, std::memory_order_relaxed);
                s_sum_us = 0; s_cnt = 0; s_max_us = 0;
            }
        }
        s_last_tick = now;
    }

    // Input-injection drive (UI thread).  Ensure the HDLS virtual controller once,
    // then push this frame's queued synthetic buttons to it.  HDLS is system-wide,
    // so this drives uMenu AND any running game.  (Screenshot now uses caps:sc
    // directly on the server thread — no render-thread handoff here anymore.)
    static bool s_hdls_tried = false;
    if (!s_hdls_tried) { s_hdls_tried = true; (void)g_DebugHdls.Ensure(); }
    if (g_DebugHdls.IsReady()) {
        // FIX-1 (2026-06-19): eliminate desktop double-tap.
        //
        // The Main desktop (MenuType::Main) owns a raw-HID pump (qd_Input.cpp
        // pump_input / hidGetTouchScreenStates).  Plutonium also calls
        // ConsumeSimulatedTouchPosition() and passes the result to every element's
        // OnInput() via the layout's touch_pos argument.  Both paths are live on
        // the desktop, so passing a non-null layout to DrainSynth caused ONE
        // /touch request to register as TWO taps at two different coordinates:
        //   • HDLS raw tap  → raw 1280×720 coord  → pump_input → icons OnInput
        //   • Plutonium sim → 1920×1080 scaled coord → layout OnInput → icons OnInput
        // The second tap hit a different icon and / or triggered a spurious press,
        // causing wrong launches and device hangs (sphaira incident 2026-06-19).
        //
        // Fix: only pass the active layout to DrainSynth when the active menu is
        // NOT MenuType::Main.  Login / lockscreen / menu screens have no raw-HID
        // pump, so they need Plutonium sim-touch for autonomous login to work.
        // The Main desktop uses only the HDLS raw tap (DrainSynth(nullptr) still
        // sets out.touch/tx/ty so HDLS SetTouch fires correctly; it just skips
        // the SimulateTouchPosition call).
        const bool is_main_desktop = ::g_MenuApplication &&
            (::g_MenuApplication->GetLoadedMenuType() == ul::menu::ui::MenuType::Main);

        pu::ui::Layout *lyt_for_synth =
            (!is_main_desktop && ::g_MenuApplication)
                ? ::g_MenuApplication->GetLayout<pu::ui::Layout>().get()
                : nullptr;

        const SynthInput s = g_InputInjector.DrainSynth(lyt_for_synth);
        (void)g_DebugHdls.SetState(s.down | s.held, 0, 0, 0, 0);
        if (s.touch) (void)g_DebugHdls.SetTouch(s.tx, s.ty);  // tap the touchscreen
        else         g_DebugHdls.ClearTouch();                // lift when no touch this frame
    }

    // FIX-2 (2026-06-19): /openwin and /closeallwin — drain on UI thread.
    // Must be on MenuType::Main and the icons element must be live.
    {
        const int open_which = g_pending_open_win.exchange(0, std::memory_order_acq_rel);
        const bool close_all = g_closeallwin_requested.exchange(false, std::memory_order_acq_rel);

        if ((open_which != 0 || close_all) && ::g_MenuApplication) {
            auto &main_lyt = ::g_MenuApplication->GetMainMenuLayout();
            if (main_lyt) {
                auto icons = main_lyt->GetQdesktopIcons();
                if (icons) {
                    if (close_all) {
                        icons->CloseAllWindows();
                        UL_LOG_INFO("debug-server: /closeallwin — all windows closed");
                    }
                    if (open_which != 0) {
                        switch (open_which) {
                            case 1: icons->OpenVaultWindow();         break;
                            case 2: icons->OpenMonitorWindow();       break;
                            case 3: icons->OpenSettingsWindow();      break;
                            case 4: icons->OpenAboutWindow();         break;
                            case 5: icons->OpenTaskManagerWindow();   break;
                            case 6: icons->OpenNintendoAppsWindow();  break;
                            case 7: icons->OpenSaveEditorWindow();    break;
                            case 8: icons->OpenCheatsWindow(0);       break;
                            default: break;
                        }
                        UL_LOG_INFO("debug-server: /openwin — opened window %d", open_which);
                    }
                }
            }
        }
    }

    // /icons — drain on the UI thread: build the JSON snapshot and signal the
    // server thread.
    //
    // The handler releases g_icons_snap_mutex BEFORE entering its spin-poll,
    // so we never contend with it here.  We are the sole writer of
    // g_icons_snap_json (the handler reads it only after ready==true, which
    // provides the happens-before ordering), so no lock is needed for the
    // write itself.  We do briefly lock when writing the string so that
    // the handler's lock-protected copy sees a consistent value.
    if (g_icons_snap_requested.load(std::memory_order_acquire)) {
        g_icons_snap_requested.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(g_icons_snap_mutex);
            g_icons_snap_json.clear();
            if (::g_MenuApplication) {
                auto &main_lyt = ::g_MenuApplication->GetMainMenuLayout();
                if (main_lyt) {
                    auto icons = main_lyt->GetQdesktopIcons();
                    if (icons) {
                        g_icons_snap_json = icons->SnapshotIconsJson();
                    }
                }
            }
        }  // mutex released before ready.store — handler can proceed immediately
        g_icons_snap_ready.store(true, std::memory_order_release);
        UL_LOG_INFO("debug-server: /icons snapshot built (%zu bytes)",
                    g_icons_snap_json.size());
    }

    // /launchnro/<idx> — drain on the UI thread.  Must come AFTER the /icons
    // drain so a host can call GET /icons then GET /launchnro/<n> in sequence
    // within the same uMenu session.  LaunchIcon triggers FadeOut+Finalize for
    // NRO/Application entries, terminating uMenu; the ACK was already sent by
    // the server thread before g_pending_launch_idx was set, so the host has
    // already received the response.
    {
        const int launch_idx = g_pending_launch_idx.exchange(-1, std::memory_order_acq_rel);
        if (launch_idx >= 0 && ::g_MenuApplication) {
            auto &main_lyt = ::g_MenuApplication->GetMainMenuLayout();
            if (main_lyt) {
                auto icons = main_lyt->GetQdesktopIcons();
                if (icons) {
                    UL_LOG_INFO("debug-server: /launchnro/%d — calling LaunchIconForDebug",
                                launch_idx);
                    icons->LaunchIconForDebug(static_cast<size_t>(launch_idx));
                }
            }
        }
    }
#else
    (void)frame;
#endif
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
