// qd_LaunchpadHostLayout.cpp — see qd_LaunchpadHostLayout.hpp for the why.

#include <ul/menu/qdesktop/qd_LaunchpadHostLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/ul_Result.hpp>
#include <switch.h>  // HidNpadButton_B / HidNpadButton_Plus

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

    QdLaunchpadHostLayout::QdLaunchpadHostLayout(QdLaunchpadElement::Ref launchpad_element)
        : launchpad_element_(launchpad_element) {
        this->SetBackgroundColor({ 0, 0, 0, 255 });
        this->Add(this->launchpad_element_);
        // v1.9.7: hot-corner overlay added last so it paints above Launchpad
        // content.  Suppressed when the search bar has focus.
        this->overlay_ = QdHotCornerOverlay::New();
        this->overlay_->SetSearchActiveRef(this->launchpad_element_->GetSearchActivePtr());
        this->Add(this->overlay_);
    }

    void QdLaunchpadHostLayout::OnMenuInput(const u64 keys_down,
                                            const u64 keys_up,
                                            const u64 keys_held,
                                            const pu::ui::TouchPoint touch_pos) {
        // QdLaunchpadElement::OnInput handles B/Plus by calling Close() +
        // SetVisible(false).  That was designed for the overlay-on-MainMenu
        // hot-corner mode (B closes the overlay, MainMenu becomes visible
        // underneath).  In MenuType::Launchpad full-menu mode the host is the
        // top-level surface — Close()+SetVisible(false) would just leave the
        // user on a black background with no way out.  Intercept B/Plus
        // here in the host to navigate back to MainMenu.
        (void)keys_up;
        (void)keys_held;
        (void)touch_pos;

        // First-frame open SFX: fires once per Launchpad session.  The flag is
        // reset to false in every close path so the next Open() plays it again.
        if(!this->sfx_open_played_) {
            this->sfx_open_played_ = true;
            if(this->launchpad_open_sfx) {
                pu::audio::PlaySfx(this->launchpad_open_sfx);
            }
        }

        if((keys_down & HidNpadButton_B) || (keys_down & HidNpadButton_Plus)) {
            UL_LOG_INFO("launchpad: B/Plus -> returning to MainMenu");
            if(this->launchpad_close_sfx) {
                pu::audio::PlaySfx(this->launchpad_close_sfx);
            }
            this->sfx_open_played_ = false;
            if(this->launchpad_element_) {
                this->launchpad_element_->Close();
                // Re-enable visibility so the next Launchpad open shows up.
                this->launchpad_element_->SetVisible(true);
            }
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
            return;
        }
        // Pending-launch dispatch: when the Element flagged a launch via
        // A/ZR, fire the launch through the desktop icons element it
        // captured at Open() time. The launch path itself owns the menu
        // transition (FadeOut for app launches, LoadMenu for builtins),
        // so we close the Launchpad after dispatching but do NOT call
        // LoadMenu(Main) here.
        if(this->launchpad_element_ && this->launchpad_element_->IsPendingLaunch()) {
            const size_t desktop_idx = this->launchpad_element_->FocusedDesktopIdx();
            UL_LOG_INFO("launchpad: pending launch dispatch desktop_idx=%zu", desktop_idx);
            if(this->tile_launch_sfx) {
                pu::audio::PlaySfx(this->tile_launch_sfx);
            }
            this->launchpad_element_->DispatchPendingLaunch();
            this->launchpad_element_->Close();
            this->launchpad_element_->SetVisible(true);
        }
    }

    bool QdLaunchpadHostLayout::OnHomeButtonPress() {
        UL_LOG_INFO("launchpad: OnHomeButtonPress -> returning to MainMenu");
        if(this->launchpad_close_sfx) {
            pu::audio::PlaySfx(this->launchpad_close_sfx);
        }
        this->sfx_open_played_ = false;
        // Free per-slot SDL textures + clear is_open_ so the next entry
        // through MenuType::Launchpad starts clean.  Open() also calls
        // FreeAllTextures() at its top, but doing it here keeps the
        // Launchpad's GPU footprint at zero while sitting on MainMenu.
        if(this->launchpad_element_) {
            this->launchpad_element_->Close();
        }
        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        return true;
    }

    void QdLaunchpadHostLayout::LoadSfx() {
        this->launchpad_open_sfx   = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/Open.wav"));
        this->launchpad_close_sfx  = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/Close.wav"));
        this->tile_launch_sfx      = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/TileLaunch.wav"));
        this->folder_filter_sfx    = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/FolderFilter.wav"));
        this->page_turn_sfx        = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/PageTurn.wav"));
        this->favorite_on_sfx      = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/FavoriteOn.wav"));
        this->favorite_off_sfx     = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/FavoriteOff.wav"));
        this->error_tone_sfx       = pu::audio::LoadSfx(ul::menu::ui::TryGetActiveThemeResource("sound/Launchpad/ErrorTone.wav"));
    }

    void QdLaunchpadHostLayout::DisposeSfx() {
        pu::audio::DestroySfx(this->launchpad_open_sfx);
        pu::audio::DestroySfx(this->launchpad_close_sfx);
        pu::audio::DestroySfx(this->tile_launch_sfx);
        pu::audio::DestroySfx(this->folder_filter_sfx);
        pu::audio::DestroySfx(this->page_turn_sfx);
        pu::audio::DestroySfx(this->favorite_on_sfx);
        pu::audio::DestroySfx(this->favorite_off_sfx);
        pu::audio::DestroySfx(this->error_tone_sfx);
        this->launchpad_open_sfx   = nullptr;
        this->launchpad_close_sfx  = nullptr;
        this->tile_launch_sfx      = nullptr;
        this->folder_filter_sfx    = nullptr;
        this->page_turn_sfx        = nullptr;
        this->favorite_on_sfx      = nullptr;
        this->favorite_off_sfx     = nullptr;
        this->error_tone_sfx       = nullptr;
    }

}
