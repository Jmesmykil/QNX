// qd_DebugObserve.hpp — Safe, read-only system-observation snapshot.
//
// Returns a compact JSON string suitable for the /observe HTTP debug route.
// Every service session is opened and closed internally; the call is safe to
// invoke from the DebugServer worker thread.  Each service block degrades
// gracefully: a failed init emits null/0 for that block and continues.
//
// Guarded by QDESKTOP_MODE + __SWITCH__ so host-side unit-test builds skip it.

#ifdef QDESKTOP_MODE
#pragma once
#include <string>

namespace ul::menu::qdesktop {

// Build a compact JSON snapshot of safe system observables.  Opens/closes each
// service session internally; safe to call from the server thread.
std::string BuildObserveJson();

} // namespace ul::menu::qdesktop
#endif // QDESKTOP_MODE
