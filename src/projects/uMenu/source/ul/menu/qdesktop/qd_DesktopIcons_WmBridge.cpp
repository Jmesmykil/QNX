// qd_DesktopIcons_WmBridge.cpp — Window-manager opener methods for QdDesktopIconsElement.
// Split from qd_DesktopIcons.cpp to keep that file focused on rendering and input.
// Same pattern as qd_AutoFolders.cpp (classification side-table) and
// qd_DesktopIcons_WmBridge.cpp (window-opener side-table).
//
// Each method:
//   1. Constructs the layout element (QdVaultLayout::New() etc.).
//   2. Calls SetContentSize(DEFAULT_WIN_W, kContentH) on the element.
//      kContentH = DEFAULT_WIN_H - TITLEBAR_H - kCornerBtn - 1 (viewport after clip fix).
//   3. Takes the next stagger position from wm_.
//   4. Creates a QdWindow via QdWindow::New(title, elem, x, y, w, h).
//   5. Wires on_minimize_begin_ to store the pointer in pending_ctx_minimize_win_,
//      which OnRender drains after wm_.RenderAll(drawer) where Renderer::Ref is live.
//   6. Calls wm_.OpenWindow(win) which wires on_close_requested and on_minimize_requested.

#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/ul_Result.hpp>   // W9-FIX: UL_LOG_INFO for singleton-guard log lines
#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_SettingsLayout.hpp>
#include <ul/menu/qdesktop/qd_MonitorLayout.hpp>
#include <ul/menu/qdesktop/qd_AboutLayout.hpp>
#include <ul/menu/qdesktop/qd_Window.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/qdesktop/qd_TaskManagerLayout.hpp>
#include <ul/menu/qdesktop/qd_NintendoAppsLayout.hpp>
#include <ul/menu/qdesktop/qd_FolderLaunchpadElement.hpp>
#include <ul/menu/qdesktop/qd_SaveEditorLayout.hpp>  // W11-SAVE
#include <ul/menu/qdesktop/qd_CheatsLayout.hpp>      // W12-CHEATS
#include <ul/menu/qdesktop/qd_ModsLayout.hpp>        // B3.1-MODS

namespace ul::menu::qdesktop {

// ── Helpers ───────────────────────────────────────────────────────────────────

static constexpr s32 kWinW = static_cast<s32>(DEFAULT_WIN_W);
static constexpr s32 kWinH = static_cast<s32>(DEFAULT_WIN_H);
// v1.10.3.6: subtract BOTTOM_BAR_H (42 px) — the dedicated bottom chrome bar that
// replaced the kCornerBtn (48 px) bottom reservation.  BL/BR buttons now live in
// this chrome band, not in raw window corners.
// kWinH=480, TITLEBAR_H=42, BOTTOM_BAR_H=42 → 480 - 42 - 42 - 1 = 395 px
static constexpr s32 kContentH = kWinH
    - static_cast<s32>(TITLEBAR_H)
    - static_cast<s32>(BOTTOM_BAR_H)
    - 1;

// v2.0.2: folder windows host a windowed launchpad whose natural canvas is
// 1920×1080.
//
// v2.0.3.4: aspect-matched dimensions so the windowed launchpad fills the
// content viewport with NO horizontal centering margin (creator HW feedback:
// "weird random extra space to the right").  At 1920×1080 natural the content
// area must be 16:9 too.  Subtracting 1 px clip + 42 px titlebar + 42 px bottom
// bar = 84 px chrome on height and 2 px on width:
//   content_h = win_h - 84,  content_w = win_w - 2
//   want content_w / content_h = 16/9  →  win_w - 2 = (win_h - 84) * 16/9
// Picking win_h = 720 (Switch handheld height) gives content_h = 636 →
// content_w = 1131 → win_w = 1133.  Uniform scale = 1131/1920 = 0.589 on both
// axes, so visual_w = 1131 = viewport, no centering margin.  At LP_ICON_W=132
// the visual icon size is 132 × 0.589 = 78 px (up from 61 px in v2.0.3.3).
static constexpr s32 kFolderWinW = 1133;
static constexpr s32 kFolderWinH = 720;

// ── Singleton helper ──────────────────────────────────────────────────────────
// W9-FIX Bug 1: shared singleton-focus helper for all Open*Window() methods.
// - If the window is already open → BringToFront (focuses it).
// - If the window is minimized   → RestoreWindow (brings it back from the dock).
// Returns true if an existing instance was found (caller should skip creating a new one).
static bool FocusExistingWindow(QdWindowManager& wm, const std::string& title) {
    auto [open_win, min_entry] = wm.FindWindowByTitle(title);
    if (open_win) {
        wm.BringToFront(open_win);
        UL_LOG_INFO("qdesktop: %s already open — focused existing", title.c_str());
        return true;
    }
    if (min_entry) {
        wm.RestoreWindow(min_entry);
        UL_LOG_INFO("qdesktop: %s minimized — restored from dock", title.c_str());
        return true;
    }
    return false;
}

// ── OpenVaultWindow ───────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenVaultWindow() {
    // W9-FIX Bug 1: singleton guard — focus or restore existing "Files" window.
    if (FocusExistingWindow(wm_, "Files")) { return; }

    QdAudio::Play(DesktopSfxEvent::VaultOpen);
    auto elem = QdVaultLayout::New(theme_);
    // W11-SAVE Part 2: wire the "Edit Pokémon save" ctx menu callback so
    // QdVaultLayout can open the save editor without a direct dependency on
    // QdDesktopIconsElement.
    elem->on_open_save_editor = [this]() { OpenSaveEditorWindow(); };

    // W12-CHEATS: wire the "View Cheats" ctx menu callback.
    elem->on_open_cheats = [this](u64 app_id) { OpenCheatsWindow(app_id); };
    // B3.1-MODS: wire the "View Mods" ctx menu callback.
    elem->on_open_mods = [this](u64 app_id) { OpenModsWindow(app_id); };
    elem->Navigate("sdmc:/switch/");

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Files", std::move(elem), wx, wy, kWinW, kWinH);
    // v2.9.10 — bottom-bar hint (creator directive: instructions belong INSIDE
    // the window chrome, not on the screen-level InputBar outside).
    win->SetHintText("A: open  : back  Y: view  ZL: side  B: close");
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenVaultWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenSettingsWindow ────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenSettingsWindow() {
    // W9-FIX Bug 1: singleton guard.
    if (FocusExistingWindow(wm_, "Settings")) { return; }

    QdAudio::Play(DesktopSfxEvent::SettingsOpen);
    auto elem = QdSettingsElement::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move for on_tick capture
    elem->Refresh();

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Settings", std::move(elem), wx, wy, kWinW, kWinH);
    // v1.10.3.8 SCOPE 1: scale logical content canvas (kNaturalH=600) to fill the
    // viewport (395 px) so all Settings rows are always visible without scrolling.
    // Wire on_tick so Settings battery / clock / network refresh every 60 frames
    // (~1 s at 60 fps). Without this the data freezes at first paint — the bug
    // creator reported on v1.10.2.1 ("None of the information is actually being
    // populated"). QdWindow's PollEvent calls on_tick periodically when state == Normal.
    win->on_tick = [elem_raw]() { elem_raw->Refresh(); };
    // v2.9.10 — bottom-bar hint.
    win->SetHintText("A: edit  : back  B: close");
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenSettingsWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenMonitorWindow ─────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenMonitorWindow() {
    // W9-FIX Bug 1: singleton guard — the renewed OOM cause.
    // Two Monitor windows means two clkrst sessions, two thermal polling
    // loops, two per-tile texture caches → cumulative IPC + GPU heap pressure.
    if (FocusExistingWindow(wm_, "Monitor")) { return; }

    QdAudio::Play(DesktopSfxEvent::MonitorOpen);
    auto elem = QdMonitorLayout::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move for owner_window_ wiring

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Monitor", std::move(elem), wx, wy, kWinW, kWinH);
    // v1.10.3.8 SCOPE 1: scale logical content canvas (kNaturalH=900) to fill the
    // viewport (395 px) so all Monitor stats rows are always visible without scrolling.
    // Wire on_tick so Monitor stats refresh every kTickRefreshHz frames (~1 s at 60 fps)
    // via Refresh() — which is gated by WindowState::Normal in QdWindow::PollEvent.
    // Without this, RefreshStats() was called inside OnRender (lines now removed),
    // which fired during MinimizeWindow's snapshot capture and crashed Atmosphère.
    win->on_tick = [elem_raw]() { elem_raw->Refresh(); };
    // v2.9.10 — bottom-bar hint.
    win->SetHintText(": scroll  B: close");
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenMonitorWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenAboutWindow ───────────────────────────────────────────────────────────

void QdDesktopIconsElement::OpenAboutWindow() {
    // W9-FIX Bug 1: singleton guard.
    if (FocusExistingWindow(wm_, "About Q OS")) { return; }

    auto elem = QdAboutElement::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move for on_tick capture
    elem->Refresh();

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("About Q OS", std::move(elem), wx, wy, kWinW, kWinH);
    // v1.10.3.8 SCOPE 1: scale logical content canvas (kNaturalH=896) to fill the
    // viewport (395 px) so the full About page is always visible without scrolling.
    // About content is mostly static but the version macro + uptime field benefit
    // from periodic refresh — keep on_tick parallel to Settings for consistency.
    win->on_tick = [elem_raw]() { elem_raw->Refresh(); };
    // v2.9.10 — bottom-bar hint.
    win->SetHintText(": scroll  B: close");
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenAboutWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenFolderWindow ──────────────────────────────────────────────────────────
// v2.0.1: Each desktop folder tile opens an icon-grid launchpad filtered to
// that category.  QdFolderLaunchpadElement wraps QdLaunchpadElement (which is
// not a QdContentElement) so QdWindow can host it.
//
// Mapping from DesktopFolderId (folder_idx) → AutoFolderIdx filter:
//   Games(0)     → NxGames   — Nintendo-titled and sideloaded NX games
//   Emulators(1) → Homebrew  — AutoFolderIdx has no separate Emulators bucket;
//   Tools(2)     → Homebrew    both map to the Homebrew/HBL bucket
//   System(3)    → System    — Hekate, sysmodule-adjacent, Switch system tools
//   Q OS(4)      → Builtin   — Q OS built-in tiles
//   Other(5)     → None      — show all items (catch-all / unclassified)
//
// QdFolderLaunchpadElement is lazy: it calls lp_->Open(this) on the first
// OnRender and applies SetFolderFilter(category) immediately after, so the
// grid is pre-filtered before the first frame paints.

void QdDesktopIconsElement::OpenFolderWindow(size_t folder_idx) {
    // Folder metadata table — parallel to kDesktopFolders[] in qd_DesktopIcons.cpp.
    // 6 entries ordered by DesktopFolderId (Games=0 … Other=5).
    struct FolderMeta {
        const char    *title;
        AutoFolderIdx  filter;
    };
    static const FolderMeta kFolderMeta[] = {
        { "Games",     AutoFolderIdx::NxGames  },   // 0
        { "Emulators", AutoFolderIdx::Homebrew },   // 1 — no Emulators bucket in AutoFolderIdx
        { "Tools",     AutoFolderIdx::Homebrew },   // 2 — no Tools bucket; shares Homebrew
        { "System",    AutoFolderIdx::System   },   // 3
        { "Q OS",      AutoFolderIdx::Builtin  },   // 4
        { "Other",     AutoFolderIdx::None     },   // 5 — show all
    };
    static constexpr size_t kFolderMetaCount =
        sizeof(kFolderMeta) / sizeof(kFolderMeta[0]);

    if (folder_idx >= kFolderMetaCount) {
        return;  // guard: caller must pass a valid index
    }
    const FolderMeta &meta = kFolderMeta[folder_idx];

    QdAudio::Play(DesktopSfxEvent::FolderOpen);
    // QdFolderLaunchpadElement takes a non-owning pointer to this element; it
    // must not outlive us.  The window manager keeps the window alive only while
    // this QdDesktopIconsElement is live, so the lifetime contract holds.
    auto elem = QdFolderLaunchpadElement::New(theme_, this, meta.filter);

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kFolderWinW, kFolderWinH, wx, wy);

    // No SetContentSize call: QdFolderLaunchpadElement::GetNaturalW/H returns
    // 1920×1080 and QdWindow applies uniform scale to fit the viewport.
    // kFolderWinW/H are sized for icon-grid legibility (see constant comment).
    auto win = QdWindow::New(meta.title, std::move(elem),
                             wx, wy, kFolderWinW, kFolderWinH);
    // v2.0.3.1: route the launchpad's status info into the window's bottom
    // bar (opposite the title at the top), per creator request.  The
    // launchpad's own status line is suppressed in windowed mode (see
    // QdLaunchpadElement::SetWindowedMode), so this is the single source of
    // bottom-bar text for folder windows.  Static hint for now; dynamic
    // counts (per-category) will land in v2.0.3.2 once a status-change
    // callback path between launchpad and host window is wired up.
    // v3.1.3 (BUG-SCROLL): updated hint to surface swipe navigation and ZL ctx menu.
    win->SetHintText("A: launch  \xc2\xb7  ZL: menu  \xc2\xb7  Swipe/L\xe2\x80\x93R: page  \xc2\xb7  B: close");
    win->on_minimize_begin_ = [this, folder_idx](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this, folder_idx]() { OpenFolderWindow(folder_idx); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenTaskManagerWindow ─────────────────────────────────────────────────────
// v1.10.3.11: Task Manager as an independent floating window.
// on_tick wires Refresh() every kTickRefreshHz frames (gated by QdWindow::PollEvent
// to WindowState::Normal only — no libnx calls during minimize snapshot capture).

void QdDesktopIconsElement::OpenTaskManagerWindow() {
    // W9-FIX Bug 1: singleton guard.
    if (FocusExistingWindow(wm_, "Tasks")) { return; }

    auto elem = QdTaskManagerElement::New(theme_, wm_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move for on_tick capture

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Tasks", std::move(elem), wx, wy, kWinW, kWinH);
    // v2.0.4.1: route Task Manager hint into the QdWindow bottom bar (between
    // the BL/BR corner buttons), opposite the title.  Same pattern as folder
    // launchpad windows.  Without this the hint paints inside the content
    // area and overlaps the row list / empty-state text.
    win->SetHintText("ZL: actions  ·  Focus: bring to front  ·  B: close");
    win->on_tick = [elem_raw]() { elem_raw->Refresh(); };
    // v2.0.4.3: eager-refresh once before the window is added so the very
    // first OnRender paints with current rows instead of "No windows open"
    // until the on_tick fires (kTickRefreshHz=60 frames = 1 sec lag).
    elem_raw->Refresh();
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenTaskManagerWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenNintendoAppsWindow ────────────────────────────────────────────────────
// v2.0: opens the Nintendo built-in apps 4×2 tile grid as an independent floating
// window.  No on_tick: launchers are synchronous (applet exits before returning)
// so no periodic refresh is needed.

void QdDesktopIconsElement::OpenNintendoAppsWindow() {
    // W9-FIX Bug 1: singleton guard.
    if (FocusExistingWindow(wm_, "Nintendo Apps")) { return; }

    auto elem = QdNintendoAppsLayout::New(theme_);
    auto* elem_raw = elem.get();  // survives std::move — for the hint on_tick
    // The "Settings" tile opens the windowed Q OS Settings in-place (no reboot):
    // stock System Settings lives inside qlaunch (which Q OS replaces) and the
    // standalone "set" applet is devkit-only, so there is nothing to launch.
    elem->SetOnOpenSettings([this]() { OpenSettingsWindow(); });

    s32 wx = 0, wy = 0;
    wm_.TakeStaggerPos(kWinW, kWinH, wx, wy);

    auto win = QdWindow::New("Nintendo Apps", std::move(elem), wx, wy, kWinW, kWinH);
    // Hint flows through the STANDARD chrome status bar; on_tick keeps it in sync
    // with the album sub-mode (Tiles / grid / image) — same mechanism as every
    // other window, no in-content hint strip.
    win->SetHintText(elem_raw->GetBottomHint());
    auto* win_raw = win.get();
    win->on_tick = [elem_raw, win_raw]() {
        win_raw->SetHintText(elem_raw->GetBottomHint());
    };
    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenNintendoAppsWindow(); });
    };
    wm_.OpenWindow(std::move(win));
}

// ── OpenSaveEditorWindow ──────────────────────────────────────────────────────
// W11-SAVE Parts 1 + 4: singleton-guarded opener for the Pokémon save editor.
//
// Window is 960×600 per the delivery spec; this is wider/shorter than the
// default kWinW×kWinH (which targets a generic content viewport) and better
// suits the two-pane save-editor canvas (1280×720 natural → scale 0.75 into
// the 960 viewport).
//
// on_tick polls GetBottomHint() every kTickRefreshHz frames so the chrome hint
// bar updates when the TitlePicker → panel mode transition fires (Part 5).
// No on_tick Refresh() call: the save editor has no live data to refresh.
//
// Singleton guard: mirrors the W9 FocusExistingWindow pattern used by
// OpenMonitorWindow and every other opener in this file.  Two simultaneous
// save-editor windows would double-buffer the same static textures without
// providing any extra utility and would waste ~1 MB of GPU texture heap.

void QdDesktopIconsElement::OpenSaveEditorWindow() {
    // W11-SAVE Part 4: singleton guard — focus or restore existing window.
    if (FocusExistingWindow(wm_, "Save Editor")) { return; }

    auto elem     = QdSaveEditorLayout::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move for on_tick capture

    s32 wx = 0, wy = 0;
    static constexpr s32 kSaveWinW = 960;
    static constexpr s32 kSaveWinH = 600;
    wm_.TakeStaggerPos(kSaveWinW, kSaveWinH, wx, wy);

    auto win = QdWindow::New("Save Editor", std::move(elem), wx, wy,
                             kSaveWinW, kSaveWinH);

    // W11-SAVE Part 5: set initial hint (TitlePicker mode on open).
    win->SetHintText(elem_raw->GetBottomHint());

    // on_tick: update hint bar whenever the navigation mode changes.
    // Captures win pointer directly — QdWindow lifetime is managed by wm_;
    // the lambda is cleared by QdWindowManager::CloseWindow before the window
    // is destroyed, so the raw pointer is always valid while on_tick may fire.
    auto* win_raw = win.get();
    win->on_tick = [elem_raw, win_raw]() {
        win_raw->SetHintText(elem_raw->GetBottomHint());
    };

    win->on_minimize_begin_ = [this](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this]() { OpenSaveEditorWindow(); });
    };
    wm_.OpenWindow(std::move(win));
    UL_LOG_INFO("save-editor: OpenSaveEditorWindow — window opened 960×600");
}

// ── OpenCheatsWindow ──────────────────────────────────────────────────────────
// W12-CHEATS: singleton-guarded opener for the Atmosphère cheat browser.
//
// When app_id == 0, opens in TitleList mode (browse all titles).
// When app_id != 0, calls OpenForTitle(app_id) which jumps directly into the
// CheatList for that title (or falls back to TitleList if no cheat file found).
//
// Window: 960×600 (same viewport as Save Editor — two-pane design fits well).
// on_tick: updates the hint bar as navigation mode changes.
// Singleton guard: mirrors every other opener in this file.

void QdDesktopIconsElement::OpenCheatsWindow(const u64 app_id) {
    if (FocusExistingWindow(wm_, "Cheats")) { return; }

    auto elem      = QdCheatsLayout::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move

    if (app_id != 0) {
        // Navigate directly to that title's cheat list.
        // OpenForTitle() can be called before first render.
        elem->OpenForTitle(app_id);
    }

    s32 wx = 0, wy = 0;
    static constexpr s32 kCheatsWinW = 960;
    static constexpr s32 kCheatsWinH = 600;
    wm_.TakeStaggerPos(kCheatsWinW, kCheatsWinH, wx, wy);

    auto win = QdWindow::New("Cheats", std::move(elem),
                             wx, wy, kCheatsWinW, kCheatsWinH);

    // Set initial hint from the layout.
    win->SetHintText(elem_raw->GetBottomHint());

    // on_tick: keep hint in sync with active navigation mode.
    auto* win_raw = win.get();
    win->on_tick = [elem_raw, win_raw]() {
        win_raw->SetHintText(elem_raw->GetBottomHint());
    };

    win->on_minimize_begin_ = [this, app_id](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this, app_id]() { OpenCheatsWindow(app_id); });
    };
    wm_.OpenWindow(std::move(win));
    UL_LOG_INFO("cheats: OpenCheatsWindow app_id=0x%llx — window opened 960×600",
                static_cast<unsigned long long>(app_id));
}

// ── OpenModsWindow ─────────────────────────────────────────────────────────────
// B3.1-MODS: singleton-guarded opener for the Atmosphère LayeredFS mod manager.
//
// Mirrors OpenCheatsWindow() exactly — same 960×600 window, same singleton guard,
// same on_tick hint-bar update, same minimize/restore lambda.
//
// app_id == 0 → TitleList mode (browse all titles with mods installed).
// app_id != 0 → jump directly into SlotList for that title via OpenForTitle().
//
// Mod changes apply NEXT LAUNCH: Atmosphère's fsmitm reads the LayeredFS
// directory tree at process launch; a mounted romfs cannot be hot-swapped
// while the game is running.  The UI communicates this via the detail pane hint.

void QdDesktopIconsElement::OpenModsWindow(const u64 app_id) {
    if (FocusExistingWindow(wm_, "Mods")) { return; }

    auto elem      = QdModsLayout::New(theme_);
    auto* elem_raw = elem.get();  // raw ptr survives std::move

    if (app_id != 0) {
        elem->OpenForTitle(app_id);
    }

    s32 wx = 0, wy = 0;
    static constexpr s32 kModsWinW = 960;
    static constexpr s32 kModsWinH = 600;
    wm_.TakeStaggerPos(kModsWinW, kModsWinH, wx, wy);

    auto win = QdWindow::New("Mods", std::move(elem),
                             wx, wy, kModsWinW, kModsWinH);

    // Set initial hint from the layout.
    win->SetHintText(elem_raw->GetBottomHint());

    // on_tick: keep hint in sync with active navigation mode.
    auto* win_raw = win.get();
    win->on_tick = [elem_raw, win_raw]() {
        win_raw->SetHintText(elem_raw->GetBottomHint());
    };

    win->on_minimize_begin_ = [this, app_id](QdWindow* w) {
        pending_ctx_minimize_win_ = w;
        wm_.SetPendingReopen(w, [this, app_id]() { OpenModsWindow(app_id); });
    };
    wm_.OpenWindow(std::move(win));
    UL_LOG_INFO("mods: OpenModsWindow app_id=0x%llx — window opened 960×600",
                static_cast<unsigned long long>(app_id));
}

} // namespace ul::menu::qdesktop
