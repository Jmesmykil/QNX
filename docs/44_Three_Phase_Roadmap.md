# 44 — Three-Phase Roadmap (Q OS)

**Author:** Captured from creator on 2026-04-24, articulated to the orchestrator
**Scope:** Long-term strategic roadmap for the Q OS project, from current applet-mode prototype to a fully-forked native operating system on Switch hardware
**Discipline mandate:** "We will keep following this process and perfect every step before moving on. Let's stabilize right now."

---

## Phase 1 — Applet-Mode + Vault (CURRENT)

**Container:** Atmosphère launcher hijack + uLaunch fork running as a Home-Menu replacement (TID `0100000000001000`). NRO launching uses the standard library-applet hijack (PhotoCapture / Album).

**Memory ceiling:** ~448 MB heap (the applet allocation Nintendo gives library-mode applets).

**Performance characteristics:**

- Applet handoff to uMenu is fast — applets are killed cleanly on Home press, the desktop is instantly back. No GPU context flush, no NS service renegotiation between sibling applet processes.
- Application handoff (game-mode hijack via Mario Kart on Erista) is slow — 200-500 ms per transition for screen-capture freeze + library-applet load + GPU context flush + NS service handshake. This is the "translation layer" feel the creator described.

**Phase 1 work plan:**

1. **Stabilize the desktop core** (in progress 2026-04-24): wallpaper, cursor, top bar, dock, icon launching, telemetry, login screen, power management, dev-tools toggles.
2. **Roll HBMenu functionality into the desktop vault.** HBMenu is just an applet-mode NRO loader at `sdmc:/hbmenu.nro` plus a ~32 MB asset bundle. Folding its functions (NRO discovery, launch, params) into the desktop:
   - Reclaims the SD-card storage HBMenu currently consumes.
   - Eliminates a separate UI surface — every NRO becomes a desktop icon or vault entry.
   - Frees the applet-launch slot for our own future use without HBMenu in the way.
3. **Applet-mode dev windows.** NXLink session manager, USB serial console, file browser, telemetry HUD, stress-mode harness — each runs as a small applet-mode window from the desktop. Sub-second open/close because applet handoff is fast.
4. **Restructure NRO management** to mirror a real-computer file-manager layout (Vault file browser, "All Apps" tray, dock auto-hide + magnification).

**Phase 1 exit criterion:** the applet-mode shell is feature-complete enough that the only reason to move to Phase 2 is the 448 MB ceiling — i.e. we have hit a feature that genuinely can't fit in applet-mode RAM (heavy emulators, browsers, multitasking with persistent background applets).

**Phase 1 explicit non-goals:**

- True background concurrency (multiple applets running simultaneously). Standard Switch applet model = one applet at a time, killed on Home press. Cold-restart with state save/restore is the workaround for the 90% case.
- Modifying signed firmware (BIS, package1, package2). Out of scope until Phase 3 at the earliest.

---

## Phase 2 — Game-Title Hijack Hard Fork

**Container:** uSystem + uLaunch + Q OS desktop fork into a single service that loads inside a hijacked game title (the "Mario Kart 8 Deluxe" or similar full-application HBL injection point).

**Memory ceiling:** ~3.2 GB heap (the full application allocation Nintendo gives game titles on Erista).

**Capability unlock at Phase 2:**

- Full hardware compute access — applet-mode GPU restrictions lift.
- Native suspend/resume on Home — pressing Home from a Q OS-hosted application puts the entire desktop into the suspended-app card on the standard Switch home screen, then re-enters it instantly when tapped. This is the OS's "minimize" semantics, applied to our whole desktop environment.
- Full NS service access — `nsextGetApplicationControlData` works, real game cover-art icons return on the desktop instead of colour-block fallbacks.
- Heavy applet-class apps (emulators, browsers, Retroarch) become first-class.

**Phase 2 trigger condition:** Phase 1 is feature-complete AND there's a concrete capability gap that can only be solved by leaving applet mode.

**Phase 2 architecture:**

- Q OS becomes a full-application binary that absorbs both uMenu's UI surface and uSystem's service layer.
- The hijacked game title hands control to Q OS at startup; Q OS owns the application's RAM and GPU.
- All Phase 1 work continues to live inside this larger container — same desktop, same vault, same dock, same applet-mode dev windows. The container just got fatter.
- The library-applet hijack model continues to be how individual NROs are launched from inside Q OS — the new container hosts a richer applet host without changing how applets are written.

**Phase 2 explicit non-goals:**

- Replacing Atmosphère or Hekate. Phase 2 is still a CFW payload running on top of Atmosphère.
- Modifying Horizon kernel-side patches. Q OS is still a userland process.

---

## Phase 3 — Full Hard Fork of uLaunch + Atmosphère

**Container:** Q OS owns the entire CFW stack from boot. Atmosphère and uLaunch are both forked into Q OS proper. Hekate is still the bootloader (or Q OS ships its own).

**Capability unlock at Phase 3:**

- "Fully functional operating system with a lot of RAM and hardware compute access" (creator's words).
- Q OS owns the kernel-side patches, fs.mitm, ldr hooks, IPC interception — everything Atmosphère currently does is re-authored under the Q OS umbrella.
- The system can finally call itself an operating system in the real sense, not just a CFW payload.

**Phase 3 trigger condition:** Phase 2 is mature, the user-facing OS is settled, and there's a concrete reason that can only be solved by owning the kernel-patch layer (custom syscalls, novel IPC protocols, scheduler changes, custom filesystem semantics).

**Phase 3 architecture:**

- Hard fork Atmosphère into `qos-stratosphere`, `qos-mesosphere`, `qos-exosphere` (or whatever naming we adopt).
- Hard fork uLaunch into `qos-shell` (already partially named in the codebase via `/qos-shell/` log paths).
- Custom syscall surface for Q OS-specific features. Custom IPC ports.
- Q OS branding everywhere — boot loader, splashes, kernel banners, all-the-way-down.

**Phase 3 explicit non-goals:**

- Replacing Horizon's microkernel itself. The kernel is small, signed, and Atmosphère's mesosphère already gives us most of what we want without re-authoring it.

---

## What this roadmap is NOT

- **Not a sprint plan.** Each phase is months-to-years. Step quality > step speed.
- **Not committed scope.** Phase 2 and Phase 3 will only happen if Phase 1 (and 2) hit real ceilings. If applet mode turns out to be enough, we may stop at Phase 1.
- **Not a justification for shortcuts.** Each phase must be feature-complete and stable before the next phase begins. Stub code, scaffolding, and "we'll fix it later" are violations of Phase discipline.

---

## Stabilization gate (current — 2026-04-24)

Before any Phase 1 expansion work (HBMenu roll-in, dev windows, restructure), the desktop core MUST be:

- ✅ Boots cleanly to login screen
- ✅ User selection → desktop transition with Q OS branding (no leftover uLaunch artifacts)
- ✅ Cursor visible with clear click-point
- ✅ Top bar with time/date/battery rendered without overlap
- ✅ Telemetry async-flush working (crash logs are recoverable)
- ✅ Power management (Reboot / Shutdown / Sleep / Reboot-to-Hekate) functional
- ✅ Dev-tools toggles (nxlink / USB serial / log flush) functional
- ⏳ Real game-cover-art icons on desktop (deferred to Phase 2 — applet-mode constraint)

When all green-checked items are verified on hardware over a sustained session (≥1 hour, no crashes), checkpoint commit + push, and Phase 1 expansion begins.

---

## Cross-references

- `docs/43_Splash_Replacement_Research.md` — branding intercept points across the boot chain
- `STATE.toml` — current build state and active staging lane
- `docs/42_Capability_Matrix.md` — feature-by-feature build status (per-version)

