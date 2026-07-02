// qd_Power.hpp — System power management for Q OS uMenu (v0.21+).
//
// Wraps libnx + Atmosphère extensions for the five user-facing power actions
// the qdesktop login screen exposes: reboot, shutdown, sleep, reboot-to-Hekate,
// and reboot-to-Hekate-UMS.
//
// Reboot / Shutdown / RebootToHekate / RebootToHekateUms are "fire and do not
// return" semantically on the happy path; on hardware the system applet is
// destroyed inside the libnx call.  RebootToHekateUms returns bool so the
// caller can detect the payload-not-found case and show a notification instead
// of silently hanging.  On the host build all functions return immediately.
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

// Reboot to Hekate UMS (USB Mass Storage) mode.
//
// Loads sdmc:/bootloader/payloads/hekate_ums.bin as the reboot payload and
// chains into Hekate via bpcAmsSetRebootPayload + bpcRebootSystem, so Hekate
// boots directly into UMS mode and the SD card appears as a USB drive on the
// host machine.
//
// Fallback: if hekate_ums.bin is absent, retries with
// sdmc:/atmosphere/reboot_payload.bin (plain Hekate boot).
//
// Returns false if neither payload path exists (no reboot is attempted; the
// caller should show a notification).  Returns true and does not return on
// the hardware success path.  Returns false on any IPC failure.
bool RebootToHekateUms();

// Returns true iff UMS reboot is supported on this firmware/CFW combination.
// Delegates to IsRebootToHekateSupported() — both require the bpc:ams IPC
// extension from Atmosphère.
bool IsRebootToHekateUmsSupported();

} // namespace ul::menu::qdesktop::power
