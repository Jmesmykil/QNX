// qd_ModsManager.cpp — Atmosphère LayeredFS mod discovery + enable/disable.
//
// B3.1 — launcher-side mod manager.
//
// Scan strategy (ScanInstalledMods):
//   For every 16-hex-char directory under sdmc:/atmosphere/contents/:
//     Check for the presence of any of these entries (enabled OR .disabled):
//       "romfs"         — full file-replacement LayeredFS tree
//       "exefs"         — exefs patchset / replacement directory
//       "exefs_patches" — directory of per-build IPS/pchtxt patch dirs
//     Each found entry becomes a ModSlot with is_enabled derived from whether
//     the ".disabled" suffix is present on disk.
//
// Toggle strategy (ToggleSlot):
//   Mirrors qd_SettingsLayout::ToggleOverlay() exactly (line ~931):
//     enabled  → rename(path, path + ".disabled")
//     disabled → rename(path + ".disabled", path)   [strips the suffix]
//   Then fsdevCommitDevice("sdmc") to flush the FAT32 write-back cache.
//   Then WriteStateSidecar() to persist the new toggle map.
//
// Sidecar format (mirrors qd_CheatsManager sidecar, manual TOML subset):
//   disabled = ["romfs", "exefs"]
// Path: sdmc:/ulaunch/mod-state/<TID>.toml
// Reading: index-scan for 'disabled' key + array parse.
// Writing: reconstruct the line from the in-memory disabled set.
//
// Title-name resolution:
//   Uses QdCheatTitleResolver::Lookup (O(1) cache lookup).
//   Fallback: "TID 0x<tid>" — identical to the cheats UI.

#include <ul/menu/qdesktop/qd_ModsManager.hpp>
#include <ul/menu/qdesktop/qd_CheatTitleResolver.hpp>
#include <ul/ul_Result.hpp>   // UL_LOG_INFO / UL_LOG_WARN

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>    // fsync, rename
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <switch.h>    // fsdevCommitDevice

namespace ul::menu::qdesktop {

// ── Internal constants ────────────────────────────────────────────────────────

static constexpr const char *kAtmContentsBase = "sdmc:/atmosphere/contents/";
static constexpr const char *kSidecarDir      = "sdmc:/ulaunch/mod-state/";

// The three canonical top-level slot names we look for under each TID dir.
// Order matters for display: romfs first, then exefs, then patches.
static const char * const kSlotNames[] = { "romfs", "exefs", "exefs_patches" };
static constexpr size_t   kSlotNameCount = 3;

// ── SidecarPath ───────────────────────────────────────────────────────────────

/*static*/ std::string QdModsManager::SidecarPath(const std::string &tid) {
    return std::string(kSidecarDir) + tid + ".toml";
}

// ── ScanTidDirectory ──────────────────────────────────────────────────────────

/*static*/ bool QdModsManager::ScanTidDirectory(const char *tid_name,
                                                  std::vector<ModSlot> &out) {
    bool found_any = false;

    for (size_t si = 0; si < kSlotNameCount; ++si) {
        const char *slot_name = kSlotNames[si];

        // Build the canonical (enabled) path.
        char enabled_path[512];
        snprintf(enabled_path, sizeof(enabled_path),
                 "%s%s/%s", kAtmContentsBase, tid_name, slot_name);

        // Build the disabled path.
        char disabled_path[512];
        snprintf(disabled_path, sizeof(disabled_path),
                 "%s%s/%s.disabled", kAtmContentsBase, tid_name, slot_name);

        struct stat st;
        bool on_disk_enabled  = (stat(enabled_path,  &st) == 0);
        bool on_disk_disabled = (!on_disk_enabled &&
                                  stat(disabled_path, &st) == 0);

        if (!on_disk_enabled && !on_disk_disabled) {
            // Neither form present — this slot does not exist for this title.
            continue;
        }

        const bool is_enabled = on_disk_enabled;

        // Determine whether the slot is a directory or a file.
        // Check whichever form actually exists.
        struct stat st2;
        const char *existing = on_disk_enabled ? enabled_path : disabled_path;
        bool is_dir = (stat(existing, &st2) == 0) &&
                       ((st2.st_mode & S_IFMT) == S_IFDIR);

        ModSlot slot;
        slot.name       = std::string(slot_name);
        slot.full_path  = std::string(enabled_path);  // always canonical (enabled) form
        slot.is_enabled = is_enabled;
        slot.is_dir     = is_dir;

        out.push_back(std::move(slot));
        found_any = true;
    }

    return found_any;
}

// ── ScanInstalledMods ─────────────────────────────────────────────────────────

/*static*/ std::vector<ModSet> QdModsManager::ScanInstalledMods() {
    std::vector<ModSet> results;

    DIR *top = opendir(kAtmContentsBase);
    if (top == nullptr) {
        UL_LOG_WARN("mods: cannot open %s", kAtmContentsBase);
        return results;
    }

    struct dirent *tid_de;
    while ((tid_de = readdir(top)) != nullptr) {
        if (tid_de->d_name[0] == '.') continue;
        if (tid_de->d_type != DT_DIR) continue;

        // TID directory must be exactly 16 hex characters.
        const size_t tid_len = strnlen(tid_de->d_name, 24);
        if (tid_len != 16) continue;

        std::vector<ModSlot> slots;
        if (!ScanTidDirectory(tid_de->d_name, slots)) {
            // No recognised mod content in this TID directory.
            continue;
        }

        // Title-name resolution: O(1) cache lookup via QdCheatTitleResolver.
        std::string title_name;
        {
            std::uint64_t tid_u64 = 0;
            if (std::sscanf(tid_de->d_name, "%016llx",
                            reinterpret_cast<unsigned long long *>(&tid_u64)) == 1) {
                title_name = QdCheatTitleResolver::Lookup(tid_u64);
            }
            if (title_name.empty()) {
                char fallback[32];
                snprintf(fallback, sizeof(fallback), "TID 0x%s", tid_de->d_name);
                title_name = fallback;
            }
        }

        ModSet ms;
        ms.tid        = std::string(tid_de->d_name);
        ms.title_name = std::move(title_name);
        ms.slots      = std::move(slots);
        results.push_back(std::move(ms));
    }
    closedir(top);

    // Sort by title name for stable display order.
    std::sort(results.begin(), results.end(),
              [](const ModSet &a, const ModSet &b) {
                  return a.title_name < b.title_name;
              });

    UL_LOG_INFO("mods: scan found %zu mod sets", results.size());
    return results;
}

// ── ToggleSlot ────────────────────────────────────────────────────────────────

/*static*/ bool QdModsManager::ToggleSlot(ModSet &mod_set, const size_t slot_idx) {
    if (slot_idx >= mod_set.slots.size()) return false;

    ModSlot &slot = mod_set.slots[slot_idx];

    // Build source and destination paths.
    // Invariant: slot.full_path always holds the ENABLED (canonical) path.
    // The disabled form is always canonical + ".disabled".
    char disabled_path[512];
    snprintf(disabled_path, sizeof(disabled_path), "%s.disabled",
             slot.full_path.c_str());

    const char *from = slot.is_enabled ? slot.full_path.c_str()  : disabled_path;
    const char *to   = slot.is_enabled ? disabled_path            : slot.full_path.c_str();

    if (std::rename(from, to) != 0) {
        UL_LOG_WARN("mods: rename(%s, %s) failed errno=%d", from, to, errno);
        return false;
    }

    UL_LOG_INFO("mods: ToggleSlot '%s/%s' : %s -> %s",
                mod_set.tid.c_str(), slot.name.c_str(),
                slot.is_enabled ? "enabled" : "disabled",
                slot.is_enabled ? "disabled" : "enabled");

    // FAT32 write-back flush — mirrors SettingsLayout::ToggleOverlay() line ~962.
    fsdevCommitDevice("sdmc");

    // Update in-memory state.
    slot.is_enabled = !slot.is_enabled;

    // Build the new disabled set from the mod set's current slot state.
    std::set<std::string> disabled_set;
    for (const auto &s : mod_set.slots) {
        if (!s.is_enabled) {
            disabled_set.insert(s.name);
        }
    }
    WriteStateSidecar(mod_set.tid, disabled_set);

    return true;
}

// ── ReadStateSidecar ──────────────────────────────────────────────────────────

/*static*/ std::set<std::string>
QdModsManager::ReadStateSidecar(const std::string &tid) {
    std::set<std::string> disabled;

    const std::string path = SidecarPath(tid);
    FILE *f = fopen(path.c_str(), "r");
    if (f == nullptr) {
        // No sidecar — derive state purely from disk (slot.is_enabled).
        return disabled;
    }

    // Parse:  disabled = ["name1", "name2"]
    // (mirrors QdCheatsManager::ReadEnabledSidecar — same hand-rolled TOML subset)
    char buf[4096];
    while (fgets(buf, sizeof(buf), f) != nullptr) {
        const char *key = strstr(buf, "disabled");
        if (key == nullptr) continue;
        const char *eq = strchr(key, '=');
        if (eq == nullptr) continue;
        const char *arr_open = strchr(eq + 1, '[');
        if (arr_open == nullptr) continue;
        const char *arr_close = strchr(arr_open + 1, ']');
        if (arr_close == nullptr) continue;

        const char *p = arr_open + 1;
        while (p < arr_close) {
            while (p < arr_close && *p != '"') ++p;
            if (p >= arr_close) break;
            ++p;  // skip opening '"'
            const char *end = p;
            while (end < arr_close && *end != '"') ++end;
            if (end > p) {
                disabled.insert(std::string(p, static_cast<size_t>(end - p)));
            }
            p = end + 1;
        }
        break;
    }
    fclose(f);

    UL_LOG_INFO("mods: sidecar '%s' loaded, %zu disabled slots",
                path.c_str(), disabled.size());
    return disabled;
}

// ── WriteStateSidecar ─────────────────────────────────────────────────────────

/*static*/ void QdModsManager::WriteStateSidecar(
        const std::string &tid,
        const std::set<std::string> &disabled) {

    // Ensure the sidecar directory exists.
    // Mirrors QdCheatsManager::WriteEnabledSidecar (W13-SIDECAR-FIX):
    // strip trailing slash before mkdir — libnx/newlib rejects trailing-slash paths.
    {
        char dir_no_slash[128];
        const size_t dlen = strnlen(kSidecarDir, sizeof(dir_no_slash) - 1);
        size_t copy_len = dlen;
        if (copy_len > 0 && kSidecarDir[copy_len - 1] == '/') --copy_len;
        memcpy(dir_no_slash, kSidecarDir, copy_len);
        dir_no_slash[copy_len] = '\0';

        const int rc = mkdir(dir_no_slash, 0777);
        if (rc != 0 && errno != EEXIST) {
            UL_LOG_WARN("mods: mkdir('%s') failed errno=%d", dir_no_slash, errno);
        }
        struct stat st;
        if (stat(dir_no_slash, &st) != 0 || (st.st_mode & S_IFMT) != S_IFDIR) {
            UL_LOG_WARN("mods: sidecar dir '%s' still missing — "
                        "toggle state will not persist", dir_no_slash);
            return;
        }
    }

    const std::string path = SidecarPath(tid);
    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        UL_LOG_WARN("mods: cannot write sidecar '%s' (errno=%d)",
                    path.c_str(), errno);
        return;
    }

    // Write:  disabled = ["name1", "name2"]
    fprintf(f, "disabled = [");
    bool first = true;
    for (const auto &name : disabled) {
        if (!first) fprintf(f, ", ");
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

    UL_LOG_INFO("mods: sidecar '%s' written, %zu disabled",
                path.c_str(), disabled.size());
}

} // namespace ul::menu::qdesktop
