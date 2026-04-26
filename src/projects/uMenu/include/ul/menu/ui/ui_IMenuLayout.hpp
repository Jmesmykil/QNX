
#pragma once
#include <pu/Plutonium>
#include <ul/smi/smi_Protocol.hpp>
#include <ul/os/os_System.hpp>
#include <ul/menu/ui/ui_MultiTextBlock.hpp>

namespace ul::menu::ui {

    class IMenuLayout : public pu::ui::Layout {
        private:
            RecursiveMutex msg_queue_lock;
            std::queue<ul::smi::MenuMessageContext> msg_queue;

            bool last_has_connection;
            u32 last_connection_strength;

            u32 last_battery_level;
            bool last_battery_is_charging;

            os::Time last_time;
            os::Date last_date;

        protected:
            void UpdateConnectionTopIcon(pu::ui::elm::Image::Ref &icon);
            
            void UpdateDateText(pu::ui::elm::TextBlock::Ref &date_text);

            void InitializeTimeText(MultiTextBlock::Ref &time_mtext, const std::string &ui_menu, const std::string &ui_name);
            void UpdateTimeText(MultiTextBlock::Ref &time_mtext);
            
            void UpdateBatteryTextAndTopIcons(pu::ui::elm::TextBlock::Ref &text, pu::ui::elm::Image::Ref &base_top_icon, pu::ui::elm::Image::Ref &charging_top_icon);

        public:
            IMenuLayout();

            void OnLayoutInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos);
            void NotifyMessageContext(const ul::smi::MenuMessageContext &msg_ctx);
            
            virtual void OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) = 0;
            virtual void OnMenuUpdate() {}
            
            virtual bool OnHomeButtonPress() = 0;

            // Q OS cycle SP4.14: invoked when uSystem forwards a long-press
            // (OS-level AppletMessage_DetectLongPressingHomeButton).  Default
            // is a no-op that returns true (consume).  qdesktop's
            // MainMenuLayout overrides this to open the dev mini-menu;
            // upstream layouts (Settings/Themes/Lockscreen/Startup) keep
            // the default since they have no surface for a dev menu.
            virtual bool OnHomeButtonLongPress() { return true; }

            virtual void LoadSfx() = 0;
            virtual void DisposeSfx() = 0;
    };

}
