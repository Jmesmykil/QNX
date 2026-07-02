// qos_fw_compat.hpp — firmware/applet pool compatibility probe
// uMenu v0.7.0-beta5 — BUG 2 fix
//
// Queries Horizon firmware version and applet pool memory sizing via
// hosversionGet() + svcGetInfo so the rest of the menu can make
// informed decisions about allocation budgets.
//
// Usage:
//   Call Init() once, early in __appInit (after appletInitialize).
//   AppletPoolHeadroom() is valid only after Init() returns true.
#pragma once

#include <cstdint>

namespace qos {
namespace fw_compat {

// Call once after appletInitialize().
// Logs results to sdmc:/switch/qos-menu-init.log via V7LOG / TELEM.
// Returns true on success, false if svcGetInfo failed (non-fatal; headroom
// will return 0 in that case).
bool Init();

// Returns (total applet pool bytes) - (used applet pool bytes).
// Returns 0 if Init() has not been called or failed.
uint64_t AppletPoolHeadroom();

} // namespace fw_compat
} // namespace qos
