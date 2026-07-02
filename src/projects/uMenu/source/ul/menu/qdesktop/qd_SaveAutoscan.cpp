// qd_SaveAutoscan.cpp — SD-card save-location autoscanner (W12-SAVE-DISCO).
//
// Walks candidate save paths on sdmc: and returns SaveScanEntry records keyed
// to QdSaveEditorLayout::kGameNames[] indices.  No save parsing is performed —
// detection is heuristic only (file name + extension matching).
//
// Game-index mapping uses the canonical kGameNames[] order:
//   0: "Pokemon: Let's Go"                (covers both Pikachu + Eevee TIDs)
//   1: "Sword / Shield"                   (covers both Sword + Shield TIDs)
//   2: "Brilliant Diamond / Shining Pearl"(covers both BDSP TIDs)
//   3: "Legends: Arceus"
//   4: "Scarlet / Violet"                 (covers both Scarlet + Violet TIDs)
//   5: kOtherGameIndex — unattributed saves found in generic paths
//
// Fix log (W12B-AUTOSCAN):
//   - Added verbose per-path probe logging (DirExists + entry count).
//   - Fixed JKSV scan: directory name contains "[<TID>]" embedded in a longer
//     game-title string; keyword match already handles names like
//     "Pokemon Scarlet [01008f6008c5e000]" via the kJksvKeywords table.
//     The REAL bug was that CountSaveFiles() on the title dir returned counts
//     from user-uid sub-dirs (which exist), but the actual backup files are
//     one level deeper.  CountSaveFiles now accepts a depth parameter so the
//     JKSV pass dives into user-uid dirs to count real backup slots.
//   - Fixed Checkpoint scan: added keyword fallback (Checkpoint may not embed
//     TIDs in folder names at all, e.g. "Pokemon Scarlet").
//   - Added saveMeta bonus path per-TID.
//   - Fixed generic sdmc:/saves/ handling: now emits a single entry with
//     game_index=kOtherGameIndex (5) instead of polluting all game slots.
//   - ProbeDir() helper centralises logging; every candidate path now logs
//     whether it existed and how many entries matched.

#include <ul/menu/qdesktop/qd_SaveAutoscan.hpp>
#include <ul/ul_Result.hpp>   // UL_LOG_INFO / UL_LOG_WARN

#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>

namespace ul::menu::qdesktop {

// ── TID → game-index mapping ──────────────────────────────────────────────────
// Maps known Pokémon title-IDs (lowercase hex, 16 chars) to kGameNames indices.

struct TidEntry {
    const char *tid_lower;  ///< 16-char lowercase hex TID (no 0x prefix).
    int         game_index; ///< Index into kGameNames[].
};

static constexpr TidEntry kTidMap[] = {
    // Let's Go Pikachu / Eevee  → kGameNames[0]
    { "010003f003a34000", 0 },   // Let's Go Pikachu
    { "0100187003a36000", 0 },   // Let's Go Eevee
    // Sword / Shield             → kGameNames[1]
    { "0100abf008968000", 1 },   // Sword
    { "01008db008c2c000", 1 },   // Shield
    // Brilliant Diamond / Pearl  → kGameNames[2]
    { "0100862011c46000", 2 },   // Brilliant Diamond
    { "010018e011d92000", 2 },   // Shining Pearl
    // Legends: Arceus            → kGameNames[3]
    { "01001f5010dfa000", 3 },   // Legends: Arceus
    // Scarlet / Violet           → kGameNames[4]
    { "0100a3d008c5c000", 4 },   // Scarlet
    { "01008f6008c5e000", 4 },   // Violet
};
static constexpr size_t kTidMapCount =
    sizeof(kTidMap) / sizeof(kTidMap[0]);

// Candidate save directory templates.  %s is replaced by the lowercase TID.
static constexpr const char *kAtmosphereTidFmt =
    "sdmc:/atmosphere/contents/%s/save/";
static constexpr const char *kAtmosphereTidAltFmt =
    "sdmc:/atmosphere/contents/%s/save_data/";
static constexpr const char *kAtmosphereTidMetaFmt =
    "sdmc:/atmosphere/contents/%s/saveMeta/";

// JKSV, Checkpoint, and generic paths are title-independent; we probe them once
// and look for sub-directories matching known TIDs or game name fragments.
//
// W12B-HOTFIX: JKSV's default layout puts game folders DIRECTLY under /JKSV/
// (e.g. /JKSV/Pokemon Brilliant Diamond/<user> - <timestamp>.zip).  The
// /JKSV/Saves/ prefix is an unusual variant some users have but is NOT the
// default.  Probe the canonical /JKSV/ root first; the W12B-FULL agent will
// extend this with an alternate-root fallback if needed.
static constexpr const char *kJksvRoot       = "sdmc:/JKSV/";
static constexpr const char *kCheckpointRoot = "sdmc:/switch/Checkpoint/saves/";
static constexpr const char *kGenericSaves   = "sdmc:/saves/";

// ── Keyword table (shared by JKSV and Checkpoint passes) ─────────────────────

struct KwEntry {
    const char *kw1;
    const char *kw2;  // nullptr = only kw1 required
    int         idx;
};

static constexpr KwEntry kGameKeywords[] = {
    // Let's Go: need "let" + ("pikachu" OR "eevee")
    { "let",      "pikachu",  0 },
    { "let",      "eevee",    0 },
    // Sword / Shield
    { "sword",    nullptr,    1 },
    { "shield",   nullptr,    1 },
    // Brilliant Diamond / Shining Pearl
    { "brilliant",nullptr,    2 },
    { "shining",  nullptr,    2 },
    // Legends: Arceus
    { "arceus",   nullptr,    3 },
    // Scarlet / Violet
    { "scarlet",  nullptr,    4 },
    { "violet",   nullptr,    4 },
    // Pokemon + broad fallback keywords that appear in both games of a pair.
    // These are checked LAST so more specific entries above win first.
    { "diamond",  nullptr,    2 },
    { "pearl",    nullptr,    2 },
    { "legends",  nullptr,    3 },
};
static constexpr size_t kGameKeywordCount =
    sizeof(kGameKeywords) / sizeof(kGameKeywords[0]);

// Match a lower-cased directory name against the shared keyword table.
// Returns the game index (0-4) or -1 if no match.
static int MatchByKeywords(const char *name_lc) {
    for (size_t ki = 0; ki < kGameKeywordCount; ++ki) {
        const KwEntry &kw = kGameKeywords[ki];
        if (strstr(name_lc, kw.kw1) == nullptr) {
            continue;
        }
        if (kw.kw2 != nullptr && strstr(name_lc, kw.kw2) == nullptr) {
            continue;
        }
        return kw.idx;
    }
    return -1;
}

// Match a lower-cased directory name by TID substring.
// Returns the game index (0-4) or -1 if no match.
static int MatchByTid(const char *name_lc) {
    for (size_t ti = 0; ti < kTidMapCount; ++ti) {
        if (strstr(name_lc, kTidMap[ti].tid_lower) != nullptr) {
            return kTidMap[ti].game_index;
        }
    }
    return -1;
}

// ── Save-file name classifier ─────────────────────────────────────────────────

static bool IsSaveLikeName(const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    // Exact names used by Switch save titles.
    if (strcmp(name, "main")     == 0 ||
        strcmp(name, "main.sav") == 0 ||
        strcmp(name, "save.bin") == 0) {
        return true;
    }
    // Extension check: .sav / .bin / .dat / .zip (JKSV backup format)
    // W12B-HOTFIX: JKSV writes "<user> - <date>_<time>.zip" archives.  Without
    // .zip recognition here, JKSV save dirs scan as containing zero entries.
    const size_t len = strlen(name);
    if (len < 4) {
        return false;
    }
    const char *dot = nullptr;
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (dot == nullptr || dot[1] == '\0') {
        return false;
    }
    char ext[8] = {};
    size_t ei = 0;
    for (const char *p = dot + 1; *p != '\0' && ei < 7; ++p, ++ei) {
        ext[ei] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    }
    return (strcmp(ext, "sav") == 0 ||
            strcmp(ext, "bin") == 0 ||
            strcmp(ext, "dat") == 0 ||
            strcmp(ext, "zip") == 0);  // W12B-HOTFIX: JKSV backups
}

// ── QdSaveAutoscan::DirExists ─────────────────────────────────────────────────

/*static*/ bool QdSaveAutoscan::DirExists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

// ── QdSaveAutoscan::ProbeDir ──────────────────────────────────────────────────
// Log the probe and update the paths_probed / paths_skipped counters.

/*static*/ bool QdSaveAutoscan::ProbeDir(SaveScanResult &result, const char *path) {
    ++result.paths_probed;
    UL_LOG_INFO("save-autoscan: probing %s", path);
    const bool exists = DirExists(path);
    UL_LOG_INFO("save-autoscan: %s DirExists=%s", path, exists ? "true" : "false");
    if (!exists) {
        ++result.paths_skipped;
    }
    return exists;
}

// ── QdSaveAutoscan::CountSaveFiles ────────────────────────────────────────────
// depth=0: count files + dirs directly inside dir_path.
// depth=1: for each immediate subdir, recurse once more and sum those counts.
//          Used for JKSV <game>/<user-uid>/<backup>/ layout so we dive past the
//          user-uid level and count real backup slots/files.

/*static*/ int QdSaveAutoscan::CountSaveFiles(const char *dir_path, const int depth) {
    DIR *d = opendir(dir_path);
    if (d == nullptr) {
        return 0;
    }
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') {
            continue;  // skip . .. and dotfiles
        }
        if (de->d_type == DT_DIR) {
            if (depth > 0) {
                // Dive into this subdir and count its contents instead of
                // counting the subdir itself.
                char sub[320];
                snprintf(sub, sizeof(sub), "%s%s/", dir_path, de->d_name);
                count += CountSaveFiles(sub, depth - 1);
            } else {
                // At depth=0 a subdirectory (e.g. a backup slot folder) counts
                // as one save slot.
                ++count;
            }
        } else if (IsSaveLikeName(de->d_name)) {
            ++count;
        }
    }
    closedir(d);
    return count;
}

// ── QdSaveAutoscan::RunScan ───────────────────────────────────────────────────

void QdSaveAutoscan::RunScan() {
    result_.entries.clear();
    result_.scan_done     = true;
    result_.scan_ok       = false;
    result_.paths_probed  = 0;
    result_.paths_skipped = 0;

    // Quick probe: can we open the sdmc: root?
    UL_LOG_INFO("save-autoscan: RunScan start");
    if (!ProbeDir(result_, "sdmc:/")) {
        UL_LOG_WARN("save-autoscan: sdmc: not accessible — scan aborted");
        return;
    }
    result_.scan_ok = true;

    // Track which game indices already have an entry so we deduplicate across
    // the multiple candidate paths.  Uses a small fixed-size array (5 named games +
    // 1 for the kOtherGameIndex slot).
    static constexpr int kGameCount = QdSaveAutoscan::kOtherGameIndex;  // = 5
    bool found[kGameCount] = {};

    // W15-C UNBOUNDED-1 FIX: cap the entry vector at a sane size.  Named-game
    // slots (0..kGameCount-1) are already deduped by `found[]`; only the
    // kOtherGameIndex slot can grow per-call and accumulates across multiple
    // calls without invalidation.  Bound it so a pathological SD with
    // thousands of /saves/ subdirs doesn't OOM the autoscan vector.
    static constexpr size_t kMaxAutoscanEntries = 64;

    auto addEntry = [&](int game_idx, std::string dir, int count) {
        if (count <= 0) {
            return;
        }
        if (result_.entries.size() >= kMaxAutoscanEntries) {
            // Drop quietly once cap is reached; the user can rescan after
            // cleaning up their SD.  Log once to make it visible.
            static bool warned = false;
            if (!warned) {
                UL_LOG_WARN("save-autoscan: entry cap %zu reached — dropping "
                            "additional saves; rescan after cleanup",
                            kMaxAutoscanEntries);
                warned = true;
            }
            return;
        }
        // kOtherGameIndex (5) is always appended (multiple generic dirs allowed).
        if (game_idx == kOtherGameIndex) {
            result_.entries.push_back({ game_idx, std::move(dir), count });
            return;
        }
        if (game_idx < 0 || game_idx >= kGameCount) {
            return;
        }
        if (!found[game_idx]) {
            found[game_idx] = true;
            result_.entries.push_back({ game_idx, std::move(dir), count });
        }
    };

    // ── Pass 1: atmosphere/contents/<TID>/{save/, save_data/, saveMeta/} ───────
    UL_LOG_INFO("save-autoscan: === Pass 1: atmosphere/contents ===");
    for (size_t ti = 0; ti < kTidMapCount; ++ti) {
        const TidEntry &te = kTidMap[ti];

        char path[320];

        // save/
        snprintf(path, sizeof(path), kAtmosphereTidFmt, te.tid_lower);
        if (ProbeDir(result_, path)) {
            int cnt = CountSaveFiles(path);
            UL_LOG_INFO("save-autoscan: %s contains %d entries", path, cnt);
            if (cnt > 0) {
                addEntry(te.game_index, std::string(path), cnt);
                UL_LOG_INFO("save-autoscan: ATMO save/ game=%d cnt=%d path=%s",
                            te.game_index, cnt, path);
                continue;
            }
        }

        // save_data/
        snprintf(path, sizeof(path), kAtmosphereTidAltFmt, te.tid_lower);
        if (ProbeDir(result_, path)) {
            int cnt = CountSaveFiles(path);
            UL_LOG_INFO("save-autoscan: %s contains %d entries", path, cnt);
            if (cnt > 0) {
                addEntry(te.game_index, std::string(path), cnt);
                UL_LOG_INFO("save-autoscan: ATMO save_data/ game=%d cnt=%d path=%s",
                            te.game_index, cnt, path);
                continue;
            }
        }

        // saveMeta/
        snprintf(path, sizeof(path), kAtmosphereTidMetaFmt, te.tid_lower);
        if (ProbeDir(result_, path)) {
            int cnt = CountSaveFiles(path);
            UL_LOG_INFO("save-autoscan: %s contains %d entries", path, cnt);
            if (cnt > 0) {
                addEntry(te.game_index, std::string(path), cnt);
                UL_LOG_INFO("save-autoscan: ATMO saveMeta/ game=%d cnt=%d path=%s",
                            te.game_index, cnt, path);
            }
        }
    }

    // ── Pass 2: JKSV /JKSV/Saves/<title [TID]>/<user-uid>/<backup>/ ──────────
    // JKSV creates: sdmc:/JKSV/Saves/<Game Title [XXXXXXXXXXXXXXXX]>/<uid>/<slot>/
    // The TID is embedded in square brackets within the game-title directory name.
    // We walk the title dirs, match by TID substring or keyword, then use
    // CountSaveFiles(title_dir, depth=1) to dive past the user-uid level and
    // count actual backup slots inside each user's subdirectory.
    UL_LOG_INFO("save-autoscan: === Pass 2: JKSV ===");
    {
        ++result_.paths_probed;
        UL_LOG_INFO("save-autoscan: probing %s", kJksvRoot);
        DIR *d = opendir(kJksvRoot);
        if (d == nullptr) {
            ++result_.paths_skipped;
            UL_LOG_INFO("save-autoscan: %s DirExists=false", kJksvRoot);
        } else {
            UL_LOG_INFO("save-autoscan: %s DirExists=true", kJksvRoot);
            struct dirent *de;
            while ((de = readdir(d)) != nullptr) {
                if (de->d_name[0] == '.') {
                    continue;
                }
                if (de->d_type != DT_DIR) {
                    continue;
                }

                // Lower-case the directory name for matching.
                char name_lc[256] = {};
                for (size_t k = 0; k < sizeof(name_lc) - 1 && de->d_name[k] != '\0'; ++k) {
                    name_lc[k] = static_cast<char>(
                        tolower(static_cast<unsigned char>(de->d_name[k])));
                }

                // TID substring match first (handles "[01008f6008c5e000]" suffix).
                int matched_idx = MatchByTid(name_lc);

                // Keyword fallback for titles without embedded TID.
                if (matched_idx < 0) {
                    matched_idx = MatchByKeywords(name_lc);
                }

                if (matched_idx < 0) {
                    UL_LOG_INFO("save-autoscan: JKSV dir '%s' — no game match, skipping",
                                de->d_name);
                    continue;
                }

                // Build path: kJksvRoot + <name> + "/"
                // depth=1 dives into user-uid subdirs and counts backup slots.
                char title_dir[320];
                snprintf(title_dir, sizeof(title_dir), "%s%s/", kJksvRoot, de->d_name);

                int cnt = CountSaveFiles(title_dir, /*depth=*/1);
                UL_LOG_INFO("save-autoscan: JKSV '%s' game=%d backup_slots=%d",
                            de->d_name, matched_idx, cnt);
                if (cnt > 0) {
                    addEntry(matched_idx, std::string(title_dir), cnt);
                }
            }
            closedir(d);
        }
    }

    // ── Pass 3: Checkpoint /switch/Checkpoint/saves/<title>/<user>/<backup>/ ───
    // Checkpoint may or may not embed TIDs in folder names.  We try TID match
    // first, then fall back to keyword match (case-insensitive substring).
    // Layout: sdmc:/switch/Checkpoint/saves/<title>/<user>/<backup>/
    // depth=1 dives past the user dir to count actual backup slots.
    UL_LOG_INFO("save-autoscan: === Pass 3: Checkpoint ===");
    {
        ++result_.paths_probed;
        UL_LOG_INFO("save-autoscan: probing %s", kCheckpointRoot);
        DIR *d = opendir(kCheckpointRoot);
        if (d == nullptr) {
            ++result_.paths_skipped;
            UL_LOG_INFO("save-autoscan: %s DirExists=false", kCheckpointRoot);
        } else {
            UL_LOG_INFO("save-autoscan: %s DirExists=true", kCheckpointRoot);
            struct dirent *de;
            while ((de = readdir(d)) != nullptr) {
                if (de->d_name[0] == '.') {
                    continue;
                }
                if (de->d_type != DT_DIR) {
                    continue;
                }

                char name_lc[256] = {};
                for (size_t k = 0; k < sizeof(name_lc) - 1 && de->d_name[k] != '\0'; ++k) {
                    name_lc[k] = static_cast<char>(
                        tolower(static_cast<unsigned char>(de->d_name[k])));
                }

                // TID fragment match (Checkpoint sometimes embeds TID).
                int matched_idx = MatchByTid(name_lc);

                // Keyword fallback — handles "Pokemon Scarlet", "Violet", etc.
                if (matched_idx < 0) {
                    matched_idx = MatchByKeywords(name_lc);
                }

                if (matched_idx < 0) {
                    UL_LOG_INFO("save-autoscan: Checkpoint dir '%s' — no game match, skipping",
                                de->d_name);
                    continue;
                }

                char title_dir[320];
                snprintf(title_dir, sizeof(title_dir), "%s%s/",
                         kCheckpointRoot, de->d_name);

                // depth=1: dive past user dir into backup slots.
                int cnt = CountSaveFiles(title_dir, /*depth=*/1);
                UL_LOG_INFO("save-autoscan: Checkpoint '%s' game=%d backup_slots=%d",
                            de->d_name, matched_idx, cnt);
                if (cnt > 0) {
                    addEntry(matched_idx, std::string(title_dir), cnt);
                }
            }
            closedir(d);
        }
    }

    // ── Pass 4: generic sdmc:/saves/ ─────────────────────────────────────────
    // If this directory exists and has contents, emit a single entry with
    // game_index=kOtherGameIndex so the TitlePicker can show an "Other" row.
    // We do NOT pollute the named game slots with generic saves.
    UL_LOG_INFO("save-autoscan: === Pass 4: generic saves/ ===");
    if (ProbeDir(result_, kGenericSaves)) {
        int cnt = CountSaveFiles(kGenericSaves);
        UL_LOG_INFO("save-autoscan: %s contains %d entries", kGenericSaves, cnt);
        if (cnt > 0) {
            result_.entries.push_back({
                kOtherGameIndex,
                std::string(kGenericSaves),
                cnt
            });
            UL_LOG_INFO("save-autoscan: generic saves/ has %d files -> Other slot", cnt);
        }
    }

    UL_LOG_INFO("save-autoscan: done — paths_probed=%d paths_skipped=%d game_entries=%zu",
                result_.paths_probed, result_.paths_skipped, result_.entries.size());
}

// ── Public API ────────────────────────────────────────────────────────────────

const SaveScanResult& QdSaveAutoscan::GetResult() {
    if (!result_.scan_done) {
        RunScan();
    }
    return result_;
}

void QdSaveAutoscan::Rescan() {
    result_.scan_done     = false;
    result_.scan_ok       = false;
    result_.paths_probed  = 0;
    result_.paths_skipped = 0;
    result_.entries.clear();
    UL_LOG_INFO("save-autoscan: cache invalidated, next GetResult() will rescan");
}

} // namespace ul::menu::qdesktop
