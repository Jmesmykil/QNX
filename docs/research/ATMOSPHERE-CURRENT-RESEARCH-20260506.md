# Atmosphère Current State Research — 2026-05-06

## Latest Atmosphère version

- **1.11.1** — released **April 7, 2025** — basic support for **22.1.0**.
- Source: https://github.com/Atmosphere-NX/Atmosphere/releases/latest
- Predecessor 1.11.0 (April 3, 2025) shipped 22.0.0 and the breaking applet-lifecycle change.
- No 1.11.2 / 1.12.x exists. 1.11.1 IS head of master.

## Installed (1.11.1-master-d04c20a04, built 2026-04-18) vs latest

A master-branch nightly tagged 1.11.1 — same release line as the GitHub tag. **Gap = ~0.** Whatever is broken at master is broken in the latest tagged build too. Hekate v6.5.2 is current-tier in the v6.x line; not the bottleneck.

## FW20 known issues (sourced)

- **Applet pool slashed by Nintendo, not AMS**: FW18.1.0 = 40 MB stealable; **FW19 = 24 MB**; **FW20 = 14 MB**. SciresM acknowledged this in 1.9.0 changelog.
- **AMS compensated, did not fix**: 1.9.0 trimmed `ams.mitm` heap by 20 MB and added the `memlet` helper to *temporarily* steal memory during romfs build. Compensation, not restoration.
- **am sysmodule applet-lifecycle break (1.11.0)**: FW22 demands clean-exit IPC; libnx homebrew never calls it. AMS shipped an `am` patch restoring prior behavior — relevant on FW22, not the FW20 pool budget.
- **TotK heavy mod-packs** documented as `Data abort (0x101)` on FW20 — direct symptom of the 14 MB ceiling.
- **uLaunch last release: 1.2.3, January 2023**, recompiled against AMS 1.10.2 / FW21.2. **No release covers FW22, no FW20 applet-budget fix.** Zero official uLaunch builds for the FW20 era.

Sources: AMS changelog, gbatemp 677205, XorTroll/uLaunch releases.

## 2011-0102 / OutOfSessionMemory — community story

- The error is **`sf::hipc::ResultOutOfSessionMemory`**, defined in Atmosphere-libs `libstratosphere/include/stratosphere/sf/hipc/sf_hipc_server_manager.hpp`. Fires when an HIPC server has no free session slots in its static pool.
- **No GBAtemp / Reddit / Atmosphère Issues thread for "2011-0102"** surfaces in web searches done 2026-05-06. Not a documented common failure mode.
- Adjacent errors 2011-0301 ("Remote Process is Dead") and 2168-0002 (issue #2519, post-20.0.1 crashes) are the public face of FW20 instability.
- Cause: pool sized at compile time on the *server* side. A custom system applet (uSystem replacing qlaunch) built with a session pool too small for FW20's tighter applet budget plus extra `am`-lifecycle peers overflows the array. This is a constant inside the consumer's own server-manager setup.

## Verdict on "newer AMS has FW20 IPC pool fixes"

**FALSE.** Three reasons:

1. No newer AMS exists — 1.11.1 IS latest, user is on master tip.
2. FW20 pool reduction is a **Nintendo kernel/AM change**. AMS cannot widen the 14 MB ceiling.
3. The HIPC session pool emitting 2011-0102 is sized per-server in **the homebrew's own code**, not in AMS sysmodules.

**PARTIAL**: 1.11.0's `am` patch affects applet IPC, but only matters on FW22. Not load-bearing for FW20.

## Recommended next step

Stop chasing AMS. **Audit uSystem / uLaunch's own HIPC server-pool constants.**

1. Grep the uLaunch fork for `MaxSessions`, `MaxServers`, `MaxDomainObjects`, and `sf::hipc::ServerManager<...>` template parameters.
2. Cross-reference against `am` IPC peers actually live on FW20 (qlaunch replacements proxy more services than pre-20.0.0).
3. Bump pool sizes in the server-manager template, or switch to a dynamic server manager if libstratosphere offers one.
4. Capture `/atmosphere/crash_reports/` — register count + faulting PC pinpoint the overflowing call site.

The bug lives in qos-ulaunch-fork, not in `/atmosphere/`.

Sources:
- https://github.com/Atmosphere-NX/Atmosphere/releases/latest
- https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md
- https://gbatemp.net/threads/how-bad-is-20-0-0-firmwares-consequence-of-14mb-from-the-applet-pool-down-from-40mb.677205/
- https://github.com/Atmosphere-NX/Atmosphere/issues/2505
- https://github.com/Atmosphere-NX/Atmosphere/issues/2519
- https://github.com/Atmosphere-NX/Atmosphere-libs/blob/master/libstratosphere/include/stratosphere/sf/hipc/sf_hipc_server_manager.hpp
- https://github.com/XorTroll/uLaunch/releases
- https://gbatemp.net/threads/atmosphere-v1-11-1-released-adds-support-for-switch-firmware-22-1-0.680929/
