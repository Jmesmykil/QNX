// qd_LaunchpadHostLayout.hpp — IMenuLayout host wrapper for QdLaunchpadElement.
//
// Why this class exists
// ---------------------
// QdLaunchpadElement is a pu::ui::elm::Element despite the "Layout"-style
// naming convention.  Plutonium's pu::ui::Layout::Add() is the canonical way
// to attach an Element to a screen, so QdLaunchpadElement must be wrapped in
// a Layout subclass.
//
// That wrapper MUST be an IMenuLayout — NOT a bare pu::ui::Layout.  Plutonium's
// GetLayout<L>() is a `std::static_pointer_cast<L>(this->lyt)` — an UNCHECKED
// downcast.  ul::menu::ui::OnMessage() does
//
//     auto ptr = g_MenuApplication->GetLayout<IMenuLayout>();
//     ptr->NotifyMessageContext(msg_ctx);
//
// If the active layout is a bare pu::ui::Layout, the cast reinterprets the
// vtable and NotifyMessageContext() accesses members (msg_queue, msg_queue_lock)
// at offsets that don't exist — Data Abort at 0x0 (Atmosphère 2168-0002).
// See qd_VaultHostLayout.hpp and qd_MonitorHostLayout.hpp for the full crash
// chain description.
//
// QdLaunchpadHostLayout is a real IMenuLayout that owns the QdLaunchpadElement
// and satisfies the IMenuLayout obligations the dispatcher relies on.

#pragma once

#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>
#include <pu/audio/audio_Audio.hpp>

namespace ul::menu::qdesktop {

    class QdLaunchpadHostLayout : public ul::menu::ui::IMenuLayout {
        public:
            // Construct with an already-built QdLaunchpadElement.  The host
            // takes ownership via shared_ptr and adds the element as a child.
            explicit QdLaunchpadHostLayout(QdLaunchpadElement::Ref launchpad_element);

            // PU_SMART_CTOR also defines `using Ref = std::shared_ptr<...>;` — do
            // NOT declare Ref above this macro or the compiler errors with a
            // redeclaration of the alias.
            PU_SMART_CTOR(QdLaunchpadHostLayout)

            // ── IMenuLayout pure-virtual obligations ─────────────────────────────

            void OnMenuInput(const u64 keys_down,
                             const u64 keys_up,
                             const u64 keys_held,
                             const pu::ui::TouchPoint touch_pos) override;

            // Return to the main desktop on HOME and consume the message.
            // Returning true tells IMenuLayout::OnLayoutInput to pop the
            // queued HomeRequest from the message queue.
            bool OnHomeButtonPress() override;

            // Load / dispose per-session Launchpad SFX.  Called by
            // MenuApplication::EnsureLayoutCreated() and DisposeAllSfx().
            void LoadSfx() override;
            void DisposeSfx() override;

            // ── Cross-worker play contracts ───────────────────────────────────────
            // Called by qd_Launchpad.cpp via the LP_PLAY_SFX macro (dynamic_pointer_cast
            // to QdLaunchpadHostLayout).  Each inline guard-checks its handle before
            // calling PlaySfx so NULL handles produce no crash (asset absent or
            // LoadSfx not yet called).
            inline void PlayTileLaunchSfx()   { if(this->tile_launch_sfx)   pu::audio::PlaySfx(this->tile_launch_sfx);   }
            inline void PlayFolderFilterSfx() { if(this->folder_filter_sfx) pu::audio::PlaySfx(this->folder_filter_sfx); }
            inline void PlayPageTurnSfx()     { if(this->page_turn_sfx)     pu::audio::PlaySfx(this->page_turn_sfx);     }
            inline void PlayFavoriteOnSfx()   { if(this->favorite_on_sfx)   pu::audio::PlaySfx(this->favorite_on_sfx);   }
            inline void PlayFavoriteOffSfx()  { if(this->favorite_off_sfx)  pu::audio::PlaySfx(this->favorite_off_sfx);  }
            inline void PlayErrorToneSfx()    { if(this->error_tone_sfx)    pu::audio::PlaySfx(this->error_tone_sfx);    }

        private:
            QdLaunchpadElement::Ref launchpad_element_;

            // ── Launchpad SFX handles (loaded in LoadSfx, disposed in DisposeSfx) ─
            pu::audio::Sfx launchpad_open_sfx  = nullptr;
            pu::audio::Sfx launchpad_close_sfx = nullptr;
            pu::audio::Sfx tile_launch_sfx     = nullptr;
            pu::audio::Sfx folder_filter_sfx   = nullptr;
            pu::audio::Sfx page_turn_sfx       = nullptr;
            pu::audio::Sfx favorite_on_sfx     = nullptr;
            pu::audio::Sfx favorite_off_sfx    = nullptr;
            pu::audio::Sfx error_tone_sfx      = nullptr;

            // First-frame open-SFX gate: reset to false every time the Launchpad
            // is closed so the next Open() plays the sound exactly once.
            bool sfx_open_played_ = false;
    };

}
