# nxlink Auto-Launch Implementation — 2026-05-06

Closes the gap identified in `QOS-NRO-SURFACE-AUDIT-20260506.md`: uMenu's
nxlink netloader now mirrors hbmenu's loadnro behaviour, dispatching the
just-received NRO via SMI immediately after the file is committed to SD.

## Modified region

`src/projects/uMenu/source/ul/menu/qdesktop/qd_NxlinkServer.cpp`
- New include line 11: `#include <ul/menu/smi/smi_Commands.hpp>`
- Replaced cmdline-discard block (formerly lines 572-595) with capture +
  auto-launch logic spanning lines 573-643.

## Captured-argv plumbing

Variable: `std::string argv_buf` — function-local, lifetime ends when
`ReceiveOne()` returns. Constructed via `argv_buf.resize(cmdlen)` then
`recvall(client_fd, argv_buf.data(), cmdlen)`. Embedded NULs in the
hbmenu wire format (multiple NUL-separated argv strings) are preserved
because `std::string` is length-counted and `util::CopyToStringBuffer`
downstream uses `memcpy` (not strcpy). Safety cap: 1024 bytes
(constant `kMaxCmdlineLen`); oversize buffers are drained for protocol
correctness and auto-launch is skipped.

## Launch invocation

```
smi::LaunchHomebrewLibraryApplet(std::string(dest_path), argv_buf);
```

`dest_path` is the `sdmc:/switch/<basename>.nro` written in step 5.
Fired AFTER `g_nxlink_scan_pending` is set and `State::Done` is published,
so the rescan signal lands even if uSystem rejects the launch. Failure
is logged at WARN; the NRO file remains on SD for manual launch.

## Build

`cd src && make umenu` — uMenu.nso built clean.
Trailing post-build asset-copy step errors (missing romfs PNGs) are
pre-existing and unrelated to this change.

uMenu.nso md5: `4a63903f576938298358d45f9dd0eaa6` (7,095,355 bytes,
2026-05-06 08:56:07).

## Config flag

Hardcoded ALWAYS-launch — matches hbmenu's default. No existing
`auto_launch_after_nxlink` flag in qd_NxlinkServer/config; comment in
source flags the gate point if a "netload-then-return" mode is added.

No SMI protocol or smi_Commands changes; only the producer side.
