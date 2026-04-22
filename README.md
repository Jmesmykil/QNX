# Q OS uLaunch Fork — Phase 1.5 qlaunch Replacement

Q OS becomes the Nintendo Switch home screen from every boot — without Hekate, without
replacing Horizon, and without holding R. This directory is a fork of
[XorTroll/uLaunch](https://github.com/XorTroll/uLaunch), a community Atmosphère
sysmodule that replaces Nintendo's stock `qlaunch` (home menu) while keeping full
Horizon compatibility underneath.

The fork is reskinned with the **Dark Liquid Glass** aesthetic developed in
`tools/mock-nro-desktop-gui` and extended with Q OS UX primitives (Vault file browser,
Dispatch command palette, dock magnify + 5s auto-hide, Cold Plasma Cascade procedural
wallpaper, EVENT telemetry grammar). Title enumeration runs through uLaunch's existing
`ns:am2` IPC path — the same path `mock-nro-desktop v0.10.0` needs to verify game
launch, so this fork and that TUI track exercise the same kernel surface.

## Relationship to sibling tools

| Tool | Role relative to this fork |
|------|---------------------------|
| `tools/mock-nro-desktop-gui/` | UX reference and source of ported primitives (dock, Vault, Dispatch, wallpaper, cursor curve). GUI v1.0.0 NRO remains launchable FROM the fork as a power-user entry point. |
| `tools/mock-nro-desktop/` | TUI baseline. `ns:am2` title-launch IPC (v0.10.0 slot) must match the sysmodule path used here. |
| `tools/switch-nro-harness/` | Correctness gate. uLaunch fork integration tests will extend the harness when the sysmodule is stable enough to call from test context. |

## Hard distinction vs Phase 2 Hekate

This lane does **NOT** replace Horizon. It replaces `qlaunch` only — the home screen
process that runs inside the Horizon + Atmosphère userspace sandbox. The Switch OS,
firmware, kernel, and system services are untouched. Disabling the sysmodule (removing
it from `atmosphere/contents/`) reverts cleanly to stock qlaunch with no other side
effects. Phase 2 (bare-metal Hekate payload) is a separate, independent lane; this
fork can ship and iterate while Phase 2 remains held.

## Upstream and license

Upstream: `https://github.com/XorTroll/uLaunch` (C++, libnx, Atmosphère sysmodule,
JSON theme system, `ns:am2` title enumeration). License details TBD — see
[LICENSE-AUDIT.md](./LICENSE-AUDIT.md) (sibling agent fills this from upstream analysis).

## Version chain and roadmap

See [ROADMAP.md](./ROADMAP.md) — it is the SSOT for the version chain, build ritual,
placement reasoning, and live status. Do not rely on file timestamps or commit messages
for version state; ROADMAP.md and `STATE.toml [qos_ulaunch_fork]` are authoritative.
