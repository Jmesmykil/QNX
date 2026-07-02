#include "qos_title_enum.hpp"
#include <switch.h>
#include <algorithm>
#include <cstring>

// ── Q OS uMenu v0.7 — title enumeration ──────────────────────────────────────
//
// IPC path: ns:am2 cmd 0  (nsListApplicationRecord)
//           ns:am2 cmd 400 (nsGetApplicationControlData)
//
// Reference lines pulled from v0.6.x source:
//   man_AppScanner.cpp:194-216  — pagination loop using NsApplicationRecord
//   man_AppScanner.cpp:242-291  — per-title control data + icon extraction
//   main.cpp:20-35              — MenuControlEntryLoadFunction (nacpGetLanguageEntry)

namespace qos {

namespace {

// Page size for nsListApplicationRecord. 32 is a safe batch that avoids
// oversized IPC buffers while keeping round-trips low.
constexpr s32 kPageSize = 32;

// Hard ceiling: 4096 titles is far beyond any real device.
constexpr int kAbsoluteMax = 4096;

// Populate name/author/icon_jpeg from a freshly-fetched NsApplicationControlData.
// Returns true when at least a name was extracted; false if NACP parse failed.
bool ParseControlData(const NsApplicationControlData &ctrl,
                      u64 actual_size,
                      InstalledTitle &out)
{
    NacpLanguageEntry *lang_entry = nullptr;
    // nacpGetLanguageEntry walks the NACP language table and returns the first
    // valid (non-empty name) entry; it is a pure-parse function with no IPC.
    if (R_FAILED(nacpGetLanguageEntry(
            const_cast<NacpStruct *>(&ctrl.nacp), &lang_entry)) ||
        lang_entry == nullptr)
    {
        return false;
    }

    out.name   = lang_entry->name;
    out.author = lang_entry->author;

    // Icon lives in ctrl.icon[], after the NacpStruct.
    // actual_size - sizeof(NacpStruct) == real JPEG byte count.
    const size_t nacp_size  = sizeof(NacpStruct);
    if (actual_size > nacp_size) {
        const size_t icon_bytes = actual_size - nacp_size;
        if (icon_bytes > 0) {
            out.icon_jpeg.assign(ctrl.icon, ctrl.icon + icon_bytes);
        }
    }
    return true;
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

bool InitTitleEnum() {
    return R_SUCCEEDED(nsInitialize());
}

void ShutdownTitleEnum() {
    nsExit();
}

std::vector<InstalledTitle> EnumerateInstalledTitles(int max_titles) {
    // Clamp: 0 means unlimited (use hard ceiling).
    const int effective_max = (max_titles > 0)
        ? std::min(max_titles, kAbsoluteMax)
        : kAbsoluteMax;

    // ── 1. Enumerate application records in pages of kPageSize ───────────────
    NsApplicationRecord page_buf[kPageSize];
    std::vector<NsApplicationRecord> all_records;
    all_records.reserve(64);

    s32 offset = 0;
    while (static_cast<int>(all_records.size()) < effective_max) {
        std::memset(page_buf, 0, sizeof(page_buf));
        s32 actual_count = 0;

        const Result rc = nsListApplicationRecord(
            page_buf,
            kPageSize,
            offset,
            &actual_count
        );
        if (R_FAILED(rc) || actual_count <= 0) {
            break;
        }

        for (s32 i = 0; i < actual_count; ++i) {
            all_records.push_back(page_buf[i]);
            if (static_cast<int>(all_records.size()) >= effective_max) {
                break;
            }
        }
        offset += actual_count;

        // If the service returned fewer records than requested, we have them all.
        if (actual_count < kPageSize) {
            break;
        }
    }

    if (all_records.empty()) {
        return {};
    }

    // ── 2. Fetch NACP + icon for each record ─────────────────────────────────
    // NsApplicationControlData is ~786 KiB — allocate once on heap and reuse.
    auto ctrl_data = new NsApplicationControlData();
    std::vector<InstalledTitle> titles;
    titles.reserve(all_records.size());

    for (const auto &rec : all_records) {
        std::memset(ctrl_data, 0, sizeof(NsApplicationControlData));
        u64 actual_size = 0;

        const Result rc = nsGetApplicationControlData(
            NsApplicationControlSource_Storage,
            rec.application_id,
            ctrl_data,
            sizeof(NsApplicationControlData),
            &actual_size
        );

        // Skip titles whose control data cannot be retrieved — caller handles
        // an incomplete list; returning a stub would violate no-stubs policy.
        if (R_FAILED(rc)) {
            continue;
        }

        InstalledTitle title;
        title.application_id = rec.application_id;

        if (!ParseControlData(*ctrl_data, actual_size, title)) {
            // NACP parse failed but we at least have a valid application_id.
            // Use hex ID as fallback name so the title appears in the launcher.
            char hex_name[24];
            snprintf(hex_name, sizeof(hex_name), "0x%016lx",
                     static_cast<unsigned long>(rec.application_id));
            title.name = hex_name;
        }

        titles.push_back(std::move(title));
    }

    delete ctrl_data;

    // ── 3. Sort by name (case-insensitive ASCII) ──────────────────────────────
    std::sort(titles.begin(), titles.end(),
        [](const InstalledTitle &a, const InstalledTitle &b) {
            const size_t len = std::min(a.name.size(), b.name.size());
            for (size_t i = 0; i < len; ++i) {
                const char ca = static_cast<char>(
                    (a.name[i] >= 'A' && a.name[i] <= 'Z')
                        ? (a.name[i] + 32) : a.name[i]);
                const char cb = static_cast<char>(
                    (b.name[i] >= 'A' && b.name[i] <= 'Z')
                        ? (b.name[i] + 32) : b.name[i]);
                if (ca != cb) {
                    return ca < cb;
                }
            }
            return a.name.size() < b.name.size();
        });

    return titles;
}

InstalledTitle GetTitleControlData(uint64_t application_id) {
    auto ctrl_data = new NsApplicationControlData();
    std::memset(ctrl_data, 0, sizeof(NsApplicationControlData));
    u64 actual_size = 0;

    const Result rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        application_id,
        ctrl_data,
        sizeof(NsApplicationControlData),
        &actual_size
    );

    InstalledTitle result;
    if (R_SUCCEEDED(rc)) {
        result.application_id = application_id;
        if (!ParseControlData(*ctrl_data, actual_size, result)) {
            char hex_name[24];
            snprintf(hex_name, sizeof(hex_name), "0x%016lx",
                     static_cast<unsigned long>(application_id));
            result.name = hex_name;
        }
    }
    // On failure: result.application_id stays 0 — caller checks this sentinel.

    delete ctrl_data;
    return result;
}

} // namespace qos
