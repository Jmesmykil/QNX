// qd_NintendoApps.cpp — Nintendo built-in app launcher implementations.
//
// v2.3.6.1 architecture fix:
// uMenu runs as a *library applet* of uSystem (which is the SystemApplet).
// Heavy full-screen library applets (Album, Mii, Profile, Web) require a
// foreground swap.  Direct libnx calls from uMenu hang because the kernel
// won't grant the swap from a nested library-applet context.
//
// Upstream uLaunch's tested pattern (see ui_Common.cpp::ShowAlbum,
// ShowMiiEdit, ShowUserPage, ShowWebPage):
//
//     g_MenuApplication->FadeOutToLibraryApplet(AppletId);  // 1. yield uMenu
//     UL_RC_ASSERT(smi::OpenXxx());                          // 2. send SMI
//     g_MenuApplication->Finalize();                         // 3. tear down LA state
//
// Without all three steps the action queue piles up forever — uSystem can't
// run the action because la::IsActive() never goes false.  Confirmed against
// /Volumes/SWITCH SD/ulaunch/log_uSystem.log: the bare-SMI v2.3.6 build
// emitted infinite "Failed to handle action in queue (type=N, will retry)".
//
// Auxiliary applets that DO work from library-applet context (no foreground
// swap needed): swkbd, error.  Those keep their direct libnx calls.
//
// Controllers (`hidLaShowControllerSupportForSystem`) works from
// library-applet context per libnx docs — keep direct path.
#include <ul/menu/qdesktop/qd_NintendoApps.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_Common.hpp>          // ShowSettingsMenu — in-place settings fallback
#include <ul/ul_Result.hpp>
// libnx applet headers (confirmed paths from /opt/devkitpro/libnx/include)
#include <switch/applets/hid_la.h>
#include <switch/applets/swkbd.h>
#include <switch/applets/error.h>

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Album ─────────────────────────────────────────────────────────────────────
// Album is the AppletId_LibraryAppletPhotoViewer (0x15) which routes to
// program ID 0x010000000000100D.  AMS has a compiled-in default at
// libstratosphere cfg_override.board.nintendo_nx.inc:35-41 that hijacks
// PhotoViewer launches to hbl.nsp with override_by_default=true.
//
// Two attempts to disarm that hijack via /atmosphere/config/override_config.ini
// (full and minimal forms) both broke the AMS reboot trampoline that
// qd_Power.cpp::RebootToHekate depends on — three independent hardware
// confirmations of the same regression on this setup.  Cause unknown
// (probably a parser-side resource interaction inside loader/exosphere),
// but reproducible enough to retire that path permanently.
//
// The Switch hardware photo button still launches the real Album applet
// directly.  This tile becomes a soft no-op that explains where to find
// Album natively.

void LaunchAlbum() {
    UL_LOG_INFO("qd_NintendoApps: Album tile tapped — directing user to physical photo button.");
    ErrorSystemConfig c = {};
    errorSystemCreate(&c,
                      "Album",
                      "The Switch console's photo button (the small round button "
                      "above the directional pad / capture button) opens the real "
                      "Album natively.  This tile is reserved for a future Q OS "
                      "photo browser that will run alongside Album without "
                      "interrupting the current uMenu session.");
    errorSystemShow(&c);
}

// ── Controllers ───────────────────────────────────────────────────────────────

void LaunchControllers() {
    HidLaControllerSupportResultInfo result_info = {};
    HidLaControllerSupportArg        arg         = {};
    hidLaCreateControllerSupportArg(&arg);
    const Result rc = hidLaShowControllerSupportForSystem(&result_info, &arg, true);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_NintendoApps: hidLaShowControllerSupportForSystem failed: 0x%08X",
                    static_cast<unsigned>(rc));
    }
}

// ── Mii Editor ───────────────────────────────────────────────────────────────
// 3-step yield+SMI+finalize.  AppletId_LibraryAppletMiiEdit = 0x12.

void LaunchMiiEditor() {
    g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMiiEdit);
    const Result rc = smi::OpenMiiEdit();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_NintendoApps: smi::OpenMiiEdit failed: 0x%08X",
                    static_cast<unsigned>(rc));
        return;
    }
    g_MenuApplication->Finalize();
}

// ── Profile Selector ─────────────────────────────────────────────────────────
// 3-step yield+SMI+finalize.  AppletId_LibraryAppletMyPage = 0x1A.
// uSystem returns the selected user via the SystemStatus dispatch path; this
// caller doesn't need the UID right now (the desktop already knows the
// active user).

void LaunchProfileSelector() {
    g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMyPage);
    const Result rc = smi::OpenUserPage();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_NintendoApps: smi::OpenUserPage failed: 0x%08X",
                    static_cast<unsigned>(rc));
        return;
    }
    g_MenuApplication->Finalize();
}

// ── Web Browser ──────────────────────────────────────────────────────────────
// 3-step yield+SMI+finalize.  AppletId_LibraryAppletWeb = 0x13.
// smi::OpenWebPage signature requires a 500-byte URL buffer reference
// (length checked at the SMI boundary); pad nintendo.com into the
// fixed-size array.

void LaunchWebBrowser() {
    char url[500] = "https://www.nintendo.com";
    g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletWeb);
    const Result rc = smi::OpenWebPage(url);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_NintendoApps: smi::OpenWebPage failed: 0x%08X",
                    static_cast<unsigned>(rc));
        return;
    }
    g_MenuApplication->Finalize();
}

// ── System Settings ───────────────────────────────────────────────────────────
// Stock Nintendo System Settings is rendered INSIDE qlaunch, which Q OS replaces
// wholesale, and the standalone "set" library applet (AppletId_LibraryAppletSet,
// 0x16) is devkit-only — libnx documents it as "not present on retail devices"
// and it is the ONLY applet so flagged (MiiEdit/Album/Cabinet/MyPage, which we
// DO launch, are not).  So on a retail console there is nothing to launch
// in-place: the real System Settings UI shipped as part of the qlaunch binary
// we replaced.  Upstream uLaunch reaches the same conclusion — its "Settings"
// entry opens uLaunch's own settings menu, never a Nintendo applet.
//
// Rebooting into stock qlaunch just to reach Settings is unacceptable UX, so the
// Nintendo-apps grid routes the Settings tile to the windowed Q OS Settings
// in-place via QdNintendoAppsLayout::on_open_settings_ (wired by the host in
// OpenNintendoAppsWindow).  This free function is only a safety fallback if that
// hook is ever unset; it opens the in-place settings menu and NEVER reboots.
void LaunchSystemSettings() {
    UL_LOG_INFO("qd_NintendoApps: System Settings tile fallback -> in-place Q OS Settings.");
    ::ul::menu::ui::ShowSettingsMenu();
}

// ── Software Keyboard (demo) ──────────────────────────────────────────────────

void LaunchKeyboardDemo() {
    SwkbdConfig c = {};
    const Result create_rc = swkbdCreate(&c, 0);
    if (R_FAILED(create_rc)) {
        UL_LOG_WARN("qd_NintendoApps: swkbdCreate failed: 0x%08X",
                    static_cast<unsigned>(create_rc));
        return;
    }
    swkbdConfigMakePresetDefault(&c);
    char buf[256] = {};
    const Result show_rc = swkbdShow(&c, buf, sizeof(buf));
    if (R_FAILED(show_rc)) {
        UL_LOG_WARN("qd_NintendoApps: swkbdShow failed: 0x%08X",
                    static_cast<unsigned>(show_rc));
    }
    swkbdClose(&c);
}

// ── Error Info Display ────────────────────────────────────────────────────────
// Uses errorSystemCreate (safe from any applet type) rather than
// errorApplicationCreate, which is restricted to [10.0.0+] Application context.

void LaunchErrorDisplay() {
    ErrorSystemConfig c = {};
    errorSystemCreate(&c,
                      "Q OS Debug Display",
                      "This window is the Q OS error info applet.\n"
                      "No error is active.");
    errorSystemShow(&c);
}

// ── App table ─────────────────────────────────────────────────────────────────

// icon_path is theme-relative (no extension) and resolved via
// TryFindLoadImage / TryGetActiveThemeResource at first paint.  Three slots
// (Profile, Keyboard, Error Info) have no shipped EntryIcon PNG and pass
// nullptr — those tiles fall through to the original solid-color render path.
// ui/Main/EntryIcon/{Album,Controllers,MiiEdit,WebBrowser,Settings}.png are
// verified-present in src/default-theme/ui/Main/EntryIcon/.
const NintendoApp kNintendoApps[kNintendoAppCount] = {
    { "Album",       { 0x22u, 0x8Bu, 0xE4u, 0xFFu }, LaunchAlbum,           "ui/Main/EntryIcon/Album"       },
    { "Controllers", { 0x43u, 0xB5u, 0x49u, 0xFFu }, LaunchControllers,     "ui/Main/EntryIcon/Controllers" },
    { "Mii Editor",  { 0xFFu, 0xA5u, 0x00u, 0xFFu }, LaunchMiiEditor,       "ui/Main/EntryIcon/MiiEdit"     },
    { "Profile",     { 0x9Bu, 0x59u, 0xB6u, 0xFFu }, LaunchProfileSelector, nullptr                          },
    { "Web Browser", { 0x1Au, 0xBCu, 0x9Cu, 0xFFu }, LaunchWebBrowser,      "ui/Main/EntryIcon/WebBrowser"  },
    { "Settings",    { 0x7Fu, 0x8Cu, 0x8Du, 0xFFu }, LaunchSystemSettings,  "ui/Main/EntryIcon/Settings"    },
    { "Keyboard",    { 0xE7u, 0x4Cu, 0x3Cu, 0xFFu }, LaunchKeyboardDemo,    nullptr                          },
    { "Error Info",  { 0xC0u, 0x39u, 0x2Bu, 0xFFu }, LaunchErrorDisplay,    nullptr                          },
};

} // namespace ul::menu::qdesktop
