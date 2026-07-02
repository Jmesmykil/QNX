# Save Editor — Next-PR Bring-Up Plan

This document describes what the follow-up PR needs to add to promote the
skeleton (QdSaveEditorLayout + QdSaveEditorHostLayout + qd_SwishCrypto) into a
working save editor for Sword/Shield and Scarlet/Violet.


## Files to Add (pkHouse Vendor Layer)

pkHouse (github.com/Insektaure/pkHouse, GPL-2.0) supplies the save-parsing
core.  We want to vendor a small, self-contained slice rather than importing
the whole repo.  Bring in these files in the order listed; each is a
prerequisite for the next.

1. `source/crypto.cpp` / `include/crypto.hpp` — SwishCrypto round-trip and
   block-hash verification.  Our qd_SwishCrypto.cpp is a spike of this step;
   the follow-up PR should replace or complement it with pkHouse's battle-
   tested implementation.  Vendor under `source/ul/menu/qdesktop/vendor/pkhouse/`.

2. `source/saves/SCBlock.cpp` — SCBlock descriptor (key, type, size, offset).
   Required before any field-level read/write.

3. `source/saves/SwShSave.cpp` — Sword/Shield save structure (party, box,
   items, trainer data offsets).  This is the minimum for the first real
   Pokemon display.

4. `source/saves/SVSave.cpp` — Scarlet/Violet save structure.  Bring in after
   SwSh is confirmed working on-device.

5. `source/saves/PKM.cpp` (or equivalent) — per-Pokemon field accessors
   (species, level, moves, stats, nickname).  Needed before the PartyBox panel
   can show real data.


## 5-Step Bring-Up Plan

**Step 1 — Boot pkHouse on-device.**
Build pkHouse's standalone nro target against the same devkitA64 toolchain.
Verify the app launches and can read a SwSh save file from the SD card.
Establishes that pkHouse's libnx nsam save-open path works on our firmware.

**Step 2 — Spike SwishCrypto round-trip (already done in this PR).**
The host-buildable qd_SwishCrypto.cpp in this PR implements the XOR+SHA round-
trip in isolation.  Next step: write a host-side test that feeds a known-good
SwSh save block through SwishDecrypt → SwishVerifyHash → SwishEncrypt and
asserts byte-identical output.  Gate: green test before any on-device work.

**Step 3 — GPL boundary audit.**
Before committing vendor source, run: `grep -r "GPL" source/vendor/pkhouse/`
and confirm all vendored files carry GPL-2.0 headers.  Add a
`source/ul/menu/qdesktop/vendor/pkhouse/LICENSE` symlink to the top-level
LICENSE file (uMenu is already GPL-2.0, so no additional obligation).
No LGPL or MIT-only files should enter this directory.

**Step 4 — UI shell with real species display.**
Wire `SwShSave::ReadParty()` into `QdSaveEditorLayout::RenderPanel()` for the
PartyBox tab.  Display species name + level from the SCBlock data.  Nickname,
moves, and stats are follow-on within this step once the species line is green.

**Step 5 — Scope to SwSh + SV.**
Limit shipping support to Sword/Shield and Scarlet/Violet in v1 (the two most
actively played titles at the time of this writing).  BDSP, PLA, and LGPE
parsers are on the roadmap but are not required for the first user-visible
release.  The TitlePicker in the skeleton intentionally lists all five games;
the non-SwSh/SV entries will show a "not yet supported" message until their
parsers land.


## Integration Hook — Where to Gate the SaveEditor

Two options are proposed; a recommendation follows.

**Option A — Dock slot.**
Add a "Save Editor" entry to the dock (slot index 6 or a configurable
overflow slot).  Pressing A on the slot calls
`g_MenuApplication->LoadMenu(MenuType::SaveEditor)` which shows the
QdSaveEditorHostLayout.  This is the lightest integration path and mirrors
how Vault, Monitor, and About are already wired.

**Option B — Vault context menu on save folders.**
When the Vault file browser navigates to `/atmosphere/contents/<title_id>/save/`,
add a context-menu item "Edit Save".  This opens the SaveEditor pre-loaded for
the matching title rather than showing the TitlePicker.  It is more discoverable
to users who are already browsing saves but requires the Vault context-menu
system to learn about the SaveEditor type.

**Recommendation: Option A (dock slot) for the follow-up PR.**
It is a two-line addition to `ui_MenuApplication.cpp` (add a MenuType and a
`LoadMenu` case) and the `QdGlobalChrome` dock renderer (one more icon slot).
Option B is a better long-term UX but depends on Vault context-menu refactoring
that has not landed yet.  Deliver Option A first; add Option B once Vault's
context-menu path is stable.


## Risk Callouts

**Anti-cheat.**
Nintendo's online ban heuristics check save-data integrity hashes.  Editing
a save file and then going online with it may trigger a ban.  The UI must
display a clear warning before any save-write operation:
"Editing saves may result in an online ban. Proceed only for offline use."

**Save corruption.**
Any write path must: (1) create a backup at `sdmc:/pkhouse-backup/<title_id>/`
before overwriting, (2) call SwishEncrypt and embed the correct outro hash
before committing, and (3) call `nsam::CommitSaveData()` — not just `Write()`.
Incomplete commits leave the save in an unreadable state.

**Save lock during a running game.**
`nsam::OpenSaveDataFileSystem()` will return `ResultSaveDataIsLocked` if the
title is currently running.  The SaveEditor must check for this error and show
"Close the game before editing saves" rather than proceeding.  Attempting to
open a locked save may corrupt it on some firmware versions.
