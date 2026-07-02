// qd_AutoFolders.cpp -- v2.0 implementation.
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
#include <cstdio>
#include <cstring>

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

// Per-bucket enable flags indexed by (AutoFolderIdx raw value - 1u).
// All buckets default to enabled. Persisted to/from kFolderSettingsPath.
static bool g_auto_folder_enabled[kTopLevelFolderCount] = {
    true,  // NxGames  (idx 1, slot 0)
    true,  // Homebrew (idx 2, slot 1)
    true,  // System   (idx 3, slot 2)
    true,  // Payloads (idx 4, slot 3)
    true,  // Builtin  (idx 5, slot 4)
};

static constexpr const char *kFolderSettingsPath =
    "sdmc:/ulaunch/qos-folder-settings.toml";
static constexpr const char *kFolderSettingsTmpPath =
    "sdmc:/ulaunch/qos-folder-settings.toml.tmp";

// Per-bucket key strings, indexed as g_auto_folder_enabled.
static constexpr const char *kFolderSettingKeys[kTopLevelFolderCount] = {
    "nxgames",
    "homebrew",
    "system",
    "payloads",
    "builtin",
};

} // namespace

// Translate ClassifyKind (legacy 7-value enum) to FolderIdx (new 9-value enum).
// NintendoGame    -> NxGames
// ThirdPartyGame  -> ThirdPartyGames
// HomebrewTool    -> Tools
// Emulator        -> Emulators   (previously collapsed into Homebrew -- fixed by v1.9)
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
        // Launchpad does not assign them a folder tile.
        return AutoFolderIdx::None;
    }

    AutoFolderIdx result = AutoFolderIdx::None;
    switch (it->second) {
        case ClassifyKind::NintendoGame:
        case ClassifyKind::ThirdPartyGame:
            result = AutoFolderIdx::NxGames;
            break;
        case ClassifyKind::HomebrewTool:
        case ClassifyKind::Emulator:
            result = AutoFolderIdx::Homebrew;
            break;
        case ClassifyKind::SystemUtil:
            result = AutoFolderIdx::System;
            break;
        case ClassifyKind::Payload:
            result = AutoFolderIdx::Payloads;
            break;
        case ClassifyKind::Builtin:
            result = AutoFolderIdx::Builtin;
            break;
        case ClassifyKind::Unknown:
        default:
            result = AutoFolderIdx::Homebrew;
            break;
    }

    if (result == AutoFolderIdx::None) {
        return AutoFolderIdx::None;
    }
    // Check the enable flag for this bucket. If disabled, treat as None.
    const size_t slot = static_cast<size_t>(result) - 1u;
    if (!g_auto_folder_enabled[slot]) {
        return AutoFolderIdx::None;
    }
    return result;
}

// ── Per-category enable/disable ──────────────────────────────────────────────

bool IsAutoFolderEnabled(AutoFolderIdx idx) {
    if (idx == AutoFolderIdx::None) {
        return false;
    }
    const size_t slot = static_cast<size_t>(idx) - 1u;
    if (slot >= kTopLevelFolderCount) {
        return false;
    }
    return g_auto_folder_enabled[slot];
}

void SetAutoFolderEnabled(AutoFolderIdx idx, bool enabled) {
    if (idx == AutoFolderIdx::None) {
        return;
    }
    const size_t slot = static_cast<size_t>(idx) - 1u;
    if (slot >= kTopLevelFolderCount) {
        return;
    }
    g_auto_folder_enabled[slot] = enabled;
    SaveAutoFolderSettings();
}

void LoadAutoFolderSettings() {
    // Default: all enabled.
    for (size_t i = 0; i < kTopLevelFolderCount; ++i) {
        g_auto_folder_enabled[i] = true;
    }

    FILE *f = fopen(kFolderSettingsPath, "r");
    if (!f) {
        // File absent -- defaults already set above.
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline / carriage return.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#' || line[0] == '[') {
            continue;
        }
        // Expect lines of the form:   key = true   or   key = false
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        // Trim spaces from key.
        while (*key == ' ') ++key;
        size_t klen = strlen(key);
        while (klen > 0 && key[klen - 1] == ' ') --klen;
        // Trim spaces from val.
        while (*val == ' ') ++val;

        // Match against known keys.
        for (size_t i = 0; i < kTopLevelFolderCount; ++i) {
            if (strncmp(key, kFolderSettingKeys[i], klen) == 0 &&
                kFolderSettingKeys[i][klen] == '\0') {
                g_auto_folder_enabled[i] = (strncmp(val, "true", 4) == 0);
                break;
            }
        }
    }
    fclose(f);
}

void SaveAutoFolderSettings() {
    FILE *f = fopen(kFolderSettingsTmpPath, "w");
    if (!f) {
        return;
    }
    fprintf(f, "# Q OS auto-folder enable flags\n");
    fprintf(f, "[folders]\n");
    for (size_t i = 0; i < kTopLevelFolderCount; ++i) {
        fprintf(f, "%s = %s\n",
                kFolderSettingKeys[i],
                g_auto_folder_enabled[i] ? "true" : "false");
    }
    fclose(f);
    // v3.7: Switch rename() can't overwrite (errno 17 EEXIST) — remove canonical first.
    remove(kFolderSettingsPath);
    rename(kFolderSettingsTmpPath, kFolderSettingsPath);
}

} // namespace ul::menu::qdesktop
