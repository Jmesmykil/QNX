// qd_DebugCapsScreen.hpp — caps:sc screen-capture helper (Q OS uMenu, QDESKTOP_MODE only).
//
// Captures the current composed screen as a JPEG via the Horizon caps:sc service
// (capsscCaptureJpegScreenShot, [9.0.0+]).  This replaces the fragile
// SDL_RenderReadPixels path in qd_DebugServer for the screenshot route: it is
// callable from any thread, needs no render-thread synchronisation, and produces
// a JPEG directly — no surface/encode step.
//
// Public interface used by qd_DebugServer (and any other caller):
//
//   std::vector<u8> jpeg;
//   if (ul::menu::qdesktop::CaptureScreenJpeg(jpeg)) {
//       // jpeg holds the JPEG bytes
//   }
//
// Note: capsscCaptureJpegScreenShot is [9.0.0+] and (before [10.0.0]) required
// debug mode.  On unsupported firmware the call returns an error rc and the
// function returns false gracefully.

#ifdef QDESKTOP_MODE
#pragma once
#include <switch.h>
#include <vector>
#include <cstdint>

namespace ul::menu::qdesktop {

// Capture the live screen as a JPEG into `out`. Returns true on success.
// Opens+closes caps:sc internally; thread-safe to call from the server thread.
bool CaptureScreenJpeg(std::vector<u8> &out);

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
