// qd_IconCategory.cpp — NRO category classifier for uMenu C++ SP1 (v1.1.12).
// Created: 2026-04-23T00:00:00Z (SP1 audit fix F-01 — undefined symbol Classify()).
//
// Implements Classify() declared in qd_IconCategory.hpp.
// Two-pass algorithm ported verbatim from
//   tools/mock-nro-desktop-gui/src/icon_category.rs  (function classify()).
//
// Pass 1: case-insensitive substring match on nacp_name against NACP_RULES.
// Pass 2: case-insensitive substring match on file_stem against FILENAME_RULES.
// Default: NroCategory::Unknown, glyph '?', rgb(80,80,80).
//
// strcasestr is a POSIX.1-2001 extension present in devkitA64's newlib.
// A fallback implementation is provided at the bottom of this file for host
// builds (macOS/Linux) where _GNU_SOURCE / _POSIX_C_SOURCE may suppress it.

#include <ul/menu/qdesktop/qd_IconCategory.hpp>
#include <cstring>
#include <cctype>

namespace ul::menu::qdesktop {

// ── Portable strcasestr ───────────────────────────────────────────────────────
// newlib on devkitA64 exposes strcasestr via <strings.h> under _GNU_SOURCE.
// Rather than fight feature-test macros, we provide our own under a local name
// so this TU is self-contained on every host.

static const char *QdStrcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return nullptr;
    if (*needle == '\0') return haystack;  // empty needle matches immediately

    const size_t nlen = strlen(needle);
    for (; *haystack != '\0'; ++haystack) {
        if (static_cast<size_t>(strlen(haystack)) < nlen) {
            return nullptr;  // remaining haystack too short
        }
        bool match = true;
        for (size_t i = 0; i < nlen; ++i) {
            if (tolower(static_cast<unsigned char>(haystack[i])) !=
                tolower(static_cast<unsigned char>(needle[i]))) {
                match = false;
                break;
            }
        }
        if (match) return haystack;
    }
    return nullptr;
}

// ── NACP_RULES ────────────────────────────────────────────────────────────────
// Verbatim from icon_category.rs NACP_RULES.
// Scanned top-to-bottom; first match wins (most-specific entries first).

struct NacpRule {
    const char  *needle;
    NroCategory  cat;
};

static constexpr NacpRule NACP_RULES[] = {
    // Q OS own NROs
    { "q os",                 NroCategory::QosApp },
    { "qos-",                 NroCategory::QosApp },
    { "qos mock",             NroCategory::QosApp },

    // Emulators
    { "retroarch",            NroCategory::Emulator },
    { "retroarch32",          NroCategory::Emulator },
    { "mesen",                NroCategory::Emulator },
    { "melonds",              NroCategory::Emulator },
    { "ryujinx",              NroCategory::Emulator },
    { "yuzu",                 NroCategory::Emulator },
    { "ppsspp",               NroCategory::Emulator },
    { "desmume",              NroCategory::Emulator },
    { "mgba",                 NroCategory::Emulator },
    { "sameboy",              NroCategory::Emulator },
    { "citra",                NroCategory::Emulator },
    { "dolphin",              NroCategory::Emulator },

    // File managers / installers
    { "goldleaf",             NroCategory::FileManager },
    { "tinfoil",              NroCategory::FileManager },
    { "jksv",                 NroCategory::FileManager },
    { "dbi",                  NroCategory::FileManager },
    { "tinwoo",               NroCategory::FileManager },
    { "lithium",              NroCategory::FileManager },
    { "nx-ovlloader",         NroCategory::FileManager },
    { "nxshell",              NroCategory::FileManager },
    { "haze",                 NroCategory::FileManager },
    { "usb-botbase",          NroCategory::FileManager },

    // System tools
    { "lockpick",             NroCategory::SystemTool },
    { "nxthemes",             NroCategory::SystemTool },
    { "nxthemesinstaller",    NroCategory::SystemTool },
    { "aio-switch-updater",   NroCategory::SystemTool },
    { "aio switch updater",   NroCategory::SystemTool },
    { "daybreak",             NroCategory::SystemTool },
    { "sys-clk",              NroCategory::SystemTool },
    { "sysclk",               NroCategory::SystemTool },
    { "atmosphere",           NroCategory::SystemTool },
    { "hekate",               NroCategory::SystemTool },
    { "sigpatches",           NroCategory::SystemTool },
    { "nx-ovlloader",         NroCategory::SystemTool },
    { "tesla",                NroCategory::SystemTool },
    { "ovlsysmodules",        NroCategory::SystemTool },

    // Utilities
    { "90dns tester",         NroCategory::Utility },
    { "switch 90dns",         NroCategory::Utility },
    { "quick reboot",         NroCategory::Utility },
    { "reboot to payload",    NroCategory::Utility },
    { "linkalho",             NroCategory::Utility },
    { "ldn_mitm",             NroCategory::Utility },
    { "ftpd",                 NroCategory::Utility },
    { "nxmtp",                NroCategory::Utility },
    { "nx-ovlloader",         NroCategory::Utility },
    { "hbmenu",               NroCategory::Utility },
    { "homebrew menu",        NroCategory::Utility },
    { "nx-hbmenu",            NroCategory::Utility },

    // Backup / dump
    { "hatsify",              NroCategory::BackupDump },
    { "downgradefixer",       NroCategory::BackupDump },
    { "warmboot extractor",   NroCategory::BackupDump },
    { "warmboot_extractor",   NroCategory::BackupDump },
    { "instinct",             NroCategory::BackupDump },
    { "instinct-nx",          NroCategory::BackupDump },
    { "nxdumptool",           NroCategory::BackupDump },
    { "dump tool",            NroCategory::BackupDump },
    { "titletakeover",        NroCategory::BackupDump },
};

// ── FILENAME_RULES ────────────────────────────────────────────────────────────
// Verbatim from icon_category.rs FILENAME_RULES.
// Scanned only when NACP matching fails.

static constexpr NacpRule FILENAME_RULES[] = {
    // Q OS
    { "qos-mock",        NroCategory::QosApp },
    { "qos-test",        NroCategory::QosApp },
    { "qos-tui",         NroCategory::QosApp },
    { "qos-harness",     NroCategory::QosApp },
    { "qos-desktop",     NroCategory::QosApp },

    // Emulators
    { "retroarch",       NroCategory::Emulator },
    { "retroarch32",     NroCategory::Emulator },
    { "mgba",            NroCategory::Emulator },
    { "melonds",         NroCategory::Emulator },
    { "ppsspp",          NroCategory::Emulator },
    { "sameboy",         NroCategory::Emulator },
    { "mesen",           NroCategory::Emulator },

    // File managers
    { "goldleaf",        NroCategory::FileManager },
    { "tinfoil",         NroCategory::FileManager },
    { "jksv",            NroCategory::FileManager },
    { "dbi",             NroCategory::FileManager },
    { "tinwoo",          NroCategory::FileManager },
    { "nxshell",         NroCategory::FileManager },

    // System tools
    { "lockpick",        NroCategory::SystemTool },
    { "nxthemes",        NroCategory::SystemTool },
    { "daybreak",        NroCategory::SystemTool },
    { "sys-clk",         NroCategory::SystemTool },
    { "sysclk",          NroCategory::SystemTool },
    { "aio-switch",      NroCategory::SystemTool },
    { "tesla",           NroCategory::SystemTool },

    // Utilities
    { "90dns",           NroCategory::Utility },
    { "quickreboot",     NroCategory::Utility },
    { "quick-reboot",    NroCategory::Utility },
    { "reboot",          NroCategory::Utility },
    { "linkalho",        NroCategory::Utility },
    { "hbmenu",          NroCategory::Utility },
    { "ftpd",            NroCategory::Utility },
    { "nxmtp",           NroCategory::Utility },

    // Backup / dump
    { "hatsify",         NroCategory::BackupDump },
    { "warmboot",        NroCategory::BackupDump },
    { "instinct",        NroCategory::BackupDump },
    { "nxdumptool",      NroCategory::BackupDump },
    { "downgrade",       NroCategory::BackupDump },
};

// ── Classify ──────────────────────────────────────────────────────────────────

CategoryResult Classify(const char *nacp_name, const char *file_stem) {
    // Pass 1: NACP name substring match.
    if (nacp_name != nullptr && nacp_name[0] != '\0') {
        for (const NacpRule &rule : NACP_RULES) {
            if (QdStrcasestr(nacp_name, rule.needle) != nullptr) {
                CategoryResult r;
                r.category = rule.cat;
                r.glyph    = CategoryGlyph(rule.cat);
                CategoryRgb(rule.cat, r.r, r.g, r.b);
                return r;
            }
        }
    }

    // Pass 2: file stem substring match.
    if (file_stem != nullptr && file_stem[0] != '\0') {
        for (const NacpRule &rule : FILENAME_RULES) {
            if (QdStrcasestr(file_stem, rule.needle) != nullptr) {
                CategoryResult r;
                r.category = rule.cat;
                r.glyph    = CategoryGlyph(rule.cat);
                CategoryRgb(rule.cat, r.r, r.g, r.b);
                return r;
            }
        }
    }

    // Default: Unknown.
    CategoryResult r;
    r.category = NroCategory::Unknown;
    r.glyph    = CategoryGlyph(NroCategory::Unknown);  // '?'
    CategoryRgb(NroCategory::Unknown, r.r, r.g, r.b);  // rgb(80,80,80)
    return r;
}

} // namespace ul::menu::qdesktop
