# Q OS uLaunch Fork — OSS Fork Governance

> Governance loaded per `reverse-engineering-governance` skill (`.github/skills/reverse-engineering-governance/SKILL.md`).
> This document is mandatory reading before any code modification to this fork.
> Creator authorization: Jamesmykil (@jamesmykil), 2026-04-18 HST.

---

## Target Classification

| Field | Value |
|-------|-------|
| Target | XorTroll/uLaunch |
| Repository | `https://github.com/XorTroll/uLaunch` |
| Classification | **Public-OSS** — public GitHub repository, community Atmosphère homebrew |
| License | TBD — see [LICENSE-AUDIT.md](./LICENSE-AUDIT.md). Sibling agent (uLaunch Upstream Analyst) verifies and documents. |
| Authorization | Creator greenlit 2026-04-18. Scope: fork, reskin with Dark Liquid Glass aesthetic, port Q OS UX primitives, deploy as Switch sysmodule. |

Per the governance skill classification table: Public-OSS targets permit full analysis, adaptation, and fork under the terms of the project's own license.

---

## Clean-Room Requirement

**NOT required.** XorTroll/uLaunch is a publicly available open-source project. There is no proprietary source being reverse-engineered. We read the source directly, fork it, and extend it. The sibling agent's `UPSTREAM-ANALYSIS.md` is an architectural map, not a substitute for reading the source.

Clean-room separation would be required only if we were reimplementing `qlaunch` (a Nintendo proprietary binary) from scratch — we are not. We are forking an existing OSS sysmodule that already provides the compatibility layer.

---

## Ethical Boundaries

| Allowed in this fork | Not allowed |
|----------------------|-------------|
| Forking and modifying uLaunch source under its license | Extracting Nintendo credentials, keys, or firmware secrets |
| Applying Dark Liquid Glass UI theme | Bypassing any DRM, license check, or authentication system |
| Porting Q OS UX primitives (Vault, Dispatch, wallpaper, telemetry) | Weaponizing any discovered Nintendo system vulnerability |
| Enumerating installed titles via `ns:am2` (same path uLaunch already uses) | Verbatim reproduction of Nintendo proprietary code (qlaunch, lm, etc.) |
| Contributing improvements back to XorTroll/uLaunch upstream | Accessing or modifying Switch OFW outside the sysmodule scope |
| Non-destructive sysmodule deployment (Atmosphère `contents/`) | Persistent firmware modifications that survive Atmosphère removal |

**Reversibility is a hard requirement.** Removing the sysmodule bundle from `atmosphere/contents/<titleID>/` must fully restore stock qlaunch behavior with no residual side effects.

---

## Upstream Policy

Improvements to uLaunch's core functionality (bug fixes, portability, theme system extensions) should be offered back to XorTroll/uLaunch upstream under the same license. This is not mandatory for Q OS-specific aesthetics (Dark Liquid Glass colors, wallpaper, Vault/Dispatch overlays) which are Astral-specific.

Decision authority: creator (Jamesmykil). If a change is both a generic uLaunch improvement AND a Q OS aesthetic change, split the commit: upstream-eligible change first, Q OS aesthetic layer on top.

---

## Fork Governance

**Versioning:** Semver under this track's own `vX.Y.Z` chain per [ROADMAP.md](./ROADMAP.md). The upstream uLaunch version is recorded in `UPSTREAM-ANALYSIS.md` (sibling agent) and `LICENSE-AUDIT.md` as the base snapshot. Fork versions are independent of upstream tags.

**Commit messages:** Follow Astral convention (`feat:`, `fix:`, `refactor:`, etc.). Reference the ROADMAP version in commit body: `roadmap: v0.3.0 Dark Liquid Glass theme`.

**Changelogs:** `archive/vX.Y.Z/RESULT.md` is the per-version record (build output, hw test result, log path). No separate CHANGELOG file — ROADMAP.md + archive/ are the history.

**Upstream sync:** When uLaunch publishes a significant new release, create a dedicated sync version (`vX.Y.Z-sync-upstream`) to rebase. Never silently absorb upstream changes mid-feature version.

---

## Non-Goals

- No DRM bypass of any kind.
- No credential extraction from Switch firmware, saves, or title data.
- No modification to Switch OFW (official firmware) outside what Atmosphère already permits via its sysmodule hook.
- No weaponization of any `ns:am2` or Horizon IPC surface beyond title enumeration and launch (the same operations any homebrew launcher performs).
- No network exfiltration of title lists, user IDs, or system info.

---

## Escalation

If any of the following arise, HALT and report to creator:

- License audit (see `LICENSE-AUDIT.md`) reveals a non-permissive or copyleft license incompatible with fork + deployment.
- Analysis reveals Nintendo proprietary code inadvertently included in uLaunch source.
- Any IPC path accessed during development produces credentials, device IDs beyond title enumeration, or user PII.
- A crash reveals a security-relevant Atmosphère or Horizon behavior not in scope.
