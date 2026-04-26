// ─────────────────────────────────────────────────────────────────────────────
// LEGACY UPSTREAM REFERENCE — DO NOT MAINTAIN
//
// This is upstream XorTroll's uScreen — a Java/JavaFX desktop app that displays
// the Switch's framebuffer over USB (VID 0x057E PID 0x3000, bulk endpoint 0x81,
// reading either RAW_RGBA or JPEG packets per UsbPacketHeader.mode).
//
// Status:
//   * The Switch-side sender (uSystem main.cpp ~line 1556) is genuinely useful
//     and stays active when ConfigEntryId::UsbScreenCaptureEnabled is set.
//   * THIS Java client is preserved as the wire-protocol behavioral spec for
//     the future Q OS native Swift rebuild — `QOS Mirror.app` (post-1.0).
//   * pom.xml hardcodes <classifier>linux</classifier> for JavaFX 12 — won't
//     run on creator's macOS without classifier patches. Removed from the
//     `package:` target in src/Makefile on 2026-04-25 to stop shipping dead
//     bytes in the zip.
//
// Per creator directive 2026-04-25T17:55Z:
//   "uScreen — if it can benefit us in some way let's rebrand and integrate it."
//
// The integration plan: native Swift macOS app reading the same wire protocol.
// Phase 2 work (post qos-ulaunch-fork v1.0). See:
//   * docs/UPSTREAM-COMPANION-APPS-STRATEGY.md  — rebuild + integration plan
//   * docs/ROADMAP.md  — "Post-1.0 native Mac companions" version chain
//   * docs/AUTONOMOUS-TEST-RIG-DESIGN.md  — K+5 visual-channel justification
//
// Do NOT modify. Do NOT re-add to the `package:` target. The
// `legacy-companion-archive` target in src/Makefile zips this tree to
// archive/legacy-companion-apps/ for posterity.
// ─────────────────────────────────────────────────────────────────────────────

package com.xortroll.ulaunch.uscreen;

import com.xortroll.ulaunch.uscreen.ui.MainApplication;

public class Main {
    public static void main(String[] args) {
        MainApplication.run(args);
    }
}