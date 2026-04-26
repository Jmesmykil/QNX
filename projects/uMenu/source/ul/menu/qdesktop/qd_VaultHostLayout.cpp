// qd_VaultHostLayout.cpp — see qd_VaultHostLayout.hpp for the why.

#include <ul/menu/qdesktop/qd_VaultHostLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/ul_Result.hpp>

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

    QdVaultHostLayout::QdVaultHostLayout(QdVaultLayout::Ref vault_element)
        : vault_element_(vault_element) {
        this->SetBackgroundColor({ 0, 0, 0, 255 });
        this->Add(this->vault_element_);
    }

    void QdVaultHostLayout::OnMenuInput(const u64 keys_down,
                                        const u64 keys_up,
                                        const u64 keys_held,
                                        const pu::ui::TouchPoint touch_pos) {
        // Vault element handles its own input via its OnInput override; the
        // base pu::ui::Layout::OnInput dispatch already routes here through the
        // child Element list, so OnMenuInput stays empty for the host.  Kept
        // explicit for IMenuLayout contract satisfaction.
        (void)keys_down;
        (void)keys_up;
        (void)keys_held;
        (void)touch_pos;
    }

    bool QdVaultHostLayout::OnHomeButtonPress() {
        UL_LOG_INFO("vault: OnHomeButtonPress -> returning to MainMenu");
        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        return true;
    }

    void QdVaultHostLayout::LoadSfx() {
        // Vault has no per-layout sfx today.  When sfx are added, load them here.
    }

    void QdVaultHostLayout::DisposeSfx() {
        // Mirrors LoadSfx; kept symmetric for future sfx work.
    }

}
