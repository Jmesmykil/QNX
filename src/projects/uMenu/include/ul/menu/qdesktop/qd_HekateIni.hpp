// qd_HekateIni.hpp -- v1.7.0-stabilize-2 reconstruction.
//
// Reads Hekate's payload INI files to discover {payload_path, icon_path}
// pairs. Used by qd_NroAsset::ResolvePayloadIcon to give creator-supplied
// payload icons priority 0 (highest) over the on-SD heuristic search.
//
// File set parsed:
//   sdmc:/bootloader/hekate_ipl.ini
//   sdmc:/bootloader/ini/*.ini    (additional payload bundles)
//
// Section format (canonical Hekate):
//   [Display Name]
//   payload=path/to/payload.bin
//   icon=path/to/icon.bmp
//   ; ... other keys we ignore (kip1, atmosphere, etc.)
//
// Per A3 + creator L16232 the full Hekate launch-path correctness work
// (key= and payload= dispatching to the actual reboot_payload helper)
// is scoped for v1.7.2. This module only surfaces the icon mappings; the
// launcher remains the existing qd_DesktopIcons IconKind::Special path.
//
// Threading: parse runs once per call from the main UI thread (during
// ResolvePayloadIcon, which itself runs at icon-decode time). No mutex.
#pragma once

#include <pu/Plutonium>
#include <string>
#include <vector>

namespace ul::menu::qdesktop {

// One entry parsed from a Hekate INI section. Fields are kept as
// std::string (not char[]) because the parser yields variable-length
// SD-relative paths and the consumer (qd_NroAsset) treats them as
// inputs to snprintf -- no IPC layout impact.
struct HekateIniEntry {
    // Section display name (between [ ]).
    std::string title;
    // Value of the payload= key (relative path, e.g. "bootloader/payloads/atmosphere.bin").
    std::string payload_path;
    // Value of the icon= key (relative path, e.g. "bootloader/res/atmosphere.bmp").
    // Empty if the section did not declare an icon= line.
    std::string icon_path;
};

// Parse all Hekate INI files reachable from the SD card and return the
// concatenated list of entries. On a host without an SD mount (no
// /sdmc:/) the returned vector is empty -- callers must handle that.
//
// File scan order: hekate_ipl.ini first, then bootloader/ini/*.ini in
// readdir order. This matches the order Hekate itself walks them, so the
// returned vector preserves Hekate's section ordering.
std::vector<HekateIniEntry> LoadAllHekateIniEntries();

} // namespace ul::menu::qdesktop
