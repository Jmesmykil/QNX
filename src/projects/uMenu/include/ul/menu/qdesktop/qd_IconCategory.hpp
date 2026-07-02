// qd_IconCategory.hpp — 7-category NRO classifier for uMenu C++ SP1 (v1.1.12).
// Ported from tools/mock-nro-desktop-gui/src/icon_category.rs.
#pragma once
#include <pu/Plutonium>
#include <cstring>

namespace ul::menu::qdesktop {

// 7 NRO categories.  Glyph chars and fallback RGB match icon_category.rs exactly.
enum class NroCategory : u8 {
    Emulator    = 0,  // glyph 'E', rgb(60,100,160)
    FileManager = 1,  // glyph 'F', rgb(50,130,80)
    SystemTool  = 2,  // glyph 'S', rgb(110,80,140)
    Utility     = 3,  // glyph 'U', rgb(170,130,30)
    BackupDump  = 4,  // glyph 'B', rgb(140,70,50)
    QosApp      = 5,  // glyph 'Q', rgb(30,80,150)
    Unknown     = 6,  // glyph '?', rgb(80,80,80)
};

// Results returned by Classify().
struct CategoryResult {
    NroCategory category;
    char  glyph;    // single ASCII char for the category badge
    u8    r, g, b;  // fallback background color
};

// Classify an NRO by its NACP name and file stem.
// Two-pass algorithm from icon_category.rs:
//   Pass 1: NACP name substring matches (case-insensitive substring scan).
//   Pass 2: file_stem substring matches.
//   Default: Unknown.
//
// nacp_name: NACP application name (may be empty string if NACP not available).
// file_stem: NRO filename without path and without extension (e.g. "hbmenu").
CategoryResult Classify(const char *nacp_name, const char *file_stem);

// Convenience: get the glyph char for a category.
inline char CategoryGlyph(NroCategory c) {
    switch (c) {
        case NroCategory::Emulator:    return 'E';
        case NroCategory::FileManager: return 'F';
        case NroCategory::SystemTool:  return 'S';
        case NroCategory::Utility:     return 'U';
        case NroCategory::BackupDump:  return 'B';
        case NroCategory::QosApp:      return 'Q';
        default:                       return '?';
    }
}

// Convenience: get the fallback RGB for a category.
inline void CategoryRgb(NroCategory c, u8 &r, u8 &g, u8 &b) {
    switch (c) {
        case NroCategory::Emulator:    r=60;  g=100; b=160; return;
        case NroCategory::FileManager: r=50;  g=130; b=80;  return;
        case NroCategory::SystemTool:  r=110; g=80;  b=140; return;
        case NroCategory::Utility:     r=170; g=130; b=30;  return;
        case NroCategory::BackupDump:  r=140; g=70;  b=50;  return;
        case NroCategory::QosApp:      r=30;  g=80;  b=150; return;
        default:                       r=80;  g=80;  b=80;  return;
    }
}

} // namespace ul::menu::qdesktop
