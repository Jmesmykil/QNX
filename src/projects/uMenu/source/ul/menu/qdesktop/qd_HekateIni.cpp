// qd_HekateIni.cpp -- v1.7.0-stabilize-2 reconstruction.
//
// Implementation of LoadAllHekateIniEntries(). Walks
// sdmc:/bootloader/hekate_ipl.ini and any sdmc:/bootloader/ini/*.ini
// files, parses the canonical Hekate INI format ([section] + key=value
// lines), and emits one HekateIniEntry per [section] that defines a
// payload= key.
//
// Robustness notes:
//   - Lines with leading whitespace are accepted (Hekate's own parser
//     tolerates them).
//   - Lines starting with ';' or '#' are comments.
//   - Trailing whitespace and CR (\r) are stripped from values.
//   - Sections without payload= are dropped (cannot be resolved to a
//     specific payload binary).
//   - Sections without icon= produce an entry with icon_path empty;
//     the consumer treats empty icon_path as "no creator icon".
//
// File-IO uses libnx via <stdio.h> on the device. On the host (test
// environment), open() returns NULL for sdmc:/* paths so the function
// returns an empty vector cleanly without aborting the test run.

#include <ul/menu/qdesktop/qd_HekateIni.hpp>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace ul::menu::qdesktop {

namespace {

// Strip leading whitespace and trailing whitespace/CR. Modifies in place.
void TrimInPlace(std::string &s) {
    size_t start = 0;
    while (start < s.size()
           && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start
           && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    s = s.substr(start, end - start);
}

// Parse one INI file at the given absolute path. Appends one entry per
// resolvable [section] to `out`. Returns the number of entries added.
size_t ParseOneIni(const char *path, std::vector<HekateIniEntry> &out) {
    FILE *fp = std::fopen(path, "r");
    if (fp == nullptr) {
        return 0;
    }
    size_t added = 0;
    char line[1024];
    HekateIniEntry cur;
    bool have_section = false;

    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        std::string s(line);
        TrimInPlace(s);
        if (s.empty()) {
            continue;
        }
        if (s[0] == ';' || s[0] == '#') {
            continue;
        }
        if (s.front() == '[' && s.back() == ']') {
            // Section transition: flush any prior section that has a
            // payload (icon optional) into `out`.
            if (have_section && !cur.payload_path.empty()) {
                out.push_back(cur);
                ++added;
            }
            cur = HekateIniEntry{};
            cur.title = s.substr(1, s.size() - 2);
            TrimInPlace(cur.title);
            have_section = true;
            continue;
        }
        if (!have_section) {
            // Free-floating key=value before any [section]: skip.
            continue;
        }
        const auto eq = s.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        TrimInPlace(key);
        TrimInPlace(val);
        if (key == "payload") {
            cur.payload_path = val;
        } else if (key == "icon") {
            cur.icon_path = val;
        }
        // All other keys (kip1=, atmosphere=, fss0=, etc.) are ignored
        // by this minimal parser. Hekate itself dispatches on them; for
        // icon resolution they are not needed.
    }
    // Flush the trailing section.
    if (have_section && !cur.payload_path.empty()) {
        out.push_back(cur);
        ++added;
    }
    std::fclose(fp);
    return added;
}

} // namespace

std::vector<HekateIniEntry> LoadAllHekateIniEntries() {
    std::vector<HekateIniEntry> result;

    // 1. Top-level hekate_ipl.ini.
    ParseOneIni("sdmc:/bootloader/hekate_ipl.ini", result);

    // 2. Additional INI bundles in sdmc:/bootloader/ini/*.ini.
    DIR *d = opendir("sdmc:/bootloader/ini");
    if (d != nullptr) {
        struct dirent *e = nullptr;
        while ((e = readdir(d)) != nullptr) {
            const char *fname = e->d_name;
            if (fname[0] == '.') {
                continue;  // skip ., .., and hidden files
            }
            // Only files ending in .ini (case-insensitive).
            const size_t flen = std::strlen(fname);
            if (flen <= 4) {
                continue;
            }
            const char *suf = fname + flen - 4;
            const bool is_ini =
                ((suf[0] | 0x20) == '.')
                && ((suf[1] | 0x20) == 'i')
                && ((suf[2] | 0x20) == 'n')
                && ((suf[3] | 0x20) == 'i');
            if (!is_ini) {
                continue;
            }
            char fullpath[512];
            std::snprintf(fullpath, sizeof(fullpath),
                          "sdmc:/bootloader/ini/%s", fname);
            ParseOneIni(fullpath, result);
        }
        closedir(d);
    }

    return result;
}

} // namespace ul::menu::qdesktop
