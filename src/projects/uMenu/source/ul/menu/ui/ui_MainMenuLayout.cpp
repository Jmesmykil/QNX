#include <ul/menu/ui/ui_MainMenuLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/menu/menu_Cache.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/util/util_String.hpp>
#include <ul/util/util_Telemetry.hpp>
#include <ul/net/net_Service.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <ul/os/os_System.hpp>
#include <ul/os/os_Applications.hpp>
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_Input.hpp>
#include <ul/menu/qdesktop/qd_Curve.hpp>
#include <ul/menu/qdesktop/qd_RecordsBin.hpp>
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>
#include <ul/menu/bt/bt_Manager.hpp>
#include <unordered_map>
#endif

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        inline loader::TargetInput CreateLaunchTargetInput(const loader::TargetInput &base_params) {
            loader::TargetInput ipt = {};
            util::CopyToStringBuffer(ipt.nro_path, base_params.nro_path);
            if(strlen(base_params.nro_argv) > 0) {
                const auto default_argv = std::string(base_params.nro_path) + " " + base_params.nro_argv;
                util::CopyToStringBuffer(ipt.nro_argv, default_argv);
            }
            else {
                util::CopyToStringBuffer(ipt.nro_argv, base_params.nro_path);
            }
            return ipt;
        }

        inline bool IsNroNonRemovable(const char *nro_path) {
            if(strcmp(nro_path, ul::HbmenuPath) == 0) {
                return true;
            }
            if(strcmp(nro_path, ul::ManagerPath) == 0) {
                return true;
            }

            return false;
        }

        inline bool IsEntryNonRemovable(const Entry &entry) {
            return entry.IsSpecial() || IsNroNonRemovable(entry.hb_info.nro_target.nro_path);
        }

        std::string g_UserName;

        char g_MenuFsPathBuffer[FS_MAX_PATH] = {};
        char g_MenuPathBuffer[FS_MAX_PATH] = {};

    }

    void MainMenuLayout::DoMoveTo(const std::string &new_path) {
#ifdef QDESKTOP_MODE
        // qdesktop owns the layout; upstream entry/folder navigation is a no-op.
        (void)new_path;
        return;
#endif
        // Empty path used as a "reload" argumnet
        if(!new_path.empty()) {
            util::CopyToStringBuffer(g_MenuFsPathBuffer, new_path);
            util::CopyToStringBuffer(g_MenuPathBuffer, this->cur_folder_path);
            UL_RC_ASSERT(smi::UpdateMenuPaths(g_MenuFsPathBuffer, g_MenuPathBuffer));
        }

        this->entry_menu->MoveTo(new_path);
    }

    void MainMenuLayout::menu_EntryInputPressed(const u64 keys_down) {
#ifdef QDESKTOP_MODE
        // qdesktop has no entry_menu; upstream input dispatch is gated off.
        (void)keys_down;
        return;
#endif
        if(keys_down & HidNpadButton_B) {
            if(this->entry_menu->IsAnySelected()) {
                pu::audio::PlaySfx(this->entry_cancel_select_sfx);
                
                this->StopSelection();
            }
            else if(this->entry_menu->IsInRoot()) {
                const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("user_logoff"), GetLanguageString("user_logoff_opt"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true );
                if(option == 0) {
                    auto log_off = false;
                    if(g_GlobalSettings.IsSuspended()) {
                        const auto option_2 = g_MenuApplication->DisplayDialog(GetLanguageString("suspended_app"), GetLanguageString("user_logoff_app_suspended"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true);
                        if(option_2 == 0) {
                            log_off = true;
                        }
                    }
                    else {
                        log_off = true;
                    }

                    if(log_off) {
                        if(g_GlobalSettings.IsSuspended()) {
                            this->DoTerminateApplication();
                        }

                        pu::audio::PlaySfx(this->logoff_sfx);

                        g_GlobalSettings.system_status.selected_user = {};
                        g_MenuApplication->LoadMenu(MenuType::Startup, true, [&]() {
                            this->MoveToRoot(false);
                        });
                    }
                }
            }
            else {
                const auto parent_path = fs::GetBaseDirectory(this->entry_menu->GetPath());
                this->PopFolder();
                this->cur_path_text->SetText(this->cur_folder_path);
                this->MoveTo(parent_path, true);
            }
        }
        else if(keys_down & HidNpadButton_A) {
            if(this->entry_menu->IsFocusedNonemptyEntry()) {
                auto &cur_entry = this->entry_menu->GetFocusedEntry();
                if(this->entry_menu->IsAnySelected()) {
                    auto do_swap = true;
                    if(cur_entry.Is<EntryType::Folder>()) {
                        do_swap = false;
                        if(this->entry_menu->IsFocusedEntrySelected()) {
                            g_MenuApplication->ShowNotification(GetLanguageString("menu_move_folder_itself"));
                        }
                        else {
                            const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("menu_selection"), GetLanguageString("menu_move_conf"), { GetLanguageString("menu_move_into_folder"), GetLanguageString("menu_move_swap"), GetLanguageString("cancel") }, true);
                            if(option == 0) {
                                auto &sel_entry = this->entry_menu->GetSelectedEntry();

                                Entry sel_entry_copy(sel_entry);
                                sel_entry_copy.MoveTo(cur_entry.GetFolderPath());
                                pu::audio::PlaySfx(this->entry_move_into_sfx);
                                this->StopSelection();
                                this->entry_menu->NotifyEntryRemoved(sel_entry);
                                this->entry_menu->OrganizeUpdateEntries();
                            }
                            else if(option == 1) {
                                do_swap = true;
                            }
                        }
                    }

                    if(do_swap) {
                        auto &sel_entry = this->entry_menu->GetSelectedEntry();
                        Entry old_cur_entry(cur_entry);
                        Entry old_sel_entry(sel_entry);
                        cur_entry.OrderSwap(sel_entry);
                        pu::audio::PlaySfx(this->entry_swap_sfx);
                        this->StopSelection();
                        this->entry_menu->NotifyEntryRemoved(old_cur_entry);
                        this->entry_menu->NotifyEntryRemoved(old_sel_entry);
                        this->entry_menu->NotifyEntryAdded(cur_entry);
                        this->entry_menu->NotifyEntryAdded(sel_entry);
                        this->entry_menu->OrganizeUpdateEntries();
                    }
                }
                else {
                    if(cur_entry.Is<EntryType::Folder>()) {
                        this->PushFolder(cur_entry.folder_info.name);
                        this->MoveTo(cur_entry.GetFolderPath(), true);
                        this->cur_path_text->SetText(this->cur_folder_path);
                    }
                    else if(cur_entry.Is<EntryType::Application>() || cur_entry.Is<EntryType::Homebrew>()) {
                        auto do_launch_entry = true;

                        if(g_GlobalSettings.IsSuspended()) {
                            // Play animations, then resume the suspended hb/app
                            if(g_GlobalSettings.IsEntrySuspended(cur_entry)) {
                                if(IsScreenCaptureBackgroundFocused()) {
                                    this->StartResume();
                                }
                                do_launch_entry = false;
                            }

                            // If the suspended entry is another one, ask the user to close it beforehand
                            // Homebrew launching code already does this checks later, this do this check only with apps
                            if(do_launch_entry && cur_entry.Is<EntryType::Application>()) {
                                do_launch_entry = false;
                                this->HandleCloseSuspended();
                                do_launch_entry = !g_GlobalSettings.IsSuspended();
                            }
                        }

                        if(do_launch_entry && cur_entry.Is<EntryType::Application>()) {
                            if(cur_entry.app_info.NeedsVerify()) {
                                pu::audio::PlaySfx(this->error_sfx);

                                auto is_being_verified = false;
                                for(const auto app_id: g_GlobalSettings.in_verify_app_ids) {
                                    if(app_id == cur_entry.app_info.app_id) {
                                        g_MenuApplication->ShowNotification(GetLanguageString("app_verify_wait"));
                                        is_being_verified = true;
                                        break;
                                    }
                                }

                                if(!is_being_verified) {
                                    const auto opt = g_MenuApplication->DisplayDialog(GetLanguageString("app_launch"), GetLanguageString("app_corrupted"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true);
                                    if(opt == 0) {
                                        UL_RC_ASSERT(smi::StartVerifyApplication(cur_entry.app_info.app_id));
                                        g_GlobalSettings.in_verify_app_ids.push_back(cur_entry.app_info.app_id);
                                    }
                                }

                                do_launch_entry = false;
                            }
                            else if(cur_entry.app_info.IsGameCardNotInserted()) {
                                pu::audio::PlaySfx(this->error_sfx);
                                g_MenuApplication->ShowNotification(GetLanguageString("app_no_gamecard"));
                                do_launch_entry = false;
                            }
                            else if(!cur_entry.app_info.HasContents()) {
                                pu::audio::PlaySfx(this->error_sfx);
                                g_MenuApplication->ShowNotification(GetLanguageString("app_no_contents"));
                                do_launch_entry = false;
                            }
                            else if(!cur_entry.app_info.CanBeLaunched()) {
                                UL_LOG_WARN("Tried to launch non-launchable application 0x%016lX with record last event %d and view flags 0x%D", cur_entry.app_info.app_id, cur_entry.app_info.record.last_event, cur_entry.app_info.view.flags);
                                pu::audio::PlaySfx(this->error_sfx);
                                g_MenuApplication->ShowNotification(GetLanguageString("app_not_launchable"));
                                do_launch_entry = false;
                            }
                            else {
                                // Check if it can be launched
                                if(cur_entry.app_info.IsNotUpdated()) {
                                    pu::audio::PlaySfx(this->error_sfx);
                                    const auto fail_rc = nsCheckApplicationLaunchVersion(cur_entry.app_info.app_id);
                                    g_MenuApplication->ShowNotification(GetLanguageString("app_needs_update_cannot_launch") + ": " + util::FormatResultDisplay(fail_rc));
                                    do_launch_entry = false;
                                }
                                /*
                                else {
                                    const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("app_launch"), GetLanguageString("app_needs_update_can_launch"), { GetLanguageString("ok"), GetLanguageString("cancel") }, true);
                                    do_launch_entry = option == 0;
                                }
                                */
                            }
                        }

                        if(do_launch_entry) {
                            if(cur_entry.Is<EntryType::Homebrew>()) {
                                this->HandleHomebrewLaunch(cur_entry);
                            }
                            else {
                                pu::audio::PlaySfx(this->launch_app_sfx);

                                g_MenuApplication->FadeOutToNonLibraryApplet();
                                const auto rc = smi::LaunchApplication(cur_entry.app_info.app_id);
                                if(R_SUCCEEDED(rc)) {
                                    g_MenuApplication->Finalize();
                                    return;
                                }
                                else {
                                    g_MenuApplication->FadeIn();
                                    g_MenuApplication->ResetFade();
                                    g_MenuApplication->ShowNotification(GetLanguageString("app_launch_error") + ": " + util::FormatResultDisplay(rc));
                                }
                            }
                        }
                    }
                    else {
                        switch(cur_entry.type) {
                            case EntryType::SpecialEntryMiiEdit: {
                                pu::audio::PlaySfx(this->open_mii_edit_sfx);
                                ShowMiiEdit();
                                break;
                            }
                            case EntryType::SpecialEntryWebBrowser: {
                                pu::audio::PlaySfx(this->open_web_browser_sfx);
                                ShowWebPage();
                                break;
                            }
                            case EntryType::SpecialEntryUserPage: {
                                pu::audio::PlaySfx(this->open_user_page_sfx);
                                ShowUserPage();
                                break;
                            }
                            case EntryType::SpecialEntrySettings: {
                                pu::audio::PlaySfx(this->open_settings_sfx);
                                ShowSettingsMenu();
                                break;
                            }
                            case EntryType::SpecialEntryThemes: {
                                pu::audio::PlaySfx(this->open_themes_sfx);
                                ShowThemesMenu();
                                break;
                            }
                            case EntryType::SpecialEntryControllers: {
                                pu::audio::PlaySfx(this->open_controllers_sfx);
                                ShowController();
                                break;
                            }
                            case EntryType::SpecialEntryAlbum: {
                                pu::audio::PlaySfx(this->open_album_sfx);
                                ShowAlbum();
                                break;
                            }
                            case EntryType::SpecialEntryAmiibo: {
                                pu::audio::PlaySfx(this->open_amiibo_sfx);

                                ShowCabinet();
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            else {
                // Move entry to a currently empty position
                if(this->entry_menu->IsAnySelected()) {
                    auto &sel_entry = this->entry_menu->GetSelectedEntry();
                    Entry prev_entry(sel_entry);
                    pu::audio::PlaySfx(this->entry_move_sfx);
                    if(sel_entry.MoveToIndex(this->entry_menu->GetFocusedEntryIndex())) {
                        this->StopSelection();
                        this->entry_menu->NotifyEntryRemoved(prev_entry);
                        this->entry_menu->NotifyEntryAdded(sel_entry);
                        this->entry_menu->OrganizeUpdateEntries();
                    }
                    else {
                        // Should not happen...
                    }
                }
                else {
                    const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("menu_new_entry"), GetLanguageString("menu_new_entry_conf"), { GetLanguageString("menu_new_folder"), GetLanguageString("menu_add_hb"), GetLanguageString("cancel") }, true);
                    if(option == 0) {
                        SwkbdConfig swkbd;
                        UL_RC_ASSERT(swkbdCreate(&swkbd, 0));
                        swkbdConfigMakePresetDefault(&swkbd);
                        swkbdConfigSetType(&swkbd, SwkbdType_All);
                        swkbdConfigSetGuideText(&swkbd, GetLanguageString("swkbd_folder_name_guide").c_str());
                        char new_folder_name_buf[500] = {};
                        const auto rc = ShowSwkbd(&swkbd, new_folder_name_buf, sizeof(new_folder_name_buf));
                        swkbdClose(&swkbd);

                        std::string new_folder_name(new_folder_name_buf);
                        while(!new_folder_name.empty() && new_folder_name.at(0) == ' ') {
                            new_folder_name.erase(new_folder_name.begin());
                        }
                        if(R_SUCCEEDED(rc) && !new_folder_name.empty()) {
                            pu::audio::PlaySfx(this->create_folder_sfx);

                            const auto folder_entry = CreateFolderEntry(this->entry_menu->GetPath(), new_folder_name, this->entry_menu->GetFocusedEntryIndex());
                            this->entry_menu->NotifyEntryAdded(folder_entry);
                            this->entry_menu->OrganizeUpdateEntries();
                            g_MenuApplication->ShowNotification(GetLanguageString("menu_folder_created"));
                        }
                    }
                    else if(option == 1) {
                        g_MenuApplication->FadeOutToNonLibraryApplet();
                        UL_RC_ASSERT(smi::ChooseHomebrew());
                        g_MenuApplication->Finalize();
                    }
                }
            }
        }
        else if(keys_down & HidNpadButton_Y) {
            if(!this->entry_menu->IsAnySelected() && this->entry_menu->IsFocusedNonemptyEntry()) {
                pu::audio::PlaySfx(this->entry_select_sfx);
                this->entry_menu->ToggleFocusedEntrySelected();
            }
        }
        else if(keys_down & HidNpadButton_X) {
            if(this->entry_menu->IsAnySelected()) {
                pu::audio::PlaySfx(this->entry_cancel_select_sfx);
                
                this->StopSelection();
            }
            else if(this->entry_menu->IsFocusedNonemptyEntry()) {
                auto &cur_entry = this->entry_menu->GetFocusedEntry();

                if(g_GlobalSettings.IsSuspended() && g_GlobalSettings.IsEntrySuspended(cur_entry)) {
                    this->HandleCloseSuspended();
                }
                else {
                    if(cur_entry.Is<EntryType::Folder>()) {
                        std::vector<std::string> options = { GetLanguageString("entry_folder_rename"), GetLanguageString("entry_remove") };
                        if(!this->entry_menu->IsInRoot()) {
                            options.push_back(GetLanguageString("entry_move_parent"));
                            options.push_back(GetLanguageString("entry_move_root"));
                        }
                        options.push_back(GetLanguageString("cancel"));
                        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("entry_options"), GetLanguageString("entry_action"), options, true);
                        if(option == 0) {
                            SwkbdConfig swkbd;
                            UL_RC_ASSERT(swkbdCreate(&swkbd, 0));
                            swkbdConfigMakePresetDefault(&swkbd);
                            swkbdConfigSetType(&swkbd, SwkbdType_All);
                            swkbdConfigSetInitialText(&swkbd, cur_entry.folder_info.name);
                            swkbdConfigSetGuideText(&swkbd, GetLanguageString("swkbd_folder_name_guide").c_str());
                            char new_folder_name[500] = {};
                            const auto rc = ShowSwkbd(&swkbd, new_folder_name, sizeof(new_folder_name));
                            swkbdClose(&swkbd);
                            
                            if(R_SUCCEEDED(rc)) {
                                util::CopyToStringBuffer(cur_entry.folder_info.name, new_folder_name);
                                cur_entry.Save();
                                this->entry_menu->OrganizeUpdateEntries();
                                g_MenuApplication->ShowNotification(GetLanguageString("menu_folder_renamed"));
                            }
                        }
                        else if(option == 1) {
                            const auto option_2 = g_MenuApplication->DisplayDialog(GetLanguageString("entry_remove"), GetLanguageString("entry_remove_conf"), { GetLanguageString("yes"), GetLanguageString("no") }, true);
                            if(option_2 == 0) {
                                this->RemoveEntry(cur_entry);
                                g_MenuApplication->ShowNotification(GetLanguageString("entry_remove_ok"));
                            }
                        }
                        else if(option == 2) {
                            this->MoveEntryToParentFolder(cur_entry);
                        }
                        else if(option == 3) {
                            this->MoveEntryToRoot(cur_entry);
                        }
                    }
                    else if(cur_entry.Is<EntryType::Homebrew>()) {
                        std::vector<std::string> options = { GetLanguageString("entry_remove") };
                        if(!this->entry_menu->IsInRoot()) {
                            options.push_back(GetLanguageString("entry_move_parent"));
                            options.push_back(GetLanguageString("entry_move_root"));
                        }
                        options.push_back(GetLanguageString("cancel"));
                        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("entry_options"), GetLanguageString("entry_action"), options, true);
                        if(option == 0) {
                            if(IsEntryNonRemovable(cur_entry)) {
                                g_MenuApplication->ShowNotification(GetLanguageString("entry_remove_special"));
                            }
                            else {
                                const auto option_2 = g_MenuApplication->DisplayDialog(GetLanguageString("entry_remove"), GetLanguageString("entry_remove_conf"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true);
                                if(option_2 == 0) {
                                    this->RemoveEntry(cur_entry);
                                    g_MenuApplication->ShowNotification(GetLanguageString("entry_remove_ok"));
                                }
                            }
                        }
                        else if(option == 1) {
                            this->MoveEntryToParentFolder(cur_entry);
                        }
                        else if(option == 2) {
                            this->MoveEntryToRoot(cur_entry);
                        }
                    }
                    else if(cur_entry.Is<EntryType::Application>()) {
                        std::vector<std::string> options = {};
                        s32 cur_option_idx = 0;
                        
                        const auto has_app_take_over = g_GlobalSettings.cache_hb_takeover_app_id != cur_entry.app_info.app_id;
                        s32 app_take_over_opt = -1;
                        if(has_app_take_over) {
                            options.push_back(GetLanguageString("app_take_over"));
                            app_take_over_opt = cur_option_idx; cur_option_idx++;
                        }

                        const auto has_non_root_opts = !this->entry_menu->IsInRoot();
                        s32 entry_move_parent_opt = -1;
                        s32 entry_move_root_opt = -1;
                        if(has_non_root_opts) {
                            options.push_back(GetLanguageString("entry_move_parent"));
                            entry_move_parent_opt = cur_option_idx; cur_option_idx++;
                            options.push_back(GetLanguageString("entry_move_root"));
                            entry_move_root_opt = cur_option_idx; cur_option_idx++;
                        }

                        options.push_back(GetLanguageString("cancel"));

                        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("entry_options"), GetLanguageString("entry_action"), options, true);
                        if(has_app_take_over && (option == app_take_over_opt)) {
                            const auto option_2 = g_MenuApplication->DisplayDialog(GetLanguageString("app_launch"), GetLanguageString("app_take_over_select"), { GetLanguageString("yes"), GetLanguageString("cancel") }, true);
                            if(option_2 == 0) {
                                g_GlobalSettings.SetHomebrewTakeoverApplicationId(cur_entry.app_info.record.id);
                                g_MenuApplication->ShowNotification(GetLanguageString("app_take_over_done"));
                            }
                        }
                        if(has_non_root_opts && (option == entry_move_parent_opt)) {
                            this->MoveEntryToParentFolder(cur_entry);
                        }
                        if(has_non_root_opts && (option == entry_move_root_opt)) {
                            this->MoveEntryToRoot(cur_entry);
                        }
                    }
                    else if(cur_entry.IsSpecial()) {
                        std::vector<std::string> options = { };
                        if(!this->entry_menu->IsInRoot()) {
                            options.push_back(GetLanguageString("entry_move_parent"));
                            options.push_back(GetLanguageString("entry_move_root"));
                        }
                        options.push_back(GetLanguageString("cancel"));
                        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("entry_options"), GetLanguageString("entry_action"), options, true);
                        if(option == 0) {
                            this->MoveEntryToParentFolder(cur_entry);
                        }
                        else if(option == 1) {
                            this->MoveEntryToRoot(cur_entry);
                        }
                    }
                }
            }
        }
        else if(keys_down & HidNpadButton_L) {
            if(this->entry_menu->MoveToPreviousPage()) {
                pu::audio::PlaySfx(this->page_move_sfx);
            }
        }
        else if(keys_down & HidNpadButton_R) {
            if(this->entry_menu->MoveToNextPage()) {
                pu::audio::PlaySfx(this->page_move_sfx);
            }
        }
    }

    void MainMenuLayout::menu_FocusedEntryChanged(const bool has_prev_entry, const bool is_prev_entry_suspended, const bool is_cur_entry_suspended) {
#ifdef QDESKTOP_MODE
        // qdesktop has no entry_menu / cur_entry_*_text; nothing to refresh.
        (void)has_prev_entry; (void)is_prev_entry_suspended; (void)is_cur_entry_suspended;
        return;
#endif
        this->cur_entry_main_text->SetVisible(true);
        this->cur_entry_sub_text->SetVisible(true);
        this->input_bar_changed = true;

        this->entry_menu_left_icon->SetVisible(!this->entry_menu->IsMenuStart());

        g_GlobalSettings.UpdateMenuIndex(this->entry_menu->GetFocusedEntryIndex());

        if(this->entry_menu->IsFocusedNonemptyEntry()) {
            auto &cur_entry = this->entry_menu->GetFocusedEntry();
            if(cur_entry.Is<EntryType::Folder>()) {
                this->SetTopMenuFolder();
                this->cur_entry_main_text->SetText(cur_entry.folder_info.name);
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryMiiEdit>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_mii_edit"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryWebBrowser>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_web_browser"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryUserPage>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(g_UserName);
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntrySettings>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_settings"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryThemes>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_themes"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryControllers>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_controllers"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryAlbum>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_album"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else if(cur_entry.Is<EntryType::SpecialEntryAmiibo>()) {
                this->SetTopMenuDefault();
                this->cur_entry_main_text->SetText(GetLanguageString("special_entry_text_amiibo"));
                this->cur_entry_sub_text->SetVisible(false);
            }
            else {
                if(cur_entry.Is<EntryType::Application>()) {
                    this->SetTopMenuApplication();
                }
                else {
                    this->SetTopMenuHomebrew();
                }

                cur_entry.TryLoadNacp();

                if(!cur_entry.control.name.empty()) {
                    this->cur_entry_main_text->SetText(cur_entry.control.name);
                }
                else {
                    this->cur_entry_main_text->SetText("???");
                }

                if(!cur_entry.control.author.empty()) {
                    if(!cur_entry.control.version.empty()) {
                        this->cur_entry_sub_text->SetText(cur_entry.control.version + ", " + cur_entry.control.author);
                    }
                    else {
                        this->cur_entry_sub_text->SetText(cur_entry.control.author);
                    }
                }
                else {
                    if(!cur_entry.control.version.empty()) {
                        this->cur_entry_sub_text->SetText(cur_entry.control.version);
                    }
                    else {
                        this->cur_entry_sub_text->SetText("???");
                    }
                }
            }
        }
        else {
            this->SetTopMenuDefault();
            this->cur_entry_main_text->SetVisible(false);
            this->cur_entry_sub_text->SetVisible(false);
        }

        if(g_GlobalSettings.IsSuspended() && has_prev_entry) {
            if(is_prev_entry_suspended && !is_cur_entry_suspended) {
                RequestHideLoseFocusScreenCaptureBackground();
            }
            else if(!is_prev_entry_suspended && is_cur_entry_suspended) {
                RequestShowGainFocusScreenCaptureBackground();
            }
        }
    }

    void MainMenuLayout::LaunchHomebrewApplication(const Entry &hb_entry) {
#ifdef QDESKTOP_MODE
        // qdesktop will route launches through QdLauncher (SP4); until then,
        // upstream homebrew launch is gated off to avoid deref of null members.
        (void)hb_entry;
        return;
#endif
        // Take care if there is a suspended app
        auto do_launch = true;
        if(g_GlobalSettings.IsSuspended()) {
            do_launch = false;
            this->HandleCloseSuspended();
            if(!g_GlobalSettings.IsSuspended()) {
                do_launch = true;
            }
        }

        if(do_launch) {
            pu::audio::PlaySfx(this->launch_hb_sfx);
            
            const auto ipt = CreateLaunchTargetInput(hb_entry.hb_info.nro_target);

            g_MenuApplication->FadeOutToNonLibraryApplet();
            const auto rc = smi::LaunchHomebrewApplication(ipt.nro_path, ipt.nro_argv);
            if(R_SUCCEEDED(rc)) {
                g_MenuApplication->Finalize();
                return;
            }
            else {
                g_MenuApplication->FadeIn();
                g_MenuApplication->ResetFade();
                g_MenuApplication->ShowNotification(GetLanguageString("app_launch_error") + ": " + util::FormatResultDisplay(rc));
            }
        }
    }

    MainMenuLayout::MainMenuLayout() : IMenuLayout(), last_quick_menu_on(false), start_time_elapsed(false), is_incrementing_decrementing(false), next_reload_user_changed(false) {
        UL_LOG_INFO("Creating MainMenuLayout...");
        const auto time = std::chrono::system_clock::now();
        this->cur_folder_path = g_GlobalSettings.system_status.last_menu_path;

        this->quick_menu = nullptr;

        this->post_suspend_sfx = nullptr;
        this->cursor_move_sfx = nullptr;
        this->page_move_sfx = nullptr;
        this->entry_select_sfx = nullptr;
        this->entry_move_sfx = nullptr;
        this->entry_swap_sfx = nullptr;
        this->entry_cancel_select_sfx = nullptr;
        this->entry_move_into_sfx = nullptr;
        this->home_press_sfx = nullptr;
        this->logoff_sfx = nullptr;
        this->launch_app_sfx = nullptr;
        this->launch_hb_sfx = nullptr;
        this->close_suspended_sfx = nullptr;
        this->open_folder_sfx = nullptr;
        this->close_folder_sfx = nullptr;
        this->open_mii_edit_sfx = nullptr;
        this->open_web_browser_sfx = nullptr;
        this->open_user_page_sfx = nullptr;
        this->open_settings_sfx = nullptr;
        this->open_themes_sfx = nullptr;
        this->open_controllers_sfx = nullptr;
        this->open_album_sfx = nullptr;
        this->open_amiibo_sfx = nullptr;
        this->open_quick_menu_sfx = nullptr;
        this->close_quick_menu_sfx = nullptr;
        this->resume_app_sfx = nullptr;
        this->create_folder_sfx = nullptr;
        this->create_hb_entry_sfx = nullptr;
        this->entry_remove_sfx = nullptr;
        this->error_sfx = nullptr;
        this->menu_increment_sfx = nullptr;
        this->menu_decrement_sfx = nullptr;

#ifdef QDESKTOP_MODE
        {
            // Q OS desktop mode: wallpaper + icon grid + top bar are the only
            // elements added.  The upstream icon ring, entry menu, and quick
            // menu are NOT instantiated.  Top-bar elements are still owned by
            // upstream IMenuLayout helpers (UpdateTimeText et al.), so we
            // initialise them here and the OnMenuUpdate gate refreshes them.
            const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();
            this->qdesktop_wallpaper = qdesktop::QdWallpaperElement::New(qdt);
            this->Add(this->qdesktop_wallpaper);
            this->qdesktop_icons = qdesktop::QdDesktopIconsElement::New(qdt);
            this->Add(this->qdesktop_icons);
            // NOTE: Application + Special entries are populated in Initialize()
            // (which runs AFTER this constructor and AFTER InitializeEntries()).
            // Calling LoadEntries() here returns an empty vector because the
            // upstream menu system hasn't loaded the per-user entry directory
            // yet.  See MainMenuLayout::Initialize() below.

            // ── Top bar ──────────────────────────────────────────────────
            // Connection / battery / time / date are pinned to explicit pixel
            // positions that form a clean horizontal row inside the 48 px
            // translucent backing strip drawn by QdDesktopIconsElement::OnRender.
            //
            // ApplyConfigForElement() is NOT called for any element here.
            // The JSON config was authored for the stock uLaunch layout
            // (top-menu-bar tabs) which does NOT exist in QDESKTOP_MODE.
            // Applying it would scatter the elements across the screen.
            //
            // IMPORTANT — MultiTextBlock reflow:
            // InitializeTimeText() calls ApplyConfigForElement internally
            // (which sets wrong upstream coords), then calls UpdatePositionsSizes()
            // which bakes child block positions at those wrong coords.
            // After SetX/SetY we MUST call UpdatePositionsSizes() again to
            // re-bake the children at the correct position.
            //
            // InitializeTimeText also calls this->Add(time_mtext) and
            // this->Add(time_mtext->Get(0/1)) internally — do NOT call Add
            // again for time_mtext or you get a double-add.
            //
            // ── Top-bar layout — formula-derived, NOT cargo-culted ──────────
            //
            // Reference: Q OS GUI proof-of-concept (mock-nro-desktop-gui v1.1.12,
            // src/wm.rs). PoC renders at 1280×720 with a 32 px strip; uMenu renders
            // at 1920×1080 with a 48 px strip. Scale factor = 1.5×.
            //
            // Formula constants (all at 1920×1080):
            //   TOPBAR_H  = qdesktop::TOPBAR_H            = 48 px (Rust 32 × 1.5)
            //   SAFE_LEFT = 12  (Rust 8  × 1.5)           // left-most element x
            //   SAFE_RIGHT= 24  (Rust 16 × 1.5)           // right margin from screen edge
            //   ICON_W    = ICON_H = 32                   // top-bar icons (TOPBAR_H − 16)
            //   GUTTER    = 16                            // K-cycle Track B: 16 px spec
            //
            // Vertical centering (text height = font size × 1.5):
            //   Y_LARGE = (TOPBAR_H − 24) / 2 = 12        // Medium font (~16×1.5)
            //   Y_SMALL = (TOPBAR_H − 12) / 2 = 18        // Small  font (~ 8×1.5)
            //   ICON_Y  = (TOPBAR_H − 32) / 2 = 8
            //
            // Horizontal anchoring (left → right and right → left):
            //   TIME_X      = SAFE_LEFT                           = 12
            //   DATE_X      = SAFE_LEFT + 188                     = 200  (≈ 12-char Medium block)
            //   BATT_TEXT_W ≈ 4 chars × 12 px/char               = 48   ("100%" Medium width)
            //   BATT_TEXT_X = SCREEN_W − SAFE_RIGHT − BATT_TEXT_W
            //                                                     = 1920 − 24 − 48 = 1848
            //   BATT_ICON_X = BATT_TEXT_X − GUTTER − ICON_W       = 1848 − 16 − 32 = 1800
            //   CONN_ICON_X = BATT_ICON_X − GUTTER − ICON_W       = 1800 − 16 − 32 = 1752
            //   BT_ICON_X   = CONN_ICON_X − GUTTER − ICON_W       = 1752 − 16 − 32 = 1704
            //                 (hidden when no BT audio device connected)
            //
            //  ┌────────────────────────────────────────────────────────────────────┐ y=0
            //  │ [time]  [date]               [bt?] [conn] [batt_icon] [batt%]     │ Y_LARGE
            //  └────────────────────────────────────────────────────────────────────┘ y=48
            //   12     200                    1704   1752    1800         1848
            //
            // CRITICAL: the connection / battery PNG assets are 100×100 px. Without
            // explicit SetWidth(32)/SetHeight(32) they overflow the 48 px strip
            // by 52 px down — that is what made every prior top-bar attempt look
            // wrong. The fix is the resize call, not yet another x coordinate.
            //
            // IMPORTANT — MultiTextBlock reflow:
            // InitializeTimeText() calls ApplyConfigForElement internally
            // (which sets wrong upstream coords), then calls UpdatePositionsSizes()
            // which bakes child block positions at those wrong coords. After SetX/
            // SetY we MUST call UpdatePositionsSizes() again to re-bake the
            // children at the correct position. InitializeTimeText also calls
            // this->Add(time_mtext) and this->Add(time_mtext->Get(0/1)) internally
            // — do NOT call Add again for time_mtext or you get a double-add.

            // v1.7.0-stabilize-2: TOPBAR_SAFE_LEFT removed -- TOPBAR_TIME_X
            // is now anchored explicitly to LP_HOTCORNER_W + TOPBAR_GUTTER
            // (= 112) instead of the legacy SAFE_LEFT margin (12). Removing
            // the unused constant avoids -Werror=unused-variable.
            constexpr s32 TOPBAR_SAFE_RIGHT = 24;
            constexpr s32 TOPBAR_ICON_W     = 32;   // square — width == height
            constexpr s32 TOPBAR_ICON_H     = 32;
            constexpr s32 TOPBAR_GUTTER     = 16;   // K-cycle Track B: 16 px inter-element padding
            constexpr s32 TOPBAR_Y_LARGE    = 12;   // (48 − 24) / 2
            constexpr s32 TOPBAR_Y_SMALL    = 18;   // (48 − 12) / 2
            constexpr s32 TOPBAR_ICON_Y     = 8;    // (48 − 32) / 2

            // Right-side anchoring: BATT_TEXT_X is computed from screen width, so
            // a future top-bar widening or screen-size change just propagates.
            constexpr s32 TOPBAR_BATT_TEXT_W = 48;  // ~"100%" in Medium font
            constexpr s32 SCREEN_W           = 1920;
            constexpr s32 BATT_TEXT_X        = SCREEN_W - TOPBAR_SAFE_RIGHT - TOPBAR_BATT_TEXT_W; // 1848
            constexpr s32 BATT_ICON_X        = BATT_TEXT_X - TOPBAR_GUTTER - TOPBAR_ICON_W;       // 1800
            constexpr s32 CONN_ICON_X        = BATT_ICON_X - TOPBAR_GUTTER - TOPBAR_ICON_W;       // 1752
            constexpr s32 BT_ICON_X          = CONN_ICON_X - TOPBAR_GUTTER - TOPBAR_ICON_W;       // 1704
            // v1.7.0-stabilize-2: time/date widgets shifted right past the
            // hot-corner widget so a 96x72 widget at (0,0) does not overlap
            // the time text. 96 (LP_HOTCORNER_W) + 16 (TOPBAR_GUTTER) = 112.
            // The date offset stays the same delta from time (188 px) so the
            // 12-char Medium block layout is preserved verbatim.
            constexpr s32 TOPBAR_TIME_X      = 112;                     // was TOPBAR_SAFE_LEFT (12)
            constexpr s32 TOPBAR_DATE_X      = TOPBAR_TIME_X + 188;     // 300 (was 200)

            // ── time (left, large) ────────────────────────────────────────────
            this->InitializeTimeText(this->time_mtext, "main_menu", "time_text");
            this->time_mtext->SetX(TOPBAR_TIME_X);
            this->time_mtext->SetY(TOPBAR_Y_LARGE);
            this->time_mtext->UpdatePositionsSizes();
            UL_LOG_INFO("[QDESKTOP topbar] time_mtext: x=%d y=%d w=%d h=%d (formula TIME_X/Y_LARGE)",
                this->time_mtext->GetX(), this->time_mtext->GetY(),
                this->time_mtext->GetWidth(), this->time_mtext->GetHeight());

            // ── date (left, small) ────────────────────────────────────────────
            this->date_text = pu::ui::elm::TextBlock::New(TOPBAR_DATE_X, TOPBAR_Y_SMALL, "...");
            this->date_text->SetColor(g_MenuApplication->GetTextColor());
            this->Add(this->date_text);
            this->date_text->SetX(TOPBAR_DATE_X);
            this->date_text->SetY(TOPBAR_Y_SMALL);
            UL_LOG_INFO("[QDESKTOP topbar] date_text: x=%d y=%d w=%d h=%d (formula DATE_X/Y_SMALL=%d)",
                this->date_text->GetX(), this->date_text->GetY(),
                this->date_text->GetWidth(), this->date_text->GetHeight(),
                TOPBAR_Y_SMALL);

            // ── connection icon (right, resized 100→32) ──────────────────────
            this->connection_top_icon = pu::ui::elm::Image::New(CONN_ICON_X, TOPBAR_ICON_Y, TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Connection/None"));
            this->Add(this->connection_top_icon);
            this->connection_top_icon->SetX(CONN_ICON_X);
            this->connection_top_icon->SetY(TOPBAR_ICON_Y);
            this->connection_top_icon->SetWidth(TOPBAR_ICON_W);   // CRITICAL: 100 → 32
            this->connection_top_icon->SetHeight(TOPBAR_ICON_H);
            UL_LOG_INFO("[QDESKTOP topbar] connection_top_icon: x=%d y=%d w=%d h=%d (resized 100→32)",
                this->connection_top_icon->GetX(), this->connection_top_icon->GetY(),
                this->connection_top_icon->GetWidth(), this->connection_top_icon->GetHeight());

            // ── battery icon (right, resized 100→32) ─────────────────────────
            this->battery_top_icon = pu::ui::elm::Image::New(BATT_ICON_X, TOPBAR_ICON_Y, TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Battery/100"));
            this->battery_charging_top_icon = pu::ui::elm::Image::New(BATT_ICON_X, TOPBAR_ICON_Y, TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Battery/Charging"));
            this->battery_charging_top_icon->SetVisible(false);
            this->Add(this->battery_top_icon);
            this->battery_top_icon->SetX(BATT_ICON_X);
            this->battery_top_icon->SetY(TOPBAR_ICON_Y);
            this->battery_top_icon->SetWidth(TOPBAR_ICON_W);   // CRITICAL: 100 → 32
            this->battery_top_icon->SetHeight(TOPBAR_ICON_H);
            UL_LOG_INFO("[QDESKTOP topbar] battery_top_icon: x=%d y=%d w=%d h=%d (resized 100→32)",
                this->battery_top_icon->GetX(), this->battery_top_icon->GetY(),
                this->battery_top_icon->GetWidth(), this->battery_top_icon->GetHeight());
            this->Add(this->battery_charging_top_icon);
            this->battery_charging_top_icon->SetX(BATT_ICON_X);
            this->battery_charging_top_icon->SetY(TOPBAR_ICON_Y);
            this->battery_charging_top_icon->SetWidth(TOPBAR_ICON_W);
            this->battery_charging_top_icon->SetHeight(TOPBAR_ICON_H);
            UL_LOG_INFO("[QDESKTOP topbar] battery_charging_top_icon: x=%d y=%d w=%d h=%d (resized 100→32)",
                this->battery_charging_top_icon->GetX(), this->battery_charging_top_icon->GetY(),
                this->battery_charging_top_icon->GetWidth(), this->battery_charging_top_icon->GetHeight());

            // ── battery percentage text (right, large) ───────────────────────
            this->battery_text = pu::ui::elm::TextBlock::New(BATT_TEXT_X, TOPBAR_Y_LARGE, "...");
            this->battery_text->SetColor(g_MenuApplication->GetTextColor());
            this->Add(this->battery_text);
            this->battery_text->SetX(BATT_TEXT_X);
            this->battery_text->SetY(TOPBAR_Y_LARGE);
            UL_LOG_INFO("[QDESKTOP topbar] battery_text: x=%d y=%d w=%d h=%d (formula BATT_TEXT_X/Y_LARGE)",
                this->battery_text->GetX(), this->battery_text->GetY(),
                this->battery_text->GetWidth(), this->battery_text->GetHeight());

            // ── Bluetooth icon (right of CONN, hidden until device connects) ──
            // Asset: ui/Main/TopIcon/Bluetooth.png (100×100 RGBA, resized to 32×32).
            // Visibility is driven by OnMenuUpdate via bt::GetConnectedAudioDevice().
            // Initial state = hidden; the first OnMenuUpdate call will show it if a
            // BT audio device is already paired.
            this->qdesktop_bt_top_icon = pu::ui::elm::Image::New(BT_ICON_X, TOPBAR_ICON_Y, TryFindLoadImageHandleDefaultOnly("ui/Main/TopIcon/Bluetooth"));
            this->qdesktop_bt_top_icon->SetX(BT_ICON_X);
            this->qdesktop_bt_top_icon->SetY(TOPBAR_ICON_Y);
            this->qdesktop_bt_top_icon->SetWidth(TOPBAR_ICON_W);
            this->qdesktop_bt_top_icon->SetHeight(TOPBAR_ICON_H);
            this->qdesktop_bt_top_icon->SetVisible(false);
            this->Add(this->qdesktop_bt_top_icon);
            this->qdesktop_last_bt_connected = false;
            UL_LOG_INFO("[QDESKTOP topbar] qdesktop_bt_top_icon: x=%d y=%d w=%d h=%d (BT_ICON_X=%d, initially hidden)",
                this->qdesktop_bt_top_icon->GetX(), this->qdesktop_bt_top_icon->GetY(),
                this->qdesktop_bt_top_icon->GetWidth(), this->qdesktop_bt_top_icon->GetHeight(),
                BT_ICON_X);

            // ── Cursor (LAST, renders on top of icons + top bar) ─────────
            // Plutonium dispatches OnInput per-element each frame, so the
            // cursor's own OnInput consumes the layout-space TouchPoint and
            // tracks the pointer.  Layout-level routing is unnecessary —
            // the cursor element is self-driving.
            this->qdesktop_cursor = qdesktop::QdCursorElement::New(qdt);
            this->Add(this->qdesktop_cursor);

            // Wire the cursor reference into the icons element so A-button
            // launches the icon under the cursor position rather than the
            // D-pad focused index.
            this->qdesktop_icons->SetCursorRef(this->qdesktop_cursor);
        }
        // Skip every other upstream UI element below — they don't exist in
        // qdesktop mode.  All upstream-element-touching member functions also
        // early-return under QDESKTOP_MODE; see LoadSfx, DisposeSfx,
        // OnMenuInput, OnHomeButtonPress, Reload, MoveTo, DoMoveTo,
        // HandleCloseSuspended, HandleHomebrewLaunch, StopSelection,
        // DoTerminateApplication, menu_EntryInputPressed,
        // menu_FocusedEntryChanged, LaunchHomebrewApplication.
        return;
#endif

        // Load banners first
        this->top_menu_default_bg = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopMenuBackground/Default"));
        this->top_menu_folder_bg = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopMenuBackground/Folder"));
        this->top_menu_app_bg = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopMenuBackground/Application"));
        this->top_menu_hb_bg = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopMenuBackground/Homebrew"));
        g_GlobalSettings.ApplyConfigForElement("main_menu", "top_menu_bg", this->top_menu_default_bg);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "top_menu_bg", this->top_menu_folder_bg);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "top_menu_bg", this->top_menu_app_bg);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "top_menu_bg", this->top_menu_hb_bg);
        this->Add(this->top_menu_default_bg);
        this->Add(this->top_menu_folder_bg);
        this->Add(this->top_menu_app_bg);
        this->Add(this->top_menu_hb_bg);

        // Then load buttons and other UI elements
#ifndef QDESKTOP_MODE
        // In QDESKTOP_MODE the top-bar replaces the uLaunch logo slot, so skip
        // instantiating logo_top_icon entirely (logo_top_icon stays nullptr).
        this->logo_top_icon = ClickableImage::New(0, 0, GetLogoTexture());
        this->logo_top_icon->SetWidth(LogoSize);
        this->logo_top_icon->SetHeight(LogoSize);
        this->logo_top_icon->SetOnClick(&ShowAboutDialog);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "logo_top_icon", this->logo_top_icon, false); // Sorry theme makers... uLaunch's logo must be visible, but can be moved
        this->Add(this->logo_top_icon);
#endif

        this->connection_top_icon = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopIcon/Connection/None"));
        g_GlobalSettings.ApplyConfigForElement("main_menu", "connection_top_icon", this->connection_top_icon);
        this->Add(this->connection_top_icon);

        this->InitializeTimeText(this->time_mtext, "main_menu", "time_text");

        this->date_text = pu::ui::elm::TextBlock::New(0, 0, "...");
        this->date_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("main_menu", "date_text", this->date_text);
        this->Add(this->date_text);

        this->battery_text = pu::ui::elm::TextBlock::New(0, 0, "...");
        this->battery_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("main_menu", "battery_text", this->battery_text);
        this->Add(this->battery_text);

        this->battery_top_icon = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopIcon/Battery/100"));
        this->battery_charging_top_icon = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/TopIcon/Battery/Charging"));
        this->battery_charging_top_icon->SetVisible(false);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "battery_top_icon", this->battery_top_icon);
        g_GlobalSettings.ApplyConfigForElement("main_menu", "battery_top_icon", this->battery_charging_top_icon);
        this->Add(this->battery_top_icon);
        this->Add(this->battery_charging_top_icon);

        this->input_bar = InputBar::New(0, 0, "ui/Main/InputBarBackground");
        g_GlobalSettings.ApplyConfigForElement("main_menu", "input_bar", this->input_bar);
        this->Add(this->input_bar);
        this->input_bar_changed = true;

        this->cur_path_text = pu::ui::elm::TextBlock::New(0, 0, this->cur_folder_path);
        this->cur_path_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("main_menu", "cur_path_text", this->cur_path_text);

        this->cur_entry_main_text = pu::ui::elm::TextBlock::New(0, 0, "...");
        this->cur_entry_main_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("main_menu", "cur_entry_main_text", this->cur_entry_main_text);

        this->cur_entry_sub_text = pu::ui::elm::TextBlock::New(0, 0, "...");
        this->cur_entry_sub_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("main_menu", "cur_entry_sub_text", this->cur_entry_sub_text);

        this->entry_menu_bg = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/EntryMenuBackground"));
        g_GlobalSettings.ApplyConfigForElement("main_menu", "entry_menu_bg", this->entry_menu_bg);
        this->Add(this->entry_menu_bg);

        this->entry_menu_left_icon = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/EntryMenuLeftIcon"));
        g_GlobalSettings.ApplyConfigForElement("main_menu", "entry_menu_left_icon", this->entry_menu_left_icon);
        this->Add(this->entry_menu_left_icon);

        this->entry_menu_right_icon = pu::ui::elm::Image::New(0, 0, TryFindLoadImageHandle("ui/Main/EntryMenuRightIcon"));
        g_GlobalSettings.ApplyConfigForElement("main_menu", "entry_menu_right_icon", this->entry_menu_right_icon);
        this->Add(this->entry_menu_right_icon);

        this->Add(this->cur_entry_main_text);
        this->Add(this->cur_entry_sub_text);

        this->Add(this->cur_path_text);
        UL_LOG_INFO("MainMenuLayout create: done before entry menu, so far %lld ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - time).count());

        this->entry_menu = EntryMenu::New(0, 0, g_GlobalSettings.system_status.last_menu_fs_path, std::bind(&MainMenuLayout::menu_EntryInputPressed, this, std::placeholders::_1), std::bind(&MainMenuLayout::menu_FocusedEntryChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), [&]() {
            pu::audio::PlaySfx(this->cursor_move_sfx);
        });
        g_GlobalSettings.ApplyConfigForElement("main_menu", "entry_menu", this->entry_menu);
        this->Add(this->entry_menu);

        this->Add(GetScreenCaptureBackground());

        this->quick_menu = QuickMenu::New();
        this->Add(this->quick_menu);

        this->startup_tp = std::chrono::steady_clock::now();
        UL_LOG_INFO("MainMenuLayout created in %lld ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - time).count());
    }

    void MainMenuLayout::LoadSfx() {
#ifdef QDESKTOP_MODE
        // qdesktop has no upstream sfx in v0.21; cursor-click + launch-confirm
        // sfx land in v0.24 via QdAudio.  Skip until then so we don't fault on
        // missing theme resources that the upstream tree expects.
        return;
#endif
        this->post_suspend_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/PostSuspend.wav"));
        this->cursor_move_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CursorMove.wav"));
        this->page_move_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/PageMove.wav"));
        this->entry_select_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntrySelect.wav"));
        this->entry_move_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntryMove.wav"));
        this->entry_swap_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntrySwap.wav"));
        this->entry_cancel_select_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntryCancelSelect.wav"));
        this->entry_move_into_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntryMoveInto.wav"));
        this->home_press_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/HomePress.wav"));
        this->logoff_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/Logoff.wav"));
        this->launch_app_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/LaunchApplication.wav"));
        this->launch_hb_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/LaunchHomebrew.wav"));
        this->close_suspended_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CloseSuspended.wav"));
        this->open_folder_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenFolder.wav"));
        this->close_folder_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CloseFolder.wav"));
        this->open_mii_edit_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenMiiEdit.wav"));
        this->open_web_browser_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenWebBrowser.wav"));
        this->open_user_page_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenUserPage.wav"));
        this->open_settings_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenSettings.wav"));
        this->open_themes_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenThemes.wav"));
        this->open_controllers_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenControllers.wav"));
        this->open_album_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenAlbum.wav"));
        this->open_amiibo_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenAmiibo.wav"));
        this->open_quick_menu_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/OpenQuickMenu.wav"));
        this->close_quick_menu_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CloseQuickMenu.wav"));
        this->resume_app_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/ResumeApplication.wav"));
        this->create_folder_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CreateFolder.wav"));
        this->create_hb_entry_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/CreateHomebrewEntry.wav"));
        this->entry_remove_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/EntryRemove.wav"));
        this->error_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/Error.wav"));
        this->menu_increment_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/MenuIncrement.wav"));
        this->menu_decrement_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Main/MenuDecrement.wav"));
    }

    void MainMenuLayout::DisposeSfx() {
#ifdef QDESKTOP_MODE
        // Symmetric with LoadSfx — nothing to dispose because nothing was loaded.
        return;
#endif
        pu::audio::DestroySfx(this->post_suspend_sfx);
        pu::audio::DestroySfx(this->cursor_move_sfx);
        pu::audio::DestroySfx(this->page_move_sfx);
        pu::audio::DestroySfx(this->entry_select_sfx);
        pu::audio::DestroySfx(this->entry_move_sfx);
        pu::audio::DestroySfx(this->entry_swap_sfx);
        pu::audio::DestroySfx(this->entry_cancel_select_sfx);
        pu::audio::DestroySfx(this->entry_move_into_sfx);
        pu::audio::DestroySfx(this->home_press_sfx);
        pu::audio::DestroySfx(this->logoff_sfx);
        pu::audio::DestroySfx(this->launch_app_sfx);
        pu::audio::DestroySfx(this->launch_hb_sfx);
        pu::audio::DestroySfx(this->close_suspended_sfx);
        pu::audio::DestroySfx(this->open_folder_sfx);
        pu::audio::DestroySfx(this->close_folder_sfx);
        pu::audio::DestroySfx(this->open_mii_edit_sfx);
        pu::audio::DestroySfx(this->open_web_browser_sfx);
        pu::audio::DestroySfx(this->open_user_page_sfx);
        pu::audio::DestroySfx(this->open_settings_sfx);
        pu::audio::DestroySfx(this->open_themes_sfx);
        pu::audio::DestroySfx(this->open_controllers_sfx);
        pu::audio::DestroySfx(this->open_album_sfx);
        pu::audio::DestroySfx(this->open_quick_menu_sfx);
        pu::audio::DestroySfx(this->close_quick_menu_sfx);
        pu::audio::DestroySfx(this->resume_app_sfx);
        pu::audio::DestroySfx(this->create_folder_sfx);
        pu::audio::DestroySfx(this->create_hb_entry_sfx);
        pu::audio::DestroySfx(this->entry_remove_sfx);
        pu::audio::DestroySfx(this->error_sfx);
    }

    void MainMenuLayout::OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
#ifdef QDESKTOP_MODE
        // qdesktop input is dispatched to child elements (QdDesktopIconsElement,
        // future QdCursor/HUD/Launchpad) by Plutonium's own per-element OnInput
        // chain.  This layout-level handler intentionally does nothing — the
        // upstream entry/quick-menu state machine is gated off in qdesktop mode.
        (void)keys_down; (void)keys_up; (void)keys_held; (void)touch_pos;
        return;
#endif
        const auto quick_menu_on = this->quick_menu->IsOn();
        if(this->last_quick_menu_on != quick_menu_on) {
            this->last_quick_menu_on = quick_menu_on;
            this->entry_menu->SetEnabled(!quick_menu_on);

            if(quick_menu_on) {
                pu::audio::PlaySfx(this->open_quick_menu_sfx);
            }
            else {
                pu::audio::PlaySfx(this->close_quick_menu_sfx);
            }
        }
        if(quick_menu_on) {
            return;
        }

        ////////////////////////////////////////////////////////

        if(this->input_bar_changed) {
            this->input_bar_changed = false;
            this->input_bar->ClearInputs();

            if(this->entry_menu->IsFocusedNonemptyEntry()) {
                if(this->entry_menu->IsAnySelected()) {
                    this->input_bar->AddSetInput(HidNpadButton_A, GetLanguageString("input_move_selected"));
                }
                else if(this->entry_menu->IsFocusedEntrySuspended()) {
                    this->input_bar->AddSetInput(HidNpadButton_A | InputBar::MetaHomeNpadButton, GetLanguageString("input_resume_suspended"));
                }
                else {
                    const auto &cur_entry = this->entry_menu->GetFocusedEntry();
                    if(cur_entry.Is<EntryType::Folder>()) {
                        this->input_bar->AddSetInput(HidNpadButton_A, GetLanguageString("input_open_folder"));
                    }
                    else {
                        this->input_bar->AddSetInput(HidNpadButton_A, GetLanguageString("input_launch_entry"));
                    }
                }

                if(this->entry_menu->IsAnySelected()) {
                    this->input_bar->AddSetInput(HidNpadButton_X, GetLanguageString("input_cancel_selection"));
                }
                else if(this->entry_menu->IsFocusedEntrySuspended()) {
                    this->input_bar->AddSetInput(HidNpadButton_X, GetLanguageString("input_close_suspended"));
                }
                else if(this->entry_menu->IsFocusedNonemptyEntry()) {
                    const auto &cur_entry = this->entry_menu->GetFocusedEntry();
                    if(!cur_entry.IsSpecial()) {
                        this->input_bar->AddSetInput(HidNpadButton_X, GetLanguageString("input_entry_options"));
                    }
                }

                if(!this->entry_menu->IsAnySelected()) {
                    this->input_bar->AddSetInput(HidNpadButton_Y, GetLanguageString("input_select_entry"));
                }

                if(this->entry_menu->IsAnySelected()) {
                    this->input_bar->AddSetInput(HidNpadButton_B, GetLanguageString("input_cancel_selection"));
                }
                else if(!this->entry_menu->IsInRoot()) {
                    this->input_bar->AddSetInput(HidNpadButton_B, GetLanguageString("input_folder_back"));
                }
            }
            else {
                if(this->entry_menu->IsAnySelected()) {
                    this->input_bar->AddSetInput(HidNpadButton_A, GetLanguageString("input_move_selected"));
                    this->input_bar->AddSetInput(HidNpadButton_B, GetLanguageString("input_cancel_selection"));
                    this->input_bar->AddSetInput(HidNpadButton_X, GetLanguageString("input_cancel_selection"));
                }
                else {
                    this->input_bar->AddSetInput(HidNpadButton_A, GetLanguageString("input_new_entry"));
                }
            }

            if(this->entry_menu->IsMenuStart()) {
                this->input_bar->AddSetInput(InputBar::MetaDpadNpadButton | InputBar::MetaAnyStickNpadButton | HidNpadButton_R, GetLanguageString("input_navigate"));
            }
            else {
                this->input_bar->AddSetInput(InputBar::MetaDpadNpadButton | InputBar::MetaAnyStickNpadButton | HidNpadButton_L | HidNpadButton_R, GetLanguageString("input_navigate"));
            }

            if(this->entry_menu->IsInRoot() && !this->entry_menu->IsAnySelected()) {
                this->input_bar->AddSetInput(HidNpadButton_B, GetLanguageString("input_logoff"));
            }

            if(g_GlobalSettings.IsSuspended() && !this->entry_menu->IsFocusedEntrySuspended()) {
                this->input_bar->AddSetInput(InputBar::MetaHomeNpadButton, GetLanguageString("input_resume_suspended"));
            }

            this->input_bar->AddSetInput(HidNpadButton_Plus | HidNpadButton_Minus, GetLanguageString("input_resize_menu"));

            this->input_bar->AddSetInput(HidNpadButton_ZL | HidNpadButton_ZR, GetLanguageString("input_quick_menu"));
        }

        ///////////////////////////////

        const auto now_tp = std::chrono::steady_clock::now();

        this->UpdateConnectionTopIcon(this->connection_top_icon);
        this->UpdateTimeText(this->time_mtext);
        this->UpdateDateText(this->date_text);
        this->UpdateBatteryTextAndTopIcons(this->battery_text, this->battery_top_icon, this->battery_charging_top_icon);
        UpdateScreenCaptureBackground(this->entry_menu->IsFocusedEntrySuspended());

        if(!this->start_time_elapsed) {
            // Wait a bit before handling sent messages
            if(std::chrono::duration_cast<std::chrono::seconds>(now_tp - this->startup_tp).count() >= MessagesWaitTimeSeconds) {
                this->start_time_elapsed = true;
            }
        }

        if(this->start_time_elapsed && IsScreenCaptureBackgroundNotTransitioning()) {
            if(g_MenuApplication->GetConsumeLastLaunchFailed()) {
                pu::audio::PlaySfx(this->error_sfx);
                g_MenuApplication->DisplayDialog(GetLanguageString("app_launch"), GetLanguageString("app_unexpected_error"), { GetLanguageString("ok") }, true);
            }
            else if(g_MenuApplication->HasChosenHomebrew()) {
                const auto nro_path = g_MenuApplication->GetConsumeChosenHomebrew();
                if(IsNroNonRemovable(nro_path.c_str())) {
                    g_MenuApplication->ShowNotification(GetLanguageString("menu_chosen_hb_special"));
                }
                else {
                    pu::audio::PlaySfx(this->create_hb_entry_sfx);

                    const auto hb_entry = CreateHomebrewEntry(g_GlobalSettings.initial_last_menu_fs_path, nro_path, nro_path, g_GlobalSettings.initial_last_menu_index);
                    this->entry_menu->NotifyEntryAdded(hb_entry);
                    this->entry_menu->OrganizeUpdateEntries();
                    g_MenuApplication->ShowNotification(GetLanguageString("menu_chosen_hb_added"));
                }
            }
            else if(g_MenuApplication->HasGameCardMountFailure()) {
                pu::audio::PlaySfx(this->error_sfx);

                const auto gc_rc = g_MenuApplication->GetConsumeGameCardMountFailure();
                g_MenuApplication->DisplayDialog(GetLanguageString("gamecard"), GetLanguageString("gamecard_mount_failed") + " " + util::FormatResultDisplay(gc_rc), { GetLanguageString("ok") }, true);
            }
            else if(g_MenuApplication->HasActiveThemeLoadFailure()) {
                pu::audio::PlaySfx(this->error_sfx);

                const auto theme_rc = g_MenuApplication->GetConsumeActiveThemeLoadFailure();
                g_MenuApplication->DisplayDialog(GetLanguageString("theme_active"), GetLanguageString("theme_load_failed") + " " + util::FormatResultDisplay(theme_rc), { GetLanguageString("ok") }, true);
            }
            else if(g_MenuApplication->HasActiveThemeOutdated()) {
                g_MenuApplication->ConsumeActiveThemeOutdated();

                if(!g_GlobalSettings.system_status.warned_about_outdated_theme) {
                    pu::audio::PlaySfx(this->error_sfx);
                    UL_RC_ASSERT(smi::NotifyWarnedAboutOutdatedTheme());
                    g_MenuApplication->DisplayDialog(GetLanguageString("theme_active"), GetLanguageString("theme_outdated"), { GetLanguageString("ok") }, true);
                }
            }
        }

        if(g_MenuApplication->GetConsumeApplicationRecordReloadNeeded()) {
            // Reload just entry infos
            ReloadApplicationEntryInfos(this->entry_menu->GetEntries());
        }

        if(g_MenuApplication->GetConsumeApplicationEntryReloadNeeded()) {
            // Reload entries
            this->MoveTo("", true);
        }

        if(g_MenuApplication->HasVerifyFinishedPending()) {
            const auto app_id = g_MenuApplication->GetConsumeVerifyFinishedApplicationId();
            const auto rc = g_MenuApplication->GetConsumeVerifyResult();
            const auto detail_rc = g_MenuApplication->GetConsumeVerifyDetailResult();

            if(R_SUCCEEDED(rc) && R_SUCCEEDED(detail_rc)) {
                g_MenuApplication->DisplayDialog(GetLanguageString("app_verify"), GetLanguageString("app_verify_ok"), { GetLanguageString("ok") }, true, LoadApplicationIconTexture(app_id));
            }
            else {
                g_MenuApplication->DisplayDialog(GetLanguageString("app_verify"), GetLanguageString("app_verify_error") + ":\n\n" + util::FormatResultDisplay(rc) + "\n" + util::FormatResultDisplay(detail_rc), { GetLanguageString("ok") }, true);
            }

            auto it = std::find(g_GlobalSettings.in_verify_app_ids.begin(), g_GlobalSettings.in_verify_app_ids.end(), app_id);
            if(it != g_GlobalSettings.in_verify_app_ids.end()) {
                g_GlobalSettings.in_verify_app_ids.erase(it);
            }
            ReloadApplicationEntryInfos(this->entry_menu->GetEntries());
        }

        if(keys_down & HidNpadButton_Minus) {
            if(!this->is_incrementing_decrementing && this->entry_menu->CanDecrementEntryHeightCount()) {
                pu::audio::PlaySfx(this->menu_decrement_sfx);
                this->is_incrementing_decrementing = true;
                this->entry_menu->DecrementEntryHeightCount();
                this->is_incrementing_decrementing = false;
            }
        }
        else if(keys_down & HidNpadButton_Plus) {
            if(!this->is_incrementing_decrementing) {
                pu::audio::PlaySfx(this->menu_increment_sfx);
                this->is_incrementing_decrementing = true;
                this->entry_menu->IncrementEntryHeightCount();
                this->is_incrementing_decrementing = false;
            }
        }
    }

    void MainMenuLayout::OnMenuUpdate() {
#ifdef QDESKTOP_MODE
        // F-06 fix: advance the icon LRU cache tick exactly once per frame.
        // OnRender no longer calls cache_.AdvanceTick() directly — this is the
        // single authoritative tick site for QDESKTOP_MODE builds.
        if (this->qdesktop_icons) {
            this->qdesktop_icons->AdvanceTick();
        }

        // ── Stick → cursor (five-zone dynamic curve, SP3.3) ──────────────────
        // pump_input is called here rather than in OnMenuInput because
        // OnMenuInput is gated off in QDESKTOP_MODE (the icons element's own
        // OnInput handles Plutonium-routed events).  OnMenuUpdate fires once
        // per frame unconditionally.
        //
        // The velocity curve is the five-zone hybrid from
        // tools/mock-nro-desktop-gui/src/curve.rs (v0.5.0):
        //   zone 1: dead-zone     [0,4500)     → 0 px/frame
        //   zone 2: precision     [4500,12000) → 0.5–2 px/frame  (linear)
        //   zone 3: linear        [12000,22000)→ 2–8 px/frame    (linear)
        //   zone 4: acceleration  [22000,30000)→ 8–24 px/frame   (quadratic)
        //   zone 5: burst         [30000,32767]→ 24–40 px/frame  (linear)
        // Plus adaptive boost (12-frame hold → 1.0×→1.5× over next 24 frames)
        // and slow-mode (ZR held → ×0.4).
        if (this->qdesktop_cursor) {
            static qdesktop::InputState  g_qd_input_state  = qdesktop::input_state_zero();
            static qdesktop::PolledFrame g_qd_polled_frame = {};

            qdesktop::pump_input(g_qd_input_state, g_qd_polled_frame);

            // Persistent per-axis curve state (file-static; OnMenuUpdate is
            // the sole caller, so no locking is needed).
            static qdesktop::StickState s_stick_x = qdesktop::stick_state_zero();
            static qdesktop::StickState s_stick_y = qdesktop::stick_state_zero();

            const bool    slow = g_qd_polled_frame.zr_held;
            const int32_t dx   = qdesktop::ComputeStickSpeed(
                                     g_qd_polled_frame.stick_r_x, s_stick_x, slow);
            // HID Y positive = up; Plutonium layout Y positive = down.  Negate.
            const int32_t dy   = -qdesktop::ComputeStickSpeed(
                                     g_qd_polled_frame.stick_r_y, s_stick_y, slow);

            if ((dx != 0 || dy != 0)) {
                s32 cx = this->qdesktop_cursor->GetCursorX();
                s32 cy = this->qdesktop_cursor->GetCursorY();
                s32 nx = cx + dx;
                s32 ny = cy + dy;
                if (nx < 0)        nx = 0;
                if (nx >= 1920)    nx = 1919;
                if (ny < 0)        ny = 0;
                if (ny >= 1080)    ny = 1079;
                this->qdesktop_cursor->SetCursorPos(nx, ny);
            }
        }

        // Periodic telemetry flush (Regression 1 fix) ────────────────────────
        // The async SPSC ring drains lazily every 8 ms in the drain thread, but
        // a hard crash (applet killed by kernel) can lose up to one drain window
        // of INFO messages.  Flushing here every 180 frames (~3 s at 60 fps)
        // bounds the worst-case INFO loss to 3 s of events instead of the full
        // SPSC buffer.  WARN/CRIT already use EmitSync (synchronous) and are
        // never affected.  The atexit handler in main.cpp covers normal exit.
        {
            static uint32_t s_flush_frame = 0u;
            if (++s_flush_frame >= 180u) {
                s_flush_frame = 0u;
                ul::tel::Flush();
            }
        }

        // Top-bar live updates — clock, date, battery, wifi, bluetooth.
        // Each update helper is a no-op when its target element is null.
        // All top-bar members are guaranteed non-null in qdesktop mode because
        // the constructor's QDESKTOP_MODE block initialises them before returning.
        this->UpdateTimeText(this->time_mtext);
        this->UpdateDateText(this->date_text);
        this->UpdateBatteryTextAndTopIcons(this->battery_text, this->battery_top_icon, this->battery_charging_top_icon);
        this->UpdateConnectionTopIcon(this->connection_top_icon);

        // ── Bluetooth icon visibility ─────────────────────────────────────────
        // Show the icon only when a BT audio device is connected.
        // GetConnectedAudioDevice() returns the device polled every 500 ms by
        // the bt::Manager thread.  A connected device has a non-zero 6-byte
        // address; we verify by comparing against the all-zero sentinel using
        // the exported AudioDeviceAddressesEqual helper.
        // We track qdesktop_last_bt_connected to avoid redundant SetVisible calls.
        if(this->qdesktop_bt_top_icon) {
            const auto bt_dev = ul::menu::bt::GetConnectedAudioDevice();
            constexpr BtdrvAddress kZeroAddr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            const bool bt_now = !ul::menu::bt::AudioDeviceAddressesEqual(bt_dev.addr, kZeroAddr);
            if(bt_now != this->qdesktop_last_bt_connected) {
                this->qdesktop_last_bt_connected = bt_now;
                this->qdesktop_bt_top_icon->SetVisible(bt_now);
            }
        }
#endif
    }

    bool MainMenuLayout::OnHomeButtonPress() {
#ifdef QDESKTOP_MODE
        // Q OS desktop: Cycle D5 (SP4.12) — Home press is a deliberate no-op
        // beyond cursor recentering, except when pressed twice within a short
        // window. That second press inside the window is the dev-menu trigger.
        //
        //   Single press        → centre cursor, log, return true (swallow)
        //   Double press <600ms → open the qdesktop dev mini-menu
        //
        // The user's mandate ("home button doesnt need to do anything except
        // when you hold it the mini menu should pop up") nominally asks for a
        // press-and-hold gesture. True hold-detection requires uSystem to
        // catch DetectLongPressingHomeButton (libnx AppletMessage value 21)
        // and forward it as a new SMI message — that's a cross-binary protocol
        // change worth doing in a future cycle. For SP4.12 we use the
        // double-press equivalent: same "deliberate gesture, not accidental"
        // property, no protocol churn, single-binary patch.
        //
        // Threshold: 600 ms (= 600,000,000 ns), measured from the previous
        // press's armGetSystemTick(). The Switch system tick is in nanoseconds
        // (armTicksToNs converts ARM timer ticks to ns; armGetSystemTick gives
        // raw ticks at 19.2 MHz, so we use armTicksToNs to get a real ns count).
        constexpr u64 DEV_MENU_DOUBLE_PRESS_NS = 600'000'000ULL;

        const u64 now_ns = armTicksToNs(armGetSystemTick());
        const u64 since_last_ns = (now_ns >= this->qdesktop_last_home_press_ns)
            ? (now_ns - this->qdesktop_last_home_press_ns)
            : 0ULL;
        const bool is_double = (this->qdesktop_last_home_press_ns != 0ULL)
                            && (since_last_ns < DEV_MENU_DOUBLE_PRESS_NS);

        if (is_double) {
            UL_LOG_INFO("qdesktop: OnHomeButtonPress -> DOUBLE press (%llu ms) "
                        "→ ShowDevMenu",
                        static_cast<unsigned long long>(since_last_ns / 1'000'000ULL));
            // Reset so a third press inside the window doesn't immediately
            // re-open the menu after Close.
            this->qdesktop_last_home_press_ns = 0ULL;
            ::ul::menu::qdesktop::ShowDevMenu();
            return true;
        }

        // Single press path:
        //   • If a game/applet is currently suspended → ResumeApplication
        //     (mirrors Switch native: pressing Home from desktop with a
        //      backgrounded game brings it back to the foreground).
        //   • Otherwise → record press time + centre cursor + swallow.
        // Cycle G3 (SP4.15): the Resume path was previously gated off in
        // QDESKTOP_MODE; without it the desktop became a one-way trip and
        // the user was stuck unable to return to the running game.
        this->qdesktop_last_home_press_ns = now_ns;
        if (g_GlobalSettings.IsTitleSuspended() && g_MenuApplication != nullptr) {
            UL_LOG_INFO("qdesktop: OnHomeButtonPress -> single + IsTitleSuspended"
                        " → ResumeApplication 0x%016lX",
                        g_GlobalSettings.system_status.suspended_app_id);
            const auto rrc = smi::ResumeApplication();
            if (R_SUCCEEDED(rrc)) {
                g_MenuApplication->FadeOutToNonLibraryApplet();
                g_MenuApplication->Finalize();
                return true;
            }
            UL_LOG_WARN("qdesktop: OnHomeButtonPress single-resume failed rc=0x%X"
                        " — falling back to cursor-centre",
                        static_cast<unsigned>(rrc));
            // Fall through to the cursor-centre default below so the press
            // doesn't feel completely dead.
        }
        UL_LOG_INFO("qdesktop: OnHomeButtonPress -> single (no-op + cursor centre)");
        if (this->qdesktop_cursor) {
            this->qdesktop_cursor->SetCursorPos(960, 540);
        }
        return true;
#endif
        pu::audio::PlaySfx(this->home_press_sfx);

        if(g_GlobalSettings.IsSuspended()) {
            this->StartResume();
        }
        else {
            if(!this->entry_menu->IsInRoot() && this->entry_menu->IsMenuStart()) {
                this->MoveToRoot(true);
            }

            this->entry_menu->Rewind();
        }

        return true;
    }

    void MainMenuLayout::MoveTo(const std::string &new_path, const bool fade, std::function<void()> action) {
#ifdef QDESKTOP_MODE
        // qdesktop has no folder navigation in v0.21.  Run the caller's action
        // (if any) so external callers that pass a continuation still progress;
        // skip the upstream fade transition (relies on null entry_menu).
        (void)new_path; (void)fade;
        if (action) { action(); }
        return;
#endif
        if(fade) {
            g_MenuApplication->SetBackgroundFade();
            g_MenuApplication->FadeOut();

            if(action) {
                action();
            }
            this->DoMoveTo(new_path);

            g_MenuApplication->FadeIn();
        }
        else {
            this->DoMoveTo(new_path);
        }
    }

    void MainMenuLayout::Initialize() {
        if(HasScreenCaptureBackground()) {
#ifndef QDESKTOP_MODE
            // post_suspend_sfx is null in qdesktop mode (LoadSfx is gated); skip
            // the PlaySfx so we don't dereference a null Mix_Chunk.  The
            // suspended-app resume path itself still runs; only the audio cue
            // is omitted in qdesktop mode (will return in v0.24 via QdAudio).
            pu::audio::PlaySfx(this->post_suspend_sfx);
#endif
        }

        // InitializeEntries MUST run in both modes — qdesktop's
        // SetApplicationEntries / SetSpecialEntries depend on the upstream
        // entry vector being populated.
        g_GlobalSettings.InitializeEntries();

#ifdef QDESKTOP_MODE
        // Populate the desktop with the upstream entry vector now that
        // InitializeEntries has run.  Order matters:
        //   1. SetApplicationEntries — truncates + appends installed games
        //   2. SetSpecialEntries     — appends Switch system applets
        // A future Reload() override will re-run both with the same vector.
        //
        // Fallback path (Cycle A3, SP4.10): when the upstream LoadEntries() returns
        // empty — which happens whenever uMenu boots in a hbloader-hosted /
        // applet-mode context (rc=0x1F800 on ns:am2 ListApplicationRecord) — we
        // synthesise the same Entry vector from sdmc:/switch/qos-apps/records.bin
        // pre-written by uManager.nro. See qd_RecordsBin.{hpp,cpp} and the canonical
        // Rust qos_apps.rs in mock-nro-desktop-gui.
        if (this->qdesktop_icons) {
            std::vector<ul::menu::Entry> entries =
                ul::menu::LoadEntries(ul::menu::GetActiveMenuPath());
            UL_LOG_INFO("qdesktop: ui_MainMenuLayout: LoadEntries returned %zu",
                        entries.size());

            // Always load records.bin so we can enrich Application entries whose
            // control.name is empty.  In hbloader/applet mode the NS sysmodule
            // denies nsextGetApplicationControlData (rc=0x1F800), so LoadEntries()
            // returns entries with empty control.name even when .m.json files exist
            // on SD.  records.bin was written by uManager.nro with NACP names from
            // nsextGetApplicationControlData(CacheOnly) which succeeds at full
            // privilege.  We build an app_id → name map from records.bin, then
            // backfill any entry whose control.name is still empty.
            {
                std::vector<ul::menu::Entry> rb_entries;
                const bool rb_ok = ul::menu::qdesktop::LoadEntriesFromRecordsBin(
                        ul::menu::qdesktop::QAPP_RECORDS_BIN_PATH, rb_entries);

                if (entries.empty() && rb_ok) {
                    // Complete fallback: no .m.json entries at all — use records.bin
                    // as the primary source.
                    UL_LOG_INFO("qdesktop: ui_MainMenuLayout: records.bin"
                                " full-fallback yielded %zu entries",
                                rb_entries.size());
                    entries = std::move(rb_entries);
                } else if (rb_ok && !rb_entries.empty()) {
                    // Partial enrichment: .m.json entries exist but have empty names
                    // due to NS applet-mode privilege failure.  Build a fast lookup
                    // table from the records.bin vector (app_id → name), then
                    // backfill each Application entry that still has no display name.
                    std::unordered_map<u64, std::string> rb_name_map;
                    rb_name_map.reserve(rb_entries.size());
                    for (const auto &rb : rb_entries) {
                        if (!rb.control.name.empty()) {
                            rb_name_map.emplace(rb.app_info.app_id,
                                                rb.control.name);
                        }
                    }

                    size_t enriched = 0;
                    for (auto &e : entries) {
                        if (e.Is<ul::menu::EntryType::Application>()
                                && e.control.name.empty()
                                && e.app_info.app_id != 0) {
                            auto it = rb_name_map.find(e.app_info.app_id);
                            if (it != rb_name_map.end()) {
                                e.control.name = it->second;
                                ++enriched;
                            }
                        }
                    }
                    UL_LOG_INFO("qdesktop: ui_MainMenuLayout: records.bin"
                                " name-enrichment: %zu/%zu entries backfilled",
                                enriched, entries.size());
                } else if (!rb_ok) {
                    UL_LOG_INFO("qdesktop: ui_MainMenuLayout: records.bin"
                                " unavailable — display names may fall back to"
                                " hex app_id labels");
                }
            }

            this->qdesktop_icons->SetApplicationEntries(entries);
            this->qdesktop_icons->SetSpecialEntries(entries);
        }
#endif
    }

    void MainMenuLayout::Reload() {
#ifdef QDESKTOP_MODE
        // qdesktop reloads its icon set via QdDesktopIconsElement::Reload()
        // during user-change events; the upstream entry_menu/quick_menu reload
        // path is gated off because those members don't exist here.
        return;
#endif
        UL_RC_ASSERT(acc::GetAccountName(g_GlobalSettings.system_status.selected_user, g_UserName));
        this->entry_menu->Initialize(g_GlobalSettings.system_status.last_menu_index, this->next_reload_user_changed ? g_GlobalSettings.system_status.last_menu_fs_path : "");
        this->quick_menu->UpdateItems();
        this->next_reload_user_changed = false;
    }

    void MainMenuLayout::HandleCloseSuspended() {
#ifdef QDESKTOP_MODE
        // qdesktop has no quick_menu / input_bar to refresh; suspension control
        // moves to QdContextMenu in SP4.
        return;
#endif
        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("suspended_app"), GetLanguageString("suspended_app_close"), { GetLanguageString("yes"), GetLanguageString("no") }, true);
        if(option == 0) {
            this->DoTerminateApplication();
            this->input_bar_changed = true;
        }
    }

    void MainMenuLayout::HandleHomebrewLaunch(const Entry &hb_entry) {
#ifdef QDESKTOP_MODE
        // Homebrew launches will route through QdLauncher in SP4; for now,
        // gate off the upstream dialog path that depends on input_bar etc.
        (void)hb_entry;
        return;
#endif
        const auto can_launch_as_app = g_GlobalSettings.cache_hb_takeover_app_id != os::InvalidApplicationId;
        if(can_launch_as_app) {
            bool def_launch_as_app = false;
            UL_ASSERT_TRUE(g_GlobalSettings.config.GetEntry(cfg::ConfigEntryId::LaunchHomebrewApplicationByDefault, def_launch_as_app));
            if(def_launch_as_app) {
                this->LaunchHomebrewApplication(hb_entry);
                return;
            }
        }

        const auto option = g_MenuApplication->DisplayDialog(GetLanguageString("hb_launch"), GetLanguageString("hb_launch_conf"), { GetLanguageString("hb_applet"), GetLanguageString("hb_app"), GetLanguageString("cancel") }, true);
        if(option == 0) {
            pu::audio::PlaySfx(this->launch_hb_sfx);
            
            const auto proper_ipt = CreateLaunchTargetInput(hb_entry.hb_info.nro_target);

            g_MenuApplication->FadeOutToNonLibraryApplet();
            UL_RC_ASSERT(smi::LaunchHomebrewLibraryApplet(proper_ipt.nro_path, proper_ipt.nro_argv));
            g_MenuApplication->Finalize();
        }
        else if(option == 1) {
            if(can_launch_as_app) {
                this->LaunchHomebrewApplication(hb_entry);
            }
            else {
                g_MenuApplication->DisplayDialog(GetLanguageString("app_launch"), GetLanguageString("app_no_take_over_app"), { GetLanguageString("ok") }, true);
            }
        }
    }

    void MainMenuLayout::StopSelection() {
#ifdef QDESKTOP_MODE
        // qdesktop's selection model lives on QdDesktopIconsElement; the
        // upstream entry_menu does not exist.
        return;
#endif
        this->entry_menu->ResetSelection();
    }

    void MainMenuLayout::DoTerminateApplication() {
#ifdef QDESKTOP_MODE
        // qdesktop will own application termination via QdContextMenu in SP4;
        // upstream entry_menu / sfx members are absent in this build.
        return;
#endif
        pu::audio::PlaySfx(this->close_suspended_sfx);

        auto &entries = this->entry_menu->GetEntries();
        u32 i = 0;
        for(const auto &entry : entries) {
            if(g_GlobalSettings.IsEntrySuspended(entry)) {
                break;
            }
            i++;
        }

        UL_RC_ASSERT(smi::TerminateApplication());

        if(i < entries.size()) {
            // We need to reload the application record since its kind/type changed after closing it
            // (only if the entry is loaded, aka in the current folder)
            entries.at(i).ReloadApplicationInfo();
        }

        g_GlobalSettings.ResetSuspendedApplication();

        RequestHideScreenCaptureBackground();
    }

}
