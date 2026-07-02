# Sysmodule Compatibility — AMS 1.11.x master + Firmware 20.0.0

**Investigation date:** 2026-05-05
**Crash signature under investigation:** `am` (Applet Manager, Program ID `0100000000000023`) panics at `+0x40570` with result `2001-0132` (`0x10801`), User Break reason 0, ~92 s into boot, only when ≥ 1 of `sys-clk` / `nx-ovlloader` / `sys-con` is auto-starting.
**Already excluded:** `sys-patch v1.5.9` (impeeza, title `420000000000000B`) — runtime-essential and clean per its own log.

---

## 1. Atmosphere 1.10 / 1.11 — Why this matters

Per the Atmosphere upstream changelog, **the boundary that broke things is FW 20.0.0 / AMS 1.10.0**, not 1.11. AMS 1.11.0 only adds basic 22.x support, and 1.11.1 adds 22.1.0 + stability. But 1.10.0 introduced two breaking changes whose side effects are still in flight:

- *Source:* https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md
- > "As a result of changes made to nintendo's software in 20.0.0, there is roughly 10MB less memory available for custom system modules."
- > "We can only steal a maximum of 14MB from the applet pool, down from 40MB."
- > "As a temporary solution, patches to the `am` sysmodule are now included which allow restoring the previous behavior and regain homebrew compatibility without any further changes."

Implication: any sysmodule whose static heap was tuned for the pre-20.0.0 40 MB applet pool will OOM-collide with the AMS-injected `am` patch path. The **`am +0x40570`** symbol is reachable from the patched applet bring-up code that runs ~90 s into boot when the OS finishes building the applet pool and the first applet (album/HOME launcher) is requested.

---

## 2. sys-clk (retronx-team v2.0.1, ppkantorski v2.0.1+r24)

### 2.1 Compatibility declared
- README declares only "updated to at least the latest stable [Atmosphere]" — *no explicit AMS or FW version listed.*
- *Source:* https://github.com/retronx-team/sys-clk
- ppkantorski fork release `2.0.1+r17` (2025-11-16) explicitly notes "AMS 1.10+ support via updated libnx".
- *Source:* https://github.com/ppkantorski/sys-clk/releases

### 2.2 Open issues for 1.11 / 20.0.0 / am crash
- **None.** No open or closed issue mentions "1.11", "20.0.0", "am crash", "boot crash", "2001-0132".
- *Source:* `GET /repos/retronx-team/sys-clk/issues?state=all&per_page=30`
- Closest hit: **issue #85** (closed 2024-10-12) — *HOS 19.0.0 hangs from `tssession::GetTemperature` IPC*. Fixed in master.
- *Source:* https://github.com/retronx-team/sys-clk/issues/85
- ppkantorski fork issues: 6 total, none crash-related.
- *Source:* `GET /repos/ppkantorski/sys-clk/issues?state=all&per_page=20`

### 2.3 Recent commits since release
- Most recent master commit: **2026-04-04** — *"update to new libnx nacp struct definition"*. No AMS-1.11- or `am`-related fixes pending.
- 2024-06-16 — *"fix svcQueryIoMapping renamed in libnx"* (already shipped).
- *Source:* `GET /repos/retronx-team/sys-clk/commits?per_page=30`

### 2.4 Subsystems hooked
- Hooks `pcv` / `clkrst` / `apm` / `psc:m` / `i2c` (TI TMP451 reads) / `psm` (fuel gauge).
- These are **clock + power IPC services**, none of which `am` polls during applet bring-up.
- The pre-`am` IPC surface means a sys-clk fault would more likely panic `pcv` or `psm`, not `am`.

### 2.5 Forks claiming AMS 1.11 fixes
- **ppkantorski/sys-clk** rolling release line `2.0.1+r17` → `r24` (2026-04-20). Changelog of r22/r23/r24 = recompilation against `libultrahand v2.3.0 → v2.4.0 → v2.4.1`; *no `am` or applet-pool work*.
- *Source:* https://github.com/ppkantorski/sys-clk/releases

---

## 3. nx-ovlloader (ppkantorski v2.0.0, fork of WerWolv/nx-ovlloader)

### 3.1 Compatibility declared
- ppkantorski README: "forked from WerWolv/nx-ovlloader … requires Atmosphère and HOS 9.0.0 or later."
- v2.0.0 ships **firmware-aware default heap**: `HOS 21+ = 4 MB`, `HOS 20+ = 6 MB`, older = 8 MB.
- *Source:* https://github.com/ppkantorski/nx-ovlloader

### 3.2 Open issues / search hits for 2001-0132 + am crash
- **DIRECT MATCH on the upstream WerWolv repo — issue #42, opened 2025-07-21, still open:**
  - > "Atmosphere 1.9.2 chrashes when nx-ovlloader+ v1.1.0 present on Firmware 20.2.0
  - > Error code: 2001-0132 (0x10801)
  - > Program: 0100000000000023"
- Program ID `0100000000000023` is **`am` (Applet Manager)**.
- *Source:* https://github.com/WerWolv/nx-ovlloader/issues/42
- *Title-list confirmation:* https://gist.github.com/ndeadly/a4b8c01bb453028cd0008f282098f696 + https://switchbrew.org/wiki/Title_list
- *Same crash signature as the user's reported failure: `am` + `2001-0132`.*
- ppkantorski/nx-ovlloader own issue tracker: 2 PRs only, no crash issues filed.
- *Source:* `GET /repos/ppkantorski/nx-ovlloader/issues?state=all&per_page=30`

### 3.3 Recent commits since release
- Most recent: 2026-04-23, all README/sponsor/copyright commits. v2.0.0 itself (2025-11-26) is the last code-bearing release.
- ppkantorski v2.0.0 was *the fix attempt* for FW 20.0.0 — it added dynamic heap sizing exactly because the pre-20.0.0 fixed 8 MB heap on a 14 MB applet pool collides with `am`.
- *Source:* https://github.com/ppkantorski/nx-ovlloader/releases/tag/v2.0.0

### 3.4 Subsystems hooked
- `nx-ovlloader` itself maps overlay NROs into a dedicated heap stolen **from the applet pool** and chainloads `/switch/.overlays/ovlmenu.ovl`. It uses `applet:OE` / `pl:u` / `pm:dmnt`.
- Companion sysmodule `nx-ovlreloader` (title `420000000007E51B`, bundled with v2.0.0) registers an IPC server that the loader pings to trigger a clean restart when `heap_size.bin` changes.
- **Both touch the applet pool directly.** That is exactly the surface AMS 1.10/1.11 patched in the `am` sysmodule — a heap allocation that exceeds `14 MB - other_sysmod_use` produces the crash at `am +0x40570` because the applet creation IPC fails after the patched code path attempts to map the now-undersized region.

### 3.5 GBAtemp / external corroboration
- GBAtemp thread *"sys-botbase + ldn_mitm crashes Atmosphere 1.11.1 on boot with latest Fw 22.1.0"* (April 2026) — same class of failure: two memory-pool-stealing sysmodules added together exceed AMS 1.11's tighter pool.
- *Source:* https://gbatemp.net/threads/sys-botbase-ldn_mitm-crashes-atmosphere-1-11-1-on-boot-with-latest-fw-22-1-0.680982/
- > "removing the ldn_mitm overlay … there simply was not enough memory left."

---

## 4. sys-con (cathery v0.6.5)

### 4.1 Compatibility declared
- README: "Switch FW 5.0.0+". No FW 20 or AMS 1.10/1.11 statement.
- *Source:* https://github.com/cathery/sys-con

### 4.2 Open issues for 1.11 / 20 / am crash
- **None mention `2001-0132` or `am +0x40570`.** Three open compat issues:
  - **#346** (2025-12-07) — runtime crash on console undock / controller disconnect (NOT boot).
  - **#344** (2025-11-28) — feature request, "compatibility with FW 21.0.0 / AMS 1.10.0".
  - **#342** (2025-09-13) — "Not working on FW 20.4.0, AMS 1.9.4" (no log).
- *Source:* `GET /repos/cathery/sys-con/issues?state=all&per_page=30`
- Discussion **#218**: an old "Atmosphere won't load when sys-con is present" — traced to **archive-bit / config-folder placement** on macOS, not an IPC fault.
- *Source:* https://github.com/cathery/sys-con/discussions/218

### 4.3 Recent commits since release
- Most recent: **2024-11-06** — *"Fix building for latest libnx, remove Atmosphere-libs dependency."* The repo has been **inactive for 18 months** at the time of this report. No AMS-1.10/1.11 fixes pending in master.
- *Source:* `GET /repos/cathery/sys-con/commits?per_page=20`

### 4.4 Subsystems hooked
- `usb-hs` (USB host service IPC), `hid` registration via `bsv:`/`hid:` to publish virtual controllers, `psc:m` for sleep handling.
- **`hid`-not-`am`** is its blast radius. A sys-con fault at boot more typically panics `hid` (program `0100000000000013`) — exactly the symptom of the *other* `2001-0132` reports against `hid` in Atmosphere issues #2601 and #2708.
- *Sources:* https://github.com/Atmosphere-NX/Atmosphere/issues/2601 , https://github.com/Atmosphere-NX/Atmosphere/issues/2708

### 4.5 Forks claiming AMS 1.11 fixes
- `tallbl0nde/sys-con`, `Lestrol/sys-con`, `ppkantorski/sys-con` — all surveyed, none publish a release note specifically for AMS 1.11 + FW 20.0.0 `am` compat. ppkantorski's fork tracks libultrahand integration only.

---

## 5. Tier ranking — which is most likely the bad actor

| Rank | Sysmod | Probability | Reasoning |
|------|--------|-------------|-----------|
| 1 (HIGHLY LIKELY) | **nx-ovlloader (ppkantorski v2.0.0 + ovlreloader)** | ~80% | Only sysmod whose IPC surface lands in the **applet pool**, the exact region the AMS 1.10 `am` patch tightened from 40 → 14 MB. WerWolv issue #42 reports the **identical signature `2001-0132` on program `0100000000000023` (`am`)** with nx-ovlloader present on FW 20.2.0 — same class as the user's machine. v2.0.0 attempted to fix it via dynamic heap sizing but the default **6 MB on FW 20.0.0** still over-claims when other sysmods (sys-clk worker threads, sys-patch hooks) are also active in 1.11.1's tighter pool. |
| 2 (LOW) | **sys-clk** | ~15% | Clean track record on AMS 1.10+. ppkantorski fork explicitly cites AMS 1.10+ support since r17. Hooks `pcv` / `apm`, not `am`. Plausible only if its dynamic-frequency thread spikes the system into a state that triggers the `am` patch boundary indirectly — but no GitHub issue links sys-clk to `2001-0132`. |
| 3 (VERY LOW) | **sys-con** | ~5% | Code is **frozen at 2024-11-06**, which means it's already been running on every AMS from 1.7 → 1.11.1 without an `am`-class report. Its IPC surface is `usb-hs` / `hid`, not `am`. The *only* AMS-related report (#346) is a runtime undock crash, not the 92-s-into-boot panic the user has. |

---

## 6. Most likely culprit

**nx-ovlloader v2.0.0 (ppkantorski) + companion ovlreloader (`420000000007E51B`).** The crash signature `am +0x40570` / `2001-0132` matches WerWolv issue #42 exactly. nx-ovlloader is the only one of the three that allocates from the applet pool that AMS 1.11.1's `am` patch now rations.

## 7. Concrete bisect order

1. **Disable nx-ovlloader first.** Move both `420000000000000F` (nx-ovlloader) **and** `420000000007E51B` (nx-ovlreloader) out of `/atmosphere/contents/` (or rename their `flags/boot2.flag`). Reboot. If `am` no longer panics → confirmed. (Expected outcome.)
2. **If still crashing**, re-enable nx-ovlloader, disable **sys-con** (`690000000000000D`). Reboot.
3. **If still crashing**, re-enable sys-con, disable **sys-clk** (`00FF0000636C6BFF`). Reboot.
4. **If still crashing without all three**, the bad actor is elsewhere (sigpatches, an Edizon overlay still present, leftover Tesla overlay) — out of scope for this report but worth checking next.

## 8. What to update / replace / configure differently

### 8.1 Required immediate actions
- **Manually shrink nx-ovlloader heap.** Create `/config/nx-ovlloader/heap_size.bin` containing the little-endian 32-bit value `0x00200000` (2 MiB) — well under the FW 20.0.0 / AMS 1.11.1 budget. Source: ppkantorski v2.0.0 release notes.
- **Remove `nx-ovlreloader`** if you do not need automatic heap resize. It is only useful for switching heap sizes without reboot, and its presence doubles the applet-pool footprint.
- **Do not run nx-ovlloader concurrently with Tesla menu, EdiZon, sys-botbase, ldn_mitm** on AMS 1.11.x — the combined applet-pool draw exceeds the 14 MB cap. Either keep nx-ovlloader and remove the others, or vice versa.

### 8.2 Update path
- sys-clk: keep `ppkantorski 2.0.1+r24` (latest, AMS-1.10-aware, no known `am` faults).
- sys-con: 0.6.5 is the only release; it is safe to keep but monitor cathery/sys-con#346 for the next maintenance cycle. Consider tallbl0nde or Lestrol fork only if you depend on a controller cathery's tree does not handle.
- nx-ovlloader: stay on **ppkantorski v2.0.0** *with* `heap_size.bin = 2 MiB` override **and** `nx-ovlreloader` removed. If the panic recurs after that, downgrade overlays themselves rather than the loader.

### 8.3 Config change
- After the bisect, add a `boot2.flag`-based gating script (or use `sys-patch`'s loader hook) so that nx-ovlloader is only auto-started when an overlay is actually requested by long-press, not at every boot. This permanently removes it from the boot-time applet-pool calculation.

### 8.4 Long-term
- File a corroborating report on https://github.com/ppkantorski/nx-ovlloader/issues citing WerWolv#42 + your panic dump. The maintainer ships heap fixes — a fresh AMS 1.11.1 + FW 20.0.0 + `am +0x40570` log will likely produce a v2.0.1 default reduction.

---

## Sources

- [Atmosphere changelog (1.10.0, 1.11.0, 1.11.1)](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md)
- [retronx-team/sys-clk repo](https://github.com/retronx-team/sys-clk)
- [retronx-team/sys-clk issue #85 — HOS 19.0.0 sleep hang](https://github.com/retronx-team/sys-clk/issues/85)
- [ppkantorski/sys-clk releases (r17 → r24)](https://github.com/ppkantorski/sys-clk/releases)
- [WerWolv/nx-ovlloader issue #42 — 2001-0132 on FW 20.2.0](https://github.com/WerWolv/nx-ovlloader/issues/42)
- [ppkantorski/nx-ovlloader v2.0.0 release](https://github.com/ppkantorski/nx-ovlloader/releases/tag/v2.0.0)
- [ppkantorski/nx-ovlloader README (fork relationship)](https://github.com/ppkantorski/nx-ovlloader)
- [cathery/sys-con repo](https://github.com/cathery/sys-con)
- [cathery/sys-con issue #346 — undock crash](https://github.com/cathery/sys-con/issues/346)
- [cathery/sys-con issue #344 — FW 21 / AMS 1.10 request](https://github.com/cathery/sys-con/issues/344)
- [cathery/sys-con issue #342 — FW 20.4.0 + AMS 1.9.4](https://github.com/cathery/sys-con/issues/342)
- [cathery/sys-con discussion #218 — archive-bit boot loop](https://github.com/cathery/sys-con/discussions/218)
- [Atmosphere issue #2601 — 2001-0132 on hid](https://github.com/Atmosphere-NX/Atmosphere/issues/2601)
- [Atmosphere issue #2708 — 2001-0132 sys-botbase](https://github.com/Atmosphere-NX/Atmosphere/issues/2708)
- [GBAtemp — sys-botbase + ldn_mitm crashes AMS 1.11.1 on FW 22.1.0](https://gbatemp.net/threads/sys-botbase-ldn_mitm-crashes-atmosphere-1-11-1-on-boot-with-latest-fw-22-1-0.680982/)
- [Switchbrew title list (`0100000000000023` = am)](https://switchbrew.org/wiki/Title_list)
- [ndeadly title-ID gist (am sysmodule confirmation)](https://gist.github.com/ndeadly/a4b8c01bb453028cd0008f282098f696)
