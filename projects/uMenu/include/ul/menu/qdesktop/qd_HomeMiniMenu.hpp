// qd_HomeMiniMenu.hpp — dev / debug toggles surfaced via the Home button on
// the qdesktop Main layout. Cycle D5 (SP4.12).
//
// Trigger: TWO Home-button presses within ~600 ms while on the qdesktop main
// layout. A single short press is a deliberate no-op in QDESKTOP_MODE per the
// user's mandate ("home button doesnt need to do anything except when you
// hold it the mini menu should pop up"). True press-and-hold detection
// requires a corresponding hook in uSystem (DetectLongPressingHomeButton →
// new SMI message); double-press is the equivalent gesture that we can
// implement entirely inside uMenu without touching the cross-binary protocol.
//
// What lives here:
//   - Atomic toggle flags consumed by the rest of qdesktop:
//       g_dev_wallpaper_enabled    -> qd_Wallpaper::OnRender early-return
//       g_dev_brand_fade_enabled   -> ui_MenuApplication::SetBackgroundFade
//                                     uses the cyan→lavender Q glyph texture
//                                     when true; falls back to C5 solid
//                                     panel colour when false
//       g_dev_dock_enabled         -> dock element visibility
//       g_dev_icons_enabled        -> desktop-icon element visibility
//       g_dev_topbar_enabled       -> top-bar visibility
//       g_dev_volume_policy_enabled-> ui_MenuApplication volume callback
//                                     short-circuits when false
//   - ShowDevMenu() — opens the dialog-driven toggle/action menu.
//
// Defaults: every toggle starts true so the desktop renders identically to
// SP4.12's stock visuals out of the box. Toggling is purely additive.
#pragma once
#include <atomic>
#include <pu/Plutonium>

namespace ul::menu::qdesktop {

    // ── Dev toggles (atomic so render threads + dialog thread can race) ────
    // All default true; flipping false hides / disables the corresponding
    // surface without rebuilding.

    extern std::atomic<bool> g_dev_wallpaper_enabled;
    extern std::atomic<bool> g_dev_brand_fade_enabled;
    extern std::atomic<bool> g_dev_dock_enabled;
    extern std::atomic<bool> g_dev_icons_enabled;
    extern std::atomic<bool> g_dev_topbar_enabled;
    extern std::atomic<bool> g_dev_volume_policy_enabled;

    // Open the dev mini-menu modally. Re-entrant via the dialog loop until
    // the user picks "Close" / cancels. Calls ::g_MenuApplication->DisplayDialog
    // internally — must be invoked from the menu-input thread (i.e. from
    // OnHomeButtonPress / a render callback), never from a background thread.
    void ShowDevMenu();

}  // namespace ul::menu::qdesktop
