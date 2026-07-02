// qd_RecordsBin.hpp — records.bin (QAPP wire format) reader for uMenu.
//
// In hbloader-hosted (library applet) mode the menu cannot call
// ns:am2 ListApplicationRecord (rc=0x1F800 permission denied), so the
// upstream `LoadEntries(GetActiveMenuPath())` returns an empty vector and
// no installed Switch games appear on the desktop.
//
// uManager.nro (sysmodule-class privilege) pre-scans the installed apps and
// writes the result to:
//
//     sdmc:/switch/qos-apps/records.bin              <- enumeration
//     sdmc:/switch/qos-apps/icons/<app_id_hex16>.jpg <- per-title cover art
//
// This module reads that file and synthesises ul::menu::Entry records that
// QdDesktopIconsElement::SetApplicationEntries() will accept.
//
// Wire format (mirror of mock-nro-desktop-gui/src/qos_apps.rs):
//
//     Header (16 bytes):
//       u32 magic    = 0x51415050  ("QAPP" LE)
//       u32 version  = 1
//       u32 count    (≤ MAX_ENTRIES = 256)
//       u32 reserved
//
//     For each entry:
//       u64 app_id
//       u8  name[512]    UTF-8 NUL-terminated
//       u8  author[256]  UTF-8 NUL-terminated
//       u64 icon_size    (≤ 0x20000 = 128 KiB)
//       u8  icon[icon_size]
//
// Returns the list as Entry records with type=Application. The synthesised
// view flags include CanLaunch + HasMainContents so SetApplicationEntries'
// CanBeLaunched() / HasContents() filters accept the entry.

#pragma once
#include <ul/menu/menu_Entries.hpp>
#include <vector>
#include <string>

namespace ul::menu::qdesktop {

    // Default canonical SD path written by uManager.
    constexpr const char QAPP_RECORDS_BIN_PATH[] = "sdmc:/switch/qos-apps/records.bin";
    constexpr const char QAPP_ICONS_DIR[]        = "sdmc:/switch/qos-apps/icons";

    // QAPP wire-format constants (mirror Rust qos_apps.rs).
    constexpr u32    QAPP_MAGIC          = 0x51415050u; // "QAPP" LE
    constexpr u32    QAPP_VERSION        = 1u;
    constexpr size_t QAPP_HEADER_BYTES   = 16u;
    constexpr size_t QAPP_ENTRY_FIXED    = 784u;  // u64 + u8[512] + u8[256] + u64
    constexpr u32    QAPP_MAX_ENTRIES    = 256u;
    constexpr u64    QAPP_MAX_ICON_BYTES = 0x20000ull; // 128 KiB

    // Read records.bin and append synthesised Entry records to `out`.
    //
    // Returns true on success (header parsed cleanly, even if 0 entries).
    // Returns false if the file is missing, has the wrong magic / version, or
    // the count exceeds QAPP_MAX_ENTRIES. Truncated bodies stop early but
    // keep whatever entries were read intact.
    //
    // Never throws; never panics. Logs UL_LOG_INFO / UL_LOG_WARN to telemetry.
    bool LoadEntriesFromRecordsBin(const char *path,
                                   std::vector<ul::menu::Entry> &out);

}
