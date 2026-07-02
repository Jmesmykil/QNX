// qd_Audio.cpp — QdAudio desktop SFX coordinator (Phase 2).
// See include/ul/menu/qdesktop/qd_Audio.hpp for design notes and wiring TODO.

#include <ul/menu/qdesktop/qd_Audio.hpp>
#include <ul/menu/ui/ui_Common.hpp>    // TryGetActiveThemeResource
#include <ul/ul_Result.hpp>            // UL_LOG_INFO

#include <cstddef>  // size_t
#include <utility>  // std::to_underlying (C++23) — not needed; use static_cast

namespace ul::menu::qdesktop {

    // ── Asset path table ─────────────────────────────────────────────────────
    // One entry per DesktopSfxEvent, in the same order as the enum.
    // All paths are relative to the active-theme root and fed through
    // TryGetActiveThemeResource().  A missing file → LoadSfx returns nullptr
    // → Play() is a no-op.  No crash, no log spam; silence is the graceful
    // fallback.
    //
    // Convention: sound/Desktop/<PascalCaseName>.wav
    //
    // To add an event: (1) add the enum value in qd_Audio.hpp, (2) add the
    // corresponding path here in the SAME position, (3) add the .wav under
    // the default theme.

    static constexpr const char * kEventAssetPath[] = {
        // DesktopSfxEvent::DesktopIconNav
        "sound/Desktop/DesktopIconNav.wav",
        // DesktopSfxEvent::AppLaunchConfirm
        "sound/Desktop/AppLaunchConfirm.wav",
        // DesktopSfxEvent::FolderOpen
        "sound/Desktop/FolderOpen.wav",
        // DesktopSfxEvent::FolderClose
        "sound/Desktop/FolderClose.wav",

        // DesktopSfxEvent::CtxMenuOpen
        "sound/Desktop/CtxMenuOpen.wav",
        // DesktopSfxEvent::CtxMenuClose
        "sound/Desktop/CtxMenuClose.wav",

        // DesktopSfxEvent::HotCornerActivate
        "sound/Desktop/HotCornerActivate.wav",

        // DesktopSfxEvent::VaultOpen
        "sound/Desktop/VaultOpen.wav",
        // DesktopSfxEvent::VaultClose
        "sound/Desktop/VaultClose.wav",
        // DesktopSfxEvent::VaultFileCopy
        "sound/Desktop/VaultFileCopy.wav",
        // DesktopSfxEvent::VaultFileDelete
        "sound/Desktop/VaultFileDelete.wav",
        // DesktopSfxEvent::VaultFileRename
        "sound/Desktop/VaultFileRename.wav",

        // DesktopSfxEvent::MonitorOpen
        "sound/Desktop/MonitorOpen.wav",
        // DesktopSfxEvent::MonitorClose
        "sound/Desktop/MonitorClose.wav",

        // DesktopSfxEvent::SettingsOpen
        "sound/Desktop/SettingsOpen.wav",
        // DesktopSfxEvent::SettingsClose
        "sound/Desktop/SettingsClose.wav",
        // DesktopSfxEvent::SettingsItemChange
        "sound/Desktop/SettingsItemChange.wav",

        // DesktopSfxEvent::LockscreenUnlockOk
        "sound/Desktop/LockscreenUnlockOk.wav",
        // DesktopSfxEvent::LockscreenUnlockFail
        "sound/Desktop/LockscreenUnlockFail.wav",

        // DesktopSfxEvent::ToastAppear
        "sound/Desktop/ToastAppear.wav",

        // DesktopSfxEvent::PowerMenuOpen
        "sound/Desktop/PowerMenuOpen.wav",
        // DesktopSfxEvent::PowerMenuClose
        "sound/Desktop/PowerMenuClose.wav",
        // DesktopSfxEvent::PowerSleepConfirm
        "sound/Desktop/PowerSleepConfirm.wav",
        // DesktopSfxEvent::PowerShutdownConfirm
        "sound/Desktop/PowerShutdownConfirm.wav",

        // DesktopSfxEvent::ThemeChange
        "sound/Desktop/ThemeChange.wav",

        // DesktopSfxEvent::LoginBgmFadeIn
        "sound/Desktop/LoginBgmFadeIn.wav",

        // DesktopSfxEvent::DialogOpen
        "sound/Desktop/DialogOpen.wav",
        // DesktopSfxEvent::DialogClose
        "sound/Desktop/DialogClose.wav",
        // DesktopSfxEvent::DialogNav
        "sound/Desktop/DialogNav.wav",
        // DesktopSfxEvent::DialogConfirm
        "sound/Desktop/DialogConfirm.wav",
        // DesktopSfxEvent::DialogCancel
        "sound/Desktop/DialogCancel.wav",
    };

    static_assert(
        sizeof(kEventAssetPath) / sizeof(kEventAssetPath[0]) ==
        static_cast<size_t>(DesktopSfxEvent::Count_),
        "kEventAssetPath must have exactly one entry per DesktopSfxEvent"
    );

    // ── File-scoped SFX handle table ─────────────────────────────────────────
    // Indexed by static_cast<size_t>(DesktopSfxEvent).  All entries are
    // nullptr until Initialize() is called.  Finalize() resets them back.

    static pu::audio::Sfx g_sfx_table[static_cast<size_t>(DesktopSfxEvent::Count_)] = {};

    // Tracks whether Initialize() has been called at least once, so Finalize()
    // and re-Initialize() are safe to call in any order.
    static bool g_initialized = false;

    // ── QdAudio implementation ────────────────────────────────────────────────

    namespace QdAudio {

        void Initialize() {
            // Re-entrant: dispose any previously loaded handles first.
            if(g_initialized) {
                Finalize();
            }
            g_initialized = true;

            UL_LOG_INFO("[QdAudio] Loading desktop SFX table (%zu events)...",
                        static_cast<size_t>(DesktopSfxEvent::Count_));

            for(size_t i = 0; i < static_cast<size_t>(DesktopSfxEvent::Count_); ++i) {
                const std::string path = ul::menu::ui::TryGetActiveThemeResource(kEventAssetPath[i]);
                // LoadSfx returns nullptr for an empty or missing path — safe.
                g_sfx_table[i] = pu::audio::LoadSfx(path);
            }

            UL_LOG_INFO("[QdAudio] Desktop SFX table loaded.");
        }

        void Finalize() {
            if(!g_initialized) {
                return;
            }
            for(size_t i = 0; i < static_cast<size_t>(DesktopSfxEvent::Count_); ++i) {
                if(g_sfx_table[i] != nullptr) {
                    pu::audio::DestroySfx(g_sfx_table[i]);
                    g_sfx_table[i] = nullptr;
                }
            }
            g_initialized = false;
            UL_LOG_INFO("[QdAudio] Desktop SFX table released.");
        }

        void Play(const DesktopSfxEvent event) {
            const size_t idx = static_cast<size_t>(event);
            // Bounds-guard: protects against a forgotten enum update and
            // against calling Play() before Initialize().
            if(idx >= static_cast<size_t>(DesktopSfxEvent::Count_)) {
                return;
            }
            pu::audio::Sfx sfx = g_sfx_table[idx];
            if(sfx != nullptr) {
                pu::audio::PlaySfx(sfx);
            }
        }

    }  // namespace QdAudio

}  // namespace ul::menu::qdesktop
