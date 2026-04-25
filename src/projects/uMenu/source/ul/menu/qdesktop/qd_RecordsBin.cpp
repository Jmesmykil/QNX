// qd_RecordsBin.cpp — see qd_RecordsBin.hpp for the QAPP wire format.
//
// Implementation note: we read the JPEG icons via the existing
// LoadJpegIconToCache() pipeline (qd_DesktopIcons.cpp), so this module only
// needs to populate Entry records — the inline icon bytes embedded in
// records.bin are deliberately skipped (we use the per-title .jpg files).

#include <ul/menu/qdesktop/qd_RecordsBin.hpp>
#include <ul/util/util_Telemetry.hpp>
#include <ul/ul_Result.hpp>
#include <cstdio>
#include <cstring>

namespace ul::menu::qdesktop {

    namespace {

        // Stable byte readers — records.bin is little-endian on all Switch
        // CPUs, so we explicitly extract LE u32 / u64 even though AArch64 is
        // already LE. This keeps the parser portable to BE hosts.
        inline u32 ReadU32LE(const u8 *p) {
            return  (static_cast<u32>(p[0]))
                  | (static_cast<u32>(p[1]) << 8)
                  | (static_cast<u32>(p[2]) << 16)
                  | (static_cast<u32>(p[3]) << 24);
        }

        inline u64 ReadU64LE(const u8 *p) {
            return  (static_cast<u64>(p[0]))
                  | (static_cast<u64>(p[1]) << 8)
                  | (static_cast<u64>(p[2]) << 16)
                  | (static_cast<u64>(p[3]) << 24)
                  | (static_cast<u64>(p[4]) << 32)
                  | (static_cast<u64>(p[5]) << 40)
                  | (static_cast<u64>(p[6]) << 48)
                  | (static_cast<u64>(p[7]) << 56);
        }

        // Build "sdmc:/switch/qos-apps/icons/<16hex>.jpg" without C++ stream
        // formatting (we want a no-throw path).
        std::string MakeIconPath(u64 app_id) {
            static constexpr const char hex[] = "0123456789abcdef";
            char hexstr[17];
            for (int i = 15; i >= 0; --i) {
                hexstr[i] = hex[app_id & 0xFu];
                app_id >>= 4;
            }
            hexstr[16] = '\0';

            std::string out;
            out.reserve(64);
            out.append(QAPP_ICONS_DIR);
            out.push_back('/');
            out.append(hexstr);
            out.append(".jpg");
            return out;
        }

        // Copy a NUL-terminated, possibly truncated UTF-8 byte field into a
        // std::string. Stops at the first '\0' or at field_len.
        std::string FieldToString(const u8 *field, size_t field_len) {
            size_t n = 0;
            while (n < field_len && field[n] != 0) {
                ++n;
            }
            return std::string(reinterpret_cast<const char *>(field), n);
        }

    } // namespace

    bool LoadEntriesFromRecordsBin(const char *path,
                                   std::vector<ul::menu::Entry> &out)
    {
        if (path == nullptr || path[0] == '\0') {
            UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: null/empty path");
            return false;
        }

        FILE *f = fopen(path, "rb");
        if (f == nullptr) {
            UL_LOG_INFO("qdesktop: LoadEntriesFromRecordsBin: %s not present"
                        " (uManager hasn't written it yet) — skipping fallback",
                        path);
            return false;
        }

        // ── Header ────────────────────────────────────────────────────────
        u8 hdr[QAPP_HEADER_BYTES];
        if (fread(hdr, 1u, QAPP_HEADER_BYTES, f) != QAPP_HEADER_BYTES) {
            UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: truncated header"
                        " in %s", path);
            fclose(f);
            return false;
        }

        const u32 magic   = ReadU32LE(hdr +  0);
        const u32 version = ReadU32LE(hdr +  4);
        const u32 count   = ReadU32LE(hdr +  8);
        // hdr+12: reserved u32, ignored.

        if (magic != QAPP_MAGIC) {
            UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: bad magic 0x%08X"
                        " (expected 0x%08X) in %s",
                        static_cast<unsigned>(magic),
                        static_cast<unsigned>(QAPP_MAGIC),
                        path);
            fclose(f);
            return false;
        }
        if (version != QAPP_VERSION) {
            UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: unsupported"
                        " version=%u (expected %u) in %s",
                        static_cast<unsigned>(version),
                        static_cast<unsigned>(QAPP_VERSION),
                        path);
            fclose(f);
            return false;
        }
        if (count > QAPP_MAX_ENTRIES) {
            UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: count %u exceeds"
                        " cap %u in %s — refusing to parse",
                        static_cast<unsigned>(count),
                        static_cast<unsigned>(QAPP_MAX_ENTRIES),
                        path);
            fclose(f);
            return false;
        }

        // ── Per-entry decode ──────────────────────────────────────────────
        u8 entry_buf[QAPP_ENTRY_FIXED];
        size_t added = 0;

        for (u32 i = 0; i < count; ++i) {
            if (fread(entry_buf, 1u, QAPP_ENTRY_FIXED, f) != QAPP_ENTRY_FIXED) {
                UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: entry %u"
                            " truncated in %s — stopping",
                            static_cast<unsigned>(i), path);
                break;
            }

            const u64 app_id    = ReadU64LE(entry_buf +   0);
            // name field: entry_buf[8..520]   (512 bytes)
            // author field: entry_buf[520..776] (256 bytes)
            const u64 icon_size = ReadU64LE(entry_buf + 776);

            if (icon_size > QAPP_MAX_ICON_BYTES) {
                UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: entry %u"
                            " app_id=0x%016llx has icon_size %llu > cap %llu"
                            " — skipping (file likely corrupt)",
                            static_cast<unsigned>(i),
                            static_cast<unsigned long long>(app_id),
                            static_cast<unsigned long long>(icon_size),
                            static_cast<unsigned long long>(QAPP_MAX_ICON_BYTES));
                // Don't try to seek over an unbounded blob; bail out so the
                // remaining entries are not interpreted from a misaligned offset.
                break;
            }

            // Skip past the inline icon bytes — we use the per-title .jpg
            // file from QAPP_ICONS_DIR for the actual JPEG decode.
            if (icon_size > 0) {
                if (fseek(f, static_cast<long>(icon_size), SEEK_CUR) != 0) {
                    UL_LOG_WARN("qdesktop: LoadEntriesFromRecordsBin: seek"
                                " past inline icon failed for entry %u"
                                " app_id=0x%016llx",
                                static_cast<unsigned>(i),
                                static_cast<unsigned long long>(app_id));
                    break;
                }
            }

            // ── Synthesise ul::menu::Entry ──────────────────────────────
            ul::menu::Entry e{};
            e.type       = ul::menu::EntryType::Application;
            e.entry_path = ""; // synthetic — not on disk
            e.index      = i;

            // Control data: name / author / icon path.
            e.control.name             = FieldToString(entry_buf + 8,   512);
            e.control.custom_name      = false;
            e.control.author           = FieldToString(entry_buf + 520, 256);
            e.control.custom_author    = false;
            e.control.version          = "";
            e.control.custom_version   = false;
            e.control.icon_path        = (icon_size > 0) ? MakeIconPath(app_id)
                                                          : std::string();
            e.control.custom_icon_path = (icon_size > 0);

            // Application info — set view flags so CanBeLaunched() returns true.
            // Flag bitset = IsValid | HasMainContents | HasContentsInstalled |
            //               CanLaunch | CanLaunch2  (qlaunch's canonical "ready
            //               to launch" combination per ns-ext.h:42-65).
            std::memset(&e.app_info, 0, sizeof(e.app_info));
            e.app_info.app_id          = app_id;
            e.app_info.record.id       = app_id;
            e.app_info.record.last_event = NsExtApplicationEvent_Present;
            e.app_info.view.app_id     = app_id;
            e.app_info.view.flags      = NsExtApplicationViewFlag_IsValid
                                       | NsExtApplicationViewFlag_HasMainContents
                                       | NsExtApplicationViewFlag_HasContentsInstalled
                                       | NsExtApplicationViewFlag_CanLaunch
                                       | NsExtApplicationViewFlag_CanLaunch2;
            e.app_info.needs_update    = false;

            out.push_back(std::move(e));
            ++added;
        }

        fclose(f);
        UL_LOG_INFO("qdesktop: LoadEntriesFromRecordsBin: parsed %s"
                    " count_hdr=%u added=%zu (out.size=%zu)",
                    path, static_cast<unsigned>(count), added, out.size());
        return true;
    }

}
