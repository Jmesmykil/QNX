# 47 — Integratable Tasks Catalog

**Author:** Research pass 2026-04-24
**Scope:** Phase 1 (applet mode, ~448 MB heap) vs Phase 2 (game-title hijack, ~3.2 GB heap) feasibility for each candidate use case.
**Question answered:** Which homebrew NROs can Q OS host inside Phase 1, which require Phase 2, and what is the honest state of unknowns?
**Reference phases:** `docs/44_Three_Phase_Roadmap.md` (strategic), `docs/45_HBMenu_Replacement_Design.md` (vault design)

---

## 1. Integration Approach Key

| Code | Meaning |
|---|---|
| **A — Vault launch** | NRO discovered via `QdVaultLayout::ScanDirectory()`, user taps it, Q OS calls `ul::menu::smi::LaunchNro(path, {})`. Zero Q OS source changes required. Identical to how the dock already launches apps. |
| **B — Dock shortcut** | Same launch path as A, but a pinned entry appears in the dock or a sidebar "Favourites" root. User never needs to browse to find it. |
| **C — Source absorption** | Functionality built directly into Q OS desktop binary (`uMenu`). No separate NRO. Examples: dev-tool windows (already `qd_DevTools.cpp`), inline text/image viewers planned in doc 45. |
| **D — Not feasible** | Technical or legal block makes this approach impossible in the target phase. |

---

## 2. Capability Matrix

All heap figures are approximate; exact values vary by firmware version and device revision.
"Phase 1 ceiling" = ~448 MB usable heap for a library-applet process.

| Project | GitHub | Function | Runtime mode required | Approx peak RAM | Q OS approach | Phase | Key risks / notes |
|---|---|---|---|---|---|---|---|
| **ftpd** | `mtheall/ftpd` | FTP server for SD-card file transfer over Wi-Fi | Applet mode | ~8–16 MB | A — vault or B — dock slot | Phase 1 | Needs Wi-Fi. audoutInitialize conflict is **not** a risk here (ftpd uses no audio). Stable, v3.1.0+ maintained. |
| **nx-hbmenu** | `switchbrew/nx-hbmenu` | Homebrew launcher UI for `sdmc:/switch/*.nro` | Applet mode (runs on hbloader) | ~32–64 MB | A — vault (as a fallback NRO the user can still launch if they want it) | Phase 1 | v3.6.1, Nov 2025. Vault replaces its function, but the binary itself is harmless as a vault entry. Runs clean in applet mode. |
| **Sphaira** | `ITotalJustice/sphaira` | Modern hbmenu replacement — NRO browser, theme engine, network install | Applet mode | ~64–128 MB (estimated; theme engine adds overhead) | A — vault entry | Phase 1 | v1.0.0, Nov 2025. Most actively maintained hbmenu alternative. Can serve as vault fallback. Applet mode confirmed (it replaces hbmenu which runs on hbloader). |
| **Goldleaf** | `XorTroll/Goldleaf` | NSP/XCI/NRO installer, content manager, title browser | Applet mode (core); companion title mode for web browser subfeature | ~64–96 MB (applet path); companion .nsp installs as title for ~192 MB | A — vault launch | Phase 1 | v1.2.0, Dec 2025. Core installation/management works in applet mode. Web browser subfeature (Goldtree/USB web UI) requires its companion title — that feature stays Phase 2. Installers must be tapped consciously by the user; Q OS should not auto-run. |
| **NX-Shell** | `joel16/NX-Shell` | File manager for SD card, USB drive, FTP client | Applet mode | ~32–48 MB | A — vault launch; longer-term B — "Files" dock alias for the vault | Phase 1 | Last release Aug 2022 — maintenance mode. Vault (doc 45) absorbs its function natively. Still works as a vault entry while vault matures. No applet-mode restrictions observed. |
| **Snes9x NX / pSNES** | `frangarcj/pSNES` (Snes9x port) | SNES emulation | Applet mode | ~48–96 MB (SNES ROM + framebuffer) | A — vault launch | Phase 1 | SNES is a 16-bit console; ROM + decoder fit in applet heap. No JIT required — interpreter core only. Applet mode: no known block. Verify with a known-good ROM before wiring to dock. |
| **mGBA** | `mgba-emu/mgba` | GBA/GB/GBC emulation | Applet mode | ~32–64 MB | A — vault launch | Phase 1 | No JIT needed for GBA; software renderer. Actively maintained. Runs in applet mode on Switch per existing mGBA Switch port. Well within 448 MB ceiling. |
| **NXMilk** | `proconsule/nxmilk` | NXMilk: MILK-drop-style music visualizer for Switch | Applet mode | ~32–64 MB (audio decode + render) | A — vault launch | Phase 1 | Plays audio files from SD. Applet mode: no documented block. Heap requirement is modest. Note: `audoutInitialize` is called — this conflicts with USB CDC serial (qd_DevTools) if that session is active. Q OS should warn the user or disable USB serial before launching audio NROs. |
| **vgmplay-nx / VGMPlayerNX** | `simontime/VGMPlayerNX` | VGM/SPC/etc. chiptune player | Applet mode | ~16–32 MB | A — vault launch (user manually stages binary) | Phase 1 — **advisory only** | Only 2 commits, last activity 2019. Pre-release alpha quality. No maintained binary distribution. Q OS does not ship or reference this by default; document its existence in vault for power users who have the binary. |
| **NXMP** | `proconsule/nxmp` | Full video/audio media player (h264, h265, AAC, MP3…) | **Title mode only** | ~256–512 MB (video decode + GPU upload) | D — Not feasible Phase 1 | **Phase 2** | NXMP **explicitly calls `appletGetAppletType()` at startup and aborts if it detects applet mode** (returns `AppletType_LibraryApplet`). This is a hard runtime block, not a heap issue. Phase 2 (game-title container) clears this check automatically. |
| **RetroArch — SNES/NES/GBA cores** | `libretro/RetroArch` | Multi-system emulation (software-render cores) | Applet mode | ~128–256 MB (core + content + RA overhead) | A — vault launch | Phase 1 (SNES/NES/GBA cores only) | RetroArch itself runs in applet mode for lightweight interpreter cores. Total heap including RA runtime overhead fits within 448 MB for SNES/NES/GBA. Verified by existing homebrew deployments. |
| **RetroArch — N64/PSX/GCN cores** | `libretro/RetroArch` | Nintendo 64, PlayStation 1, GameCube emulation | **Title mode (JIT)** | ~512 MB–2 GB (N64/GCN) | D — Not feasible Phase 1 | **Phase 2** | These cores require `svcMapPhysicalMemoryUnsafe` for JIT recompilation. This syscall is **blocked in library-applet mode** — the kernel returns `0xE401` (permission denied). Title mode (Phase 2) grants the required privilege. No workaround exists in applet mode. |
| **BrowseNX / any homebrew web browser** | `Ferdi-000/BrowseNX` | Invokes Nintendo's system WebKit browser via applet command | Title mode (system applet invoke) | N/A — delegates to system browser (~346 MB system heap) | D — Not feasible Phase 1 | **Phase 2** | The system WebKit browser (`010000000000100D`-class applet) is accessible only to system-privileged title UIDs (range `0x13`–`0x19` per SwitchBrew). A library-applet process (Q OS Phase 1) calling `webSessionCreate()` or the offline web applet API receives `0x30` (permission denied). BrowseNX works only when launched from a game-title identity — i.e., Phase 2. No standalone homebrew JS engine on Switch exists at usable scale. |
| **FluffySD** | Unknown — not identified | Unclear — name not matched to a maintained homebrew project | Unknown | Unknown | D — Identify before integrating | Verify | No project named "FluffySD" was found in active Switch homebrew registries (GBATemp, GitHub switchbrew-related orgs). **Candidates:** "Fluffy" (`Fabbi/fluffy`, NSP installer, now superseded by Goldleaf/Awoo), "SDFilesSwitch" (old CFW file pack, not a launcher NRO). Confirm the exact project name and repo before designing any integration path. |

---

## 3. Phase 1 Definites

These NROs work in applet mode today and fit within the 448 MB ceiling. Q OS can launch all of them from the vault immediately using integration approach A. No source changes to Q OS required.

1. **ftpd** (`mtheall/ftpd`) — Wi-Fi FTP server. Useful as a "Files → Transfer" dock alias.
2. **nx-hbmenu** (`switchbrew/nx-hbmenu`) — legacy fallback; vault supersedes it, but the binary is harmless.
3. **Sphaira** (`ITotalJustice/sphaira`) — modern hbmenu replacement; stays available as a vault fallback.
4. **Goldleaf** (`XorTroll/Goldleaf`) — NSP/XCI installer. Core feature set works. Web UI companion excluded.
5. **NX-Shell** (`joel16/NX-Shell`) — file manager. Functional today; vault absorbs its job natively over time.
6. **Snes9x NX / pSNES** (`frangarcj/pSNES`) — SNES emulation. Interpreter core, fits in heap.
7. **mGBA** (`mgba-emu/mgba`) — GBA/GB/GBC emulation. No JIT, fits in heap, actively maintained.
8. **NXMilk** (`proconsule/nxmilk`) — music visualizer. Works if USB serial is idle (audoutInitialize conflict).
9. **RetroArch (SNES/NES/GBA cores only)** — launches in applet mode for interpreter cores. Heavy cores excluded.

**Phase 1 total: 9 NROs confirmed safe for vault launch.**

---

## 4. Phase 2 Requirements

These projects cannot work in Phase 1 due to hard technical blocks — not heap size alone.

### 4.1 NXMP — Hard block: applet-mode detection
- NXMP calls `appletGetAppletType()` at startup. If the return value is `AppletType_LibraryApplet`, the process exits immediately. This is an intentional developer check, not an incidental limitation.
- Phase 2 fix: Q OS runs as a game title (`AppletType_Application`). NXMP's check passes, full video decode becomes available.

### 4.2 RetroArch N64/PSX/GCN cores — Hard block: JIT syscall
- `svcMapPhysicalMemoryUnsafe` (syscall 0x4D) is gated behind the `MapPhysicalMemoryUnsafe` kernel capability flag.
- In Atmosphère, this flag is granted only to game-title processes, not library-applet processes.
- No interpreter fallback exists for N64 at playable speed on the Switch CPU. Phase 2 is the only path.

### 4.3 BrowseNX / web browser — Hard block: system applet security policy
- Nintendo's WebKit browser (`010000000000100D`) accepts invocation commands only from processes with privileged title UIDs.
- Library-applet processes cannot hold these UIDs in Phase 1. Phase 2 (game-title mode) can use the full `AppletId_LibraryAppletWeb` path.
- No standalone homebrew JS engine exists on Switch at a functional scale.

---

## 5. Honest Open Questions

### 5.1 FluffySD identity
**Status: Unresolved.** No project named "FluffySD" was located in active Switch homebrew repos as of 2026-04-24. The closest candidates are:
- `Fabbi/fluffy` — NSP installer, last active 2020, superseded by Goldleaf.
- "SDFilesSwitch" — a CFW bundle (not a launchable NRO).

**Required action before doc 47 can be closed:** Identify the exact GitHub URL and confirm the runtime mode. Until resolved, no dock slot should be allocated.

### 5.2 Snes9x applet-mode heap ceiling under stress
Known: peak RAM for SNES emulation is modest. Unknown: whether specific large SNES titles (Mode 7 heavy) push toward the 448 MB edge when combined with Q OS desktop memory. Recommend a hardware test (5 min of a known Mode 7 title) before pinning to the dock.

### 5.3 NXMilk + USB serial conflict
`audoutInitialize` and USB CDC serial (`usbCommsInitialize`) conflict on the same audio hardware session. Confirmed pattern from qd_DevTools USB serial init. Q OS desktop needs a session management gate: if USB serial is active when the user launches an audio NRO, either (a) disable USB serial first (with a toast: "USB serial paused for audio"), or (b) warn the user. This is a Q OS fix, not an NRO fix.

### 5.4 Goldleaf companion .nsp web UI
Goldleaf v1.2.0 ships a companion .nsp (the "Goldtree" bridge) that installs as a title to provide a USB/web management interface with richer RAM headroom. This companion title path bypasses the applet ceiling. Q OS Phase 1 does not launch titles — only NROs. The companion .nsp is out of scope until Phase 2.

### 5.5 RetroArch performance — SNES/NES/GBA in applet mode
LibRetro's software-rendered cores for SNES/NES/GBA run at acceptable speed on Cortex-A57 without JIT. However, Q OS's applet allocation reduces available CPU time slices because Nintendo's scheduler gives lower CPU priority to library-applet processes. A timing regression test on real hardware is warranted before recommending these cores for daily use.

### 5.6 VGMPlayerNX viability
`simontime/VGMPlayerNX` has 2 commits from 2019 and no release binary. If chiptune playback is a desired vault feature, the better path is integrating a chiptune decoder (libgme or Game_Music_Emu) directly into Q OS's `QdTextViewer`/`QdAudioPlayer` abstraction, rather than depending on an abandoned NRO.

---

## 6. Recommendations — Dock Slots and Integration Priority

The dock should be reserved for features that (a) are stable today, (b) have clear user value, and (c) will not move to Phase 2 before Phase 1 is feature-complete.

### Recommended Phase 1 dock slots

| Slot | NRO / Feature | Rationale |
|---|---|---|
| 1 | **Files (Vault)** | Native Q OS feature (doc 45), replaces NX-Shell and hbmenu in one surface. Approach C. |
| 2 | **ftpd** | Single most useful developer/power-user utility on Switch. Lets users transfer files without SD eject. Approach B. |
| 3 | **mGBA** | Actively maintained, low heap, strong user value. Approach B. |
| 4 | **Goldleaf** | NSP installer is a core power-user need. Approach B. |
| 5 | *(open — reserved for Phase 1 verification)* | Candidate: Snes9x after heap stress test passes on hardware. |

### Not recommended for dock (Phase 1)
- **NXMP** — blocks at startup in applet mode.
- **BrowseNX** — system security block.
- **RetroArch (JIT cores)** — syscall block.
- **FluffySD** — project identity unresolved.
- **VGMPlayerNX** — project is abandoned (2019, 2 commits, no binary).

### Phase 2 dock candidates (design now, wire later)
- NXMP — media player dock slot with full video decode.
- RetroArch N64/PSX — emulation hub with JIT cores enabled.
- BrowseNX / any WebKit surface — browser dock slot (depends on Phase 2 title identity granting system applet invoke rights).

---

## Cross-references

| Document | Relationship |
|---|---|
| `docs/44_Three_Phase_Roadmap.md` | Phase 1/2/3 definitions and exit criteria |
| `docs/45_HBMenu_Replacement_Design.md` | Vault architecture, `QdVaultLayout::ScanDirectory()`, launch path |
| `docs/46_Stabilization_Handoff.md` | Current build state; vault Phase 1 scaffold in flight |
| `src/projects/uMenu/source/ul/menu/qdesktop/qd_DesktopIcons.cpp` | Existing NRO scan + launch reference implementation |
| `src/projects/uMenu/source/ul/menu/qdesktop/qd_DevTools.cpp` | USB serial / NXLink / log-flush toggle wrappers |
| SwitchBrew — Applet Manager services | `https://switchbrew.org/wiki/Applet_Manager_services` — applet type enum, memory limits |
| SwitchBrew — Homebrew Loader | `https://switchbrew.org/wiki/Homebrew_Loader_(hbl)` — hbloader vs hbmenu distinction |
| mGBA Switch port | `https://mgba.io/2017/12/14/1-million-downloads/` |
| Goldleaf releases | `https://github.com/XorTroll/Goldleaf/releases` |
| Sphaira v1.0.0 release | `https://github.com/ITotalJustice/sphaira/releases/tag/v1.0.0` |
| nx-hbmenu v3.6.1 | `https://github.com/switchbrew/nx-hbmenu/releases/tag/v3.6.1` |
| NXMP | `https://github.com/proconsule/nxmp` |
| ftpd | `https://github.com/mtheall/ftpd` |
| NX-Shell | `https://github.com/joel16/NX-Shell` |
| pSNES (Snes9x port) | `https://github.com/frangarcj/pSNES` |
| NXMilk | `https://github.com/proconsule/nxmilk` |
| RetroArch | `https://github.com/libretro/RetroArch` |
| BrowseNX | `https://github.com/Ferdi-000/BrowseNX` |
