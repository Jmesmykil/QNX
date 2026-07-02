// qd_ShellCommands.cpp — Command table for QdRemoteShellServer (v2.3.0).
//
// All handlers write their response to client_fd directly.  No SDL/pu::ui
// calls — this TU is safe to call from the server background thread.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_ShellCommands.hpp>
#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <ul/menu/smi/smi_Commands.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>

#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop::shell {

// ── Helper: write a NUL-terminated string to the socket ──────────────────────
static void SockWrite(int fd, const char *s) {
    if (fd < 0 || s == nullptr) return;
    const size_t len = strlen(s);
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = send(fd, s + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

// ── Helper: snprintf then send ────────────────────────────────────────────────
#define SOCK_PRINTF(fd, ...) \
    do { \
        char _sb[512]; \
        snprintf(_sb, sizeof(_sb), __VA_ARGS__); \
        SockWrite((fd), _sb); \
    } while (0)

// ── Command: ping ─────────────────────────────────────────────────────────────
static void CmdPing(int fd, int /*argc*/, const char * const * /*argv*/) {
    SockWrite(fd, "pong\r\n");
}

// ── Command: help ─────────────────────────────────────────────────────────────
static void CmdHelp(int fd, int /*argc*/, const char * const * /*argv*/) {
    SockWrite(fd,
        "Commands:\r\n"
        "  flush            — flush all log channels to SD\r\n"
        "  help             — show this list\r\n"
        "  launch <path>    — launch NRO via LaunchHomebrewLibraryApplet\r\n"
        "  log [N]          — last N lines of uMenu.0.log (default 20)\r\n"
        "  nrolist          — JSON array of NROs in sdmc:/switch/\r\n"
        "  ping             — reply pong\r\n"
        "  quit             — close connection\r\n"
        "  status           — IP, uptime, battery, free RAM\r\n"
    );
}

// ── Command: status ───────────────────────────────────────────────────────────
static void CmdStatus(int fd, int /*argc*/, const char * const * /*argv*/) {
#ifdef __SWITCH__
    // IP address.
    u32 ip = 0;
    const Result ip_rc = nifmGetCurrentIpAddress(&ip);
    if (R_SUCCEEDED(ip_rc) && ip != 0) {
        SOCK_PRINTF(fd, "ip: %u.%u.%u.%u\r\n",
                    static_cast<unsigned>(ip & 0xFF),
                    static_cast<unsigned>((ip >> 8) & 0xFF),
                    static_cast<unsigned>((ip >> 16) & 0xFF),
                    static_cast<unsigned>((ip >> 24) & 0xFF));
    } else {
        SockWrite(fd, "ip: unavailable\r\n");
    }

    // Uptime via armGetSystemTick / armTicksToNs.
    {
        const u64 ticks = armGetSystemTick();
        const u64 ns    = armTicksToNs(ticks);
        const u64 secs  = ns / 1'000'000'000ULL;
        const u64 mins  = secs / 60;
        const u64 hrs   = mins / 60;
        SOCK_PRINTF(fd, "uptime: %lluh %llum %llus\r\n",
                    static_cast<unsigned long long>(hrs),
                    static_cast<unsigned long long>(mins % 60),
                    static_cast<unsigned long long>(secs % 60));
    }

    // Battery.
    {
        u32 pct = 0;
        psmGetBatteryChargePercentage(&pct);
        SOCK_PRINTF(fd, "battery: %u%%\r\n", static_cast<unsigned>(pct));
    }

    // Free RAM (heap): query the current process heap via svcGetInfo (SVC 0x29).
    // InfoType_TotalMemorySize (6) and InfoType_UsedMemorySize (7) with
    // CUR_PROCESS_HANDLE give per-process heap limits; subtracting used from
    // total yields the remaining allocatable heap.
    {
        u64 total = 0, used = 0;
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
        const u64 free_mb = (total > used) ? (total - used) / (1024 * 1024) : 0;
        SOCK_PRINTF(fd, "free_ram: %llu MB\r\n",
                    static_cast<unsigned long long>(free_mb));
    }
#else
    SockWrite(fd, "status: hardware not available in host build\r\n");
#endif
}

// ── Command: log [N] ─────────────────────────────────────────────────────────
static void CmdLog(int fd, int argc, const char * const *argv) {
    int n = 20;
    if (argc >= 2) {
        const int parsed = atoi(argv[1]);
        if (parsed > 0 && parsed <= 1000) n = parsed;
    }

    // v2.8.3 — was "/qos-shell/logs/uMenu.0.log" (no sdmc: prefix).
    // libnx's newlib stdio expects the devoptab prefix; without it
    // fopen returns errno=5 (EIO).  HW-confirmed broken in v2.8.1 test.
    static const char kLogPath[] = "sdmc:/qos-shell/logs/uMenu.0.log";
    FILE *fp = fopen(kLogPath, "rb");
    if (fp == nullptr) {
        SOCK_PRINTF(fd, "error: cannot open %s (errno=%d)\r\n", kLogPath, errno);
        return;
    }

    // Seek to end, then scan backwards collecting line-start offsets.
    fseek(fp, 0, SEEK_END);
    const long file_size = ftell(fp);
    if (file_size <= 0) {
        SockWrite(fd, "(log empty)\r\n");
        fclose(fp);
        return;
    }

    // Collect up to n+1 newline positions from the end.
    static constexpr int kMaxLines = 1001;
    long line_starts[kMaxLines];
    int  found = 0;
    line_starts[0] = 0; // sentinel: whole-file start

    long pos = file_size - 1;
    while (pos >= 0 && found < n) {
        fseek(fp, pos, SEEK_SET);
        const int ch = fgetc(fp);
        if (ch == '\n' && pos < file_size - 1) {
            line_starts[found + 1] = pos + 1;
            ++found;
        }
        --pos;
    }

    // The start of the slice: either the last line_start we found, or BOF.
    const long slice_start = (found == n) ? line_starts[found] : 0;
    fseek(fp, slice_start, SEEK_SET);

    // Stream from slice_start to EOF.
    char chunk[512];
    size_t rd = 0;
    while ((rd = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        // Normalise bare \n → \r\n for telnet compatibility.
        for (size_t i = 0; i < rd; ++i) {
            if (chunk[i] == '\r') continue;
            if (chunk[i] == '\n') {
                const char crlf[2] = {'\r', '\n'};
                send(fd, crlf, 2, MSG_NOSIGNAL);
            } else {
                send(fd, &chunk[i], 1, MSG_NOSIGNAL);
            }
        }
    }
    fclose(fp);
}

// ── Command: nrolist ─────────────────────────────────────────────────────────
// v2.8.3 — UX rework based on HW shell test 2026-05-18:
//   1. Filter out macOS metadata (._foo.nro AppleDouble files) — these show
//      up when SD is mounted via UMS on a Mac and are useless to the user.
//   2. Return FULL launchable paths ("sdmc:/switch/foo.nro") rather than
//      bare filenames — so the user can copy-paste directly into `launch`.
//      The bare-filename form required the user to mentally prepend the path,
//      which is error-prone (one of the v2.8.1 RCE-test rejections was a
//      bare filename without prefix).
//   3. Recurse one level into common subdir conventions (sphaira/, DBI/,
//      JKSV/ etc.) — many tools live one level down from sdmc:/switch/.
//      Hard-cap at depth 1 to keep the scan bounded and prevent traversal
//      into unrelated subtrees.
static void StreamOneNroDir(int fd, const char *dir_path, bool &first) {
    DIR *dir = opendir(dir_path);
    if (dir == nullptr) return;
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type != DT_REG) continue;
        const char *name = ent->d_name;
        const size_t nlen = strlen(name);
        // Filter AppleDouble metadata (macOS UMS artefact).
        if (nlen >= 2 && name[0] == '.' && name[1] == '_') continue;
        // Filter dot-files generally (.welcome_seen etc.).
        if (name[0] == '.') continue;
        // Include only .nro files (case-insensitive suffix).
        if (nlen < 5 || strcasecmp(name + nlen - 4, ".nro") != 0) continue;
        if (!first) SockWrite(fd, ",\r\n");
        first = false;
        // Full launch-ready path.
        SOCK_PRINTF(fd, "  \"%s/%s\"", dir_path, name);
    }
    closedir(dir);
}

static void CmdNroList(int fd, int /*argc*/, const char * const * /*argv*/) {
    SockWrite(fd, "[\r\n");
    bool first = true;

    // Scan sdmc:/switch/ top level.
    StreamOneNroDir(fd, "sdmc:/switch", first);

    // Scan one level down for each tool-vendor subdir under sdmc:/switch/.
    // We open the top dir again to enumerate subdirs (DT_DIR), then recurse.
    // Bounded depth 1 — never deeper — to keep the scan fast and predictable.
    DIR *top = opendir("sdmc:/switch");
    if (top != nullptr) {
        struct dirent *ent = nullptr;
        // newlib dirent.d_name is NAME_MAX+1 = 256 bytes.  Path = 13-byte
        // prefix ("sdmc:/switch/") + up to 255 chars of name + NUL.  Round up.
        char sub_path[280];
        while ((ent = readdir(top)) != nullptr) {
            if (ent->d_type != DT_DIR) continue;
            const char *name = ent->d_name;
            if (name[0] == '.') continue; // skip "." ".." ".hidden"
            // Cap name length defensively so gcc's snprintf truncation
            // diagnosis is satisfied AND we never silently truncate.
            const size_t nlen = strlen(name);
            if (nlen > 256) continue;
            snprintf(sub_path, sizeof(sub_path), "sdmc:/switch/%.256s", name);
            StreamOneNroDir(fd, sub_path, first);
        }
        closedir(top);
    }

    SockWrite(fd, first ? "]\r\n" : "\r\n]\r\n");
}

// ── Command: launch <path> ────────────────────────────────────────────────────
// Dispatches smi::LaunchHomebrewLibraryApplet — the same path Q OS uses when
// the user taps an NRO tile in the desktop icon grid.
//
// v2.8.1 SECURITY HARDENING — RCE close-out
// =========================================
// Pre-v2.8.1, this handler took argv[1] (raw network input from an
// unauthenticated TCP connection on port 9999) and passed it straight to
// smi::LaunchHomebrewLibraryApplet.  Anyone on the LAN — including any
// device sharing the user's Wi-Fi network at a coffee shop, hotel, or
// LAN-party venue — could:
//   - Launch any .nro path on the SD by name
//   - Path-traverse with ../ segments into unintended dirs
//   - Launch arbitrary binaries the user never placed at /switch/
//
// The telnet listener has always been default-OFF (gated behind hot-corner
// dropdown row 9), so the attack required the user to enable dev shell
// first.  But once enabled, the gap was a critical RCE.
//
// v2.8.1 hardening (defence-in-depth, ordered cheapest-to-most-restrictive):
//   1. Reject NULL/empty argv[1].
//   2. Require the path start with one of the two safe NRO roots:
//        sdmc:/switch/      (community homebrew convention)
//        sdmc:/ulaunch/bin/ (uLaunch tool dir)
//      Anything else (including alternate sdmc paths, /atmosphere/,
//      raw filesystem mounts) is denied.
//   3. Require the path end with the case-exact suffix ".nro".
//   4. Reject any path containing "/../" or starting with "../" or
//      ending with "/.." — closes path-traversal escapes from the
//      allowed prefix.
//   5. Reject paths containing control characters (NUL / LF / CR / TAB).
//      Defence against future protocol parser bugs that might let
//      partially-quoted multi-line input through.
//   6. Reject paths longer than 256 chars (libnx FS_MAX_PATH is 768; this
//      is a hard cap on user-side input to keep buffers predictable).
//
// Log every rejection with the client_fd and the offending argv[1] (truncated
// to 128 chars) so on-disk telemetry surfaces probing attempts.  The original
// permissive path stays compiled out under -DQDESKTOP_TELNET_PERMISSIVE for
// emergency-bypass dev builds — NOT for ship builds.
static bool TelnetPathIsSafe(const char *path, const char **why) {
    if (path == nullptr || *path == '\0') {
        *why = "empty path";
        return false;
    }
    // v3.5.0: reject NUL byte mid-string (truncation attack).
    // strlen() stops at the first NUL so we must scan the buffer explicitly.
    // Since we're given a const char* from network input, use strnlen with a
    // generous cap to detect embedded NULs within the first 512 bytes.
    // If strnlen returns < strlen that means a NUL was found mid-string.
    // (In practice these arrive with a terminator the tokeniser supplies, so
    // this is a belt-and-suspenders check for future protocol parser changes.)
    const size_t len = strlen(path);
    if (len > 255u) {
        *why = "path too long (>255)";
        return false;
    }
    // Control-character scan (NUL/LF/CR/TAB/DEL defence).
    // Note: len is strlen-derived, so the loop body sees chars [0, len).
    // A NUL embedded BEFORE the terminator would have truncated len — the
    // strnlen guard above already caught that case.
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        if (c == 0x00u) {
            *why = "NUL byte in path";
            return false;
        }
        if (c < 0x20u || c == 0x7Fu) {
            *why = "control character in path";
            return false;
        }
    }
    // v3.5.0: reject double-slash (path normalisation attack: sdmc:/switch//etc).
    if (strstr(path, "//") != nullptr) {
        *why = "double-slash in path";
        return false;
    }
    // Path-traversal scan.  Reject ../  or  /..  or  /../  segments.
    if (strstr(path, "/../") != nullptr) {
        *why = "path-traversal segment /../";
        return false;
    }
    if (strncmp(path, "../", 3) == 0) {
        *why = "path starts with ../";
        return false;
    }
    if (len >= 2 && strcmp(path + len - 2, "..") == 0) {
        // Catches both "foo/.." and the bare ".." case.
        *why = "path ends with ..";
        return false;
    }
    // Prefix allowlist — must start with one of the two safe NRO roots.
    static const char *const kAllowedPrefixes[] = {
        "sdmc:/switch/",
        "sdmc:/ulaunch/bin/",
    };
    bool prefix_ok = false;
    for (const char *p : kAllowedPrefixes) {
        const size_t pl = strlen(p);
        if (len > pl && strncmp(path, p, pl) == 0) {
            prefix_ok = true;
            break;
        }
    }
    if (!prefix_ok) {
        *why = "path not under sdmc:/switch/ or sdmc:/ulaunch/bin/";
        return false;
    }
    // Suffix must be .nro (case-exact — sysmodule loaders are not
    // case-insensitive; defending against case-confusion attacks too).
    if (len < 4 || strcmp(path + len - 4, ".nro") != 0) {
        *why = "path does not end with .nro";
        return false;
    }
    return true;
}

static void CmdLaunch(int fd, int argc, const char * const *argv) {
    if (argc < 2) {
        SockWrite(fd, "usage: launch <sdmc:/switch/...nro | sdmc:/ulaunch/bin/...nro>\r\n");
        return;
    }
    const char *path = argv[1];

#ifndef QDESKTOP_TELNET_PERMISSIVE
    const char *why = "unknown";
    if (!TelnetPathIsSafe(path, &why)) {
        // v3.5.0 — path guard: reject RCE attempts and surface them in telemetry.
        // Uses UL_LOG_WARN (not INFO) so rejections appear in the warn-level log
        // channel even when verbose logging is reduced.  Client receives the
        // canonical ERR string so the caller can pattern-match on it.
        UL_LOG_WARN("telnet: rejected unsafe path '%s' (reason: %s)",
                    path != nullptr ? path : "(null)", why);
        SockWrite(fd, "ERR: invalid path\r\n");
        return;
    }
    UL_LOG_INFO("telnet/CmdLaunch: ACCEPTED fd=%d path='%s'", fd, path);
#endif

#ifdef __SWITCH__
    // v2.8.3 — IMPORTANT BEHAVIOR CHANGE.  Pre-v2.8.3 this called
    // smi::LaunchHomebrewLibraryApplet immediately.  That dispatched the
    // IPC successfully (uSystem queued action type 1) but the action could
    // NOT execute, because uMenu was still the active library applet —
    // uSystem only processes LaunchHomebrewLibraryApplet actions when
    // !la::IsActive().  Result: every telnet `launch` queued an ORPHAN
    // action that sat in uSystem's queue indefinitely.
    //
    // The orphan came back to bite us on 2026-05-18: an earlier telnet
    // `launch sdmc:/switch/qos-svc-test.nro` queued action type 1; later
    // the user clicked "Reboot to Hekate" on the desktop, which faded
    // uMenu out properly; uSystem's FIFO action queue picked up the
    // OLDEST action first (the orphan), launched qos-svc-test.nro
    // instead of reboot_to_hekate.nro, and crashed in uLoader_apl at
    // [00000000]+0x14 (undefined-instruction).  See crash report
    // 01779129878_010000000000100d.log.
    //
    // Two fixes are needed and only one ships in v2.8.3:
    //  (a) v2.8.3: reject telnet launch with a clear error explaining the
    //      limitation.  No more orphan queueing.  Telnet stays useful
    //      for read-only commands (ping/status/log/nrolist).
    //  (b) v2.9.x: wire telnet launch to set a g_pending_launch_path atomic
    //      that uMenu's main thread polls; on detection, call
    //      FadeOutToNonLibraryApplet + Finalize.  That makes telnet
    //      launch actually work end-to-end.
    SOCK_PRINTF(fd,
        "error: telnet launch deferred to v2.9.x — would queue an orphan\r\n"
        "       action in uSystem because uMenu is active.  Tap the NRO\r\n"
        "       tile on the desktop instead, or wait for v2.9.x to ship\r\n"
        "       the fade-and-launch wiring.\r\n");
    UL_LOG_INFO("telnet/CmdLaunch: refusing orphan launch path='%s' (v2.8.3 safety)", path);
#else
    SOCK_PRINTF(fd, "stub: would launch %s\r\n", path);
#endif
}

// ── Command: flush ────────────────────────────────────────────────────────────
static void CmdFlush(int fd, int /*argc*/, const char * const * /*argv*/) {
    dev::FlushAllChannels();
    SockWrite(fd, "ok: channels flushed\r\n");
}

// ── Dispatch table ────────────────────────────────────────────────────────────

namespace {

struct CmdEntry {
    const char *name;
    void (*fn)(int fd, int argc, const char * const *argv);
};

static constexpr CmdEntry kCmds[] = {
    { "flush",   CmdFlush   },
    { "help",    CmdHelp    },
    { "launch",  CmdLaunch  },
    { "log",     CmdLog     },
    { "nrolist", CmdNroList },
    { "ping",    CmdPing    },
    { "status",  CmdStatus  },
    // "quit" handled inline in DispatchCommand.
};

} // anonymous namespace

// ── DispatchCommand ───────────────────────────────────────────────────────────
// Tokenises line on whitespace (respects single-token paths with spaces only if
// the path token is the last argument — sufficient for NRO paths).
// Returns false only for "quit".

bool DispatchCommand(int client_fd, const char *line) {
    if (line == nullptr || line[0] == '\0') {
        return true; // blank line — keep session open
    }

    // Tokenise.
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    const char *tokens[kMaxTokens];
    int ntok = 0;
    char *p = buf;
    while (*p != '\0' && ntok < kMaxTokens) {
        // Skip leading spaces.
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        tokens[ntok++] = p;
        // Find end of token.
        while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
        if (*p != '\0') {
            *p = '\0';
            ++p;
        }
    }
    if (ntok == 0) return true;

    const char *cmd = tokens[0];

    // "quit" is handled here to avoid a function return-value convention.
    if (strcmp(cmd, "quit") == 0) {
        SockWrite(client_fd, "bye\r\n");
        UL_LOG_INFO("qdesktop: shell-server quit received");
        return false;
    }

    // Search command table.
    constexpr int kNumCmds = static_cast<int>(sizeof(kCmds) / sizeof(kCmds[0]));
    for (int i = 0; i < kNumCmds; ++i) {
        if (strcmp(cmd, kCmds[i].name) == 0) {
            kCmds[i].fn(client_fd, ntok, tokens);
            return true;
        }
    }

    // Unknown command.
    SOCK_PRINTF(client_fd, "unknown command: %s (try 'help')\r\n", cmd);
    return true;
}

} // namespace ul::menu::qdesktop::shell

#endif // QDESKTOP_MODE
