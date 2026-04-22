#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace qos {

struct InstalledTitle {
    uint64_t application_id = 0;   // Title ID
    std::string name;              // From NACP.application_names[0].name
    std::string author;            // From NACP.application_names[0].author
    std::vector<uint8_t> icon_jpeg; // Raw JPEG bytes (up to 128KB typical)
};

// One-time init: call nsInitialize if not already done by main.
// Returns true on success, false on service init failure.
bool InitTitleEnum();

// Shutdown: calls nsExit. Only call if InitTitleEnum() returned true.
void ShutdownTitleEnum();

// Enumerate all installed titles on the device. Paginates NsListApplicationRecord
// internally. Returns the full list sorted by name. Each entry includes icon bytes.
// If max_titles > 0, stops after that many. Pass 0 for unlimited.
std::vector<InstalledTitle> EnumerateInstalledTitles(int max_titles = 0);

// Fetch only the icon + names for a single title. Returns a populated struct
// on success (application_id matches the parameter). On failure, returns a
// struct with application_id=0.
InstalledTitle GetTitleControlData(uint64_t application_id);

} // namespace qos
