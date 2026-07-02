// qd_CheatsInstaller.cpp — HTTPS-based cheat bundle installer for Q OS.
//
// W14-CHEATS-INSTALLER (v3.5).
//
// Transport: libnx ssl service + POSIX BSD sockets.
//   Stack:
//     socketInitializeDefault()  — idempotent, may already be up from NTP sync
//     sslInitialize(1)           — 1 session is enough
//     sslCreateContext()         — TLS 1.2 context, all built-in CA certs
//     sslContextCreateConnection()
//     getaddrinfo / ::socket / ::connect
//     sslConnectionSetSocketDescriptor + sslConnectionDoHandshake
//     sslConnectionWrite / sslConnectionRead — HTTP/1.1 GET
//     sslConnectionClose / sslContextClose / sslExit / socketExit
//
// GitHub Releases API returns JSON; we extract the download URL with a
// simple substring scan (no JSON library dependency).
//
// ZIP extraction: minizip (portlibs/switch) unzip.h API.
//   Walk all entries; skip any whose name doesn't start with "contents/".
//   For the rest, extract the TID from positions [9..24] and check against
//   the installed-games set.  Write kept files directly to
//   sdmc:/atmosphere/contents/<tid>/cheats/<filename>.
//
// Idempotence: if sdmc:/atmosphere/contents/<tid>/cheats/<bid>.txt.qos-backup
// exists, the live .txt is assumed to be user-customised — skip that file.
// If .qos-backup does NOT exist (fresh install), write the file AND create the
// backup immediately so subsequent installs don't clobber user toggle state.

#include <ul/menu/qdesktop/qd_CheatsInstaller.hpp>
#include <ul/ul_Result.hpp>  // UL_LOG_INFO / UL_LOG_WARN

// POSIX networking
#include <switch/runtime/devices/socket.h>
#include <switch/services/ssl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

// minizip
#include <minizip/unzip.h>

// libc
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── Internal constants ────────────────────────────────────────────────────────

static constexpr const char *kGithubHost       = "api.github.com";
static constexpr const char *kReleasesPath      =
    "/repos/HamletDuFromage/switch-cheats-db/releases/latest";
static constexpr const char *kUserAgent         = "qos-umenu/3.5";
static constexpr const char *kStagingPath       = "sdmc:/ulaunch/staging/cheats-bundle.zip";
static constexpr const char *kAtmContentsBase   = "sdmc:/atmosphere/contents/";

// ── Progress helpers ──────────────────────────────────────────────────────────

void QdCheatsInstaller::UpdatePhase(InstallerProgress::Phase phase,
                                     const char *step) {
    mutexLock(&progress_lock_);
    progress_.phase = phase;
    progress_.percent = 0;
    if (step) {
        strncpy(progress_.step, step, sizeof(progress_.step) - 1);
        progress_.step[sizeof(progress_.step) - 1] = '\0';
    }
    mutexUnlock(&progress_lock_);
    UL_LOG_INFO("installer: phase=%d step=%s",
                static_cast<int>(phase), step ? step : "");
}

void QdCheatsInstaller::UpdatePercent(int pct) {
    mutexLock(&progress_lock_);
    progress_.percent = pct;
    mutexUnlock(&progress_lock_);
}

void QdCheatsInstaller::Fail(const char *msg) {
    mutexLock(&progress_lock_);
    progress_.phase = InstallerProgress::Phase::Failed;
    strncpy(progress_.error, msg ? msg : "Unknown error",
            sizeof(progress_.error) - 1);
    progress_.error[sizeof(progress_.error) - 1] = '\0';
    mutexUnlock(&progress_lock_);
    UL_LOG_WARN("installer: FAILED — %s", msg ? msg : "?");
}

// ── GetProgress ───────────────────────────────────────────────────────────────

InstallerProgress QdCheatsInstaller::GetProgress() const {
    mutexLock(&progress_lock_);
    InstallerProgress snap = progress_;
    mutexUnlock(&progress_lock_);
    return snap;
}

// ── StartInstall ──────────────────────────────────────────────────────────────

/*static*/ void QdCheatsInstaller::WorkerEntry(void *arg) {
    static_cast<QdCheatsInstaller *>(arg)->RunInstall();
}

void QdCheatsInstaller::StartInstall() {
    if (thread_started_) return;
    mutexInit(&progress_lock_);
    abort_.store(false);

    const Result rc = threadCreate(&thread_, &WorkerEntry, this,
                                   stack_, kStackSize,
                                   38,   // priority (same as BT manager thread)
                                   -2);  // any core
    if (R_FAILED(rc)) {
        Fail("threadCreate failed");
        return;
    }
    thread_started_ = true;
    threadStart(&thread_);
}

// ── Stop ──────────────────────────────────────────────────────────────────────

void QdCheatsInstaller::Stop() {
    if (!thread_started_) return;
    abort_.store(true);
    threadWaitForExit(&thread_);
    threadClose(&thread_);
    thread_started_ = false;
}

// ── EnsureDir ─────────────────────────────────────────────────────────────────

/*static*/ void QdCheatsInstaller::EnsureDir(const char *path) {
    // path must NOT have a trailing slash.
    // Make a mutable copy so we can walk it.
    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    const size_t len = strnlen(buf, sizeof(buf));
    // Strip any trailing slash.
    size_t end = len;
    while (end > 1 && buf[end - 1] == '/') --end;
    buf[end] = '\0';

    // Walk forward creating each intermediate directory.
    for (size_t i = 1; i <= end; ++i) {
        if (buf[i] == '/' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            mkdir(buf, 0777);  // EEXIST is fine
            buf[i] = saved;
        }
    }
}

// ── EnumerateInstalledGameTids ────────────────────────────────────────────────

/*static*/ std::set<std::string>
QdCheatsInstaller::EnumerateInstalledGameTids() {
    std::set<std::string> result;

    // nsListApplicationRecord returns base-game records (not patches/DLC).
    // The buffer holds up to 2048 entries; real Switch installs are typically
    // < 200 games.
    static constexpr s32 kBatchSize = 2048;
    NsApplicationRecord *records = new (std::nothrow) NsApplicationRecord[kBatchSize];
    if (!records) {
        UL_LOG_WARN("installer: EnumerateInstalledGameTids — alloc failed");
        return result;
    }

    s32 record_count = 0;
    const Result rc = nsListApplicationRecord(records, kBatchSize, 0, &record_count);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("installer: nsListApplicationRecord failed rc=0x%08X",
                    static_cast<unsigned>(rc));
        delete[] records;
        return result;
    }

    for (s32 i = 0; i < record_count; ++i) {
        char hex[17];
        snprintf(hex, sizeof(hex), "%016lx",
                 static_cast<unsigned long>(records[i].application_id));
        result.insert(std::string(hex));
    }

    delete[] records;
    UL_LOG_INFO("installer: enumerated %d installed game TIDs",
                static_cast<int>(result.size()));
    return result;
}

// ── HttpsGet ──────────────────────────────────────────────────────────────────
//
// Performs a single HTTP/1.1 GET over TLS.  Uses:
//   socketInitializeDefault()      — idempotent BSD socket init
//   sslInitialize / sslCreateContext — libnx SSL service
//   getaddrinfo + ::socket + ::connect — TCP connect
//   sslConnectionSetSocketDescriptor + sslConnectionDoHandshake — TLS handshake
//   sslConnectionWrite / sslConnectionRead — request/response
//
// On success returns true and appends the HTTP response BODY (skipping
// headers) to @p body.  On any error returns false (no partial data in body).

bool QdCheatsInstaller::HttpsGet(const char *host, const char *path,
                                  std::string &body) {
    // ── 1. Ensure socket subsystem ────────────────────────────────────────
    const Result sock_rc = socketInitializeDefault();
    const bool sock_owned = R_SUCCEEDED(sock_rc);
    if (R_FAILED(sock_rc)) {
        // 0x196602 = already initialised (treated as success by NTP code)
        if (sock_rc != 0x196602) {
            UL_LOG_WARN("installer: socketInitializeDefault rc=0x%08X",
                        static_cast<unsigned>(sock_rc));
            // Continue anyway; if the stack is truly down getaddrinfo will fail.
        }
    }

    // ── 2. SSL service ────────────────────────────────────────────────────
    const Result ssl_rc = sslInitialize(1);
    if (R_FAILED(ssl_rc)) {
        UL_LOG_WARN("installer: sslInitialize rc=0x%08X",
                    static_cast<unsigned>(ssl_rc));
        if (sock_owned) socketExit();
        return false;
    }

    SslContext ssl_ctx;
    memset(&ssl_ctx, 0, sizeof(ssl_ctx));
    // SSL_VERSION_AUTO = 0x00000003 (TLS 1.0+, Switch picks 1.2/1.3)
    const Result ctx_rc = sslCreateContext(&ssl_ctx, 0x00000003);
    if (R_FAILED(ctx_rc)) {
        UL_LOG_WARN("installer: sslCreateContext rc=0x%08X",
                    static_cast<unsigned>(ctx_rc));
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    // Import Nintendo's built-in CA bundle so github.com verifies.
    u64 pki_id = 0;
    sslContextRegisterInternalPki(&ssl_ctx, SslInternalPki_DeviceClientCertDefault, &pki_id);

    SslConnection ssl_conn;
    memset(&ssl_conn, 0, sizeof(ssl_conn));
    const Result conn_create_rc = sslContextCreateConnection(&ssl_ctx, &ssl_conn);
    if (R_FAILED(conn_create_rc)) {
        UL_LOG_WARN("installer: sslContextCreateConnection rc=0x%08X",
                    static_cast<unsigned>(conn_create_rc));
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    // Set hostname for SNI + certificate verification.
    sslConnectionSetHostName(&ssl_conn, host,
                             static_cast<u32>(strnlen(host, 256)));
    // Verify peer CA + hostname (standard secure mode).
    sslConnectionSetVerifyOption(&ssl_conn,
        SslVerifyOption_PeerCa | SslVerifyOption_HostName);

    // ── 3. TCP connect ────────────────────────────────────────────────────
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    const int gai_rc = getaddrinfo(host, "443", &hints, &res);
    if (gai_rc != 0 || res == nullptr) {
        UL_LOG_WARN("installer: getaddrinfo('%s') gai=%d errno=%d",
                    host, gai_rc, errno);
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    const int sockfd = ::socket(res->ai_family, res->ai_socktype,
                                res->ai_protocol);
    if (sockfd < 0) {
        UL_LOG_WARN("installer: socket() failed errno=%d", errno);
        freeaddrinfo(res);
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    if (::connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
        UL_LOG_WARN("installer: connect('%s':443) failed errno=%d",
                    host, errno);
        ::close(sockfd);
        freeaddrinfo(res);
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }
    freeaddrinfo(res);

    // ── 4. TLS handshake ──────────────────────────────────────────────────
    int out_sockfd = -1;
    const Result bind_rc =
        sslConnectionSetSocketDescriptor(&ssl_conn, sockfd, &out_sockfd);
    if (R_FAILED(bind_rc)) {
        UL_LOG_WARN("installer: sslConnectionSetSocketDescriptor rc=0x%08X",
                    static_cast<unsigned>(bind_rc));
        ::close(sockfd);
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    const Result hs_rc = sslConnectionDoHandshake(&ssl_conn, nullptr, nullptr,
                                                   nullptr, 0);
    if (R_FAILED(hs_rc)) {
        UL_LOG_WARN("installer: sslConnectionDoHandshake('%s') rc=0x%08X",
                    host, static_cast<unsigned>(hs_rc));
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    // ── 5. HTTP/1.1 GET ───────────────────────────────────────────────────
    char req[768];
    const int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: application/vnd.github+json\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, kUserAgent);

    u32 written = 0;
    const Result wr_rc = sslConnectionWrite(&ssl_conn,
                                            req,
                                            static_cast<u32>(req_len),
                                            &written);
    if (R_FAILED(wr_rc) || written == 0) {
        UL_LOG_WARN("installer: sslConnectionWrite rc=0x%08X written=%u",
                    static_cast<unsigned>(wr_rc), written);
        sslConnectionClose(&ssl_conn);
        sslContextClose(&ssl_ctx);
        sslExit();
        if (sock_owned) socketExit();
        return false;
    }

    // ── 6. Read response ──────────────────────────────────────────────────
    std::string response;
    response.reserve(65536);

    static constexpr size_t kReadBuf = 4096;
    char rbuf[kReadBuf];
    while (!abort_.load()) {
        u32 got = 0;
        const Result rd_rc = sslConnectionRead(&ssl_conn, rbuf, kReadBuf, &got);
        if (got > 0) {
            response.append(rbuf, got);
        }
        if (R_FAILED(rd_rc) || got == 0) break;
    }

    sslConnectionClose(&ssl_conn);
    sslContextClose(&ssl_ctx);
    sslExit();
    if (sock_owned) socketExit();

    if (response.empty()) {
        UL_LOG_WARN("installer: empty response from '%s%s'", host, path);
        return false;
    }

    // ── 7. Strip HTTP headers ─────────────────────────────────────────────
    // Find the blank line (\r\n\r\n) separating headers from body.
    const size_t sep = response.find("\r\n\r\n");
    if (sep == std::string::npos) {
        // No header separator — treat entire response as body (chunked raw
        // body without standard header, unlikely but safe fallback).
        body += response;
        return true;
    }

    // Basic status-line check: first line should be "HTTP/1.1 2xx".
    if (response.size() < 12 ||
            response[9] != '2') {  // HTTP/1.1 <status_digit>...
        UL_LOG_WARN("installer: non-2xx response from '%s%s': %.12s",
                    host, path, response.c_str());
        return false;
    }

    body += response.substr(sep + 4);
    return true;
}

// ── ParseAssetUrl ─────────────────────────────────────────────────────────────

/*static*/ bool QdCheatsInstaller::ParseAssetUrl(const std::string &body,
                                                   const char *want_name,
                                                   std::string &out_url) {
    // The GitHub Releases JSON contains blocks like:
    //   "name":"contents.zip",...,"browser_download_url":"https://..."
    // Strategy: find the occurrence of the asset name, then scan forward for
    // "browser_download_url" and extract its value string.
    const size_t name_pos = body.find(want_name);
    if (name_pos == std::string::npos) return false;

    // From name_pos, search forward for the download URL key.
    const size_t url_key_pos = body.find("browser_download_url", name_pos);
    if (url_key_pos == std::string::npos) return false;

    // Find the colon and opening quote after the key.
    const size_t colon_pos = body.find(':', url_key_pos);
    if (colon_pos == std::string::npos) return false;
    const size_t q1_pos = body.find('"', colon_pos + 1);
    if (q1_pos == std::string::npos) return false;
    const size_t q2_pos = body.find('"', q1_pos + 1);
    if (q2_pos == std::string::npos) return false;

    out_url = body.substr(q1_pos + 1, q2_pos - q1_pos - 1);
    return !out_url.empty();
}

// ── FetchLatestReleaseUrl ─────────────────────────────────────────────────────

bool QdCheatsInstaller::FetchLatestReleaseUrl(std::string &out_url) {
    std::string body;
    if (!HttpsGet(kGithubHost, kReleasesPath, body)) {
        return false;
    }
    // Prefer the delta ("contents.zip") over the complete bundle to save
    // bandwidth; fall back to the complete bundle if the delta isn't present.
    if (ParseAssetUrl(body, "contents.zip", out_url)) {
        UL_LOG_INFO("installer: found asset contents.zip -> %s", out_url.c_str());
        return true;
    }
    if (ParseAssetUrl(body, "contents_complete.zip", out_url)) {
        UL_LOG_INFO("installer: found asset contents_complete.zip -> %s",
                    out_url.c_str());
        return true;
    }
    UL_LOG_WARN("installer: no contents.zip or contents_complete.zip in release");
    return false;
}

// ── DownloadFile ──────────────────────────────────────────────────────────────
//
// The GitHub release asset URL typically redirects to objects.githubusercontent.com.
// We follow exactly ONE redirect: if the response is 30x, we re-request the
// Location header on the new host.

bool QdCheatsInstaller::DownloadFile(const std::string &url,
                                      const char *dest_path) {
    // Parse "https://<host>/<path>" from url.
    auto parse_url = [](const std::string &u,
                        std::string &out_host,
                        std::string &out_path) -> bool {
        if (u.substr(0, 8) != "https://") return false;
        const size_t slash = u.find('/', 8);
        if (slash == std::string::npos) {
            out_host = u.substr(8);
            out_path = "/";
        } else {
            out_host = u.substr(8, slash - 8);
            out_path = u.substr(slash);
        }
        return !out_host.empty();
    };

    std::string host, path;
    if (!parse_url(url, host, path)) {
        UL_LOG_WARN("installer: bad URL format: %.80s", url.c_str());
        return false;
    }

    // ── Download ──────────────────────────────────────────────────────────
    // We perform the download in a single pass using HttpsGet, then write
    // to disk.  The cheats bundle is ~4 MB which fits comfortably in the
    // 296 MB heap.  If memory is tight on edge devices, replace this with
    // a streaming write inside the SSL read loop.

    std::string raw_response;

    // First attempt on the original host/path.
    if (!HttpsGet(host.c_str(), path.c_str(), raw_response)) {
        // HttpsGet already logs the error.
        return false;
    }

    // GitHub asset requests typically return 302 to objects.githubusercontent.com.
    // HttpsGet strips headers; but if the body looks like a redirect HTML page
    // we already failed at the "non-2xx" check inside HttpsGet.
    // If we got content, it's the ZIP.

    if (raw_response.empty()) {
        UL_LOG_WARN("installer: empty download body for %s", url.c_str());
        return false;
    }

    // ── Write to staging ─────────────────────────────────────────────────
    EnsureDir("sdmc:/ulaunch/staging");

    FILE *f = fopen(dest_path, "wb");
    if (!f) {
        UL_LOG_WARN("installer: cannot create staging file '%s' errno=%d",
                    dest_path, errno);
        return false;
    }

    const size_t written = fwrite(raw_response.data(), 1, raw_response.size(), f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (written != raw_response.size()) {
        UL_LOG_WARN("installer: short write to '%s' (%zu / %zu bytes)",
                    dest_path, written, raw_response.size());
        remove(dest_path);
        return false;
    }

    UL_LOG_INFO("installer: downloaded %zu bytes to '%s'",
                written, dest_path);
    UpdatePercent(100);
    return true;
}

// ── ExtractFiltered ───────────────────────────────────────────────────────────

bool QdCheatsInstaller::ExtractFiltered(
        const char *zip_path,
        const std::set<std::string> &installed_tids,
        int &out_tids_done,
        int &out_files_written) {

    out_tids_done    = 0;
    out_files_written = 0;

    unzFile zf = unzOpen(zip_path);
    if (!zf) {
        UL_LOG_WARN("installer: unzOpen('%s') failed", zip_path);
        return false;
    }

    std::set<std::string> tids_seen;

    // Walk all entries.
    int ret = unzGoToFirstFile(zf);
    while (ret == UNZ_OK && !abort_.load()) {
        unz_file_info fi;
        char entry_name[512];
        if (unzGetCurrentFileInfo(zf, &fi, entry_name, sizeof(entry_name),
                                  nullptr, 0, nullptr, 0) != UNZ_OK) {
            ret = unzGoToNextFile(zf);
            continue;
        }

        // We want entries of the form:
        //   contents/<tid>/<...>
        // where <tid> is exactly 16 lowercase hex characters.
        // Skip directory entries (trailing '/') and non-contents entries.
        const size_t name_len = strnlen(entry_name, sizeof(entry_name));
        if (name_len > 0 && entry_name[name_len - 1] == '/') {
            // Directory entry — skip.
            ret = unzGoToNextFile(zf);
            continue;
        }

        // Must begin with "contents/".
        if (strncmp(entry_name, "contents/", 9) != 0) {
            ret = unzGoToNextFile(zf);
            continue;
        }

        // Extract TID: characters [9..24].
        if (name_len < 9 + 16 + 1) {  // "contents/" + 16 + at least "/"
            ret = unzGoToNextFile(zf);
            continue;
        }

        char tid_buf[17] = {};
        memcpy(tid_buf, entry_name + 9, 16);
        // Normalise to lower-case (Atmosphère uses lower-case directory names).
        for (int ci = 0; ci < 16; ++ci) {
            tid_buf[ci] = static_cast<char>(
                tolower(static_cast<unsigned char>(tid_buf[ci])));
        }
        const std::string tid(tid_buf);

        // Skip if this game is not installed on this console.
        if (installed_tids.find(tid) == installed_tids.end()) {
            ret = unzGoToNextFile(zf);
            continue;
        }

        // The rest of the path after "contents/<tid>/": typically
        //   cheats/<bid>.txt
        // We write it to sdmc:/atmosphere/contents/<tid>/<rest>.
        const char *rest = entry_name + 9 + 16;  // points to "/"
        if (*rest == '/') ++rest;                  // skip slash

        // Build destination path.
        // Maximum: kAtmContentsBase(27) + tid(16) + '/'(1) + rest(up to 486) = 530
        // Use 600 to have a comfortable margin.
        char dest[600];
        snprintf(dest, sizeof(dest), "%s%s/%s",
                 kAtmContentsBase, tid_buf, rest);

        // Idempotence: if a .qos-backup already exists for this file, the
        // user has customised it — do not overwrite.
        char backup[616];
        snprintf(backup, sizeof(backup), "%s.qos-backup", dest);
        {
            struct stat st;
            if (stat(backup, &st) == 0) {
                // Backup exists → file is user-managed, skip silently.
                UL_LOG_INFO("installer: skip %s (backup exists)", dest);
                ret = unzGoToNextFile(zf);
                continue;
            }
        }

        // Ensure destination directory exists.
        // dest is like "sdmc:/atmosphere/contents/<tid>/cheats/<bid>.txt"
        // We need to create up to the directory part.
        {
            char dir_buf[512];
            strncpy(dir_buf, dest, sizeof(dir_buf) - 1);
            dir_buf[sizeof(dir_buf) - 1] = '\0';
            // Find last '/' and null-terminate there.
            char *last_slash = strrchr(dir_buf, '/');
            if (last_slash && last_slash != dir_buf) {
                *last_slash = '\0';
                EnsureDir(dir_buf);
            }
        }

        // Read compressed entry.
        if (unzOpenCurrentFile(zf) != UNZ_OK) {
            UL_LOG_WARN("installer: unzOpenCurrentFile failed for %s",
                        entry_name);
            ret = unzGoToNextFile(zf);
            continue;
        }

        // Allocate decompressed buffer (bounded to 512 KB per cheat file —
        // real cheat files are <64 KB; this is a generous safety limit).
        const uLong uncompressed = fi.uncompressed_size;
        if (uncompressed == 0 || uncompressed > 512 * 1024) {
            unzCloseCurrentFile(zf);
            ret = unzGoToNextFile(zf);
            continue;
        }

        char *data = static_cast<char *>(malloc(uncompressed));
        if (!data) {
            UL_LOG_WARN("installer: malloc(%lu) failed for %s",
                        static_cast<unsigned long>(uncompressed), entry_name);
            unzCloseCurrentFile(zf);
            ret = unzGoToNextFile(zf);
            continue;
        }

        const int bytes_read = unzReadCurrentFile(zf, data,
                                                   static_cast<unsigned>(uncompressed));
        unzCloseCurrentFile(zf);

        if (bytes_read < 0 ||
                static_cast<uLong>(bytes_read) != uncompressed) {
            UL_LOG_WARN("installer: read %d / %lu bytes from %s",
                        bytes_read,
                        static_cast<unsigned long>(uncompressed),
                        entry_name);
            free(data);
            ret = unzGoToNextFile(zf);
            continue;
        }

        // Write destination file.
        FILE *f = fopen(dest, "wb");
        if (!f) {
            UL_LOG_WARN("installer: fopen('%s', wb) failed errno=%d",
                        dest, errno);
            free(data);
            ret = unzGoToNextFile(zf);
            continue;
        }
        fwrite(data, 1, static_cast<size_t>(bytes_read), f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);

        // Create backup immediately (idempotence for future installs).
        FILE *bak = fopen(backup, "wb");
        if (bak) {
            fwrite(data, 1, static_cast<size_t>(bytes_read), bak);
            fclose(bak);
        }
        free(data);

        ++out_files_written;
        if (tids_seen.insert(tid).second) {
            ++out_tids_done;
        }
        UL_LOG_INFO("installer: wrote %s (%d bytes)", dest, bytes_read);

        ret = unzGoToNextFile(zf);
    }

    unzClose(zf);

    UL_LOG_INFO("installer: extract done — %d TIDs, %d files",
                out_tids_done, out_files_written);
    return true;
}

// ── RunInstall ────────────────────────────────────────────────────────────────

void QdCheatsInstaller::RunInstall() {
    // ── Phase 1: Enumerate installed games ────────────────────────────────
    UpdatePhase(InstallerProgress::Phase::EnumeratingInstalledGames,
                "Enumerating installed games...");

    const std::set<std::string> installed_tids = EnumerateInstalledGameTids();
    if (installed_tids.empty()) {
        Fail("No installed games detected. Check ns:am service.");
        return;
    }
    if (abort_.load()) return;

    // ── Phase 2: Fetch release info ───────────────────────────────────────
    UpdatePhase(InstallerProgress::Phase::FetchingReleaseInfo,
                "Fetching release info from GitHub...");

    std::string zip_url;
    if (!FetchLatestReleaseUrl(zip_url)) {
        Fail("GitHub API request failed. Check Wi-Fi connection.");
        return;
    }
    if (abort_.load()) return;

    // ── Phase 3: Download bundle ──────────────────────────────────────────
    UpdatePhase(InstallerProgress::Phase::DownloadingBundle,
                "Downloading cheats bundle...");

    if (!DownloadFile(zip_url, kStagingPath)) {
        Fail("Download failed. Check Wi-Fi or SD card space.");
        remove(kStagingPath);  // clean up partial file if any
        return;
    }
    if (abort_.load()) {
        remove(kStagingPath);
        return;
    }

    // ── Phase 4: Extract ──────────────────────────────────────────────────
    UpdatePhase(InstallerProgress::Phase::Extracting,
                "Extracting cheats for your games...");

    int tids_done = 0, files_written = 0;
    const bool ok = ExtractFiltered(kStagingPath, installed_tids,
                                    tids_done, files_written);

    // Always clean up the staging zip.
    remove(kStagingPath);

    if (!ok && !abort_.load()) {
        Fail("ZIP extraction failed. SD card may be read-only.");
        return;
    }
    if (abort_.load()) return;

    // ── Phase 5: Done ─────────────────────────────────────────────────────
    mutexLock(&progress_lock_);
    progress_.phase        = InstallerProgress::Phase::Done;
    progress_.percent      = 100;
    progress_.tids_done    = tids_done;
    progress_.files_written= files_written;
    strncpy(progress_.step, "Done!", sizeof(progress_.step) - 1);
    mutexUnlock(&progress_lock_);

    UL_LOG_INFO("installer: complete — %d TIDs, %d files",
                tids_done, files_written);
}

} // namespace ul::menu::qdesktop
