
#pragma once
#include <ul/ul_Include.hpp>

// ── Q OS App Scanner — v0.2.3 internal ──────────────────────────────────────
//
// Enumerates all installed Switch titles via nsListApplicationRecord (cmd 0
// on IApplicationManagerInterface / ns:am2) and writes a flat binary index to
// the SD card that hbloader-hosted NROs (mock-nro-desktop, mock-nro-desktop-gui)
// can read directly.  Those mocks receive rc=0x1F800 when they try ns:am2
// themselves because hbloader does not grant the required permissions.  uManager
// runs as a companion applet with sysmodule-class privileges, so it can call
// nsListApplicationRecord and nsGetApplicationControlData successfully.
//
// ## Output paths
//
//   sdmc:/switch/qos-apps/records.bin  — versioned binary index (readers MUST
//                                        validate MAGIC + VERSION before use)
//   sdmc:/switch/qos-apps/records.json — human-readable debug copy (identical
//                                        data; may be opened in any text editor)
//   sdmc:/switch/qos-apps/icons/<app_id_hex>.jpg
//                                      — JPEG icon extracted from NACP for each
//                                        title; filename is lowercase 16-char hex
//                                        of the u64 application ID, e.g.
//                                        "0100000000010000.jpg".
//
// ## Binary wire format (records.bin)
//
//   struct Header {
//       u32 magic;     // 0x51415050 = b"QAPP" little-endian
//       u32 version;   // 1
//       u32 count;     // number of Entry structs that follow
//       u32 reserved;  // 0
//   };  // 16 bytes
//
//   struct Entry {
//       u64 app_id;      // 64-bit program/application ID
//       u8  name[512];   // ApplicationName UTF-8, NUL-terminated
//       u8  author[256]; // DeveloperName UTF-8, NUL-terminated
//       u64 icon_size;   // byte length of the inline JPEG that immediately follows
//   };  // 784 bytes + icon_size bytes of JPEG data
//
//   The file is a contiguous stream: Header, then for each title (Entry header
//   immediately followed by icon_size raw JPEG bytes).  Readers seeking a specific
//   icon MUST walk every entry sequentially because icon sizes vary.
//
// ## Atomic write guarantee
//
//   Both output files are written to a ".tmp" sibling first, then renamed over
//   the final path.  Readers therefore always observe a complete, valid file or
//   the previous version — never a partial write.
//
// ## Usage from main.cpp
//
//   Call ScanAndWriteAppList() once during Initialize(), after nsInitialize() and
//   before the Plutonium UI loop starts.  The function returns a ScanResult
//   summary suitable for logging.  It never throws and never terminates the
//   process on failure; any error is captured in the returned ScanResult.

namespace ul::man {

    // ── Binary format constants ───────────────────────────────────────────────

    /// Four-byte magic that identifies a valid records.bin file.
    /// Byte order on-disk: Q=0x51, A=0x41, P=0x50, P=0x50 (LE u32 = 0x51415050).
    constexpr u32 QAppMagic = 0x51415050U;

    /// Wire format version embedded in records.bin Header.version.
    constexpr u32 QAppVersion = 1U;

    /// Maximum number of installed titles enumerated in one scan.
    constexpr size_t MaxScanEntries = 256;

    /// Maximum size of a single icon JPEG that will be written.
    /// Titles with icons larger than this limit will have icon_size = 0 written
    /// and no icon file created.  NsApplicationControlData::icon is 0x20000 bytes;
    /// we cap at that value.
    constexpr size_t MaxIconBytes = 0x20000; // 128 KiB

    // ── Output directory / file paths ─────────────────────────────────────────

    /// Root output directory on the SD card.
    constexpr const char QosAppsDir[]        = "sdmc:/switch/qos-apps";
    constexpr const char QosIconsDir[]       = "sdmc:/switch/qos-apps/icons";
    constexpr const char RecordsBinPath[]    = "sdmc:/switch/qos-apps/records.bin";
    constexpr const char RecordsBinTmpPath[] = "sdmc:/switch/qos-apps/records.bin.tmp";
    constexpr const char RecordsJsonPath[]   = "sdmc:/switch/qos-apps/records.json";
    constexpr const char RecordsJsonTmpPath[]= "sdmc:/switch/qos-apps/records.json.tmp";

    // ── Result summary ────────────────────────────────────────────────────────

    /// Summary returned by ScanAndWriteAppList().
    struct ScanResult {
        /// Number of titles successfully enumerated and written.
        u32 count;
        /// Total byte size of records.bin (0 on write failure).
        size_t bin_size;
        /// Total byte size of all icon files written.
        size_t icons_total_bytes;
        /// Human-readable summary line (first 3 app names, or error description).
        std::string summary;
        /// True if records.bin was written successfully.
        bool ok;
    };

    // ── Public API ────────────────────────────────────────────────────────────

    /// Enumerate all installed titles via ns:am2 and write records.bin,
    /// records.json, and per-title icon JPEGs to sdmc:/switch/qos-apps/.
    ///
    /// Never throws.  Failures are captured in the returned ScanResult.
    /// Call once after nsInitialize() in main.cpp Initialize().
    ScanResult ScanAndWriteAppList();

}
