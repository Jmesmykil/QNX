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

### Post-1.0 native Mac companions (Swift SwiftUI per all-apps-Swift mandate)

| Ver | Status | Feature | Why |
|---|---|---|---|
| `companion-v0.1` | 🗺️ PLANNED | **QOS Mirror.app** — native macOS Swift app that displays the Switch's framebuffer over USB. Reads the existing Switch-side `usbScreenCapture` protocol already implemented in uSystem (`UsbMode::Jpeg` + `UsbMode::Rgba`, VID 0x057E PID 0x3000). | Replaces dead Java uScreen with real Swift app per all-apps-Swift mandate. Unblocks: K+5 test rig visual channel, headless dev, demo recording. See `docs/UPSTREAM-COMPANION-APPS-STRATEGY.md`. |
| `companion-v0.2` | 🗺️ PLANNED | **QOS Theme Designer.app** — native macOS Swift app that lets users build `.qtheme` packs (renamed `.zip` containing `manifest.json` + `ui/` tree). uMenu's existing theme loader already reads from the cache layout this writes. | Replaces dead Java uDesigner. Creator directive 2026-04-25T17:55Z: "let them theme Q OS themselves later down the road." See `docs/UPSTREAM-COMPANION-APPS-STRATEGY.md`. |

---

## Process rules (non-negotiable)

1. **ONE change per version** — exactly one hypothesis, one feature. Two things = two versions.
2. **Never build on a crashing base** — if vN crashes on hw, revert to vN-1 or fix as a targeted patch (vN.1); never add features on top of a fatal.
3. **Archive ritual on every build** — before `make`, move prior `src/out/*.nsp` + assets to `archive/vX.Y.Z/` where X.Y.Z is the version being left. See §6.
4. **SSOT carries the version** — `STATE.toml [qos_ulaunch_fork]` + this file. Never infer version from filename.
5. **Every version must be observable** — if a change can't be verified from `sdmc:/switch/qos-ulaunch-vX.Y.Z.log`, add observability first as its own step.
6. **Governance before code** — license confirmed, `INTEGRATION-SPEC.md` consumed before any C++ modification.

---

## Placement reasoning

**Theme (v0.3) before code ports (v0.4+):** The JSON theme system is pure data. If anything is wrong with the upstream build (v0.2), it shows up here at minimum blast radius before we touch C++ logic.

**Dock (v0.4) before Vault (v0.5):** Dock layout is a prerequisite for Vault's panel positioning. Fix the frame before hanging art on it.

**Vault (v0.5) before Dispatch (v0.6):** Both are overlay panels. Vault is simpler (file list, no fuzzy search, no IPC beyond FS). Proves the panel-open/close lifecycle cleanly before Dispatch adds title enumeration complexity.

**Wallpaper (v0.7) after dock + panels:** Wallpaper is render-order-dependent — it goes behind everything. Easier to layer it correctly once dock/panel z-ordering is proven.

**Telemetry (v0.8) before stress (v0.9):** Can't run stress mode blind. Telemetry must write the log that stress mode is judged against.

**Stress + interop (v0.9) before 1.0.0 gate:** Every major regression surface exercised before the hw-proven release tag.

---

## Dependencies

```
v0.1.0 scaffold + license
   └── v0.2.0 upstream baseline
          └── v0.3.0 Dark Liquid Glass theme
                 └── v0.4.0 dock magnify + auto-hide
                        ├── v0.5.0 Vault browser
                        │      └── v0.6.0 Dispatch palette ──── v0.9.0 stress + interop
                        └── v0.7.0 wallpaper ──────────────────────────────────┘
                               └── v0.8.0 telemetry
v1.0.0 hw-proven gate (requires v0.9.0 PASS + log 0-errors)
```

---

## §6 — Archive Ritual (memorize, run before every build)

```sh
# 1. Identify current version from STATE.toml
CUR=$(grep 'current_version' ../../STATE.toml | grep -A5 'qos_ulaunch_fork' | head -1 | sed 's/.*= *//' | tr -d '"')

# 2. Archive prior outputs
mkdir -p archive/v${CUR}
[ -f src/out/exefs.nsp ] && mv src/out/exefs.nsp archive/v${CUR}/
[ -d src/out/themes ]    && mv src/out/themes     archive/v${CUR}/
# Stamp result after hw test:
# echo "PASS|FAIL|CRASH: <detail>" > archive/v${CUR}/RESULT.md

# 3. Bump version in STATE.toml [qos_ulaunch_fork] current_version = "X.Y.Z"
#    (main thread does this — agents propose, don't apply)

# 4. Build
cd src && make

# 5. Stage to SD (UMS)
cp src/out/exefs.nsp "/Volumes/SWITCH SD/atmosphere/contents/<TITLEID>/exefs.nsp"
# <TITLEID> to be filled by sibling agent from UPSTREAM-ANALYSIS.md

# 6. Eject per feedback_switch_sd_eject_after_push.md
diskutil eject "/Volumes/SWITCH SD"
```

---

## Coordinated with sibling tracks

- `mock-nro-desktop-gui v1.0.0` NRO must be hw-proven before uLaunch v1.0.0 can call it in interop (v0.9.0 step).
- `mock-nro-desktop v0.10.0` (ns:am2 game-launch) exercises the same IPC path as uLaunch title enumeration — both must reach PASS before the Phase 1.5 → Phase 2 bridge is considered proven.
- `switch-nro-harness` correctness gate is independent; it runs in EL0 userspace and does not test sysmodule-context code. Integration tests for the sysmodule itself are a future extension to the harness (post-v1.0.0 scope).
