# Build Guide — Q OS uLaunch Fork

> This guide becomes actionable at v0.2.0 (upstream merge). At v0.1.0 (current),
> `src/` is empty — there is nothing to build yet.
> TitleID for the SD install path comes from `UPSTREAM-ANALYSIS.md` (sibling agent).

---

## Toolchain

Install via devkitPro pacman (macOS: `brew install devkitpro-pacman`):

```sh
sudo dkp-pacman -S switch-dev       # aarch64-none-elf cross-compiler + libnx headers
sudo dkp-pacman -S libnx            # Nintendo Switch userspace library (pinned to version uLaunch requires — see UPSTREAM-ANALYSIS.md)
sudo dkp-pacman -S switch-tools     # elf2nso, npdmtool, build_pfs0
```

Verify:
```sh
aarch64-none-elf-gcc --version      # should show devkitA64 cross compiler
```

The workspace Rust toolchain (`rust-toolchain.toml` at QOS root) is NOT used here —
this is a C++ sysmodule, not a Rust NRO. devkitPro's `aarch64-none-elf-g++` is the compiler.

---

## Build (after v0.2.0 upstream merge)

```sh
cd QOS/tools/qos-ulaunch-fork/src/

# Run the archive ritual FIRST (see §6 in ROADMAP.md)
# ... then:

make
```

Expected outputs in `src/out/`:
- `exefs.nsp` — the sysmodule NSP to deploy under Atmosphère
- `themes/` — theme asset directory (Dark Liquid Glass JSON + assets, from v0.3.0 onwards)

Build errors in devkitPro C++ projects are almost always one of:
- Missing `DEVKITPRO` env var → add `export DEVKITPRO=/opt/devkitpro` to your shell profile
- Wrong libnx version → match exactly what uLaunch requires (see `UPSTREAM-ANALYSIS.md`)
- Missing `switch-tools` package → run the pacman install above

---

## Archive ritual (MANDATORY before every build)

See [ROADMAP.md §6](./ROADMAP.md) for the full script. Summary:
1. Move `src/out/exefs.nsp` + `src/out/themes/` to `archive/vX.Y.Z/`
2. Bump `STATE.toml [qos_ulaunch_fork] current_version`
3. Run `make`

Never build without archiving first. Never ship two `.nsp` files side-by-side on the SD.

---

## Stage to SD Card (UMS)

```sh
# Replace <TITLEID> with the value from UPSTREAM-ANALYSIS.md
TITLEID="<TITLEID>"

cp src/out/exefs.nsp "/Volumes/SWITCH SD/atmosphere/contents/${TITLEID}/exefs.nsp"

# Copy theme assets if applicable (v0.3.0+)
# cp -r src/out/themes/ "/Volumes/SWITCH SD/atmosphere/contents/${TITLEID}/themes/"

# Eject per switch-ums-only workflow mandate
diskutil eject "/Volumes/SWITCH SD"
```

---

## Install Verification

After booting the Switch with the sysmodule staged:

1. Atmosphère intercepts qlaunch and loads the sysmodule.
2. The Q OS home screen appears instead of the stock Nintendo home menu.
3. Log should appear at `sdmc:/switch/qos-ulaunch-vX.Y.Z.log` (from v0.8.0 telemetry onwards).

**If the sysmodule crashes:** Atmosphère will attempt to fall back to stock qlaunch. If it does not recover automatically, hold Power → power off → boot normally (without custom sysmodule) by temporarily removing `atmosphere/contents/<TITLEID>/` or renaming `exefs.nsp`. This is the reversibility guarantee.

**Never** need RCM recovery for a sysmodule crash (per `feedback_safe_return_must_be_100_percent.md`).

---

## Eject policy

Eject the SD card only when READY to test — not before. While additional build
iterations are queued, keep the SD mounted. Eject is the final step of the "done"
ritual once the last artifact for a session is staged. See `feedback_switch_sd_eject_after_push.md`.
