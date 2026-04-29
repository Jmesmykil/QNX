// qd_FolderClassifier.cpp — v1.9 unified folder classification engine.
//
// Six-rule pipeline (priority, first-match wins):
//   Rule 1: user TOML override  (sdmc:/ulaunch/qos-folder-overrides.toml)
//   Rule 2: Builtin             (builtin:* stable_id prefix)
//   Rule 3: Payload             (payload:* stable_id prefix)
//   Rule 4: Nintendo publisher  (top byte of app_id == 0x01)
//   Rule 5: Third-party Application (app:* stable_id, not Nintendo)
//   Rule 6: NRO auto-classify   (signal cascade: keyword → NACP → path → Homebrew)
//
// Threading: all callers on the main UI thread. No mutex needed.

#include <ul/menu/qdesktop/qd_FolderClassifier.hpp>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>

namespace ul::menu::qdesktop {

// ── paths ─────────────────────────────────────────────────────────────────────

static constexpr const char *kOverridePath =
    "sdmc:/ulaunch/qos-folder-overrides.toml";
static constexpr const char *kOverrideTmpPath =
    "sdmc:/ulaunch/qos-folder-overrides.toml.tmp";

// ── singleton ─────────────────────────────────────────────────────────────────

QdFolderClassifier &QdFolderClassifier::Get() {
    static QdFolderClassifier s_instance;
    return s_instance;
}

// ── reset ─────────────────────────────────────────────────────────────────────

void QdFolderClassifier::Reset() {
    table_.clear();
    bucket_counts_.fill(0);
    // overrides_ survives Reset() — user choices are preserved across scans.
}

// ── TOML helpers ─────────────────────────────────────────────────────────────
// Hand-rolled minimal parser scoped to the shape written by PersistOverrides:
//
//   [stable_id]
//   folder = "FolderName"
//
// Assumptions:
//   - Lines are NUL-terminated after fgets.
//   - stable_id has no embedded ']'.
//   - folder values are one of the canonical display_names in kFolderSpecs.
//   - Unknown folder names are skipped silently.

static FolderIdx FolderIdxFromName(const char *name) {
    for (size_t i = 0; i < kFolderCount; ++i) {
        if (strcmp(name, kFolderSpecs[i].display_name) == 0) {
            return kFolderSpecs[i].idx;
        }
    }
    return FolderIdx::None;
}

static const char *FolderIdxToName(FolderIdx idx) {
    for (size_t i = 0; i < kFolderCount; ++i) {
        if (kFolderSpecs[i].idx == idx) {
            return kFolderSpecs[i].display_name;
        }
    }
    return nullptr;
}

// Trim trailing whitespace (\r \n \t space) from a buffer in-place.
static void TrimRight(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n'
                     || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

void QdFolderClassifier::LoadOverrides() {
    overrides_.clear();

    FILE *f = fopen(kOverridePath, "r");
    if (!f) {
        return;
    }

    char line[512];
    char current_sid[512] = {};
    while (fgets(line, sizeof(line), f)) {
        TrimRight(line);
        if (line[0] == '[') {
            // Section header: [stable_id]
            char *end = strchr(line + 1, ']');
            if (end) {
                *end = '\0';
                strncpy(current_sid, line + 1, sizeof(current_sid) - 1);
                current_sid[sizeof(current_sid) - 1] = '\0';
            }
        } else if (strncmp(line, "folder = \"", 10) == 0) {
            // Key-value: folder = "Name"
            char *name_start = line + 10;
            char *name_end   = strchr(name_start, '"');
            if (name_end && current_sid[0] != '\0') {
                *name_end = '\0';
                const FolderIdx fi = FolderIdxFromName(name_start);
                if (fi != FolderIdx::None) {
                    overrides_.emplace(current_sid, fi);
                }
            }
        }
        // Blank lines and comments (#) are ignored.
    }
    fclose(f);
}

void QdFolderClassifier::PersistOverrides() {
    // Write to .tmp then atomic rename — avoids partial file on crash/OOM.
    FILE *f = fopen(kOverrideTmpPath, "w");
    if (!f) {
        return;
    }
    for (const auto &kv : overrides_) {
        const char *name = FolderIdxToName(kv.second);
        if (!name) {
            continue;
        }
        fprintf(f, "[%s]\nfolder = \"%s\"\n\n", kv.first.c_str(), name);
    }
    fclose(f);
    rename(kOverrideTmpPath, kOverridePath);
}

// ── NRO auto-classification cascade (Rule 6) ──────────────────────────────────
//
// Signal order (first match wins):
//   A. kAutoFolderSpecs keyword match (case-insensitive substring of NRO path stem)
//   B. NACP author == "Nintendo" → NxGames (unlikely for an NRO but guards edge cases)
//   C. NACP name two-pass:
//      C1. exact lowercase keyword match in name_lower
//      C2. substring match
//   D. Path prefix: sdmc:/switch/qos → QOS
//   E. Default: Homebrew

struct NroKeywordSpec {
    const char *keyword;
    FolderIdx   folder;
};

static const NroKeywordSpec kNroKeywords[] = {
    // Emulators — matched before generic keywords.
    { "yuzu",            FolderIdx::Emulators },
    { "ryujinx",         FolderIdx::Emulators },
    { "retroarch",       FolderIdx::Emulators },
    { "melonds",         FolderIdx::Emulators },
    { "ppsspp",          FolderIdx::Emulators },
    { "desmume",         FolderIdx::Emulators },
    { "mupen64",         FolderIdx::Emulators },
    { "dolphin",         FolderIdx::Emulators },
    { "citra",           FolderIdx::Emulators },
    { "bsnes",           FolderIdx::Emulators },
    { "snes9x",          FolderIdx::Emulators },
    { "mgba",            FolderIdx::Emulators },
    { "nestopia",        FolderIdx::Emulators },
    { "mednafen",        FolderIdx::Emulators },
    { "mesen2",          FolderIdx::Emulators },
    { "mesen-s",         FolderIdx::Emulators },
    { "bsnes-hd",        FolderIdx::Emulators },
    { "mupen64plus",     FolderIdx::Emulators },
    { "duckstation",     FolderIdx::Emulators },
    { "swanstation",     FolderIdx::Emulators },
    { "scummvm",         FolderIdx::Emulators },
    // System utilities — clocking, overlays, recovery.
    { "sys-clk",         FolderIdx::System },
    { "sysclk",          FolderIdx::System },
    { "nxovl",           FolderIdx::System },
    { "monitor",         FolderIdx::System },
    { "hekate",          FolderIdx::System },
    { "fusee",           FolderIdx::System },
    { "hwfly",           FolderIdx::System },
    { "picofly",         FolderIdx::System },
    { "vault",           FolderIdx::System },
    // Tools — package managers, file managers, theme tools.
    { "goldleaf",        FolderIdx::Tools },
    { "tinfoil",         FolderIdx::Tools },
    { "awoo",            FolderIdx::Tools },
    { "dbi",             FolderIdx::Tools },
    { "haze",            FolderIdx::Tools },
    { "ftpd",            FolderIdx::Tools },
    { "nxshell",         FolderIdx::Tools },
    { "nxmtp",           FolderIdx::Tools },
    { "tinwoo",          FolderIdx::Tools },
    { "lockpick",        FolderIdx::Tools },
    { "linkalho",        FolderIdx::Tools },
    { "netman",          FolderIdx::Tools },
    { "nxtheme",         FolderIdx::Tools },
    { "daybreak",        FolderIdx::Tools },
    { "sphaira",         FolderIdx::Tools },
    { "umanager",        FolderIdx::Tools },
    // Generic homebrew shell.
    { "hbmenu",          FolderIdx::Homebrew },
    // Sentinel.
    { nullptr,           FolderIdx::None },
};

// Case-insensitive substring search (avoids <strings.h> dependency).
static bool CiFindStr(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') { return true; }
    if (!haystack)                    { return false; }
    for (size_t i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        for (; needle[j] != '\0'; ++j) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') { h = static_cast<char>(h + 32); }
            if (n >= 'A' && n <= 'Z') { n = static_cast<char>(n + 32); }
            if (h != n) { break; }
        }
        if (needle[j] == '\0') { return true; }
    }
    return false;
}

FolderIdx QdFolderClassifier::AutoClassifyNro(const std::string &nro_path,
                                               const std::string &nacp_name,
                                               const std::string &nacp_author,
                                               bool               is_qos_nro) {
    // Q OS preempt.
    if (is_qos_nro) {
        return FolderIdx::QOS;
    }
    // Path-based Q OS preempt.
    if (CiFindStr(nro_path.c_str(), "qos-") || CiFindStr(nro_path.c_str(), "/qos/")) {
        return FolderIdx::QOS;
    }

    // Signal A: keyword match against nro_path (basename / full path).
    const char *path_cstr = nro_path.c_str();
    for (const NroKeywordSpec *k = kNroKeywords; k->keyword; ++k) {
        if (CiFindStr(path_cstr, k->keyword)) {
            return k->folder;
        }
    }

    // Signal B: NACP author heuristic.
    if (!nacp_author.empty() &&
        (nacp_author == "Nintendo" || nacp_author == "NINTENDO")) {
        return FolderIdx::NxGames;
    }

    // Signal C: NACP name keyword match.
    if (!nacp_name.empty()) {
        const char *name_cstr = nacp_name.c_str();
        // C1 exact lowercase match.
        char lower[64];
        size_t ci = 0;
        for (; name_cstr[ci] != '\0' && ci < 63u; ++ci) {
            char c = name_cstr[ci];
            lower[ci] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        }
        lower[ci] = '\0';
        for (const NroKeywordSpec *k = kNroKeywords; k->keyword; ++k) {
            if (strcmp(lower, k->keyword) == 0) {
                return k->folder;
            }
        }
        // C2 substring match.
        for (const NroKeywordSpec *k = kNroKeywords; k->keyword; ++k) {
            if (CiFindStr(name_cstr, k->keyword)) {
                return k->folder;
            }
        }
    }

    // Signal D: path prefix — sdmc:/switch/ is the default homebrew root;
    // anything deeper that hasn't matched is generic Homebrew.
    // (Q OS paths already handled above.)

    return FolderIdx::Homebrew;
}

// ── bucket helper ─────────────────────────────────────────────────────────────

static size_t BucketIndex(FolderIdx idx) {
    // FolderIdx values 1..8 map to bucket_counts_ indices 0..7.
    const size_t raw = static_cast<size_t>(idx);
    if (raw == 0 || raw > 8) {
        return SIZE_MAX;
    }
    return raw - 1u;
}

static void IncrBucket(std::array<size_t, kFolderCount> &counts, FolderIdx idx) {
    const size_t bi = BucketIndex(idx);
    if (bi < kFolderCount) {
        ++counts[bi];
    }
}

// ── public classify API ───────────────────────────────────────────────────────

FolderIdx QdFolderClassifier::ClassifyNro(const std::string &stable_id,
                                           const std::string &nro_path,
                                           const std::string &nacp_name,
                                           const std::string &nacp_author,
                                           bool               is_qos_nro) {
    // Rule 1: user override.
    {
        const auto it = overrides_.find(stable_id);
        if (it != overrides_.end()) {
            const FolderIdx fi = it->second;
            table_[stable_id] = fi;
            IncrBucket(bucket_counts_, fi);
            return fi;
        }
    }

    // Rules 2-6: auto-classify.
    const FolderIdx fi = AutoClassifyNro(nro_path, nacp_name, nacp_author, is_qos_nro);
    table_[stable_id] = fi;
    IncrBucket(bucket_counts_, fi);
    return fi;
}

FolderIdx QdFolderClassifier::ClassifyApplication(const std::string &stable_id,
                                                   u64                app_id,
                                                   bool               is_nintendo_publisher) {
    // Rule 1: user override.
    {
        const auto it = overrides_.find(stable_id);
        if (it != overrides_.end()) {
            const FolderIdx fi = it->second;
            table_[stable_id] = fi;
            IncrBucket(bucket_counts_, fi);
            return fi;
        }
    }

    // Rule 4: Nintendo publisher → NxGames.
    // Rule 5: third-party → ThirdPartyGames.
    (void)app_id;
    const FolderIdx fi = is_nintendo_publisher ? FolderIdx::NxGames
                                               : FolderIdx::ThirdPartyGames;
    table_[stable_id] = fi;
    IncrBucket(bucket_counts_, fi);
    return fi;
}

FolderIdx QdFolderClassifier::ClassifyPayload(const std::string &stable_id,
                                               const std::string &payload_name) {
    // Rule 1: user override.
    {
        const auto it = overrides_.find(stable_id);
        if (it != overrides_.end()) {
            const FolderIdx fi = it->second;
            table_[stable_id] = fi;
            IncrBucket(bucket_counts_, fi);
            return fi;
        }
    }

    // Rule 3: all payloads → Payloads bucket.
    (void)payload_name;
    const FolderIdx fi = FolderIdx::Payloads;
    table_[stable_id] = fi;
    IncrBucket(bucket_counts_, fi);
    return fi;
}

FolderIdx QdFolderClassifier::ClassifyBuiltin(const std::string &stable_id,
                                               const std::string &builtin_name) {
    // Rule 1: user override.
    {
        const auto it = overrides_.find(stable_id);
        if (it != overrides_.end()) {
            const FolderIdx fi = it->second;
            table_[stable_id] = fi;
            IncrBucket(bucket_counts_, fi);
            return fi;
        }
    }

    // Rule 2: builtins → System (they are system-level Q OS entries).
    (void)builtin_name;
    const FolderIdx fi = FolderIdx::System;
    table_[stable_id] = fi;
    IncrBucket(bucket_counts_, fi);
    return fi;
}

// ── readers ───────────────────────────────────────────────────────────────────

FolderIdx QdFolderClassifier::Lookup(const std::string &stable_id) const {
    const auto it = table_.find(stable_id);
    return (it != table_.end()) ? it->second : FolderIdx::None;
}

size_t QdFolderClassifier::BucketCount(FolderIdx idx) const {
    const size_t bi = BucketIndex(idx);
    return (bi < kFolderCount) ? bucket_counts_[bi] : 0u;
}

// ── user override mutators ────────────────────────────────────────────────────

void QdFolderClassifier::SetUserOverride(const std::string &stable_id, FolderIdx idx) {
    overrides_[stable_id] = idx;
    PersistOverrides();
    // Update the live table immediately so the next Lookup() returns the new value
    // without requiring a full Reset()+reclassify pass.
    const auto old_it = table_.find(stable_id);
    if (old_it != table_.end()) {
        const size_t old_bi = BucketIndex(old_it->second);
        if (old_bi < kFolderCount && bucket_counts_[old_bi] > 0) {
            --bucket_counts_[old_bi];
        }
    }
    table_[stable_id] = idx;
    IncrBucket(bucket_counts_, idx);
}

void QdFolderClassifier::RegisterDirect(const std::string &stable_id, FolderIdx idx) {
    // Adjust bucket counts when overwriting an existing entry.
    const auto old_it = table_.find(stable_id);
    if (old_it != table_.end() && old_it->second != idx) {
        const size_t old_bi = BucketIndex(old_it->second);
        if (old_bi < kFolderCount && bucket_counts_[old_bi] > 0) {
            --bucket_counts_[old_bi];
        }
        old_it->second = idx;
        IncrBucket(bucket_counts_, idx);
    } else if (old_it == table_.end()) {
        table_[stable_id] = idx;
        IncrBucket(bucket_counts_, idx);
    }
    // If same value: no-op (avoid double-counting).
}

void QdFolderClassifier::RemoveUserOverride(const std::string &stable_id) {
    overrides_.erase(stable_id);
    PersistOverrides();
    // The live table entry remains (with its auto-classified value); it will be
    // refreshed on the next Reset()+reclassify pass (e.g., next Launchpad Open).
}

} // namespace ul::menu::qdesktop
