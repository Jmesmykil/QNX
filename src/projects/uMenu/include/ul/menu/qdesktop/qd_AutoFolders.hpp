// qd_AutoFolders.hpp -- v1.7.0-stabilize-2 reconstruction.
//
// The auto-folder side table maps each desktop entry (keyed by a stable ID
// string) to a bucket index (AutoFolderIdx). It is populated at scan time
// by qd_DesktopIcons (ScanNros, SetApplicationEntries, ScanPayloads,
// PopulateBuiltins) and consumed at render time by qd_Launchpad to render
// the auto-folder tile strip and filter-by-bucket.
//
// CRITICAL: this file exists OUTSIDE NroEntry / LpItem so the libnx IPC
// command table is not perturbed (v1.6.10 hard-crash mitigation per A7).
// The classification is keyed by a stable_id string -- never by struct
// extension.
//
// Stable ID convention (mirrors the documented format in
// qd_DesktopIcons.hpp / qd_Launchpad.hpp):
//   NRO          -> nro_path verbatim ("sdmc:/switch/sys-clk.nro")
//   Application  -> "app:<hex16>"     ("app:01007ef000118000")
//   Payload      -> "payload:<fname>" ("payload:Atmosphere.bin")
//   Builtin      -> "builtin:<name>"  ("builtin:Vault")
//   Special PNG  -> "builtin:<name>"  ("builtin:Settings")
//
// Per K+1 SSOT, the top-level folder set is:
//   1. NX Games   (Nintendo first-party titles, IconKind::Application + IsNintendoPublisher)
//   2. Homebrew   (IconKind::Nro from sdmc:/switch/)
//   3. System     (IconKind::Special, e.g., Settings, Album, Themes)
//   4. Payloads   (IconKind::Special with payload:* stable_id)
//   5. Builtin    (Q OS dock built-ins; rendered last in Launchpad strip)
//
#pragma once

#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>  // ClassifyKind enum
#include <string>
#include <cstddef>

namespace ul::menu::qdesktop {

// Top-level auto-folder bucket discriminant. Value 0 (None) is the "no
// filter / show all" sentinel. Values 1..N map 1:1 to kTopLevelFolders[]
// entries; the qd_Launchpad render code relies on this 1-based mapping
// (see bucket_count[raw - 1u] usage).
enum class AutoFolderIdx : u8 {
    None    = 0,
    NxGames = 1,
    Homebrew = 2,
    System   = 3,
    Payloads = 4,
    Builtin  = 5,
};

// Number of populated bucket entries in kTopLevelFolders[]. Iteration
// uses `for (size_t fi = 0; fi < kTopLevelFolderCount; ++fi)`.
constexpr size_t kTopLevelFolderCount = 5;

// One entry per top-level bucket. The display_name is the user-facing
// label rendered into the folder-tile strip; idx is the bucket value
// stored in g_entry_classification_ for entries assigned to this bucket.
struct TopLevelFolderSpec {
    AutoFolderIdx  idx;
    const char    *display_name;
};

// 5-entry array, mirroring AutoFolderIdx ordering 1..5 (skipping None).
// The Launchpad walks this array at render time. Order is LOAD-BEARING:
// kTopLevelFolders[0] must be the bucket whose idx value is 1 (NxGames),
// and so on, so that bucket_count[raw - 1u] indexes correctly.
inline constexpr TopLevelFolderSpec kTopLevelFolders[kTopLevelFolderCount] = {
    { AutoFolderIdx::NxGames,  "NX Games" },
    { AutoFolderIdx::Homebrew, "Homebrew" },
    { AutoFolderIdx::System,   "System"   },
    { AutoFolderIdx::Payloads, "Payloads" },
    { AutoFolderIdx::Builtin,  "Builtin"  },
};

// Side-table mutators (used by the four scan functions in qd_DesktopIcons).
// Registers (or overwrites) the classification for a stable_id. Called
// once per entry per scan pass.
void RegisterClassification(const std::string &stable_id, ClassifyKind kind);

// Clears the entire classification table. Called at the top of the
// QdDesktopIconsElement constructor before any scan, so a re-init does
// not accumulate stale entries from a previous enumeration.
void ClearClassifications();

// Side-table reader (used by qd_Launchpad at render/filter time).
// Returns AutoFolderIdx::None if the stable_id is not registered.
// The mapping rules:
//   ClassifyKind::NintendoGame   -> NxGames
//   ClassifyKind::ThirdPartyGame -> NxGames (treated as installed apps)
//   ClassifyKind::HomebrewTool   -> Homebrew
//   ClassifyKind::Emulator       -> Homebrew
//   ClassifyKind::SystemUtil     -> System
//   ClassifyKind::Payload        -> Payloads
//   ClassifyKind::Builtin        -> Builtin
//   ClassifyKind::Unknown        -> Homebrew (best-fit fallback)
AutoFolderIdx LookupFolderIdx(const std::string &stable_id);

} // namespace ul::menu::qdesktop
