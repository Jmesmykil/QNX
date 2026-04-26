#include <ul/menu/ui/ui_StartupMenuLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/acc/acc_Accounts.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_Power.hpp>
#include <ul/menu/qdesktop/qd_DevTools.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/ui_Types.hpp>
#endif

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    void StartupMenuLayout::user_DefaultKey(const AccountUid uid) {
        // Note: menu loading is invoked below instead of here so that the main menu doesn't also register the button input which caused this action...
        this->load_menu = true;
        pu::audio::PlaySfx(this->user_select_sfx);
        g_GlobalSettings.SetSelectedUser(uid);

        auto &main_menu_lyt = g_MenuApplication->GetMainMenuLayout();
        if(main_menu_lyt != nullptr) {
            main_menu_lyt->NotifyNextReloadUserChanged();
        }
        g_MenuApplication->LoadMenu(MenuType::Main);
    }

    void StartupMenuLayout::create_DefaultKey() {
        pu::audio::PlaySfx(this->user_create_sfx);

        g_MenuApplication->FadeOutToLibraryApplet(AppletId_LibraryAppletMyPage);
        UL_RC_ASSERT(smi::OpenAddUser());
        g_MenuApplication->Finalize();
    }

    StartupMenuLayout::StartupMenuLayout() : IMenuLayout() {
        this->load_menu = false;

        this->user_create_sfx = nullptr;
        this->user_select_sfx = nullptr;

#ifdef QDESKTOP_MODE
        {
            // Q OS qdesktop login screen.  Wallpaper + branding + user cards +
            // power row + dev-tools row.  No upstream menu elements are
            // instantiated.  All upstream-touching member functions
            // early-return under QDESKTOP_MODE; see LoadSfx, DisposeSfx,
            // OnMenuInput, OnHomeButtonPress, ReloadMenu.
            const qdesktop::QdTheme qdt = qdesktop::QdTheme::DarkLiquidGlass();

            // ── 1. Wallpaper (Cold Plasma Cascade, full 1920×1080 blit) ─────
            this->qd_wallpaper = qdesktop::QdWallpaperElement::New(qdt);
            this->Add(this->qd_wallpaper);

            // ── 2. "Q OS" brand — programmatic SDL_Texture*, lazy-rasterised on
            //    first render frame via EnsureBrandingTextures().  No upstream
            //    romfs:/Logo.png loaded; qd_brand_tex_ is nullptr until then.
            // (nothing to construct here — rendered directly in OnMenuUpdate)

            // ── 3. "Q OS" wordmark below brand ───────────────────────────────
            // Approximate centre; exact x depends on rendered text width.
            // TextBlock anchors at top-left, so we offset by ~half of the
            // expected rendered width.  The theme's large size is ~48 px.
            this->qd_wordmark = pu::ui::elm::TextBlock::New(0, 420, "Q OS");
            this->qd_wordmark->SetColor(g_MenuApplication->GetTextColor());
            this->qd_wordmark->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large));
            g_GlobalSettings.ApplyConfigForElement("startup_menu", "qd_wordmark", this->qd_wordmark);
            this->Add(this->qd_wordmark);

            // ── 4. Version string below wordmark ─────────────────────────────
            this->qd_version = pu::ui::elm::TextBlock::New(0, 478, UL_VERSION);
            this->qd_version->SetColor(qdt.text_secondary);
            this->qd_version->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
            g_GlobalSettings.ApplyConfigForElement("startup_menu", "qd_version", this->qd_version);
            this->Add(this->qd_version);

            // ── 5. Clock (top-right, mirrored from main_menu config slot) ────
            this->InitializeTimeText(this->time_mtext, "main_menu", "time_text");
            this->Add(this->time_mtext);

            // ── 6. Date below clock ────────────────────────────────────────
            this->date_text = pu::ui::elm::TextBlock::New(0, 0, "...");
            this->date_text->SetColor(g_MenuApplication->GetTextColor());
            g_GlobalSettings.ApplyConfigForElement("main_menu", "date_text", this->date_text);
            this->Add(this->date_text);

            // ── 7. User cards (one per Switch account, centred horizontally) ─
            // Layout: each card is CARD_W=240.  Gap between cards = 32 px.
            // Total width for N cards = N*240 + (N-1)*32.
            // Centre offset = (1920 - total_width) / 2.
            this->qd_focused_card = 0;

            std::vector<AccountUid> user_ids;
            const Result list_rc = acc::ListAccounts(user_ids);
            UL_LOG_INFO("qdesktop: ListAccounts rc=0x%08X count=%zu",
                        list_rc, user_ids.size());
            if(R_SUCCEEDED(list_rc) && !user_ids.empty()) {
                const s32 card_w    = qdesktop::QdUserCardElement::CARD_W;
                const s32 gap       = 24;
                const s32 n         = static_cast<s32>(user_ids.size());
                const s32 total_w   = n * card_w + (n - 1) * gap;
                const s32 start_x   = (1920 - total_w) / 2;
                // Vertical centre: screen height=1080, card height=320.
                // Centre of screen = 540.  Top of card = 540 - 320/2 = 380.
                const s32 card_y    = 380;

                UL_LOG_INFO("qdesktop: building %d card(s) start_x=%d card_y=%d",
                            n, start_x, card_y);

                for(s32 i = 0; i < n; i++) {
                    const AccountUid uid = user_ids[static_cast<size_t>(i)];
                    const s32 cx = start_x + i * (card_w + gap);

                    std::string name;
                    if(R_FAILED(acc::GetAccountName(uid, name))) {
                        name = "User";
                    }

                    u8   *icon_buf  = nullptr;
                    size_t icon_sz  = 0;
                    // LoadAccountImage allocates; card element takes ownership of
                    // the decoded texture, raw buffer is freed after construction.
                    if(R_FAILED(acc::LoadAccountImage(uid, icon_buf, icon_sz))) {
                        icon_buf = nullptr;
                        icon_sz  = 0;
                    }

                    UL_LOG_INFO("qdesktop: card[%d] name='%s' x=%d y=%d icon_sz=%zu",
                                i, name.c_str(), cx, card_y,
                                icon_buf ? icon_sz : 0u);

                    auto card = qdesktop::QdUserCardElement::New(
                        qdt, uid, name, icon_buf, icon_sz);

                    if(icon_buf != nullptr) {
                        delete[] icon_buf;
                    }

                    card->SetPos(cx, card_y);
                    card->SetFocused(i == 0);
                    card->SetOnSelect([this](const AccountUid sel_uid) {
                        this->onUserSelected(sel_uid);
                    });
                    this->Add(card);
                    this->qd_user_cards.push_back(std::move(card));
                }
            }

            // ── 8. Power button row (centred, y=950) ─────────────────────────
            // Four buttons: Restart Shutdown Sleep Hekate — each BTN_W=180, gap=24.
            // Total = 4*180 + 3*24 = 792.  Centre = (1920-792)/2 = 564.
            {
                const s32 btn_w = qdesktop::QdPowerButtonElement::BTN_W;
                const s32 gap   = 24;
                const s32 total = 4 * btn_w + 3 * gap;
                const s32 bx    = (1920 - total) / 2;
                const s32 by    = 950;

                this->qd_btn_restart  = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Restart,  "Restart");
                this->qd_btn_shutdown = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Shutdown, "Shutdown");
                this->qd_btn_sleep    = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Sleep,    "Sleep");
                this->qd_btn_hekate   = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Hekate,   "Hekate");

                this->qd_btn_restart->SetPos(bx,                        by);
                this->qd_btn_shutdown->SetPos(bx + (btn_w + gap),       by);
                this->qd_btn_sleep->SetPos   (bx + 2 * (btn_w + gap),   by);
                this->qd_btn_hekate->SetPos  (bx + 3 * (btn_w + gap),   by);

                this->qd_btn_restart->SetOnClick([]() {
                    qdesktop::power::Reboot();
                });
                this->qd_btn_shutdown->SetOnClick([]() {
                    qdesktop::power::Shutdown();
                });
                this->qd_btn_sleep->SetOnClick([]() {
                    qdesktop::power::Sleep();
                });
                this->qd_btn_hekate->SetOnClick([]() {
                    qdesktop::power::RebootToHekate();
                });

                // Gray out Hekate button when the Atmosphère extension is absent.
                this->qd_btn_hekate->SetEnabled(qdesktop::power::IsRebootToHekateSupported());

                this->Add(this->qd_btn_restart);
                this->Add(this->qd_btn_shutdown);
                this->Add(this->qd_btn_sleep);
                this->Add(this->qd_btn_hekate);
            }

            // ── 9. Dev-tools row (bottom-left, y=950) ────────────────────────
            // Three Custom buttons used purely as click regions.
            // State labels are separate TextBlocks overlaid on each button;
            // their text is refreshed each frame by RefreshDevToolLabels().
            // Positions: x=32, x=232, x=432 (column width = btn_w + gap = 200).
            {
                const s32 by     = 950;
                const s32 bx0    = 32;
                const s32 bstep  = qdesktop::QdPowerButtonElement::BTN_W + 20;

                this->qd_btn_nxlink    = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Custom, "Nxlink");
                this->qd_btn_usbserial = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Custom, "USB Serial");
                this->qd_btn_flush     = qdesktop::QdPowerButtonElement::New(qdt, qdesktop::QdPowerButtonElement::Kind::Custom, "Flush Logs");

                this->qd_btn_nxlink->SetPos   (bx0,             by);
                this->qd_btn_usbserial->SetPos(bx0 + bstep,     by);
                this->qd_btn_flush->SetPos    (bx0 + 2 * bstep, by);

                this->qd_btn_nxlink->SetOnClick([this]() {
                    if(qdesktop::dev::IsNxlinkActive()) {
                        qdesktop::dev::DisableNxlink();
                    } else {
                        qdesktop::dev::TryEnableNxlink();
                    }
                    this->RefreshDevToolLabels();
                });
                this->qd_btn_usbserial->SetOnClick([this]() {
                    if(qdesktop::dev::IsUsbSerialActive()) {
                        qdesktop::dev::DisableUsbSerial();
                    } else {
                        qdesktop::dev::TryEnableUsbSerial();
                    }
                    this->RefreshDevToolLabels();
                });
                this->qd_btn_flush->SetOnClick([]() {
                    qdesktop::dev::FlushAllChannels();
                });

                this->Add(this->qd_btn_nxlink);
                this->Add(this->qd_btn_usbserial);
                this->Add(this->qd_btn_flush);

                // State-label TextBlocks overlaid on the dev-tool buttons.
                // Initial text is set here; RefreshDevToolLabels() updates each frame.
                const s32 lbl_y_offset = qdesktop::QdPowerButtonElement::BTN_H + 4;

                this->qd_lbl_nxlink = pu::ui::elm::TextBlock::New(
                    bx0,
                    by + lbl_y_offset,
                    "Nxlink: OFF");
                this->qd_lbl_nxlink->SetColor(qdt.text_secondary);
                this->qd_lbl_nxlink->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
                this->Add(this->qd_lbl_nxlink);

                this->qd_lbl_usbserial = pu::ui::elm::TextBlock::New(
                    bx0 + bstep,
                    by + lbl_y_offset,
                    "USB: OFF");
                this->qd_lbl_usbserial->SetColor(qdt.text_secondary);
                this->qd_lbl_usbserial->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
                this->Add(this->qd_lbl_usbserial);
            }
        }
        // Skip all upstream UI elements — they don't exist in qdesktop mode.
        // All upstream-element-touching member functions also early-return under
        // QDESKTOP_MODE; see LoadSfx, DisposeSfx, OnMenuInput, OnHomeButtonPress,
        // ReloadMenu.
        return;
#endif

        this->info_text = pu::ui::elm::TextBlock::New(0, 0, GetLanguageString("startup_welcome_info"));
        this->info_text->SetColor(g_MenuApplication->GetTextColor());
        g_GlobalSettings.ApplyConfigForElement("startup_menu", "info_text", this->info_text);
        this->Add(this->info_text);

        this->users_menu = pu::ui::elm::Menu::New(0, 0, UsersMenuWidth, g_MenuApplication->GetMenuBackgroundColor(), g_MenuApplication->GetMenuFocusColor(), UsersMenuItemSize, UsersMenuItemsToShow);
        g_GlobalSettings.ApplyConfigForElement("startup_menu", "users_menu", this->users_menu);
        this->Add(this->users_menu);

        this->Add(GetScreenCaptureBackground());
    }

    void StartupMenuLayout::LoadSfx() {
#ifdef QDESKTOP_MODE
        // qdesktop login screen has no sfx in this version.
        return;
#endif
        this->user_create_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Startup/UserCreate.wav"));
        this->user_select_sfx = pu::audio::LoadSfx(TryGetActiveThemeResource("sound/Startup/UserSelect.wav"));
    }

    void StartupMenuLayout::DisposeSfx() {
#ifdef QDESKTOP_MODE
        // Symmetric with LoadSfx — nothing to dispose.
        return;
#endif
        pu::audio::DestroySfx(this->user_create_sfx);
        pu::audio::DestroySfx(this->user_select_sfx);
    }

    void StartupMenuLayout::OnMenuInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) {
#ifdef QDESKTOP_MODE
        // qdesktop input is dispatched to child elements by Plutonium's own
        // per-element OnInput chain.  D-pad card navigation is handled here.
        (void)keys_up; (void)keys_held; (void)touch_pos;

        if(!this->qd_user_cards.empty()) {
            const s32 n = static_cast<s32>(this->qd_user_cards.size());
            if(keys_down & HidNpadButton_Left) {
                const s32 prev = this->qd_focused_card;
                this->qd_focused_card = (this->qd_focused_card - 1 + n) % n;
                this->qd_user_cards[static_cast<size_t>(prev)]->SetFocused(false);
                this->qd_user_cards[static_cast<size_t>(this->qd_focused_card)]->SetFocused(true);
                UL_LOG_INFO("qdesktop: focus %d -> %d (Left)", prev, this->qd_focused_card);
            }
            else if(keys_down & HidNpadButton_Right) {
                const s32 prev = this->qd_focused_card;
                this->qd_focused_card = (this->qd_focused_card + 1) % n;
                this->qd_user_cards[static_cast<size_t>(prev)]->SetFocused(false);
                this->qd_user_cards[static_cast<size_t>(this->qd_focused_card)]->SetFocused(true);
                UL_LOG_INFO("qdesktop: focus %d -> %d (Right)", prev, this->qd_focused_card);
            }
            else if((keys_down & HidNpadButton_A) || (keys_down & HidNpadButton_ZR)) {
                UL_LOG_INFO("qdesktop: A/ZR pressed on card %d", this->qd_focused_card);
                // Activate the currently focused card.
                // Pass the raw keys_down so the card's OnInput can also see ZR.
                this->qd_user_cards[static_cast<size_t>(this->qd_focused_card)]->OnInput(
                    keys_down, 0, 0, pu::ui::TouchPoint{});
            }
        }
        return;
#endif
        UpdateScreenCaptureBackground(false);
    }

    void StartupMenuLayout::OnMenuUpdate() {
#ifdef QDESKTOP_MODE
        // Per-frame live updates: clock, date, dev-tool state labels.
        this->UpdateTimeText(this->time_mtext);
        this->UpdateDateText(this->date_text);
        this->RefreshDevToolLabels();

        // Lazy-build brand + hints textures on the first frame when the
        // renderer is up, then blit them every frame.
        this->EnsureBrandingTextures();

        SDL_Renderer *r = pu::ui::render::GetMainRenderer();
        if(r != nullptr) {
            // ── "Q OS" brand title ────────────────────────────────────────────
            if(this->qd_brand_tex_ != nullptr) {
                int bw = 0, bh = 0;
                SDL_QueryTexture(this->qd_brand_tex_, nullptr, nullptr, &bw, &bh);
                const SDL_Rect bdst { this->qd_brand_x_, this->qd_brand_y_, bw, bh };
                SDL_RenderCopy(r, this->qd_brand_tex_, nullptr, &bdst);
            }

            // ── Controller hints bar (bottom of screen) ───────────────────────
            if(this->qd_hints_tex_ != nullptr) {
                int hw = 0, hh = 0;
                SDL_QueryTexture(this->qd_hints_tex_, nullptr, nullptr, &hw, &hh);
                const SDL_Rect hdst { this->qd_hints_x_, QD_HINTS_Y, hw, hh };
                SDL_RenderCopy(r, this->qd_hints_tex_, nullptr, &hdst);
            }
        }
#endif
    }

    bool StartupMenuLayout::OnHomeButtonPress() {
#ifdef QDESKTOP_MODE
        // Home on the login screen is a no-op — the user must select an account
        // or power off.  Consume the event to prevent upstream handling.
        return true;
#endif
        // ...
        return true;
    }

    void StartupMenuLayout::ReloadMenu() {
#ifdef QDESKTOP_MODE
        // qdesktop login screen does not use the upstream users_menu; nothing to reload.
        return;
#endif
        this->users_menu->ClearItems();

        std::vector<AccountUid> user_ids;
        UL_RC_ASSERT(acc::ListAccounts(user_ids));
        for(const auto &user_id: user_ids) {
            std::string name;
            if(R_SUCCEEDED(acc::GetAccountName(user_id, name))) {
                auto user_item = pu::ui::elm::MenuItem::New(name);

                u8 *user_icon_buf = nullptr;
                size_t user_icon_size = 0;
                UL_RC_ASSERT(acc::LoadAccountImage(user_id, user_icon_buf, user_icon_size));
                auto user_icon = pu::sdl2::TextureHandle::New(pu::ui::render::LoadImageFromBuffer(user_icon_buf, user_icon_size));
                delete[] user_icon_buf;
                user_item->SetIcon(user_icon);

                user_item->AddOnKey(std::bind(&StartupMenuLayout::user_DefaultKey, this, user_id));
                user_item->SetColor(g_MenuApplication->GetTextColor());
                this->users_menu->AddItem(user_item);
            }
        }

        auto create_user_item = pu::ui::elm::MenuItem::New(GetLanguageString("startup_add_user"));
        create_user_item->SetColor(g_MenuApplication->GetTextColor());
        create_user_item->AddOnKey(std::bind(&StartupMenuLayout::create_DefaultKey, this));
        this->users_menu->AddItem(create_user_item);

        this->users_menu->SetSelectedIndex(0);
    }

#ifdef QDESKTOP_MODE

    StartupMenuLayout::~StartupMenuLayout() {
        if(this->qd_brand_tex_ != nullptr) {
            SDL_DestroyTexture(this->qd_brand_tex_);
            this->qd_brand_tex_ = nullptr;
        }
        if(this->qd_hints_tex_ != nullptr) {
            SDL_DestroyTexture(this->qd_hints_tex_);
            this->qd_hints_tex_ = nullptr;
        }
    }

    void StartupMenuLayout::EnsureBrandingTextures() {
        // Both textures are nullptr until the renderer is live (first OnMenuUpdate
        // frame).  After that they're never rebuilt — freed only in the destructor.

        if(this->qd_brand_tex_ == nullptr) {
            // "Q OS" in white at the largest available registered font size (45pt).
            const pu::ui::Color white { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
            this->qd_brand_tex_ = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large),
                "Q OS", white);
            if(this->qd_brand_tex_ != nullptr) {
                int bw = 0, bh = 0;
                SDL_QueryTexture(this->qd_brand_tex_, nullptr, nullptr, &bw, &bh);
                // Centre horizontally on 1920-wide screen; y already set to 220.
                this->qd_brand_x_ = (1920 - bw) / 2;
                UL_LOG_INFO("qdesktop: brand tex %dx%d x=%d y=%d",
                            bw, bh, this->qd_brand_x_, this->qd_brand_y_);
            } else {
                UL_LOG_WARN("qdesktop: brand tex RenderText returned nullptr");
            }
        }

        if(this->qd_hints_tex_ == nullptr) {
            // Controller-hint bar: soft-white (70% alpha) at Small font size.
            const pu::ui::Color hint_clr { 0xFFu, 0xFFu, 0xFFu, 0xB3u };
            // Unicode left/right arrows + A glyph, readable on any TTF font:
            const std::string hint_str =
                "\u2190 \u2192  Select user        A  Log in        + Power      \u2212 Dev Tools";
            this->qd_hints_tex_ = pu::ui::render::RenderText(
                pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                hint_str, hint_clr, 1880u);  // soft cap 20px margin each side
            if(this->qd_hints_tex_ != nullptr) {
                int hw = 0, hh = 0;
                SDL_QueryTexture(this->qd_hints_tex_, nullptr, nullptr, &hw, &hh);
                this->qd_hints_x_ = (1920 - hw) / 2;
                UL_LOG_INFO("qdesktop: hints tex %dx%d x=%d y=%d",
                            hw, hh, this->qd_hints_x_, QD_HINTS_Y);
            } else {
                UL_LOG_WARN("qdesktop: hints tex RenderText returned nullptr");
            }
        }
    }

    void StartupMenuLayout::onUserSelected(const AccountUid uid) {
        // Mirror user_DefaultKey exactly: set user, notify main menu, load.
        this->load_menu = true;
        g_GlobalSettings.SetSelectedUser(uid);

        auto &main_menu_lyt = g_MenuApplication->GetMainMenuLayout();
        if(main_menu_lyt != nullptr) {
            main_menu_lyt->NotifyNextReloadUserChanged();
        }
        g_MenuApplication->LoadMenu(MenuType::Main);
    }

    void StartupMenuLayout::RefreshDevToolLabels() {
        // Update the two dev-tool state labels each frame.
        // The Flush button label is static ("Flush Logs") and never changes.
        if(this->qd_lbl_nxlink) {
            this->qd_lbl_nxlink->SetText(
                qdesktop::dev::IsNxlinkActive() ? "Nxlink: ON" : "Nxlink: OFF");
        }
        if(this->qd_lbl_usbserial) {
            this->qd_lbl_usbserial->SetText(
                qdesktop::dev::IsUsbSerialActive() ? "USB: ON" : "USB: OFF");
        }
    }

#endif

}
