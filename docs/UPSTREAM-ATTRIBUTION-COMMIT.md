# Upstream Attribution Commit (Draft)

> **Purpose:** Pre-stage the attribution / loveletter commit message that lands as the FIRST
> commit on the public-fork branch. Per creator: *"If we release we will hard fork on git and
> they will all get full credit. This is a loveletter to all of them."*
> **Generated:** 2026-04-25T14:00:00Z
> **Status:** DRAFT — not yet committed. Lands when public-release prep enters its commit
> phase (after Track A/B/E land + asset rebrand pass + repo visibility flip).

---

## Commit subject

```
Hard-fork attribution: credit to uLaunch, Plutonium, Atmosphère, libnx, devkitPro
```

## Commit body

```
Q OS uMenu is a hard fork of XorTroll's uLaunch (https://github.com/XorTroll/uLaunch),
released under GPL-2.0-only. This commit exists to make the credit explicit, in code as
well as in LICENSE-AUDIT.md, before the public release of the fork.

Direct upstream lineage
-----------------------
* uLaunch     — XorTroll (Stary2001)        GPL-2.0-only
                The entire qlaunch-replacement architecture, the SMI IPC pattern between
                uSystem and uMenu, the NRO-loader path, the uManager installer. Q OS
                rebranded the desktop UI (qdesktop/), but every system-level capability in
                this fork descends from XorTroll's work. Without uLaunch this project would
                be three years of reverse engineering instead of three weeks of
                rebranding.

* Plutonium   — XorTroll                    GPL-2.0-only
                The UI framework. Every Element, Layout, Renderer, font registration,
                touch dispatch — Plutonium. We added RenderTextAutoFit (auto-shrink without
                "..." truncation) and a few cached-texture patterns. The framework itself
                is XorTroll's.

Switch-homebrew foundations
---------------------------
* Atmosphère  — Atmosphere-NX team          GPL-2.0-only / GPL-2.0-or-later (mixed)
                Custom firmware. Without Atmosphère there is no way to run uMenu as the
                qlaunch replacement. The exefs override mechanism, the contents/ tree, the
                sys-patch interaction — all Atmosphère.

* libnx       — devkitPro / switchbrew      ISC
                The C runtime / system bindings (psm, nifm, btdrv, vi, ns, hid, fs, etc.)
                that uMenu and uSystem call into. ISC-licensed permissive groundwork.

* devkitPro   — devkitPro contributors      Mixed (predominantly permissive)
                The aarch64 toolchain (devkitA64), ELF→NSO conversion, the build system
                glue that turns this codebase into a runnable .nso. Q OS builds with
                devkitA64 GCC 15.2.0.

* Hekate      — CTCaer + naehrwert          GPL-2.0-only
                The bootloader chain that injects Atmosphère before Horizon. Q OS dual-
                boots from Hekate's Nyx menu.

Tooling & runtime helpers we link or call
-----------------------------------------
* nlohmann/json     MIT          — Q OS-app records.bin parsing, theme manifests
* kuba--/zip        MIT          — packaging
* ocornut/imgui     MIT          — debug/dev menus (carried from upstream uLaunch)
* nothings/stb      Public Domain — image decode
* nx-hbloader       ISC          — homebrew loader path
* sys-patch (impeeza/borealis)  GPL-2.0-only — runtime sigpatches dependency
* sys-con           GPL-2.0-only — USB controller / serial sysmodule

Specific thanks
---------------
The patterns we learned from reading upstream code:
- The exefs.nsp override mechanism for replacing qlaunch (uSystem README + Atmosphère docs)
- The SMI cmif command numbering convention (uLaunch SystemImpl.cpp)
- The NACP-icon extraction and grid-rendering loop (uLaunch MenuApplication.cpp)
- The Plutonium Renderer's font-cache architecture (Plutonium pu_RendererPRC.cpp)
- The "force-mount sdmc and re-init log" startup sequence (uLaunch main.cpp)

What Q OS adds on top
---------------------
QDESKTOP_MODE — a 1920×1080 desktop layout with auto-grid icons, a 6-slot dock, layered
top bar, login screen, and Q OS-branded special-icon dispatch. The architectural
contribution is small relative to what was inherited; the renaming, rebranding, and added
features sit on top of XorTroll's framework.

License
-------
This fork remains GPL-2.0-only as required by GPLv2 inheritance from uLaunch, Plutonium,
and Atmosphère. The LICENSE file is the canonical legal text. Where Q OS adds original
art assets, those assets are released CC-BY-SA-4.0 (or as the creator decides at
release time) — see assets/ for the asset license file. Upstream art assets remain
under their original GPLv2 license and are recorded in the historical commit log.

Maintainers' note
-----------------
If you are XorTroll, the Atmosphère team, the devkitPro maintainers, or any other
upstream maintainer reading this: this fork exists because your work made it possible.
The intent is not to replace or compete — it is to extend uLaunch's reach with a Q OS-
specific desktop UI layer, and to bring the visual experience closer to a Mac-class
launcher for users on the Switch homebrew side. If anything in this fork merits going
back upstream, every patch is yours to take, no strings attached.

— Q OS uMenu maintainers, 2026
```

---

## Pre-commit checklist (before this lands on a public branch)

1. ☐ Confirm `LICENSE` file at repo root is the unmodified GPLv2 text (no edits).
2. ☐ Confirm `LICENSE-AUDIT.md` lists every dependency above with current SHA / version.
3. ☐ Confirm asset license file exists under `assets/` documenting Q OS-original art license.
4. ☐ Confirm no upstream maintainer name is misspelled (verify against each project's README).
5. ☐ Confirm Q OS additions (QDESKTOP_MODE, qdesktop/) are clearly fenced with copyright
      headers crediting Q OS as the author of the *delta*, not the framework.
6. ☐ Add `CONTRIBUTING.md` pointing at upstream uLaunch for framework-level changes and
      this fork for desktop-layer changes.
7. ☐ Add `README.md` "Origin" section linking back to upstream uLaunch with attribution
      paragraph above the Q OS-specific feature list.

---

## Distribution notes

- **hb-appstore** (`fortheusers/hb-appstore`) submission must include the same attribution
  body in the manifest's `description` field (first paragraph). Category: `System Mod`.
- **Sphaira** catalog submission likewise. Repo visibility must be public before submission.
- **GitHub release ZIP** must contain a top-level `CREDITS.md` with the body above.

---

Updated: 2026-04-25T14:00:00Z
