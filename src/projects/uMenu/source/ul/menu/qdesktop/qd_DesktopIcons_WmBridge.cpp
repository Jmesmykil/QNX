// qd_DesktopIcons_WmBridge.cpp — Window-manager opener methods for QdDesktopIconsElement.
// Split from qd_DesktopIcons.cpp to keep that file focused on rendering and input.
// Same pattern as qd_AutoFolders.cpp (classification side-table) and
// qd_DesktopIcons_WmBridge.cpp (window-opener side-table).
//
// Each method:
//   1. Constructs the layout element (QdVaultLayout::New() etc.).
//   2. Calls SetContentSize(DEFAULT_WIN_W, DEFAULT_WIN_H - TITLEBAR_H) on the element.
//   3. Takes the next stagger position from wm_.
//   4. Creates a QdWindow via QdWindow::New(title, elem, x, y, w, h).
//   5. Calls wm_.OpenWindow(win) which wires on_close_requested and on_minimize_requested.

#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_SettingsLayout.hpp>
#include <ul/menu/qdesktop/qd_MonitorLayout.hpp>
#include <ul/menu/qdesktop/qd_AboutLayout.hpp>
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>

namespace ul::menu::qdesktop {

// ── Helpers ───────────────────────────────────────────────────────────────────

static constexpr s32 kWinW = static_cast<s32>(DEFAULT_WIN_W);
static constexpr s32 kWinH = static_cast<s32>(DEFAULT_WIN_H);
static constexpr s32 kContentH = kWinH - static_cast<s32>(TITLEBAR_H);  // 438 px

// ── OpenVaultWindow ───────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenVaultWindow() {
    auto elem = QdVaultLayout::New(theme_);
    elem->SetContentSize(kWinW, kContentH);
    elem->Navigate("sdmc:/switch/");

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Files", std::move(elem), wx, wy, kWinW, kWinH);
    wm_.OpenWindow(std::move(win));
}

// ── OpenSettingsWindow ────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenSettingsWindow() {
    auto elem = QdSettingsElement::New(theme_);
    elem->SetContentSize(kWinW, kContentH);

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Settings", std::move(elem), wx, wy, kWinW, kWinH);
    wm_.OpenWindow(std::move(win));
}

// ── OpenMonitorWindow ─────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenMonitorWindow() {
    auto elem = QdMonitorLayout::New(theme_);
    elem->SetContentSize(kWinW, kContentH);

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Monitor", std::move(elem), wx, wy, kWinW, kWinH);
    wm_.OpenWindow(std::move(win));
}

// ── OpenAboutWindow ───────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenAboutWindow() {
    auto elem = QdAboutElement::New(theme_);
    elem->SetContentSize(kWinW, kContentH);
    elem->Refresh();

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("About Q OS", std::move(elem), wx, wy, kWinW, kWinH);
    wm_.OpenWindow(std::move(win));
}

} // namespace ul::menu::qdesktop
