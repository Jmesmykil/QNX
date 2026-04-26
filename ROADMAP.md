# Q OS uLaunch Fork — Roadmap (Phase 1.5 Bridge Lane)

> SSOT for the `qos-ulaunch-fork` version chain.
> Phase 1.5 bridging lane: between Phase 1 (Atmosphère NROs from hbmenu) and Phase 2 (Hekate bare-metal — HELD 2026-04-18).
> Sibling tracks: [`tools/mock-nro-desktop-gui/ROADMAP.md`](../mock-nro-desktop-gui/ROADMAP.md) (UX source) | [`tools/mock-nro-desktop/ROADMAP.md`](../mock-nro-desktop/ROADMAP.md) (TUI baseline) | [`tools/switch-nro-harness/ROADMAP.md`](../switch-nro-harness/ROADMAP.md) (correctness gate)

## Live status (updated 2026-04-25T08:00Z)

- **Current state:** SP3.1 + SP3.2 + Telemetry v0.21 BUILT. SP3.1 VERIFIED on hardware. SP3.3 (icon population) IN FLIGHT.
- **On hardware:** Cold Plasma Cascade wallpaper renders at 1280×720 native. Upstream icon ring is GONE. Top bar (time/date/battery/connection) renders. uSystem: upstream XorTroll v1.2.0 (fork uSystem NPDM rejected by fw 20.0.0 — tracked separately).
- **Next deploy:** SP3.2 cursor + SP3.3 icons when agent ace69bc8 finishes.
- **Phase 2 Hekate:** HELD independently. This lane does not depend on it.

### Sub-port completion status

| Sub-port | Status | Notes |
|---|---|---|
| SP1 — Wallpaper (Cold Plasma Cascade) | ✅ HARDWARE-VERIFIED | 1280×720 native, bilinear-scale to 1920×1080 |
| SP2 — GPU-pool fix (8 MB → 3.5 MB) | ✅ BUILT + STAGED | 45/45 host tests |
| SP3 — Input pump (libnx 4.x PadState) | ✅ BUILT + STAGED | 88/88 host tests; inert until SP5 |
| SP3.1 — QDESKTOP_MODE layout ownership | ✅ HARDWARE-VERIFIED | Upstream icon ring suppressed |
| SP3.2 — Cursor + top bar + touch remap | ✅ BUILT (hw pending) | `qd_Cursor.hpp/cpp`; touch scale 3/2 correct |
| Telemetry v0.21 | ✅ BUILT (hw pending) | RingFile 4×512KB; boot-seq counter; 11 log strings |
| SP3.3 — Icon population | 🔄 IN FLIGHT | Agent ace69bc8; `IconKind` discriminator + `SetApplicationEntries` + NCA icon load; build-green |
| SP4 — OSK | 📋 PLANNED | After SP3.3 complete |
| SP5 — Input dispatch / cursor-driver wiring | 📋 PLANNED | D-pad/cursor → FocusSurface dispatch |

## Version chain

| Ver | Status | Feature | Why HERE |
|---|---|---|---|
| `v0.1.0` | 📋 PLANNED-SCAFFOLDING | Scaffold + license audit + consume `INTEGRATION-SPEC.md` + empty C++ sysmodule skeleton that builds clean on `aarch64-none-elf` devkitPro toolchain | Governance FIRST per `reverse-engineering-governance` skill. Nothing else can start until license is confirmed and upstream arch is mapped. |
| `v0.2.0` | 📋 PLANNED | Upstream XorTroll/uLaunch merged as baseline into `src/`; builds clean on our toolchain; produces `exefs.nsp` | Establish known-good upstream build before any modification. If it doesn't build stock, fix that in isolation. |
| `v0.3.0` | 📋 PLANNED | Dark Liquid Glass theme pack (wallpaper background + color tokens + font selections) applied via uLaunch's JSON theme system | Theme system is the lowest-risk port — pure data, no C++ logic changes. Proves the reskin path before touching code. |
| `v0.4.0` | 📋 PLANNED | Dock magnify (1.4x/1.2x/1.05x) + 5s/12-frame auto-hide — ported from `mock-nro-desktop-gui v0.16.0` | Dock behavior is self-contained in the menu renderer; isolate it from Vault/Dispatch complexity. |
| `v0.5.0` | 📋 PLANNED | Vault file browser module — sidebar + column view + SD browse, ported from `mock-nro-desktop-gui` Vault primitive | Vault is a standalone panel. Port after dock is stable so layout is known-good first. |
| `v0.6.0` | 📋 PLANNED | Dispatch command palette — keyboard-first fuzzy title search + NRO launcher, ported from `mock-nro-desktop-gui` | Requires `ns:am2` title enumeration to be wired (upstream already provides this). Port after Vault proves the panel framework. |
| `v0.7.0` | 📋 PLANNED | Cold Plasma Cascade procedural wallpaper (6 plasma blooms + 80 stars + 18 data-streams, seed `1364153183`) — ported from `mock-nro-desktop-gui v0.12.0` | Wallpaper is render-only; no input or IPC changes. Goes here because theme (v0.3) proved the visual layer and dock (v0.4) proved animation. |
| `v0.8.0` | 📋 PLANNED | EVENT telemetry grammar integration — CURVE/ANIM/INPUT/FINDER/VAULT event lines per `docs/NRO-TELEMETRY-SPEC.md` written to `sdmc:/switch/qos-ulaunch-vX.Y.Z.log` | Telemetry is mandatory before hw-proven gate per creator mandate. Port the grammar from GUI track; adapt to sysmodule log path. |
| `v0.9.0` | 📋 PLANNED | Polish + stress mode (hold ZL+ZR 3s = 60s synthetic-input sweep) + interop test: `mock-nro-desktop-gui-v1.0.0.nro` launchable from Dispatch | All UX primitives landed; now exercise them under stress. Interop confirms the NRO-launch path (ns:am2 chainload) that TUI v0.10.0 also needs. |
| `v1.0.0` | 📋 GOAL | hw-proven Q OS qlaunch replacement — every boot on Switch OG (Erista / Tegra X1) starts Q OS home screen. Log `sdmc:/switch/qos-ulaunch-v1.0.0.log` must show 0 errors + stress mode pass. | Ships as Atmosphère sysmodule bundle under `atmosphere/contents/<titleID>/exefs.nsp`. Non-destructive: remove the bundle to restore stock qlaunch. |

