# uSystem Build-Graph mtime Archaeology — 2026-05-05

Anchor: `/tmp/anchor_apr26` mtime = **Apr 26 11:48** (last-known-working uSystem.nsp).
Scope: every file consumed by a uSystem release link, modified after the anchor.

## Timeline

| When | What changed | Scope |
|---|---|---|
| Apr 18 09:16 | `libstratosphere.a` — **last rebuild** (unchanged since) | Atmosphere-libs |
| Apr 27 21:35 | `Plutonium/render_Renderer.{hpp,cpp}`, `render_SDL2.cpp` (uMenu-side) | Plutonium |
| Apr 28 17:50 | `Plutonium/ui_Application.{hpp,cpp}` (uMenu-side) | Plutonium |
| Apr 28 21:19 | `libpu.a` rebuilt | Plutonium |
| **May 5 21:13** | `libnx-ext.a`, `libnx-ipcextd.a` rebuilt | libnx-ext |
| **May 5 21:13** | uCommon `.o` files rebuilt | uCommon |
| **May 5 21:14:05** | `libuCommon.a` rebuilt | uCommon |
| **May 5 21:14:12** | uSystem `main.o` + 10 sibling `.o` files | uSystem |
| **May 5 21:14:46** | uSystem `.elf`, `.nsp`, `.nso`, `.npdm` written | uSystem |
| **May 5 21:17:30** | `smi_Protocol.hpp` + `main.cpp` modified IN PLACE (post-build edit) | uCommon, uSystem |
| **May 5 21:17** | `src/Makefile` modified (VERSION_MICRO 4 → 6) | root |

External toolchain / Atmosphere-libs / libnx headers: **zero files** newer than anchor. devkitpro tree last touched Apr 14 (root-owned). No `.ccache`/`.cache` in tree.

## Top 3 suspects (descending likelihood)

1. **`src/projects/uSystem/source/main.cpp`** — uncommitted diff adds 50-line `RebootToStockQlaunch` case that calls `ul::fs::ExistsFile`/`ul::fs::RenameFile` and links file-rename machinery into uSystem. Comment in the diff itself confesses: *"tipped uSystem into a black-screen boot regression on v2.3.6"*. Last good HEAD for this file: `9246dc1b` (Apr 25 11:41).
2. **`src/libs/uCommon/include/ul/smi/smi_Protocol.hpp`** — uncommitted diff adds `RebootToStockQlaunch` to `SystemMessage` enum. Forces `libuCommon.a` to be rebuilt and changes IPC ABI between uSystem and uMenu. Last good HEAD: `03a07477` (Apr 25 10:15).
3. **`src/Makefile`** — uncommitted bump `VERSION_MICRO 4 → 6` (skips 5). Cosmetic by itself, but proves the running build is `2.3.6` not the committed HEAD `2.3.4` — i.e. the binary on disk does not match any commit.

## Atmosphere-libs hypothesis: ruled out

`libstratosphere.a` mtime is Apr 18; no header in `src/libs/Atmosphere-libs/` is newer than the anchor; no `.o` under `Atmosphere-libs/libstratosphere/build/` was touched. The `.a` is stale-but-consistent — it would re-link with the same symbols today as on Apr 26. Atmosphere is not the regression vector.

## Recommended action

```sh
cd /Users/nsa/QOS && \
  git checkout -- tools/qos-ulaunch-fork/src/Makefile \
                  tools/qos-ulaunch-fork/src/libs/uCommon/include/ul/smi/smi_Protocol.hpp \
                  tools/qos-ulaunch-fork/src/projects/uSystem/source/main.cpp && \
  rm -rf tools/qos-ulaunch-fork/src/projects/uSystem/build \
         tools/qos-ulaunch-fork/src/projects/uSystem/out \
         tools/qos-ulaunch-fork/src/libs/uCommon/build \
         tools/qos-ulaunch-fork/src/libs/uCommon/lib/libuCommon.a
```

Then rebuild uSystem from clean. That reproduces the Apr 26 11:48 binary because every other input (Atmosphere, libnx, devkitA64, Plutonium headers consumed by uSystem) is unchanged from the working state.

## Caveat — Plutonium drift (uMenu side, not uSystem)

uSystem does not link Plutonium, so the Apr 27–28 Plutonium edits and the Apr 28 `libpu.a` rebuild are NOT in uSystem's dependency closure. They affect uMenu only. Same applies to the May 5 `libnx-ext` rebuilds — uSystem links `libnx-ipcextd.a` for system-mode IPC, but that lib's source headers (`include/...`) didn't change after the anchor; only the compiled `.o`/`.a` were touched, which is consistent with a `make clean` rebuild from the same source. Reverting the three files above is sufficient.
