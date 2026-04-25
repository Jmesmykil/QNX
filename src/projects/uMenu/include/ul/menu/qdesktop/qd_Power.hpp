// qd_Power.hpp — System power management for Q OS uMenu (v0.21+).
//
// Wraps libnx + Atmosphère extensions for the four user-facing power actions
// the qdesktop login screen exposes: reboot, shutdown, sleep, reboot-to-Hekate.
//
// All four functions are "fire and do not return" semantically; on hardware
// the system applet is suspended/destroyed inside the libnx call.  On the
// host build the implementations are no-ops so unit tests link cleanly.
//
// Usage:
//   if (user_clicked_power_button) {
//       ul::menu::qdesktop::power::Reboot();
//   }
#pragma once

namespace ul::menu::qdesktop::power {

// Clean reboot via bpcRebootSystem.  Returns only on the host build (no-op);
// on hardware the applet does not return.
void Reboot();

// Clean shutdown via bpcShutdownSystem.  Returns only on the host build.
void Shutdown();

// Enter standby / sleep via appletStartSleepSequence(true).  Returns when the
// console is woken back up; the caller should treat the post-call frame as a
// fresh resume (re-fetch time, redraw fully).
void Sleep();

// Reboot to Hekate's bootloader payload at /atmosphere/reboot_payload.bin.
// Uses bpcAmsRebootToPayload (Atmosphère extension).  Falls back to a plain
// reboot if the extension is unavailable; in both cases logs the outcome.
void RebootToHekate();

// Returns true iff the Atmosphère bpcAmsRebootToPayload extension is
// detectable on this firmware/CFW combination.  The login UI uses this to
// gray out the "Reboot to Hekate" button when not supported.
bool IsRebootToHekateSupported();

} // namespace ul::menu::qdesktop::power
