#pragma once
#include <cstdint>
#include <cstddef>

// qos_smi_client — thin wrapper over ul::menu::smi + ul::menu::smi::sf
// Provides a plain-C interface for the v0.7 deko3d entry point.
// No SDL2, no Plutonium, no std::string in the API surface.
//
// Protocol:  SMI! magic 0x21494D53
// Transport: libnx AppletStorage (ILibraryAppletSession, 0x8000-byte chunks)
// Peer:      uSystem TID 0x0100000000001000

namespace qos::smi {

// One-time init. Opens the SMI private-service channel to uSystem.
// Must be called after appletInitialize(). Returns true on success.
bool Init();

// Tear down. Stops the message-receiver thread and closes the service handle.
void Shutdown();

// Tell uSystem to launch an installed application by title-id.
// On success uSystem suspends uMenu and starts the application;
// main should exit cleanly afterwards.
// Returns true if uSystem accepted the command.
bool LaunchApplication(uint64_t application_id);

// Tell uSystem to launch a homebrew NRO by path (sdmc:/... style, null-terminated).
// Same lifecycle semantics as LaunchApplication.
bool LaunchHomebrewByPath(const char *nro_path);

// Pump any pending menu messages from uSystem.
// Calls the registered message callbacks for every message waiting in the
// private-service queue. Non-blocking — returns immediately if the queue
// is empty. Returns true if at least one message was dispatched.
bool ProcessPendingMessages();

// Notify uSystem that uMenu's layout is ready (call after first rendered frame).
// Implemented as UpdateMenuPaths with empty paths — that is the v0.6.x signal
// for "menu is live and accepting layout updates from uSystem".
// Returns true if the command was accepted.
bool NotifyLayoutReady();

// Ask uSystem for the current start mode.
// Reads the MenuStartMode that was pushed into the applet input storage at boot.
// Returns the numeric MenuStartMode value:
//   0 = Invalid, 1 = StartupMenu, 2 = StartupMenuPostBoot,
//   3 = MainMenu, 4 = SettingsMenu
// Returns -1 on error.
int QueryStartMode();

} // namespace qos::smi
