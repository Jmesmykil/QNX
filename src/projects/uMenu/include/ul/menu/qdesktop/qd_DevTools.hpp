// qd_DevTools.hpp — Developer-mode toggles for Q OS uMenu (v0.21+).
//
// Surfaces three runtime-toggleable diagnostic channels from the qdesktop
// login screen:
//   - nxlink: redirect stdout to a host listener over UDP (libnx).
//   - USB serial: redirect stdout to USB-CDC for serial-cable debugging.
//   - Telemetry flush: force a synchronous fsync of all open log channels.
//
// The toggles are idempotent: enabling an already-active channel is a no-op
// that returns true.  Disabling an inactive channel is a no-op that returns
// silently.  All implementations log every state transition via
// UL_TEL_INFO(Cat::Generic, ...) so the path of activations is reconstructable
// from /qos-shell/logs/uMenu.0.log after a session.
//
// Usage from the login UI:
//   if (user_clicked_nxlink_toggle) {
//       if (ul::menu::qdesktop::dev::IsNxlinkActive()) {
//           ul::menu::qdesktop::dev::DisableNxlink();
//       } else {
//           const bool ok = ul::menu::qdesktop::dev::TryEnableNxlink();
//           // surface ok/!ok to the user via toast
//       }
//   }
#pragma once

namespace ul::menu::qdesktop::dev {

// ── nxlink ──────────────────────────────────────────────────────────────────

// True iff a nxlink session is currently active and stdout is being mirrored
// to the host listener.
bool IsNxlinkActive();

// Discover a nxlink host on the LAN (libnx nxlinkConnectToHost broadcast),
// dup stdout to the resulting socket.  Returns true on success.  On any
// failure (no host found, socket error) the channel is left inactive and
// the function returns false.
bool TryEnableNxlink();

// Close the nxlink socket and revert stdout.  Safe to call when inactive.
void DisableNxlink();

// ── USB serial (CDC ACM) ────────────────────────────────────────────────────

// True iff USB serial output is currently active.
bool IsUsbSerialActive();

// Initialise libnx usbCommsInitializeEx with a CDC profile and dup stdout
// to the USB endpoint.  Returns true on success.
bool TryEnableUsbSerial();

// Tear down USB serial and revert stdout.  Safe to call when inactive.
void DisableUsbSerial();

// ── Telemetry flush ─────────────────────────────────────────────────────────

// Force a synchronous flush of every open log channel (RingFile + nxlink +
// USB serial), then fdatasync the SD-card ring.  Use this from the login
// screen's "Flush logs" button before unplugging the SD card.
void FlushAllChannels();

} // namespace ul::menu::qdesktop::dev
