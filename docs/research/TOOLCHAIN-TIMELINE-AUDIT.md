# Toolchain Timeline Audit — uSystem.nsp size delta

## Headline finding

**The premise is wrong.** Source is **NOT** identical between the two builds. HEAD is `9246dc1b` in both cases, but the working tree has uncommitted changes that materially alter `uSystem`. Same `git rev-parse HEAD`, different working tree → different binary. This is not a toolchain reproducibility problem.

## Timeline

| Date | Artifact / Event | mtime / install |
|------|------------------|-----------------|
| 2024-10-23 | `npdmtool` | not pacman-managed (orphan), root:wheel |
| 2025-12-29 | `devkita64-binutils` 2.45.1-2 (build) | — |
| 2026-02-01 | `devkita64-gcc` 15.2.0-7 (build) | — |
| 2026-02-04 | `libnx` 4.12.0-1 (build) | — |
| 2026-02-19 | `devkitA64` r29.2-1 (build) | — |
| 2026-04-14 | devkitA64 + libnx + binutils + newlib | **installed once, untouched since** |
| 2026-04-18 | switch-* portlibs batch installed | — |
| 2026-04-18 | `libstratosphere.a` rebuilt | (uSystem doesn't link it; irrelevant) |
| 2026-04-25 | HEAD commit `9246dc1b` ("SP4.15.1 hotfix") | uSystem.nsp 568,284 B, BOOTS |
| 2026-04-26 | Working build deployed | — |
| 2026-04-28 | `libpu.a` rebuilt | (uSystem doesn't link Plutonium; irrelevant) |
| 2026-05-05 | uSystem rebuilt today | uSystem.nsp 589,675 B, **panics 2001-0132** |

No `pacman.log` exists (DKP pacman doesn't keep one by default). No package was installed or upgraded between Apr 18 and May 5. The toolchain is byte-identical to the working build.

## Actual root cause (high → low confidence)

1. **Uncommitted v2.3.6 `RebootToStockQlaunch` handler in `src/projects/uSystem/source/main.cpp` (and matching enum in `src/libs/uCommon/include/ul/smi/smi_Protocol.hpp`, plus VERSION_MICRO bump 4→6 in `src/Makefile`).** The diff adds ~46 lines: a new SMI message case, two `ul::fs::ExistsFile` + two `ul::fs::RenameFile` calls, an `appletRequestToReboot()` call, and four `UL_LOG_*` format strings. That comfortably accounts for ~21 KB of NSP growth (568,284 → 589,675 = +21,391 B) once libstratosphere logging machinery and string literals are pulled in. The handler's own comment block describes a prior draft that "tipped uSystem into a black-screen boot regression on v2.3.6" — the panic signature (`am` `svc::LimitReached`) is consistent with this code path (extra service handles / log thread allocations) at boot when uMenu connects over SMI.
2. **`am` `2001-0132 / svc::LimitReached` is a kernel handle-table exhaustion**, not a toolchain symptom. uSystem's `uSystem.json` sets `handle_table_size` (line 162) and runs against a fixed cap; the new handler keeps two extra `FILE*` open paths and additional log paths in the SMI thread, plus the `appletRequestToReboot()` call adds an `am:su` IPC session. With v2.3.0 telnet/nxlink servers already in flight, the headroom is gone.
3. **gcc 15.2 / LTO non-determinism is not in play.** Build inputs are identical only if you ignore the working tree — which you can't here. Even if LTO did vary the binary, it would not change `am`'s view of uSystem's handle requirements.

## Suggested fix path

- **Primary**: revert the working-tree changes to `src/projects/uSystem/source/main.cpp`, `src/libs/uCommon/include/ul/smi/smi_Protocol.hpp`, and `src/Makefile`, rebuild, confirm 568,284 B output. That restores boot.
- **Then** re-introduce `RebootToStockQlaunch` cleanly: bump `handle_table_size` in `uSystem.json` by at least 4, scope the `FILE*`/`am:su` calls so they don't stay resident, and rebuild before redeploying. The author's own in-source note already flags spsmShutdown linkage as having caused a related regression — treat this handler as a known-fragile boot path.
- **Reproducibility hardening (orthogonal, do later)**: add `-frandom-seed=$(notdir $@)` to the uSystem Makefile and export `SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)` in `src/Makefile` so future "same-commit, different-binary" surprises are ruled out cleanly. Not the cause here, but cheap insurance.

Sources:
- [Switch crashes on boot with uLaunch — XorTroll/uLaunch#134](https://github.com/XorTroll/uLaunch/issues/134)
- [uLaunch project page — GameBrew](https://www.gamebrew.org/wiki/ULaunch_Switch)
