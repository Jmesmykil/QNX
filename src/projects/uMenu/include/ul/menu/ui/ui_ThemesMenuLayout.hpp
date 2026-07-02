
#pragma once
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/cfg/cfg_Config.hpp>

namespace ul::menu::ui {

    class ThemesMenuLayout : public IMenuLayout {
        public:
            static constexpr u32 ThemesMenuWidth = 1720;
            static constexpr u32 ThemesMenuItemSize = 180;
            static constexpr u32 ThemesMenuItemsToShow = 5;

        private:
            pu::ui::elm::Menu::Ref themes_menu;
            pu::ui::elm::TextBlock::Ref info_text;
            std::vector<cfg::Theme> loaded_themes;
            std::vector<pu::sdl2::TextureHandle::Ref> loaded_theme_icons;
            pu::audio::Sfx theme_change_sfx;
            pu::audio::Sfx back_sfx;

            // v2.5.0 — in-binary theme packs prepended to the Themes menu.
            // Layout: [in-binary 0..in_binary_count-1] then [.ultheme files +
            // 'default theme' reset entry]. theme_DefaultKey() uses this count
            // to branch backends (qdesktop::SetActiveThemePack vs cfg::SetActiveTheme).
            size_t in_binary_count = 0;

            void theme_DefaultKey();
            // Build a 180x180 RGBA palette-swatch icon for in-binary pack idx
            // (Q OS / Neon / Minimal / Retro / Cards / Pastel / Dark /
            // Gradient / Blueprint / Pixel). Returns a TextureHandle::Ref
            // ready for MenuItem::SetIcon. Nullptr on SDL allocation failure.
            pu::sdl2::TextureHandle::Ref MakeInBinaryIcon(size_t idx);

        public:
            ThemesMenuLayout();
            PU_SMART_CTOR(ThemesMenuLayout)

            void OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;
            bool OnHomeButtonPress() override;
            void LoadSfx() override;
            void DisposeSfx() override;

            void Reload();
    };

}
