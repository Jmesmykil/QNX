// qd_NintendoApps.hpp — Nintendo built-in app launcher table for Q OS uMenu v2.0.
//
// Each NintendoApp entry pairs a display label, a fallback tile colour, and a
// fully-implemented launcher function.  All launchers use libnx library-applet
// wrappers that are safe from AppletType_SystemApplet (uMenu's context).
//
// eShop is NOT in this table: AppletId_LibraryAppletShop (0x14) requires live
// Nintendo account authentication that uMenu cannot provide.  It was evaluated
// and dropped rather than stubbed.
//
// System Settings is NOT a launchable applet on retail: its UI is rendered
// inside qlaunch (which Q OS replaces) and the standalone "set" applet (0x16)
// is devkit-only.  The Settings tile therefore opens the windowed Q OS Settings
// in-place via QdNintendoAppsLayout::on_open_settings_ — it never reboots.
// Album is likewise handled in-window (caps:a browser) because PhotoViewer
// (0x15) is hijacked to hbl on this setup.
#pragma once
#include <pu/Plutonium>
#include <switch.h>           // v1.10.3.10.5 main-thread fix: ul/ul_Types.hpp doesn't exist;
                              // libnx switch.h provides u8/u16/u32/u64/s32/Result.

namespace ul::menu::qdesktop {

/// One row in the Nintendo apps table.
struct NintendoApp {
    const char      *label;     ///< Display label shown on the tile.
    pu::ui::Color    color;     ///< Fallback tile background colour (RGBA8888).
    void           (*launch)(); ///< Launcher; called synchronously on tile tap.
    /// Theme-relative path (no extension) for the tile icon, resolved at
    /// runtime via TryFindLoadImage. nullptr ⇒ tile renders solid-color fallback
    /// only (the launcher table currently uses this for Profile, Keyboard, and
    /// Error Info, which have no shipped EntryIcon PNG).
    const char      *icon_path;
};

/// Total number of entries in kNintendoApps[].
static constexpr size_t kNintendoAppCount = 8;

/// Flat table of all launchers — defined in qd_NintendoApps.cpp.
extern const NintendoApp kNintendoApps[kNintendoAppCount];

// ── Individual launcher declarations (defined in qd_NintendoApps.cpp) ─────────

/// Opens the system Album applet (photo/video viewer).
void LaunchAlbum();

/// Opens the Controller Support applet (pair/test controllers).
void LaunchControllers();

/// Opens the Mii Editor applet.
void LaunchMiiEditor();

/// Opens the Profile Selector applet (select or create user account).
void LaunchProfileSelector();

/// Opens the built-in web browser applet pointed at nintendo.com.
void LaunchWebBrowser();

/// Safety fallback for the Settings tile — opens Q OS Settings in-place.
/// (Normally the tile is intercepted by QdNintendoAppsLayout::on_open_settings_;
/// stock System Settings is not launchable on retail — see the .cpp.)
void LaunchSystemSettings();

/// Opens the software keyboard applet for a quick typing demo.
void LaunchKeyboardDemo();

/// Opens the Error Info applet for diagnostics (errorSystemCreate path).
void LaunchErrorDisplay();

} // namespace ul::menu::qdesktop
