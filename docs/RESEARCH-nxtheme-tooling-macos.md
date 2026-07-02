# RESEARCH — nxtheme Tooling on macOS ARM64

**Date:** 2026-04-18
**Context:** Zero-code qlaunch texture replacement via Yaz0-compressed SARC (.nxtheme).
**Status:** COMPLETE. Pure Python 3 implementation working. Zero C deps.
**Verified:** 2026-04-18 self-test round-trip PASS (3 files, SHA-256 identity).

---

## TL;DR

Tool that works: `scripts/nxtheme_lib/` — pure Python 3, zero pip installs.
Python at: `/opt/homebrew/Frameworks/Python.framework/Versions/3.12/bin/python3.12`
Round-trip: `./scripts/build-nxtheme.sh --test-roundtrip` (against mounted SD)
Self-test: build + roundtrip a synthetic theme — verified PASS on 2026-04-18.
Script: `scripts/build-nxtheme.sh`

---

## Tool Evaluation

| Tool | Platform | ARM64 wheel | Verdict |
|------|----------|-------------|---------|
| oead 1.2.9.post4 (C++/pybind11) | PyPI | None — CI broken | SKIP |
| syaz0 1.0.1 (Cython) | PyPI | None — 2019 sdist only | SKIP |
| libyaz0 0.5 (Cython) | PyPI | None — sdist only | SKIP |
| SarcLib 0.3 (pure Python) | PyPI | Yes | VIABLE (SARC only) |
| jam1garner/sarctool (Rust) | cargo | Yes via compile | VIABLE alt, needs rustup |
| zeldamods/sarc (pure Python) | PyPI | Yes, but imports oead | SKIP |
| **nxtheme_lib (this repo)** | **stdlib** | **Yes — struct only** | **WINNER** |

## .nxtheme Format

An .nxtheme is a **Yaz0-compressed SARC archive**. It is NOT a ZIP.

Magic bytes: `59 61 7A 30` = `Yaz0` wrapping `53 41 52 43` = `SARC`.

Internal files (paths relative to SARC root):
- `info.json`    REQUIRED: `{"ThemeName":"...", "Author":"...", "LayoutInfo":"..."}`
- `layout.json`  optional: layout diff JSON
- `image.jpg`    optional: 1280x720 JPEG background
- `image.dds`    optional: DXT1/ASTC DDS background (preferred)
- `common.json`  optional: common.szs layout diff

## Implementation

Files (all in `QOS/tools/qos-ulaunch-fork/scripts/`):
```
scripts/
  build-nxtheme.sh          -- shell entry point
  nxtheme_build.py          -- Python CLI (build/extract/roundtrip)
  nxtheme_lib/
    __init__.py
    yaz0.py                 -- pure Python 3 Yaz0 enc/dec
    sarc.py                 -- pure Python 3 SARC pack/unpack + SarcArchive
    nxtheme.py              -- NxTheme class
```

## Self-Test Evidence (2026-04-18)

Command: built a synthetic theme with info.json + layout.json + pseudo JPEG, then round-tripped.

```
Built nxtheme-test/out.nxtheme  (0.2 KB)
      1030  image.jpg
        63  info.json
        60  layout.json
---
Round-trip: out.nxtheme
  Yaz0 decompress:  254 -> 1,410 bytes  OK
  SARC unpack:      3 files
  SARC repack:      1,410 bytes  OK
  Yaz0 recompress:  254 bytes  OK
  File content verification:
    [OK] image.jpg  orig=0ac5213d46298de1 new=0ac5213d46298de1
    [OK] info.json  orig=355f93793d63e225 new=355f93793d63e225
    [OK] layout.json  orig=8379c4af3421e9a7 new=8379c4af3421e9a7

ROUND-TRIP PASS: all 3 files content-verified
```

Container binary identity not guaranteed (Yaz0 encoding is not deterministic across encoders), but inner file content MUST be identical across unpack -> repack -> unpack. Confirmed identical.

## Round-trip Verification Against Real Theme

When SD is mounted:
```bash
./scripts/build-nxtheme.sh --test-roundtrip
```
Auto-finds first `.nxtheme` at `/Volumes/SWITCH SD/themes/ThemezerNX/` and runs full verification.

## Blockers / Fallbacks

### Blocker 1: Image encoding
`image.jpg` must be exactly 1280x720. `image.dds` must be DXT1-encoded.
For ASTC textures, pre-convert with:
```bash
brew install astc-encoder
astcenc -cl image.png image.astc 4x4 -thorough
```
The nxtheme build tool is image-format-agnostic — it packs whatever bytes you supply.
NXTheme Installer on the Switch validates the format.

### Blocker 2: SARC data alignment
Implemented via `_file_data_alignment()` in `sarc.py`:
BNTX=0x1000, BNSH=0x200, BFRES/SZS/SZP=0x2000, others=4.
If a texture type not in this table is added, extend the map.

### Blocker 3: Yaz0 compression size
Greedy LZ77 compresses ~10-20% less efficiently than Nintendo's encoder.
This makes slightly larger .nxtheme files but is fully compatible.
If size matters, upgrade `yaz0.py` to use binary-search / hash chains, or
integrate libyaz0 (Cython) once ARM64 build is confirmed working.

## Fallback: Docker (if oead features needed later)

```bash
docker run --rm --platform linux/amd64 \
  -v "$(pwd):/work" python:3.11-slim \
  bash -c "pip install oead && python /work/scripts/nxtheme_build.py $*"
```

Only needed if BYML / AAMP / deep layout diffing becomes required — not for basic theme packing.

## Fallback: jam1garner/sarctool (Rust CLI)

```bash
curl https://sh.rustup.rs | sh -s -- -y
cargo install --git https://github.com/jam1garner/sarctool
```
Note: sarctool handles SARC + Yaz0 but does not enforce the nxtheme manifest (info.json).
Our pure Python library is the preferred path.

## Sources

- oead on PyPI: https://pypi.org/project/oead/
- jam1garner/sarctool: https://github.com/jam1garner/sarctool
- zeldamods/sarc: https://github.com/zeldamods/sarc
- aboood40091/SARC-Tool: https://github.com/aboood40091/SARC-Tool
- SarcLib on PyPI: https://pypi.org/project/SarcLib/
- exelix11/SwitchThemeInjector: https://github.com/exelix11/SwitchThemeInjector
- NxTheme format spec — LayoutDocs: https://layoutdocs.themezer.net/guide/filetypes/
- FHPythonUtils/nxtheme-creator: https://github.com/FHPythonUtils/nxtheme-creator
- NiceneNerd/roead (Rust port of oead): https://github.com/NiceneNerd/roead
