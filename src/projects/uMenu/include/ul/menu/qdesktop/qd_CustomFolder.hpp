// qd_CustomFolder.hpp — User-created custom desktop folder for Q OS.
//
// A custom folder is a named desktop tile that holds a list of NRO paths
// (stable_ids) chosen by the user. Custom folders are DISTINCT from the
// five auto-folder tiles: they appear as additional tiles on the desktop
// after the five fixed buckets.
//
// Data model
// ----------
//   - Up to MAX_CUSTOM_FOLDERS folders.
//   - Each folder stores a display name and a list of member NRO paths
//     (stable_ids). Member list is unbounded at the model level; the Launchpad
//     view naturally limits visible count to the grid size.
//
// Persistence
// -----------
//   sdmc:/ulaunch/qos-custom-folders.toml  (flat text, NOT TOML-parsed)
//   Format:
//     [folder]
//     name=My Games
//     member=sdmc:/switch/game.nro
//     member=sdmc:/switch/other.nro
//     [folder]
//     name=Emulators
//     member=sdmc:/switch/Ryujinx.nro
//
//   Written with atomic tmp+rename. Loaded once at startup.
//
// ZL create-flow
// ---------------
//   QdDesktopIconsElement::OnInput calls CreateFolderFlow() when ZL is pressed
//   over truly empty desktop (Path 4, line 3893 of qd_DesktopIcons.cpp).
//   CreateFolderFlow() uses the libnx software keyboard (swkbd) to prompt for
//   a name, then appends the new folder and saves. The caller is responsible
//   for triggering a desktop relayout after the call returns true.
//
// CRITICAL: CustomFolder structs MUST NOT be added to NroEntry or LpItem.
// The side-table-keyed-by-stable-id pattern is used here exactly as in
// qd_AutoFolders to avoid the v1.6.10 IPC struct-size crash.
#pragma once

#include <pu/Plutonium>
#include <string>
#include <vector>
#include <cstddef>

namespace ul::menu::qdesktop {

// ── Limits ────────────────────────────────────────────────────────────────────

/// Maximum number of user-created folders.
static constexpr size_t MAX_CUSTOM_FOLDERS = 16;

/// Maximum length of a folder display name (chars, excluding NUL).
static constexpr size_t CUSTOM_FOLDER_NAME_MAX = 32;

// ── CustomFolder ──────────────────────────────────────────────────────────────

struct CustomFolder {
    char                     name[CUSTOM_FOLDER_NAME_MAX + 1]; ///< display name
    std::vector<std::string> members;                           ///< stable_ids of member NROs
};

// ── Process-singleton access ──────────────────────────────────────────────────

/// Return the process-singleton list of custom folders.
/// Guaranteed non-null; may be empty.
std::vector<CustomFolder> &GetCustomFolders();

// ── Persistence ──────────────────────────────────────────────────────────────

/// Load custom folders from sdmc:/ulaunch/qos-custom-folders.toml.
/// Replaces any previously loaded list. Called once at startup.
/// Silently no-ops (empty list) if file is absent or corrupt.
void LoadCustomFolders();

/// Save custom folders to sdmc:/ulaunch/qos-custom-folders.toml.
/// Uses atomic tmp+rename. Silently no-ops on write error.
void SaveCustomFolders();

// ── ZL create-flow ────────────────────────────────────────────────────────────

/// Invoke the libnx software keyboard to prompt for a folder name, then
/// create the folder and save. Returns true if a folder was created (the
/// caller should re-layout the desktop). Returns false if the user cancelled
/// or the folder list is full.
///
/// Called from QdDesktopIconsElement::OnInput Path 4 when ZL is pressed over
/// truly empty desktop.
bool CreateFolderFlow();

// ── Membership helpers ────────────────────────────────────────────────────────

/// Return the index of the custom folder that contains stable_id,
/// or SIZE_MAX if it is not a member of any custom folder.
size_t LookupCustomFolderIdx(const std::string &stable_id);

/// Add stable_id to the folder at folder_idx and save. No-ops if already present.
void AddToCustomFolder(size_t folder_idx, const std::string &stable_id);

/// Remove stable_id from all custom folders it appears in, and save.
void RemoveFromAllCustomFolders(const std::string &stable_id);

} // namespace ul::menu::qdesktop
