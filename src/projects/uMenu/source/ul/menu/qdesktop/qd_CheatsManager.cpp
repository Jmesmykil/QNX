// qd_CheatsManager.cpp — Atmosphère cheat code parser + sidecar enable/disable.
//
// W12-CHEATS (v3.4 wave 3).
//
// Parser state machine (ParseCheatFile):
//   IDLE        — waiting for the first '[Name]' header line.
//   IN_CHEAT    — accumulating hex-code lines for the current entry.
// Transitions:
//   '[...]' line  →  save current entry (if any); start new entry → IN_CHEAT.
//   non-empty line in IN_CHEAT  →  append to current entry's lines[].
//   blank line in IN_CHEAT  →  ignored (whitespace separator inside a cheat).
//   EOF  →  flush current entry.
//
// Sidecar format (manual TOML subset; no library dependency):
//   enabled = ["cheat name 1", "cheat name 2"]
// Single key, single line array.  Names are double-quoted; commas separate entries.
// Reading is done with a simple index-scan; writing reconstructs the line.
//
// WriteCheatEnabledState (.txt writeback, wave 3):
//   Reads the entire .txt into memory, walks line-by-line applying ';' comment /
//   uncomment edits, then atomically replaces the original via tmp→rename.
//   On the first call for a given file, copies the original to <file>.qos-backup
//   (only if the backup does not already exist — never overwrites a backup).
//   Master Code (first '[...]' block) is always kept enabled.

#include <ul/menu/qdesktop/qd_CheatsManager.hpp>
#include <ul/menu/qdesktop/qd_CheatTitleResolver.hpp>  // v3.6: async NACP resolver
#include <ul/ul_Result.hpp>   // UL_LOG_INFO / UL_LOG_WARN

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>      // fsync, rename
#include <cerrno>        // errno (W13-SIDECAR-FIX diagnostics)
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <switch.h>      // nsGetApplicationControlData, NsApplicationControlData, AccountUid

namespace ul::menu::qdesktop {

// ── Internal constants ────────────────────────────────────────────────────────

static constexpr const char *kAtmContentsBase = "sdmc:/atmosphere/contents/";
static constexpr const char *kSidecarDir      = "sdmc:/ulaunch/cheats-enabled/";

// ── SidecarPath ───────────────────────────────────────────────────────────────

/*static*/ std::string QdCheatsManager::SidecarPath(const std::string &tid,
                                                      const std::string &bid) {
    return std::string(kSidecarDir) + tid + "_" + bid + ".toml";
}

// ── ScanInstalledCheats ───────────────────────────────────────────────────────

/*static*/ std::vector<CheatFile> QdCheatsManager::ScanInstalledCheats() {
    std::vector<CheatFile> results;

    DIR *top = opendir(kAtmContentsBase);
    if (top == nullptr) {
        UL_LOG_WARN("cheats: cannot open %s — no cheats directory", kAtmContentsBase);
        return results;
    }

    struct dirent *tid_de;
    while ((tid_de = readdir(top)) != nullptr) {
        // Skip dot entries and non-directory entries.
        if (tid_de->d_name[0] == '.') continue;
        if (tid_de->d_type != DT_DIR) continue;

        // TID directory must be exactly 16 hex chars.
        const size_t tid_len = strnlen(tid_de->d_name, 24);
        if (tid_len != 16) continue;

        // Check for cheats sub-directory.
        char cheats_dir[256];
        snprintf(cheats_dir, sizeof(cheats_dir),
                 "%s%s/cheats/", kAtmContentsBase, tid_de->d_name);

        DIR *cd = opendir(cheats_dir);
        if (cd == nullptr) continue;

        struct dirent *bid_de;
        while ((bid_de = readdir(cd)) != nullptr) {
            if (bid_de->d_name[0] == '.') continue;
            if (bid_de->d_type == DT_DIR) continue;

            // BID file must end with ".txt".
            const size_t fname_len = strnlen(bid_de->d_name, 32);
            if (fname_len < 5) continue;
            const char *ext = bid_de->d_name + fname_len - 4;
            if (strcmp(ext, ".txt") != 0) continue;

            // BID is the filename without ".txt" — should be 16 hex chars.
            const size_t bid_len = fname_len - 4;
            if (bid_len != 16) continue;

            std::string tid(tid_de->d_name);
            std::string bid(bid_de->d_name, bid_len);

            char full_path[512];
            snprintf(full_path, sizeof(full_path),
                     "%s%s/cheats/%s", kAtmContentsBase,
                     tid_de->d_name, bid_de->d_name);

            // Count cheats by quick-parsing header lines.
            int count = 0;
            FILE *f = fopen(full_path, "r");
            if (f != nullptr) {
                char line[256];
                while (fgets(line, sizeof(line), f) != nullptr) {
                    const size_t ll = strnlen(line, sizeof(line));
                    if (ll >= 2 && line[0] == '[') {
                        ++count;
                    }
                }
                fclose(f);
            }

            // Title name resolution.
            //
            // v3.6 redesign 2026-05-28: O(1) Lookup against the on-disk
            // cache populated by QdCheatTitleResolver::StartResolve.  Pure
            // map lookup — no heap allocs on the scan-each-TID hot path.
            // Fallback to "TID 0x<hex>" when not yet in cache; resolver
            // re-runs on Cheats window open and refreshes labels in-place.
            std::string title_name;
            {
                std::uint64_t tid_u64 = 0;
                if (std::sscanf(tid_de->d_name, "%016llx",
                                reinterpret_cast<unsigned long long *>(&tid_u64)) == 1) {
                    title_name = QdCheatTitleResolver::Lookup(tid_u64);
                }
                if (title_name.empty()) {
                    char fallback[32];
                    snprintf(fallback, sizeof(fallback), "TID 0x%s",
                             tid_de->d_name);
                    title_name = fallback;
                }
            }

            CheatFile cf;
            cf.tid         = tid;
            cf.bid         = bid;
            cf.title_name  = std::move(title_name);
            cf.cheat_count = count;
            cf.file_path   = std::string(full_path);
            results.push_back(std::move(cf));
        }
        closedir(cd);
    }
    closedir(top);

    // Sort by title name for stable display order.
    std::sort(results.begin(), results.end(),
              [](const CheatFile &a, const CheatFile &b) {
                  return a.title_name < b.title_name;
              });

    UL_LOG_INFO("cheats: scan found %zu cheat files", results.size());
    return results;
}

// ── ParseCheatFile ────────────────────────────────────────────────────────────

/*static*/ CheatList QdCheatsManager::ParseCheatFile(const std::string &path) {
    CheatList result;

    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        UL_LOG_WARN("cheats: cannot open cheat file '%s'", path.c_str());
        return result;
    }

    // State machine: IDLE or IN_CHEAT.
    enum class State { Idle, InCheat };
    State state = State::Idle;

    CheatEntry current;

    auto flush_current = [&]() {
        if (!current.name.empty()) {
            // Mark Master Code — first entry in the file.
            if (result.empty()) {
                current.is_master_code = true;
            }
            result.push_back(std::move(current));
            current = CheatEntry{};
        }
    };

    char line_buf[512];
    while (fgets(line_buf, sizeof(line_buf), f) != nullptr) {
        // Strip trailing '\r' and '\n'.
        size_t ll = strnlen(line_buf, sizeof(line_buf));
        while (ll > 0 && (line_buf[ll - 1] == '\n' || line_buf[ll - 1] == '\r')) {
            line_buf[--ll] = '\0';
        }

        // Skip pure-whitespace lines (blank separator lines between cheats).
        bool all_ws = true;
        for (size_t i = 0; i < ll; ++i) {
            if (!isspace(static_cast<unsigned char>(line_buf[i]))) {
                all_ws = false;
                break;
            }
        }
        if (all_ws) continue;

        // W13-BUG-FIX: strip leading ';' chars BEFORE inspection.  Previously
        // the parser blindly `continue`'d on any ';'-prefixed line, making
        // disabled cheats (`;[Name]` + `;<code>`) completely invisible to the
        // UI.  Now we track whether the line WAS commented out and use that
        // to seed CheatEntry::currently_enabled for the header.
        size_t lead_semi = 0;
        while (lead_semi < ll && line_buf[lead_semi] == ';') {
            ++lead_semi;
        }
        const char *bare = line_buf + lead_semi;
        const size_t bare_len = ll - lead_semi;
        const bool was_commented = (lead_semi > 0);

        // '[Name]' header (possibly `;[Name]` for a disabled cheat): flush
        // current entry, start new one.  The current entry's enabled state
        // was already captured when its header was parsed.
        if (bare_len >= 2 && bare[0] == '[') {
            flush_current();
            // Find closing ']'.
            const char *close = strchr(bare + 1, ']');
            if (close != nullptr) {
                current.name = std::string(bare + 1,
                                           static_cast<size_t>(close - (bare + 1)));
            } else {
                // No closing bracket — use rest of line as name.
                current.name = std::string(bare + 1);
            }
            current.currently_enabled = !was_commented;
            state = State::InCheat;
            continue;
        }

        // Hex-code line inside a cheat block.  Strip any leading ';' so we
        // preserve the canonical code line even when the cheat is disabled.
        if (state == State::InCheat) {
            current.lines.emplace_back(bare, bare_len);
        }
    }
    flush_current();
    fclose(f);

    UL_LOG_INFO("cheats: parsed '%s' -> %zu cheats",
                path.c_str(), result.size());
    return result;
}

// ── ReadEnabledSidecar ────────────────────────────────────────────────────────

/*static*/ std::set<std::string>
QdCheatsManager::ReadEnabledSidecar(const std::string &tid,
                                     const std::string &bid) {
    std::set<std::string> enabled;

    const std::string path = SidecarPath(tid, bid);
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        // No sidecar → every cheat disabled (except Master Code, always on).
        return enabled;
    }

    // Read first (and only) line that looks like:
    //   enabled = ["name1", "name2"]
    char buf[4096];
    while (fgets(buf, sizeof(buf), f) != nullptr) {
        // Find 'enabled' key.
        const char *eq = strstr(buf, "enabled");
        if (eq == nullptr) continue;
        eq = strchr(eq, '=');
        if (eq == nullptr) continue;
        // Find opening '['.
        const char *arr_open = strchr(eq + 1, '[');
        if (arr_open == nullptr) continue;
        const char *arr_close = strchr(arr_open + 1, ']');
        if (arr_close == nullptr) continue;

        // Parse comma-separated quoted names between '[' and ']'.
        const char *p = arr_open + 1;
        while (p < arr_close) {
            // Find opening quote.
            while (p < arr_close && *p != '"') ++p;
            if (p >= arr_close) break;
            ++p;  // skip '"'
            // Find closing quote.
            const char *end = p;
            while (end < arr_close && *end != '"') ++end;
            if (end > p) {
                enabled.insert(std::string(p, static_cast<size_t>(end - p)));
            }
            p = end + 1;
        }
        break;  // Only one 'enabled' line expected.
    }
    fclose(f);

    UL_LOG_INFO("cheats: sidecar '%s' loaded, %zu enabled",
                path.c_str(), enabled.size());
    return enabled;
}

// ── WriteEnabledSidecar ───────────────────────────────────────────────────────

/*static*/ void QdCheatsManager::WriteEnabledSidecar(
        const std::string &tid,
        const std::string &bid,
        const std::set<std::string> &enabled) {

    // Ensure the sidecar directory exists.
    //
    // W13-SIDECAR-FIX: previous code called `mkdir(kSidecarDir, 0777)` where
    // kSidecarDir has a TRAILING SLASH ("sdmc:/ulaunch/cheats-enabled/").
    // libnx/newlib's mkdir rejects trailing-slash paths (returns -1, errno
    // ENOTDIR), so the directory was never created.  The subsequent fopen()
    // then silently failed because its parent didn't exist, sidecar TOMLs
    // never persisted, and reopening the Cheats window showed every cheat
    // back at its file default — i.e. toggles appeared to do nothing.
    //
    // Strip the trailing slash for the mkdir call.  Also stat after mkdir to
    // verify success and log on failure so future diagnosis takes seconds.
    {
        char dir_no_slash[128];
        const size_t dlen = strnlen(kSidecarDir, sizeof(dir_no_slash) - 1);
        size_t copy_len = dlen;
        if (copy_len > 0 && kSidecarDir[copy_len - 1] == '/') --copy_len;
        memcpy(dir_no_slash, kSidecarDir, copy_len);
        dir_no_slash[copy_len] = '\0';

        const int rc = mkdir(dir_no_slash, 0777);
        if (rc != 0 && errno != EEXIST) {
            UL_LOG_WARN("cheats: mkdir('%s') failed errno=%d",
                        dir_no_slash, errno);
        }
        struct stat st;
        if (stat(dir_no_slash, &st) != 0 || (st.st_mode & S_IFMT) != S_IFDIR) {
            UL_LOG_WARN("cheats: sidecar dir '%s' still missing after mkdir — "
                        "toggle state will not persist",
                        dir_no_slash);
            return;
        }
    }

    const std::string path = SidecarPath(tid, bid);
    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        UL_LOG_WARN("cheats: cannot write sidecar '%s' (errno=%d)",
                    path.c_str(), errno);
        return;
    }

    // Write the single 'enabled = ["n1", "n2"]' line.
    fprintf(f, "enabled = [");
    bool first = true;
    for (const auto &name : enabled) {
        if (!first) fprintf(f, ", ");
        // Escape double-quotes in the name (rare but defensive).
        fprintf(f, "\"");
        for (char c : name) {
            if (c == '"' || c == '\\') fputc('\\', f);
            fputc(c, f);
        }
        fprintf(f, "\"");
        first = false;
    }
    fprintf(f, "]\n");
    fclose(f);

    UL_LOG_INFO("cheats: sidecar '%s' written, %zu enabled", path.c_str(), enabled.size());
}

// ── WriteCheatEnabledState ────────────────────────────────────────────────────

/*static*/ int QdCheatsManager::WriteCheatEnabledState(
        const std::string &cheat_file_path,
        const std::string &tid,
        const std::string &bid,
        const std::vector<std::pair<std::string, bool>> &states) {

    // ── Build a name→enabled lookup map ──────────────────────────────────────
    std::unordered_map<std::string, bool> want;
    want.reserve(states.size());
    for (const auto &kv : states) {
        want[kv.first] = kv.second;
    }

    // ── Backup: copy original once (never overwrite) ─────────────────────────
    const std::string backup_path = cheat_file_path + ".qos-backup";
    {
        struct stat st_dummy;
        if (stat(backup_path.c_str(), &st_dummy) != 0) {
            // Backup does not exist yet — create it.
            FILE *src = fopen(cheat_file_path.c_str(), "rb");
            if (src == nullptr) {
                UL_LOG_WARN("cheats-wb: cannot open '%s' for backup",
                            cheat_file_path.c_str());
                return -1;
            }
            FILE *dst = fopen(backup_path.c_str(), "wb");
            if (dst == nullptr) {
                fclose(src);
                UL_LOG_WARN("cheats-wb: cannot create backup '%s'",
                            backup_path.c_str());
                // Non-fatal: continue without backup rather than aborting the write.
            } else {
                char copy_buf[4096];
                size_t n;
                while ((n = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0) {
                    fwrite(copy_buf, 1, n, dst);
                }
                fflush(dst);
                fsync(fileno(dst));
                fclose(dst);
                fclose(src);
                UL_LOG_INFO("cheats-wb: backup created '%s'", backup_path.c_str());
            }
        }
    }

    // ── Read entire file into memory ─────────────────────────────────────────
    FILE *rf = fopen(cheat_file_path.c_str(), "r");
    if (rf == nullptr) {
        UL_LOG_WARN("cheats-wb: cannot open '%s' for reading",
                    cheat_file_path.c_str());
        return -1;
    }

    std::vector<std::string> in_lines;
    in_lines.reserve(256);
    {
        char lbuf[512];
        while (fgets(lbuf, sizeof(lbuf), rf) != nullptr) {
            in_lines.emplace_back(lbuf);
        }
    }
    fclose(rf);

    // ── Walk and apply comment/uncomment edits ───────────────────────────────
    // State:
    //   current_cheat_enabled:   desired state for lines belonging to the
    //                            currently-active cheat block (after header).
    //   is_master_code_block:    true while processing the first '[...]' block
    //                            — Master Code is never disabled.
    //   in_cheat_block:          true after a header line, until next blank or
    //                            next header.
    // Edge cases:
    //   ';[Name]'  — header already commented; strip ';' if enabling.
    //   '  [Name]' — leading whitespace preserved; ';' inserted at column 0.
    //   Blank/whitespace-only lines — passed through unchanged (they are
    //                            block separators and must not gain a ';').

    bool in_cheat_block           = false;
    bool current_cheat_enabled    = true;
    bool first_cheat_seen         = false;

    std::vector<std::string> out_lines;
    out_lines.reserve(in_lines.size());

    for (const auto &raw : in_lines) {
        // Working copy: strip trailing CR/LF so we can inspect/rebuild cleanly.
        std::string line = raw;
        // We'll re-add the original line ending at write time; keep it from raw.
        // Find content end (before CR/LF).
        size_t content_end = line.size();
        while (content_end > 0 &&
               (line[content_end - 1] == '\n' || line[content_end - 1] == '\r')) {
            --content_end;
        }
        const std::string ending = line.substr(content_end); // e.g. "\n" or "\r\n"
        std::string content = line.substr(0, content_end);

        // ── Blank / all-whitespace line → pass through ────────────────────
        bool all_ws = true;
        for (char c : content) {
            if (!isspace(static_cast<unsigned char>(c))) { all_ws = false; break; }
        }
        if (all_ws) {
            // Blank line: pass through unchanged.
            //
            // W14-B-WARN-01 FIX: previously this reset `in_cheat_block = false`,
            // which created an asymmetry with `ParseCheatFile` (which keeps the
            // current header's identity across blank lines).  Net effect of the
            // old behaviour: a cheat file with a blank line between `[Header]`
            // and its first code line would silently skip the toggle on those
            // code lines (they were classified as "outside any block").  Real
            // cheat files rarely have this pattern but several community-
            // sourced .txt do.  Keep the block context across blanks.
            out_lines.push_back(raw);
            continue;
        }

        // ── Strip leading ';' prefix for inspection (may re-add below) ────
        // We need to detect whether this is a header line (possibly commented).
        // Strip ALL leading ';' characters so we can check for '['.
        size_t semicolon_count = 0;
        while (semicolon_count < content.size() &&
               content[semicolon_count] == ';') {
            ++semicolon_count;
        }
        const std::string bare = content.substr(semicolon_count);

        // ── Header line: '[Name]' (possibly ';[Name]' or ';;[Name]') ──────
        // Check bare content (after stripping ';') starts with '['.
        if (!bare.empty() && bare[0] == '[') {
            in_cheat_block = true;

            // Extract the cheat name from '[Name]'.
            const char *open_bracket = bare.c_str();  // points at '['
            const char *close        = strchr(open_bracket + 1, ']');
            std::string cheat_name;
            if (close != nullptr) {
                cheat_name = std::string(open_bracket + 1,
                                        static_cast<size_t>(close - (open_bracket + 1)));
            } else {
                cheat_name = std::string(open_bracket + 1);
            }

            // Master Code: first '[...]' block ever seen → always enabled.
            if (!first_cheat_seen) {
                first_cheat_seen      = true;
                current_cheat_enabled = true;
            } else {
                // Look up desired state; default to enabled if not in map.
                auto it = want.find(cheat_name);
                current_cheat_enabled = (it == want.end()) ? true : it->second;
            }

            // Rebuild the header line with correct ';' state.
            if (current_cheat_enabled) {
                // Remove any leading ';' (uncomment).
                out_lines.push_back(bare + ending);
            } else {
                // Add exactly one ';' prefix (comment out).
                out_lines.push_back(";" + bare + ending);
            }
            continue;
        }

        // ── Code line inside a cheat block ───────────────────────────────
        if (in_cheat_block) {
            if (current_cheat_enabled) {
                // Uncomment: remove ALL leading ';' so legacy doubly-commented
                // lines are also fixed up.
                out_lines.push_back(bare + ending);
            } else {
                // Comment out: one ';' prefix.
                out_lines.push_back(";" + bare + ending);
            }
            continue;
        }

        // ── Line outside any known block (e.g. file header comment) ──────
        out_lines.push_back(raw);
    }

    // W13-DIRECT-WRITE: skip the tmp+rename pattern entirely.  FAT32 / newlib
    // doesn't support atomic rename-over-existing, and my previous fix
    // (remove + rename retry) could LOSE THE FILE if both renames failed —
    // dest gets removed, then second rename fails, file vanishes from disk,
    // user reports "the whole folder disappears" because ScanInstalledCheats
    // no longer finds the .txt to list it.
    //
    // Safer: open the destination directly with mode "w" (truncate), write
    // all lines, fsync, close.  If the write fails partway, the file is
    // corrupted but the .qos-backup we made earlier in this function is
    // intact — user can manually restore.  No file ever vanishes.
    FILE *wf = fopen(cheat_file_path.c_str(), "w");
    if (wf == nullptr) {
        UL_LOG_WARN("cheats-wb: cannot open '%s' for writing (errno=%d)",
                    cheat_file_path.c_str(), errno);
        return -1;
    }

    for (const auto &ol : out_lines) {
        if (fwrite(ol.data(), 1, ol.size(), wf) != ol.size()) {
            UL_LOG_WARN("cheats-wb: short write to '%s' (errno=%d) — "
                        "restore from %s.qos-backup if needed",
                        cheat_file_path.c_str(), errno,
                        cheat_file_path.c_str());
            fclose(wf);
            return -1;
        }
    }
    fflush(wf);
    fsync(fileno(wf));
    fclose(wf);

    UL_LOG_INFO("cheats-wb: '%s' rewritten (%zu lines), %zu states applied",
                cheat_file_path.c_str(), out_lines.size(), states.size());

    // ── Keep sidecar TOML in sync ─────────────────────────────────────────────
    std::set<std::string> enabled_set;
    for (const auto &kv : states) {
        if (kv.second) enabled_set.insert(kv.first);
    }
    WriteEnabledSidecar(tid, bid, enabled_set);

    return 0;
}

} // namespace ul::menu::qdesktop
