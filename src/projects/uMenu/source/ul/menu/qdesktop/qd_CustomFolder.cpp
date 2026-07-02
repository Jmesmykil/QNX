// qd_CustomFolder.cpp — User-created custom desktop folder for Q OS.
//
// Persistence format: sdmc:/ulaunch/qos-custom-folders.toml
//
//   [folder]
//   name=My Games
//   member=sdmc:/switch/game.nro
//   member=sdmc:/switch/other.nro
//   [folder]
//   name=Emulators
//   member=sdmc:/switch/Ryujinx.nro
//
// Written atomically via tmp+rename.  Loaded once at startup.
//
// swkbd pattern mirrors ui_SettingsMenuLayout.cpp to stay consistent
// with the rest of the codebase.

#include <ul/menu/qdesktop/qd_CustomFolder.hpp>
#include <cstdio>
#include <cstring>

namespace ul::menu::qdesktop {

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr const char *kCustomFolderPath =
    "sdmc:/ulaunch/qos-custom-folders.toml";
static constexpr const char *kCustomFolderTmpPath =
    "sdmc:/ulaunch/qos-custom-folders.toml.tmp";

// ── Process-singleton ────────────────────────────────────────────────────────

std::vector<CustomFolder> &GetCustomFolders() {
    // Function-static to avoid the static-init order fiasco.
    static std::vector<CustomFolder> g_folders;
    return g_folders;
}

// ── Persistence ───────────────────────────────────────────────────────────────

void LoadCustomFolders() {
    auto &folders = GetCustomFolders();
    folders.clear();

    FILE *f = fopen(kCustomFolderPath, "r");
    if (!f) {
        // Absent file is normal on first boot — empty list is valid.
        return;
    }

    char line[512];
    CustomFolder current;
    bool in_folder = false;

    auto flush_folder = [&]() {
        if (in_folder && current.name[0] != '\0' &&
            folders.size() < MAX_CUSTOM_FOLDERS) {
            folders.push_back(current);
        }
        // v1.10.3.10.5 main-thread fix: CustomFolder has non-trivial members
        // (std::string / std::vector), so memset is undefined behavior under
        // -Werror=class-memaccess.  Reset via assignment to a fresh instance.
        current = CustomFolder{};
        in_folder = false;
    };

    while (fgets(line, static_cast<int>(sizeof(line)), f)) {
        // Strip trailing newline / CR.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (strcmp(line, "[folder]") == 0) {
            flush_folder();
            in_folder = true;
        } else if (in_folder && strncmp(line, "name=", 5) == 0) {
            strncpy(current.name, line + 5, CUSTOM_FOLDER_NAME_MAX);
            current.name[CUSTOM_FOLDER_NAME_MAX] = '\0';
        } else if (in_folder && strncmp(line, "member=", 7) == 0) {
            current.members.emplace_back(line + 7);
        }
        // Any unrecognised line is silently ignored — forward-compat.
    }

    flush_folder();
    fclose(f);
}

void SaveCustomFolders() {
    const auto &folders = GetCustomFolders();

    FILE *f = fopen(kCustomFolderTmpPath, "w");
    if (!f) {
        // SD card not writable — silently no-op.
        return;
    }

    for (const auto &folder : folders) {
        fprintf(f, "[folder]\n");
        fprintf(f, "name=%s\n", folder.name);
        for (const auto &member : folder.members) {
            fprintf(f, "member=%s\n", member.c_str());
        }
    }

    fclose(f);

    // Atomic rename — if the rename fails, the tmp stays behind but the
    // original is never corrupted.
    // v3.7: Switch rename() can't overwrite an existing file (errno 17 EEXIST),
    // so the save silently no-ops after the first write — drop the canonical first.
    remove(kCustomFolderPath);
    rename(kCustomFolderTmpPath, kCustomFolderPath);
}

// ── ZL create-flow ────────────────────────────────────────────────────────────

bool CreateFolderFlow() {
    auto &folders = GetCustomFolders();
    if (folders.size() >= MAX_CUSTOM_FOLDERS) {
        // Folder list is full — cannot create.
        return false;
    }

    SwkbdConfig swkbd;
    if (R_FAILED(swkbdCreate(&swkbd, 0))) {
        return false;
    }

    swkbdConfigMakePresetDefault(&swkbd);
    swkbdConfigSetType(&swkbd, SwkbdType_All);
    swkbdConfigSetGuideText(&swkbd, "Folder name");
    swkbdConfigSetStringLenMax(&swkbd, static_cast<u32>(CUSTOM_FOLDER_NAME_MAX));

    char name_buf[CUSTOM_FOLDER_NAME_MAX + 1] = {};
    bool created = false;

    if (R_SUCCEEDED(swkbdShow(&swkbd, name_buf, sizeof(name_buf)))) {
        name_buf[CUSTOM_FOLDER_NAME_MAX] = '\0';
        // User may have submitted an empty string — skip.
        if (name_buf[0] != '\0') {
            CustomFolder new_folder;
            // v1.10.3.10.5 main-thread fix: bounded-copy avoids
            // -Werror=stringop-truncation when source length equals dst size.
            const size_t src_len = strlen(name_buf);
            const size_t copy_len = (src_len < CUSTOM_FOLDER_NAME_MAX)
                                    ? src_len : CUSTOM_FOLDER_NAME_MAX;
            memcpy(new_folder.name, name_buf, copy_len);
            new_folder.name[copy_len] = '\0';
            // members starts empty — user populates later.
            folders.push_back(std::move(new_folder));
            SaveCustomFolders();
            created = true;
        }
    }

    swkbdClose(&swkbd);
    return created;
}

// ── Membership helpers ────────────────────────────────────────────────────────

size_t LookupCustomFolderIdx(const std::string &stable_id) {
    const auto &folders = GetCustomFolders();
    for (size_t i = 0; i < folders.size(); ++i) {
        for (const auto &member : folders[i].members) {
            if (member == stable_id) {
                return i;
            }
        }
    }
    return SIZE_MAX;
}

void AddToCustomFolder(size_t folder_idx, const std::string &stable_id) {
    auto &folders = GetCustomFolders();
    if (folder_idx >= folders.size()) {
        return;
    }
    auto &members = folders[folder_idx].members;
    for (const auto &m : members) {
        if (m == stable_id) {
            // Already a member — no-op.
            return;
        }
    }
    members.push_back(stable_id);
    SaveCustomFolders();
}

void RemoveFromAllCustomFolders(const std::string &stable_id) {
    auto &folders = GetCustomFolders();
    bool changed = false;
    for (auto &folder : folders) {
        auto &members = folder.members;
        const size_t before = members.size();
        members.erase(
            std::remove(members.begin(), members.end(), stable_id),
            members.end()
        );
        if (members.size() != before) {
            changed = true;
        }
    }
    if (changed) {
        SaveCustomFolders();
    }
}

} // namespace ul::menu::qdesktop
