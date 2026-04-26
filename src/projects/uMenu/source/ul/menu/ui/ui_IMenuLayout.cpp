#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/net/net_Service.hpp>

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        s32 g_HomeButtonPressHandleCount = 0;

        std::vector<std::string> g_WeekdayList;

        void EnsureWeekdayList() {
            if(g_WeekdayList.empty()) {
                for(u32 i = 0; i < 7; i++) {
                    g_WeekdayList.push_back(GetLanguageString("week_day_short_" + std::to_string(i)));
                }
            }
        }

        void OnFinishedSleep() {
            // Reset and reinitialize audio (force-avoid post-sleep audio stutter in audout)
            g_MenuApplication->DisposeAllSfx();
            ul::menu::ui::DisposeAllBgm();
            pu::audio::Finalize();
    
            UL_ASSERT_TRUE(pu::audio::Initialize(MIX_INIT_MP3));
            g_MenuApplication->LoadBgmSfxForCreatedMenus();
    
            // Load lockscreen, if enabled
            bool lockscreen_enabled;
            UL_ASSERT_TRUE(g_GlobalSettings.config.GetEntry(cfg::ConfigEntryId::LockscreenEnabled, lockscreen_enabled));
            if(lockscreen_enabled) {
                g_MenuApplication->LoadMenu(MenuType::Lockscreen, true);
            }
    
            g_MenuApplication->StartPlayBgm();
        }

    }

    void IMenuLayout::UpdateConnectionTopIcon(pu::ui::elm::Image::Ref &icon) {
        u32 conn_strength;
        const auto has_conn = net::HasConnection(conn_strength);
        if((this->last_has_connection != has_conn) || (this->last_connection_strength != conn_strength)) {
            this->last_has_connection = has_conn;
            this->last_connection_strength = conn_strength;
            if(has_conn) {
                icon->SetImage(TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Connection/" + std::to_string(conn_strength)));
            }
            else {
                icon->SetImage(TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Connection/None"));
            }
            // Cycle K-topiconsfit: Plutonium's Image::SetImage resets the
            // rendered size to the new texture's natural dimensions.  Our
            // top-bar PNGs are 100×100 source intended to render at 32×32,
            // so without a re-apply here the icon balloons back to 100×100
            // every time the connection strength changes — overlapping the
            // adjacent battery icon and producing the "icons on the right
            // are overlapping each other and cluttered" symptom the creator
            // reported.  TOPBAR_ICON_W/H are 32 (defined in
            // ui_MainMenuLayout.cpp constructor); duplicated here as
            // literals because that file's constants aren't exported.
            icon->SetWidth(32);
            icon->SetHeight(32);
        }
    }

    void IMenuLayout::UpdateDateText(pu::ui::elm::TextBlock::Ref &date_text) {
        const auto cur_date = os::GetCurrentDate();
        if(this->last_date != cur_date) {
            this->last_date = cur_date;

            char cur_date_str[0x40] = {};
            sprintf(cur_date_str, "%02d/%02d (%s)", cur_date.day, cur_date.month, g_WeekdayList.at(cur_date.weekday_idx).c_str());
            date_text->SetText(cur_date_str);
        }
    }

    void IMenuLayout::InitializeTimeText(MultiTextBlock::Ref &time_mtext, const std::string &ui_menu, const std::string &ui_name) {
        // Two-block layout: Get(0) = "H:MM", Get(1) = " AM"/" PM".
        // ApplyConfigForElement reads upstream UI.json coords (e.g. x=1508 for
        // stock uLaunch layout) — callers in QDESKTOP mode MUST override SetX/
        // SetY + UpdatePositionsSizes() immediately after this call.
        time_mtext = MultiTextBlock::New(0, 0);
        time_mtext->Add(pu::ui::elm::TextBlock::New(0, 0, "12:00"));
        time_mtext->Add(pu::ui::elm::TextBlock::New(0, 0, " PM"));
        g_GlobalSettings.ApplyConfigForElement(ui_menu, ui_name, time_mtext);
        for(auto &text: time_mtext->GetAll()) {
            text->SetColor(g_MenuApplication->GetTextColor());
        }
        time_mtext->UpdatePositionsSizes();
        this->Add(time_mtext);
        this->Add(time_mtext->Get(0));
        this->Add(time_mtext->Get(1));
    }

    void IMenuLayout::UpdateTimeText(MultiTextBlock::Ref &time_mtext) {
        const auto cur_time = os::GetCurrentTime();

        if((this->last_time.h != cur_time.h) || (this->last_time.min != cur_time.min)) {
            // 12-hour format: "H:MM" in Get(0), " AM"/" PM" in Get(1).
            // Both blocks updated together so reflow only happens once per change.
            const u32 h12 = (cur_time.h % 12 == 0) ? 12 : (cur_time.h % 12);
            const char *const ampm = (cur_time.h < 12) ? "AM" : "PM";

            char cur_hm_str[0x40] = {};
            sprintf(cur_hm_str, "%d:%02d", h12, cur_time.min);
            time_mtext->Get(0)->SetText(cur_hm_str);

            char cur_ampm_str[0x10] = {};
            sprintf(cur_ampm_str, " %s", ampm);
            time_mtext->Get(1)->SetText(cur_ampm_str);

            // Reflow child blocks so AM/PM follows H:MM without a gap.
            time_mtext->UpdatePositionsSizes();

            this->last_time = cur_time;
        }
    }

    void IMenuLayout::UpdateBatteryTextAndTopIcons(pu::ui::elm::TextBlock::Ref &text, pu::ui::elm::Image::Ref &base_top_icon, pu::ui::elm::Image::Ref &charging_top_icon) {
        const auto battery_level = os::GetBatteryLevel();
        const auto is_charging = os::IsConsoleCharging();
        if((this->last_battery_level != battery_level) || (this->last_battery_is_charging != is_charging)) {
            this->last_battery_level = battery_level;
            this->last_battery_is_charging = is_charging;

            const auto battery_str = std::to_string(battery_level) + "%";
            text->SetText(battery_str);

            // Floor-down to nearest 10 (10, 20, ..., 100) so icon matches actual level
            auto battery_lvl_norm = (battery_level / 10) * 10;
            if(battery_lvl_norm < 10) {
                battery_lvl_norm = 10;
            }
            if(battery_lvl_norm > 100) {
                battery_lvl_norm = 100;
            }
            const auto battery_img = "ui/Main/TopIcon/Battery/" + std::to_string(battery_lvl_norm);
            base_top_icon->SetImage(TryFindLoadImageHandleDefaultOnly(battery_img));
            // Cycle K-topiconsfit: same Plutonium SetImage size-reset bug as
            // UpdateConnectionTopIcon — re-apply 32×32 every swap so the
            // 100×100 source PNG renders at top-bar size, not natural.
            base_top_icon->SetWidth(32);
            base_top_icon->SetHeight(32);
            charging_top_icon->SetVisible(is_charging);
        }
    }

    IMenuLayout::IMenuLayout() : Layout(), msg_queue_lock(), msg_queue(), last_has_connection(false), last_connection_strength(0), last_battery_level(0), last_battery_is_charging(false), last_time(), last_date() {
        this->SetBackgroundImage(GetBackgroundTexture());
        this->SetOnInput(std::bind(&IMenuLayout::OnLayoutInput, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
        this->AddRenderCallback(std::bind(&IMenuLayout::OnMenuUpdate, this));

        EnsureWeekdayList();
    }

    void IMenuLayout::OnLayoutInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
        {
            ScopedLock lk(this->msg_queue_lock);

            if(!this->msg_queue.empty()) {
                const auto first_msg = this->msg_queue.front();

                switch(first_msg.msg) {
                    case smi::MenuMessage::HomeRequest: {
                        g_HomeButtonPressHandleCount++;
                        if(g_HomeButtonPressHandleCount == 1) {
                            if(this->OnHomeButtonPress()) {
                                g_HomeButtonPressHandleCount = 0;
                                this->msg_queue.pop();
                            }
                        }
                        break;
                    }
                    case smi::MenuMessage::GameCardMountFailure: {
                        g_MenuApplication->NotifyGameCardMountFailure(first_msg.gc_mount_failure.mount_rc);
                        this->msg_queue.pop();
                        break;
                    }
                    case smi::MenuMessage::SdCardEjected: {
                        this->msg_queue.pop();
                        while(true) {
                            const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("sd_card"), GetLanguageString("sd_card_ejected"), { GetLanguageString("shutdown"), GetLanguageString("reboot") }, false);
                            if(option == 0) {
                                ShutdownSystem();
                            }
                            else if(option == 1) {
                                RebootSystem();
                            }
                        }
                        break;
                    }
                    case smi::MenuMessage::PreviousLaunchFailure: {
                        g_MenuApplication->NotifyLaunchFailed();
                        this->msg_queue.pop();
                        break;
                    }
                    case smi::MenuMessage::ChosenHomebrew: {
                        g_MenuApplication->NotifyHomebrewChosen(first_msg.chosen_hb.nro_path);
                        this->msg_queue.pop();
                        break;
                    }
                    case smi::MenuMessage::FinishedSleep: {
                        this->msg_queue.pop();
                        OnFinishedSleep();
                        break;
                    }
                    case smi::MenuMessage::ApplicationRecordsChanged: {
                        g_MenuApplication->NotifyApplicationRecordReloadNeeded();
                        if(first_msg.app_records_changed.records_added_or_deleted) {
                            // Need to also reload entries as well
                            g_MenuApplication->NotifyApplicationEntryReloadNeeded();
                        }
                        this->msg_queue.pop();
                        break;
                    }
                    case smi::MenuMessage::ApplicationVerifyProgress: {
                        const auto progress = (float)first_msg.app_verify_progress.done / (float)first_msg.app_verify_progress.total;
                        g_MenuApplication->NotifyApplicationVerifyProgress(first_msg.app_verify_progress.app_id, progress);

                        this->msg_queue.pop();
                        break;
                    }
                    case smi::MenuMessage::ApplicationVerifyResult: {
                        g_MenuApplication->NotifyApplicationVerifyProgress(first_msg.app_verify_rc.app_id, NAN);
                        g_MenuApplication->NotifyVerifyFinished(first_msg.app_verify_rc.app_id, first_msg.app_verify_rc.rc, first_msg.app_verify_rc.detail_rc);
                        
                        this->msg_queue.pop();
                        break;
                    }
                    default: {
                        this->msg_queue.pop();
                        break;
                    }
                }
            }
        }

        this->OnMenuInput(keys_down, keys_up, keys_held, touch_pos);
    }

    void IMenuLayout::NotifyMessageContext(const smi::MenuMessageContext &msg_ctx) {
        ScopedLock lk(this->msg_queue_lock);

        // Remove consequent homemenu requests
        if(msg_ctx.msg == smi::MenuMessage::HomeRequest) {
            if(!this->msg_queue.empty()) {
                if(this->msg_queue.front().msg == smi::MenuMessage::HomeRequest) {
                    return;
                }
            }
        }
        else if(msg_ctx.msg == smi::MenuMessage::ApplicationVerifyProgress) {
            if(!this->msg_queue.empty()) {
                if(this->msg_queue.front().msg == smi::MenuMessage::ApplicationVerifyProgress) {
                    this->msg_queue.pop();
                }
            }
        }

        this->msg_queue.push(msg_ctx);
    }

}
