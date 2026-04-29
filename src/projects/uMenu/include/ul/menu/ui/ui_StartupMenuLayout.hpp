
#pragma once
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_Wallpaper.hpp>
#include <ul/menu/qdesktop/qd_UserCard.hpp>
#include <ul/menu/qdesktop/qd_PowerButton.hpp>
#include <SDL2/SDL.h>
#include <vector>
#endif

namespace ul::menu::ui {

    class StartupMenuLayout : public IMenuLayout {
        public:
            static constexpr u32 UsersMenuWidth = 1320;
            static constexpr u32 UsersMenuItemSize = 150;
            static constexpr u32 UsersMenuItemsToShow = 5;

        private:
            bool load_menu;
            pu::ui::elm::TextBlock::Ref info_text;
            pu::ui::elm::Menu::Ref users_menu;
            pu::audio::Sfx user_create_sfx;
            pu::audio::Sfx user_select_sfx;

            void user_DefaultKey(const AccountUid uid);
            void create_DefaultKey();

#ifdef QDESKTOP_MODE
            // ── qdesktop login screen elements ─────────────────────────────
            qdesktop::QdWallpaperElement::Ref qd_wallpaper;

            // "Q OS" brand title — rendered per-frame into a local SDL_Texture*
            // (v1.8.2 LRU fix: cache-owned ptrs cannot be stored across frames).
            // Centred horizontally at y=220.

            // Branding text
            pu::ui::elm::TextBlock::Ref qd_version;    // UL_VERSION string

            // Clock / date (top-right)
            MultiTextBlock::Ref        time_mtext;
            pu::ui::elm::TextBlock::Ref date_text;

            // User cards — one per Switch account
            std::vector<qdesktop::QdUserCardElement::Ref> qd_user_cards;
            // Focused card index (D-pad navigation between cards)
            s32 qd_focused_card;

            // Power buttons (Restart / Shutdown / Sleep / Hekate)
            qdesktop::QdPowerButtonElement::Ref qd_btn_restart;
            qdesktop::QdPowerButtonElement::Ref qd_btn_shutdown;
            qdesktop::QdPowerButtonElement::Ref qd_btn_sleep;
            qdesktop::QdPowerButtonElement::Ref qd_btn_hekate;

            // Dev-tool buttons (bottom-left)
            // Using Custom-kind power buttons for click regions; state text
            // is shown by separate TextBlocks so we can update them each frame
            // without needing a SetLabel() method.
            qdesktop::QdPowerButtonElement::Ref qd_btn_nxlink;
            qdesktop::QdPowerButtonElement::Ref qd_btn_usbserial;
            qdesktop::QdPowerButtonElement::Ref qd_btn_flush;

            // State labels overlaid on dev-tool buttons (updated each frame)
            pu::ui::elm::TextBlock::Ref qd_lbl_nxlink;
            pu::ui::elm::TextBlock::Ref qd_lbl_usbserial;

            // Bottom-of-screen controller-hints bar — rendered per-frame into a
            // local SDL_Texture* (v1.8.2 LRU fix).
            // Centred horizontally at y=1020 (60 px from bottom).
            static constexpr s32 QD_HINTS_Y = 1020;

            // ── qdesktop helpers ────────────────────────────────────────────
            void onUserSelected(const AccountUid uid);
            void RefreshDevToolLabels();
#endif

        public:
            StartupMenuLayout();
            ~StartupMenuLayout();
            PU_SMART_CTOR(StartupMenuLayout)

            void OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;
            void OnMenuUpdate() override;
            bool OnHomeButtonPress() override;
            void LoadSfx() override;
            void DisposeSfx() override;

            void ReloadMenu();
    };

}
