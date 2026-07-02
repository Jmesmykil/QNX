// qd_ShellCommands.hpp — Remote shell command table for QdRemoteShellServer.
//
// Each command handler receives the connected client fd, an argv-style token
// array (tokens[0] = command name), and the token count.  The handler writes
// its response directly to client_fd and returns.  No return value — all
// errors are written to the client as human-readable text.
//
// Command table (alphabetical):
//   flush    — call dev::FlushAllChannels()
//   help     — list all commands
//   launch   — smi::LaunchHomebrewLibraryApplet for an NRO path
//   log      — last N lines of sdmc:/qos-shell/logs/uMenu.0.log (default 20)
//   nrolist  — JSON array of NRO basenames under sdmc:/switch/
//   ping     — reply "pong"
//   quit     — signal the session loop to close the connection
//   status   — IP, uptime, battery %, free RAM
#pragma once

#ifdef QDESKTOP_MODE

namespace ul::menu::qdesktop::shell {

// Maximum tokens per command line (command + args).
static constexpr int kMaxTokens = 8;

// Entry point: tokenise `line` and dispatch to the matching handler.
// Writes the full response (including trailing newline) to client_fd.
// Returns true for all commands except "quit"; returns false for "quit" so
// ServeClient() knows to end the session.
bool DispatchCommand(int client_fd, const char *line);

} // namespace ul::menu::qdesktop::shell

#endif // QDESKTOP_MODE
