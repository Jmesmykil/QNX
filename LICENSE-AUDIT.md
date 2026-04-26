# License Audit — Q OS uLaunch Fork

> Populated from `UPSTREAM-ANALYSIS.md` (sibling agent, 2026-04-18).
> Creator decision required — see section at end. Do not begin v0.2.0 work until creator confirms.

---

## Primary License

- **License identifier:** GNU General Public License v2.0 (GPLv2-only)
- **License file location in upstream:** `LICENSE` (full GPLv2 text, no "or later" clause)
- **SPDX expression:** `GPL-2.0-only`
- **Copyright holder:** XorTroll
- **Permissive / copyleft:** Copyleft (strong)

---

## Third-Party Components

| Component | License | Notes |
|-----------|---------|-------|
| Plutonium (XorTroll/Plutonium) | GPLv2 | Same author; GPLv2 propagates |
| Atmosphere-libs (Atmosphere-NX/Atmosphere-libs) | GPLv2 | Statically linked; propagates GPLv2 to whole project |
| libnx-ext (XorTroll/libnx-ext) | likely ISC/MIT | libnx itself is ISC; submodule not deeply cloned |
| nlohmann/json | MIT | Permissive; no copyleft propagation |
| kuba--/zip | MIT | Permissive |
| ocornut/imgui | MIT | Permissive |
| nothings/stb | Public Domain / MIT dual | Permissive |
| nx-hbloader | ISC | Confirmed in `nx-hbloader.LICENSE.md` |

**Project-level license is GPLv2** because Atmosphere-libs and Plutonium (both GPLv2, both statically linked) propagate that obligation to the whole binary.

---

## Attribution Requirements

- Keep XorTroll's copyright notices intact in all modified source files.
- Ship the `LICENSE` file (GPLv2 full text) alongside any binary distribution.
- A NOTICE or README acknowledgment crediting XorTroll/uLaunch is good practice.

---

## Binary Distribution Obligations

Distributing `exefs.nsp` to a Switch SD card counts as distribution under GPLv2.

| Obligation | Requirement |
|------------|-------------|
| Fork + modify | YES — permitted |
| Redistribute binaries | YES — permitted, but source must accompany or be offered in writing for 3 years |
| Keep fork GPLv2 | YES — derivative work must remain GPLv2; Q OS UX code added to uLaunch source is also GPLv2 |
| Keep Q OS patches proprietary | NO — any file modified or linked against GPLv2 code inherits GPLv2 |
| Change the license | NO — GPLv2-only, no upgrade path |
| Internal use only (no SD distribution) | GPLv2 does NOT restrict private use — if the fork runs only on our own device and is never distributed, source disclosure is not legally required |

**GOVERNANCE.md "improvements go back to upstream under same license" is fully consistent with GPLv2.**

---

## Creator Decision — RESOLVED 2026-04-18

Creator statement (verbatim): **"this is for personal use licenses dont apply. this will be free. all confirmed"**

- [x] **GPLv2 for the fork accepted** — personal use only, no public distribution at present, no commercial model, no license obligations attach under this distribution mode.
- [x] **Distribution model: FREE PERSONAL USE.** No `exefs.nsp` shipped publicly. Runs on creator's own Switch. GPLv2 does not restrict private use, so disclosure obligations do not apply.
- [x] **STATE.toml updated** — `[qos_ulaunch_fork] upstream_license` now records the creator resolution verbatim. `personal_use_only = true`. `distribution_model = "free-personal-use"`.

### If distribution mode ever changes
If the fork is later shared publicly (GitHub release, SD card to third parties, etc.), GPLv2 obligations attach at that moment:
- Must ship `LICENSE` (full GPLv2 text) with binaries
- Must offer source (accompanying or in writing for 3 years)
- Must keep XorTroll's copyright notices intact
- Q OS UX code added to uLaunch source must remain GPLv2

Re-open this checklist if distribution intent changes.

### Clean-room alternative — NOT PURSUED
A clean-room qlaunch replacement avoiding GPLv2 was an option but is not needed under personal-use framing. Filed for reference only.
