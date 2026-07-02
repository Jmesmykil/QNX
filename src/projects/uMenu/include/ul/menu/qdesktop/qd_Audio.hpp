// qd_Audio.hpp — QdAudio: central desktop SFX coordinator (Phase 2).
//
// DESIGN
// ------
// All ~40 "silent" desktop-interaction events funnel through ONE entry point:
//
//     ul::menu::qdesktop::QdAudio::Play(DesktopSfxEvent)
//
// This avoids every layout owning its own Mix_Chunk handles and having to
// share/pass theme-resource paths.  The coordinator owns the loaded-SFX
// table, loads on Initialize(), disposes on Finalize(), and plays with a
// null-safe guard so missing .wav files never crash.
//
// LIFECYCLE (hooked in ui_MenuApplication.cpp)
// --------------------------------------------
//   QdAudio::Initialize()  — called from MenuApplication::LoadBgmSfxForCreatedMenus()
//   QdAudio::Finalize()    — called from MenuApplication::DisposeAllSfx()
//
// THREAD SAFETY
// -------------
// Play() must only be called from the Plutonium render/input thread (the same
// thread that owns the SDL_mixer channel state).  No internal locking needed.
//
// ASSET PATH CONVENTION
// ---------------------
// All SFX live under the active-theme root:
//     sound/Desktop/<EventName>.wav
// Missing files → LoadSfx returns nullptr → Play is a no-op.  The game never
// crashes on a missing asset.
//
// ────────────────────────────────────────────────────────────────────────────
// TODO — WIRING PASS (follow-up agent; do NOT touch layout files now)
// ────────────────────────────────────────────────────────────────────────────
// Each call-site below should call QdAudio::Play(DesktopSfxEvent::<X>) in
// the appropriate input/state handler.  File paths are relative to the source
// root (source/ul/menu/qdesktop/) unless noted.
//
//  DesktopSfxEvent::DesktopIconNav
//      → qd_DesktopIcons.cpp  — D-pad nav between desktop icons
//
//  DesktopSfxEvent::AppLaunchConfirm
//      → qd_DesktopIcons.cpp  — A-press / double-tap to launch an app
//
//  DesktopSfxEvent::FolderOpen
//      → qd_DesktopIcons.cpp  — folder icon opened (enter folder view)
//
//  DesktopSfxEvent::FolderClose
//      → qd_DesktopIcons.cpp  — folder closed / B-press out of folder
//
//  DesktopSfxEvent::CtxMenuOpen
//      → qd_ContextMenu.cpp   — right-click / ZL context menu appears
//
//  DesktopSfxEvent::CtxMenuClose
//      → qd_ContextMenu.cpp   — context menu dismissed (B / focus lost)
//
//  DesktopSfxEvent::HotCornerActivate
//      → qd_HotCornerOverlay.cpp / qd_HotCornerDropdown.cpp — corner triggered
//
//  DesktopSfxEvent::VaultOpen
//      → qd_VaultHostLayout.cpp  — Vault window opens
//
//  DesktopSfxEvent::VaultClose
//      → qd_VaultHostLayout.cpp  — Vault window dismissed
//
//  DesktopSfxEvent::VaultFileCopy
//      → qd_VaultLayout.cpp  — file copy operation confirmed
//
//  DesktopSfxEvent::VaultFileDelete
//      → qd_VaultLayout.cpp  — file delete confirmed
//
//  DesktopSfxEvent::VaultFileRename
//      → qd_VaultLayout.cpp  — file rename committed
//
//  DesktopSfxEvent::MonitorOpen
//      → qd_MonitorHostLayout.cpp  — Monitor window opens
//
//  DesktopSfxEvent::MonitorClose
//      → qd_MonitorHostLayout.cpp  — Monitor window dismissed
//
//  DesktopSfxEvent::SettingsOpen
//      → qd_SettingsLayout.cpp  — QSettings surface shown
//
//  DesktopSfxEvent::SettingsClose
//      → qd_SettingsLayout.cpp  — QSettings dismissed (B / back)
//
//  DesktopSfxEvent::SettingsItemChange
//      → qd_SettingsLayout.cpp  — a toggle / value changes
//
//  DesktopSfxEvent::LockscreenUnlockOk
//      → qd_LockscreenLayout.cpp  — correct PIN / biometric accepted
//
//  DesktopSfxEvent::LockscreenUnlockFail
//      → qd_LockscreenLayout.cpp  — wrong PIN / biometric rejected
//
//  DesktopSfxEvent::ToastAppear
//      → ui_MenuApplication.cpp  — ShowNotification() call-site
//
//  DesktopSfxEvent::PowerMenuOpen
//      → qd_HotCornerRightDropdown.cpp / qd_HomeMiniMenu.cpp — power menu shown
//
//  DesktopSfxEvent::PowerMenuClose
//      → qd_HotCornerRightDropdown.cpp / qd_HomeMiniMenu.cpp — power menu dismissed
//
//  DesktopSfxEvent::PowerSleepConfirm
//      → qd_Power.cpp  — user confirmed sleep action
//
//  DesktopSfxEvent::PowerShutdownConfirm
//      → qd_Power.cpp  — user confirmed shutdown action
//
//  DesktopSfxEvent::ThemeChange
//      → ui_ThemesMenuLayout.cpp (source/ul/menu/ui/) — theme selected/applied
//
//  DesktopSfxEvent::LoginBgmFadeIn
//      → ui_MenuApplication.cpp  — StartPlayBgm() when loaded_menu == Startup
//        (fire after Mix_FadeInMusic, not before)
//
//  DesktopSfxEvent::DialogOpen
//      → ui_MenuApplication.cpp  — DisplayDialog() entry
//
//  DesktopSfxEvent::DialogClose
//      → ui_MenuApplication.cpp  — DisplayDialog() return (any option chosen)
//
//  DesktopSfxEvent::DialogNav
//      → ui_MenuApplication.cpp / dialog input handler — D-pad between options
//
//  DesktopSfxEvent::DialogConfirm
//      → ui_MenuApplication.cpp / dialog input handler — A-press / confirm option
//
//  DesktopSfxEvent::DialogCancel
//      → ui_MenuApplication.cpp / dialog input handler — B-press / cancel option
//
// ────────────────────────────────────────────────────────────────────────────

#pragma once

#include <pu/audio/audio_Audio.hpp>   // pu::audio::Sfx, LoadSfx, PlaySfx, DestroySfx

namespace ul::menu::qdesktop {

    // ── DesktopSfxEvent ──────────────────────────────────────────────────────
    // Exhaustive list of ~40 desktop interaction events that were previously
    // silent.  Add new events here; give each a canonical .wav name below in
    // qd_Audio.cpp's kEventAssetPath table.
    enum class DesktopSfxEvent {
        // Desktop icon grid navigation
        DesktopIconNav,         // D-pad move between icons
        AppLaunchConfirm,       // A-press / tap to launch
        FolderOpen,             // enter a folder
        FolderClose,            // exit a folder (B / back)

        // Context menu
        CtxMenuOpen,
        CtxMenuClose,

        // Hot corner
        HotCornerActivate,

        // Vault file manager
        VaultOpen,
        VaultClose,
        VaultFileCopy,
        VaultFileDelete,
        VaultFileRename,

        // System Monitor
        MonitorOpen,
        MonitorClose,

        // Q OS Settings
        SettingsOpen,
        SettingsClose,
        SettingsItemChange,

        // Lockscreen
        LockscreenUnlockOk,
        LockscreenUnlockFail,

        // Toast notification
        ToastAppear,

        // Power menu
        PowerMenuOpen,
        PowerMenuClose,
        PowerSleepConfirm,
        PowerShutdownConfirm,

        // Theme
        ThemeChange,

        // Login BGM
        LoginBgmFadeIn,

        // Generic dialogs (DisplayDialog family)
        DialogOpen,
        DialogClose,
        DialogNav,
        DialogConfirm,
        DialogCancel,

        // Sentinel — keep last
        Count_,
    };

    // ── QdAudio namespace ─────────────────────────────────────────────────────
    // Free-function API; no class needed — the SFX table is file-scoped in
    // qd_Audio.cpp.  The Initialize/Finalize pair mirrors the pattern used by
    // QdLaunchpadHostLayout::LoadSfx / DisposeSfx but at application scope.
    namespace QdAudio {

        // Load all desktop SFX assets from the active theme.  Call from
        // MenuApplication::LoadBgmSfxForCreatedMenus().
        //
        // Re-entrant: calling Initialize() a second time (e.g. after a theme
        // switch) calls Finalize() first to release the old handles, then reloads.
        void Initialize();

        // Release all loaded Mix_Chunk handles.  Safe to call even if
        // Initialize() was never called (all handles start as nullptr).
        // Called from MenuApplication::DisposeAllSfx().
        void Finalize();

        // Play the SFX for the given event.  No-op if the event's asset was
        // not found or Initialize() has not been called.  Safe to call at any
        // frequency (SDL_mixer handles channel arbitration).
        void Play(DesktopSfxEvent event);

    }  // namespace QdAudio

}  // namespace ul::menu::qdesktop
