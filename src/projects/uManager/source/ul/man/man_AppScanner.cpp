#include <ul/man/man_AppScanner.hpp>
#include <ul/ul_Include.hpp>
#include <ul/ul_Result.hpp>     // UL_LOG_INFO / UL_LOG_WARN
#include <ul/fs/fs_Stdio.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

// ── Q OS App Scanner implementation — v0.2.3 internal ───────────────────────
//
// Calls nsListApplicationRecord (libnx) to enumerate all installed titles, then
// nsGetApplicationControlData for each to extract the NACP ApplicationName /
// DeveloperName and the JPEG icon.  Writes:
//
//   sdmc:/switch/qos-apps/records.bin   (binary index, atomic via .tmp rename)
//   sdmc:/switch/qos-apps/records.json  (human-readable debug copy, same atom)
//   sdmc:/switch/qos-apps/icons/<hex>.jpg  (per-title JPEG icons)
//
// See man_AppScanner.hpp for the complete wire-format specification.

namespace ul::man {

    namespace {

        // ── Binary on-disk structures ─────────────────────────────────────────

        // Records.bin header — 16 bytes.
        struct __attribute__((packed)) BinHeader {
            uint32_t magic;     // QAppMagic = 0x51415050
            uint32_t version;   // QAppVersion = 1
            uint32_t count;
            uint32_t reserved;  // 0
        };
        static_assert(sizeof(BinHeader) == 16, "BinHeader must be 16 bytes");

        // Per-entry header in records.bin — 784 bytes, followed by icon_size JPEG.
        struct __attribute__((packed)) BinEntry {
            uint64_t app_id;
            uint8_t  name[512];
            uint8_t  author[256];
            uint64_t icon_size;
        };
        static_assert(sizeof(BinEntry) == 784, "BinEntry must be 784 bytes");

        // ── NACP name extraction ──────────────────────────────────────────────

        /// Copy at most dst_len-1 bytes from the NUL-terminated UTF-8 src into dst,
        /// always NUL-terminating.  Clears dst on invalid UTF-8 / empty input.
        static void CopyNacpString(uint8_t *dst, size_t dst_len,
                                   const char *src, size_t src_len) {
            if(!dst || !src || dst_len == 0) {
                return;
            }
            // Find NUL terminator.
            size_t nul = 0;
            while(nul < src_len && src[nul] != '\0') { ++nul; }

            const size_t copy_len = std::min(nul, dst_len - 1);
            std::memcpy(dst, src, copy_len);
            dst[copy_len] = '\0';
        }

        /// Walk up to 16 NacpLanguageEntry slots and return the first non-empty
        /// ApplicationName.  Returns nullptr if all slots are blank.
        static const char *FirstNonEmptyName(const NacpStruct *nacp) {
            for(int i = 0; i < 16; ++i) {
                if(nacp->lang[i].name[0] != '\0') {
                    return nacp->lang[i].name;
                }
            }
            return nullptr;
        }

        /// Walk up to 16 NacpLanguageEntry slots and return the first non-empty
        /// DeveloperName.  Returns nullptr if all slots are blank.
        static const char *FirstNonEmptyAuthor(const NacpStruct *nacp) {
            for(int i = 0; i < 16; ++i) {
                if(nacp->lang[i].author[0] != '\0') {
                    return nacp->lang[i].author;
                }
            }
            return nullptr;
        }

        // ── Hex formatting ────────────────────────────────────────────────────

        /// Write a 16-char lowercase hex representation of tid into buf (no NUL).
        /// buf must be at least 16 bytes.
        static void FormatHex16(char *buf, uint64_t tid) {
            static constexpr const char Hex[] = "0123456789abcdef";
            for(int i = 0; i < 16; ++i) {
                buf[i] = Hex[(tid >> (60 - i * 4)) & 0xF];
            }
        }

        // ── Directory / path helpers ──────────────────────────────────────────

        /// Create a directory (and its parent chain) if it does not already exist.
        static void EnsureDir(const char *path) {
            if(!fs::ExistsDirectory(std::string(path))) {
                fs::EnsureCreateDirectory(std::string(path));
            }
        }

        // ── Icon file writing ─────────────────────────────────────────────────

        /// Write icon JPEG bytes to icons/<hex>.jpg atomically.
        /// Returns the number of bytes written, or 0 on failure.
        static size_t WriteIconFile(uint64_t app_id,
                                    const uint8_t *jpeg_data, size_t jpeg_size) {
            if(!jpeg_data || jpeg_size == 0 || jpeg_size > MaxIconBytes) {
                return 0;
            }

            // Build paths: sdmc:/switch/qos-apps/icons/<hex16>.jpg
            char hex[17];
            FormatHex16(hex, app_id);
            hex[16] = '\0';

            std::string icon_path = std::string(QosIconsDir) + "/" + hex + ".jpg";
            std::string icon_tmp  = icon_path + ".tmp";

            // Write to .tmp then rename.
            if(!fs::WriteFile(icon_tmp, jpeg_data, jpeg_size, /*overwrite=*/true)) {
                return 0;
            }
            if(!fs::RenameFile(icon_tmp, icon_path)) {
                fs::DeleteFile(icon_tmp);
                return 0;
            }
            return jpeg_size;
        }

        // ── JSON helpers ──────────────────────────────────────────────────────

        /// Append a JSON-safe escaped string (ASCII / UTF-8) into out.
        /// Escapes: " \\ / \b \f \n \r \t and control chars as \uXXXX.
        static void AppendJsonString(std::string &out, const char *s) {
            if(!s) {
                out += "null";
                return;
            }
            out += '"';
            for(; *s; ++s) {
                const unsigned char c = static_cast<unsigned char>(*s);
                switch(c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if(c < 0x20) {
                            // Control character — encode as \uXXXX.
                            char esc[8];
                            snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
                            out += esc;
                        }
                        else {
                            out += static_cast<char>(c);
                        }
                        break;
                }
            }
            out += '"';
        }

        // ── Scan data holder ──────────────────────────────────────────────────

        struct AppInfo {
            uint64_t app_id;
            char     name[512];
            char     author[256];
            // Icon data heap-allocated, nullptr when icon unavailable or too large.
            uint8_t *icon_data;
            size_t   icon_size;
        };

    }   // anonymous namespace

    // ── ScanAndWriteAppList ───────────────────────────────────────────────────

    ScanResult ScanAndWriteAppList() {
        ScanResult result{};
        result.ok = false;

        // ── 1. Enumerate installed application records ────────────────────────

        // NsApplicationRecord: 24 bytes (application_id u64, last_event u8,
        // attributes u8, _reserved u6, last_updated u64).
        NsApplicationRecord record_buf[MaxScanEntries] = {};
        s32 total_written = 0;
        s32 offset = 0;

        while(true) {
            s32 batch_written = 0;
            const auto rc = nsListApplicationRecord(
                record_buf + total_written,
                static_cast<s32>(MaxScanEntries) - total_written,
                offset,
                &batch_written
            );
            if(R_FAILED(rc) || batch_written <= 0) {
                break;
            }
            total_written += batch_written;
            offset += batch_written;
            if(total_written >= static_cast<s32>(MaxScanEntries)) {
                break;
            }
        }

        if(total_written == 0) {
            result.summary = "ScanAndWriteAppList: no installed titles found (total=0)";
            result.ok = true;  // Not an error — device may have no titles.
            result.count = 0;
            return result;
        }

        // ── 2. Fetch control data (NACP + icon) per title ─────────────────────

        // NsApplicationControlData is large (~786 KiB) so allocate on heap.
        auto ctrl_data = new NsApplicationControlData();
        std::vector<AppInfo> apps;
        apps.reserve(static_cast<size_t>(total_written));

        // F1 (SP4.14 cycle): retry with CacheOnly when Storage source fails.
        // The Storage source talks to the actual title NSP/NCA on disk.  For
        // unsigned/orphaned/partially-installed titles, that read returns an
        // NS-module RC and we previously fell straight to a hex fallback name
        // (visible to the user as e.g. "0x059f5d9789168000").  CacheOnly hits
        // ns's in-memory cache populated at boot from the Application Control
        // database — much more permissive — so we get the real NACP name as
        // long as the title was ever shown on a Horizon home screen.  Both
        // failing is the only path that keeps the hex fallback.
        for(s32 i = 0; i < total_written; ++i) {
            const u64 app_id = record_buf[i].application_id;

            AppInfo info{};
            info.app_id    = app_id;
            info.icon_data = nullptr;
            info.icon_size = 0;
            info.name[0]   = '\0';
            info.author[0] = '\0';

            // Try sources in order: Storage (signed-installed titles, returns
            // NACP+icon), then CacheOnly (any title ns has indexed, returns
            // NACP, often no icon).
            constexpr NsApplicationControlSource SourceLadder[] = {
                NsApplicationControlSource_Storage,
                NsApplicationControlSource_CacheOnly,
            };

            bool extracted = false;
            for(const auto src : SourceLadder) {
                std::memset(ctrl_data, 0, sizeof(NsApplicationControlData));
                u64 actual_size = 0;

                const auto rc = nsGetApplicationControlData(
                    src,
                    app_id,
                    ctrl_data,
                    sizeof(NsApplicationControlData),
                    &actual_size
                );
                if(R_FAILED(rc)) {
                    UL_LOG_WARN("uManager: nsGetApplicationControlData(src=%d, app=0x%016lx) rc=0x%08X",
                                static_cast<int>(src),
                                static_cast<unsigned long>(app_id),
                                static_cast<unsigned>(rc));
                    continue;  // try next source in the ladder
                }

                // Extract ApplicationName and DeveloperName.
                const NacpStruct *nacp = &ctrl_data->nacp;

                const char *app_name = FirstNonEmptyName(nacp);
                if(app_name == nullptr) {
                    // NACP returned but every language slot is blank — try the
                    // next source.  CacheOnly sometimes returns a stale
                    // header-only entry whose lang[] is all-zero.
                    UL_LOG_WARN("uManager: NACP empty for app=0x%016lx via src=%d — trying next source",
                                static_cast<unsigned long>(app_id),
                                static_cast<int>(src));
                    continue;
                }

                CopyNacpString(
                    reinterpret_cast<uint8_t*>(info.name),
                    sizeof(info.name),
                    app_name, sizeof(nacp->lang[0].name)
                );

                const char *app_author = FirstNonEmptyAuthor(nacp);
                if(app_author) {
                    CopyNacpString(
                        reinterpret_cast<uint8_t*>(info.author),
                        sizeof(info.author),
                        app_author, sizeof(nacp->lang[0].author)
                    );
                }

                // Copy icon if within size limit.  CacheOnly may report
                // actual_size == sizeof(NacpStruct) (NACP only, no icon) —
                // the inner check handles that gracefully.
                const size_t nacp_size = sizeof(NacpStruct);
                if(actual_size > nacp_size) {
                    const size_t icon_bytes = actual_size - nacp_size;
                    if(icon_bytes > 0 && icon_bytes <= MaxIconBytes) {
                        info.icon_data = new uint8_t[icon_bytes];
                        std::memcpy(info.icon_data, ctrl_data->icon, icon_bytes);
                        info.icon_size = icon_bytes;
                    }
                }

                extracted = true;
                break;  // got a real name — stop walking the ladder
            }

            if(!extracted) {
                // Both sources failed (or returned all-blank NACP).  Hex
                // fallback so the entry still has a printable label.
                snprintf(info.name, sizeof(info.name), "0x%016lx",
                         static_cast<unsigned long>(app_id));
            }

            apps.push_back(std::move(info));
        }

        delete ctrl_data;

        // ── 3. Ensure output directories exist ────────────────────────────────

        EnsureDir(QosAppsDir);
        EnsureDir(QosIconsDir);

        // ── 4. Write icon files ───────────────────────────────────────────────

        size_t icons_total = 0;
        for(const auto &app : apps) {
            if(app.icon_data && app.icon_size > 0) {
                icons_total += WriteIconFile(app.app_id, app.icon_data, app.icon_size);
            }
        }
        result.icons_total_bytes = icons_total;

        // ── 5. Build records.bin in memory and write atomically ───────────────

        // Compute total buffer size: header + for each entry (BinEntry + icon bytes).
        size_t bin_total = sizeof(BinHeader);
        for(const auto &app : apps) {
            bin_total += sizeof(BinEntry) + app.icon_size;
        }

        auto bin_buf = new uint8_t[bin_total]();
        size_t bin_pos = 0;

        // Write header.
        BinHeader hdr{};
        hdr.magic    = QAppMagic;
        hdr.version  = QAppVersion;
        hdr.count    = static_cast<uint32_t>(apps.size());
        hdr.reserved = 0;
        std::memcpy(bin_buf + bin_pos, &hdr, sizeof(hdr));
        bin_pos += sizeof(hdr);

        // Write entries.
        for(const auto &app : apps) {
            BinEntry entry{};
            entry.app_id    = app.app_id;
            entry.icon_size = app.icon_size;
            std::memcpy(entry.name,   app.name,   sizeof(entry.name));
            std::memcpy(entry.author, app.author, sizeof(entry.author));
            std::memcpy(bin_buf + bin_pos, &entry, sizeof(entry));
            bin_pos += sizeof(entry);

            if(app.icon_size > 0 && app.icon_data) {
                std::memcpy(bin_buf + bin_pos, app.icon_data, app.icon_size);
                bin_pos += app.icon_size;
            }
        }

        // Atomic write: tmp then rename.
        const bool bin_ok = fs::WriteFile(
            RecordsBinTmpPath, bin_buf, bin_total, /*overwrite=*/true
        );
        if(bin_ok) {
            if(!fs::RenameFile(RecordsBinTmpPath, RecordsBinPath)) {
                fs::DeleteFile(RecordsBinTmpPath);
            }
            else {
                result.bin_size = bin_total;
            }
        }

        delete[] bin_buf;

        // ── 6. Build records.json in memory and write atomically ──────────────

        std::string json;
        json.reserve(apps.size() * 128 + 64);
        json += "{\"version\":1,\"count\":";
        json += std::to_string(apps.size());
        json += ",\"entries\":[";

        for(size_t i = 0; i < apps.size(); ++i) {
            const auto &app = apps[i];
            if(i > 0) { json += ','; }

            char hex_id[32];
            snprintf(hex_id, sizeof(hex_id), "0x%016lx",
                     static_cast<unsigned long>(app.app_id));

            json += "{\"app_id\":";
            AppendJsonString(json, hex_id);
            json += ",\"name\":";
            AppendJsonString(json, app.name);
            json += ",\"author\":";
            AppendJsonString(json, app.author[0] ? app.author : nullptr);
            json += ",\"icon_size\":";
            json += std::to_string(app.icon_size);
            json += '}';
        }
        json += "]}";

        const bool json_ok = fs::WriteFileString(RecordsJsonTmpPath, json, /*overwrite=*/true);
        if(json_ok) {
            if(!fs::RenameFile(RecordsJsonTmpPath, RecordsJsonPath)) {
                fs::DeleteFile(RecordsJsonTmpPath);
            }
        }

        // ── 7. Free icon heap buffers ─────────────────────────────────────────

        for(auto &app : apps) {
            delete[] app.icon_data;
            app.icon_data = nullptr;
        }

        // ── 8. Build summary string ───────────────────────────────────────────

        std::string summary = "scan ok count=" + std::to_string(apps.size());
        summary += " bin_size=" + std::to_string(result.bin_size);
        summary += " icons_bytes=" + std::to_string(icons_total);
        summary += " first3=[";
        for(size_t i = 0; i < std::min(apps.size(), static_cast<size_t>(3)); ++i) {
            if(i > 0) { summary += ", "; }
            summary += apps[i].name[0] ? apps[i].name : "(unnamed)";
        }
        summary += ']';

        result.count   = static_cast<u32>(apps.size());
        result.summary = summary;
        result.ok      = bin_ok;
        return result;
    }

}   // namespace ul::man
