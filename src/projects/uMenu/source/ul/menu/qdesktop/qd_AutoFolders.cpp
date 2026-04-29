// qd_AutoFolders.cpp -- v1.7.0-stabilize-2 reconstruction.
//
// Implementation of the auto-folder side table declared in
// qd_AutoFolders.hpp. The table is a process-singleton std::unordered_map
// keyed by stable_id string -> ClassifyKind. It is sized to scale with
// the icon count (~50 entries today, capped by MAX_ICONS = 48 + builtins
// + payloads in the same range), so a flat hash map is appropriate.
//
// Threading: all callers run on the main UI thread (constructor, scan
// passes, OnRender / OnInput). No mutex is needed.

#include <ul/menu/qdesktop/qd_AutoFolders.hpp>
#include <ul/menu/qdesktop/qd_FolderClassifier.hpp>
#include <unordered_map>

namespace ul::menu::qdesktop {

namespace {

// Process-singleton table. Lifetime spans the entire uMenu run.
// Cleared by ClearClassifications() before each enumeration pass so a
// re-init does not accumulate stale entries.
//
// Why a function-static rather than a translation-unit-static:
// guaranteed initialization-on-first-use ordering (no static-init order
// fiasco with the first scan pass that fires from QdDesktopIconsElement's
// constructor).
std::unordered_map<std::string, ClassifyKind> &Table() {
    static std::unordered_map<std::string, ClassifyKind> g_table;
    return g_table;
}

} // namespace

// Translate ClassifyKind (legacy 7-value enum) to FolderIdx (new 9-value enum).
// NintendoGame    -> NxGames
// ThirdPartyGame  -> ThirdPartyGames
// HomebrewTool    -> Tools
// Emulator        -> Emulators   (previously collapsed into Homebrew — fixed by v1.9)
// SystemUtil      -> System
// Payload         -> Payloads
// Builtin         -> Homebrew    (builtins live in Q OS / Homebrew visually)
// Unknown         -> Homebrew    (best-fit fallback unchanged)
static FolderIdx ClassifyKindToFolderIdx(ClassifyKind kind) {
    switch (kind) {
        case ClassifyKind::NintendoGame:   return FolderIdx::NxGames;
        case ClassifyKind::ThirdPartyGame: return FolderIdx::ThirdPartyGames;
        case ClassifyKind::HomebrewTool:   return FolderIdx::Tools;
        case ClassifyKind::Emulator:       return FolderIdx::Emulators;
        case ClassifyKind::SystemUtil:     return FolderIdx::System;
        case ClassifyKind::Payload:        return FolderIdx::Payloads;
        case ClassifyKind::Builtin:        return FolderIdx::Homebrew;
        case ClassifyKind::Unknown:
        default:                           return FolderIdx::Homebrew;
    }
}

void RegisterClassification(const std::string &stable_id, ClassifyKind kind) {
    Table()[stable_id] = kind;
    // Mirror into QdFolderClassifier so the new 9-bucket system stays in sync.
    QdFolderClassifier::Get().RegisterDirect(stable_id, ClassifyKindToFolderIdx(kind));
}

void ClearClassifications() {
    Table().clear();
}

AutoFolderIdx LookupFolderIdx(const std::string &stable_id) {
    auto &t = Table();
    const auto it = t.find(stable_id);
    if (it == t.end()) {
        // Unregistered entries fall through to "no bucket" so the
        // Launchpad does not assign them a folder tile. Caller code
        // treats AutoFolderIdx::None as "show in default ungrouped pass."
        return AutoFolderIdx::None;
    }
    switch (it->second) {
        case ClassifyKind::NintendoGame:
        case ClassifyKind::ThirdPartyGame:
            // K+1 SSOT: both Nintendo first-party and third-party
            // installed Applications land in the NX Games bucket. The
            // distinction is preserved in the classification table for
            // future use (e.g., color coding the tile) but the bucket
            // is the same.
            return AutoFolderIdx::NxGames;
        case ClassifyKind::HomebrewTool:
        case ClassifyKind::Emulator:
            // Both NRO categories share the Homebrew bucket; the per-NRO
            // glyph/category visual distinction is rendered by
            // qd_IconCategory at the icon level, not the folder level.
            return AutoFolderIdx::Homebrew;
        case ClassifyKind::SystemUtil:
            return AutoFolderIdx::System;
        case ClassifyKind::Payload:
            return AutoFolderIdx::Payloads;
        case ClassifyKind::Builtin:
            return AutoFolderIdx::Builtin;
        case ClassifyKind::Unknown:
        default:
            // Best-fit fallback: an unknown kind is most likely a
            // homebrew NRO that did not match the AutoFolderSpec rule
            // table, so route it to Homebrew. This keeps the bucket
            // populated and discoverable rather than orphaned.
            return AutoFolderIdx::Homebrew;
    }
}

} // namespace ul::menu::qdesktop
