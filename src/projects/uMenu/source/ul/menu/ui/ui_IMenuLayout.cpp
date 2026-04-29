#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/ui/ui_Common.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/qdesktop/qd_DesktopIcons.hpp>
#include <ul/net/net_Service.hpp>
#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <vector>
#include <cstring>

extern ul::menu::ui::GlobalSettings g_GlobalSettings;
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::ui {

    namespace {

        s32 g_HomeButtonPressHandleCount = 0;

        std::vector<std::string> g_WeekdayList;

        // ── Procedural icon helpers ───────────────────────────────────────────
        // All icons are 32×32 RGBA (SDL_PIXELFORMAT_ABGR8888, byte order R,G,B,A).
        // CreateTexture32 takes a filled 32×32×4-byte buffer and returns a
        // TextureHandle the caller can pass directly to Image::SetImage().
        // Ownership of the SDL_Texture* transfers to the TextureHandle smart ptr.
        //
        // Native-pulls task: replace Battery/{10..100,Charging}.png and
        // Connection/{0,1,2,3,None}.png with live-data procedural draws.
        // Services already initialized in main.cpp:
        //   psm via psmInitialize() (line 90)
        //   nifm via ul::net::Initialize() (line 89)
        // Data already polled per-frame in qd_MonitorLayout.cpp.

        static constexpr s32 kIconDim = 32; // icon width == height in pixels

        // Set one RGBA pixel in a kIconDim×kIconDim buffer (byte order R,G,B,A).
        inline void SetPx(u8 *buf, s32 x, s32 y, u8 r, u8 g, u8 b, u8 a = 0xFF) {
            if(x < 0 || x >= kIconDim || y < 0 || y >= kIconDim) { return; }
            u8 *p = buf + (y * kIconDim + x) * 4;
            p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        }

        // Fill an axis-aligned rectangle inside the icon buffer.
        void FillRect32(u8 *buf, s32 x, s32 y, s32 w, s32 h,
                        u8 r, u8 g, u8 b, u8 a = 0xFF) {
            for(s32 dy = 0; dy < h; dy++) {
                for(s32 dx = 0; dx < w; dx++) {
                    SetPx(buf, x + dx, y + dy, r, g, b, a);
                }
            }
        }

        // Upload a kIconDim×kIconDim RGBA buffer to a new SDL_Texture and wrap
        // it in a TextureHandle::Ref.  Returns nullptr on SDL failure.
        pu::sdl2::TextureHandle::Ref CreateTexture32(const u8 *rgba) {
            SDL_Renderer *rend = pu::ui::render::GetMainRenderer();
            if(!rend) { return nullptr; }
            SDL_Texture *tex = SDL_CreateTexture(rend,
                                                 SDL_PIXELFORMAT_ABGR8888,
                                                 SDL_TEXTUREACCESS_STATIC,
                                                 kIconDim, kIconDim);
            if(!tex) { return nullptr; }
            SDL_UpdateTexture(tex, nullptr, rgba, kIconDim * 4);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            return pu::sdl2::TextureHandle::New(tex);
        }

        // ── Battery icon ─────────────────────────────────────────────────────
        // Layout (32×32, all coordinates inclusive of the given pixel):
        //   Outer border: 2px-thick rectangle, x=2..27, y=8..23  (26 wide, 16 tall)
        //   "Nub" (positive terminal): x=28..29, y=13..18         (2 wide, 6 tall)
        //   Fill region inside border: x=4..25, y=10..21          (22 wide, 12 tall)
        //   Fill width proportional to battery_level/100 * 22 pixels
        //   Color:
        //     level < 10  → red   #FF4444
        //     level < 20  → gray  #888888
        //     level >= 20 → cyan  #7DD3FC
        //   Charging bolt glyph (white, 5 wide × 9 tall, centered at x=14..18, y=9..17):
        //     A pixelated downward-pointing lightning bolt drawn when is_charging==true.
        pu::sdl2::TextureHandle::Ref MakeBatteryIcon(u32 level, bool is_charging) {
            std::vector<u8> buf(kIconDim * kIconDim * 4, 0);
            u8 *b = buf.data();

            // Choose fill color based on level
            u8 fr, fg, fb;
            if(level < 10) {
                fr = 0xFF; fg = 0x44; fb = 0x44; // red
            } else if(level < 20) {
                fr = 0x88; fg = 0x88; fb = 0x88; // gray
            } else {
                fr = 0x7D; fg = 0xD3; fb = 0xFC; // cyan #7DD3FC
            }

            // Border color matches fill color for a monochrome look
            const u8 br = fr, bg_ = fg, bb = fb;

            // Draw outer border (2px thick) — top/bottom rows, left/right columns
            // Body spans x=[2..27], y=[8..23] (26 wide, 16 tall)
            constexpr s32 bx = 2, by = 8, bw = 26, bh = 16;
            // Top 2 rows
            FillRect32(b, bx, by,     bw, 2, br, bg_, bb);
            // Bottom 2 rows
            FillRect32(b, bx, by+bh-2, bw, 2, br, bg_, bb);
            // Left 2 columns (middle rows only)
            FillRect32(b, bx,     by+2, 2, bh-4, br, bg_, bb);
            // Right 2 columns (middle rows only)
            FillRect32(b, bx+bw-2, by+2, 2, bh-4, br, bg_, bb);

            // "Nub" (positive terminal): 2×6 to the right of the body
            FillRect32(b, bx+bw, by+5, 2, 6, br, bg_, bb);

            // Fill region: x=[bx+2..bx+bw-3], y=[by+2..by+bh-3] = 22×12
            constexpr s32 fx = bx+2, fy = by+2, fw = bw-4, fh = bh-4;
            const s32 fill_w = static_cast<s32>((level * fw + 50) / 100);
            if(fill_w > 0) {
                FillRect32(b, fx, fy, fill_w, fh, fr, fg, fb);
            }

            // Charging bolt glyph (white pixels, centered in body)
            // 5-wide × 9-tall bitmap, top-left at (fx + fw/2 - 2, fy + fh/2 - 4)
            if(is_charging) {
                const s32 gx = fx + fw/2 - 2; // ~13
                const s32 gy = fy + 1;         // ~11
                // Row 0: pixels at col 3,4
                SetPx(b, gx+3, gy+0, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+4, gy+0, 0xFF, 0xFF, 0xFF);
                // Row 1: pixels at col 2,3,4
                SetPx(b, gx+2, gy+1, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+3, gy+1, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+4, gy+1, 0xFF, 0xFF, 0xFF);
                // Row 2: pixels at col 1,2,3,4
                SetPx(b, gx+1, gy+2, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+2, gy+2, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+3, gy+2, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+4, gy+2, 0xFF, 0xFF, 0xFF);
                // Row 3: pixels at col 0,1,2,3,4 (widest row)
                SetPx(b, gx+0, gy+3, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+1, gy+3, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+2, gy+3, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+3, gy+3, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+4, gy+3, 0xFF, 0xFF, 0xFF);
                // Row 4: pixels at col 0,1,2 (upper-right half of bolt tapering)
                SetPx(b, gx+0, gy+4, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+1, gy+4, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+2, gy+4, 0xFF, 0xFF, 0xFF);
                // Row 5: pixels at col 0,1
                SetPx(b, gx+0, gy+5, 0xFF, 0xFF, 0xFF);
                SetPx(b, gx+1, gy+5, 0xFF, 0xFF, 0xFF);
                // Row 6: pixel at col 0
                SetPx(b, gx+0, gy+6, 0xFF, 0xFF, 0xFF);
            }

            return CreateTexture32(b);
        }

        // ── Connection (WiFi strength) icon ───────────────────────────────────
        // 4 vertical bars representing signal strength 0-3 + disconnected.
        // Layout: bars are 5px wide with 2px gaps, bottom-aligned at y=24.
        //   Bar 0 (leftmost):  x= 2, height= 6
        //   Bar 1:             x= 9, height=10
        //   Bar 2:             x=16, height=16
        //   Bar 3 (rightmost): x=23, height=24
        // Active (strength >= bar_index+1): cyan #7DD3FC
        // Inactive:                         gray #3A3A3A
        // Disconnected (has_conn==false):   all bars gray + "X" overlay in dim red
        pu::sdl2::TextureHandle::Ref MakeConnectionIcon(bool has_conn, u32 strength) {
            std::vector<u8> buf(kIconDim * kIconDim * 4, 0);
            u8 *b = buf.data();

            static constexpr s32 kBarBottom = 25; // bottom y of all bars
            static constexpr s32 kBarW = 5;
            // Bar x-origins and heights
            static constexpr s32 kBarX[4] = { 2,  9, 16, 23 };
            static constexpr s32 kBarH[4] = { 6, 11, 17, 25 };

            for(s32 i = 0; i < 4; i++) {
                const bool active = has_conn && (strength > static_cast<u32>(i));
                const u8 r = active ? 0x7D : 0x3A;
                const u8 g = active ? 0xD3 : 0x3A;
                const u8 bv = active ? 0xFC : 0x3A;
                FillRect32(b, kBarX[i], kBarBottom - kBarH[i], kBarW, kBarH[i], r, g, bv);
            }

            // Disconnected: draw an "X" in dim red over the icon
            if(!has_conn) {
                // Two 1-pixel diagonal lines from (8,6) to (23,21) and (23,6) to (8,21)
                // Bresenham line from top-left to bottom-right
                {
                    s32 x0=8, y0=6, x1=23, y1=21;
                    s32 dx=x1-x0, dy=y1-y0;
                    s32 steps = (dx > dy) ? dx : dy;
                    for(s32 k=0; k<=steps; k++) {
                        s32 px = x0 + dx * k / steps;
                        s32 py = y0 + dy * k / steps;
                        SetPx(b, px, py, 0xCC, 0x33, 0x33);
                        // 2px wide line: also draw adjacent pixel
                        SetPx(b, px+1, py, 0xCC, 0x33, 0x33);
                    }
                }
                // Bresenham line from top-right to bottom-left
                {
                    s32 x0=23, y0=6, x1=8, y1=21;
                    s32 dx=x0-x1, dy=y1-y0; // positive deltas
                    s32 steps = (dx > dy) ? dx : dy;
                    for(s32 k=0; k<=steps; k++) {
                        s32 px = x0 - dx * k / steps;
                        s32 py = y0 + dy * k / steps;
                        SetPx(b, px, py, 0xCC, 0x33, 0x33);
                        SetPx(b, px-1, py, 0xCC, 0x33, 0x33);
                    }
                }
            }

            return CreateTexture32(b);
        }

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
            // Native-pulls task: replaced Connection/{0,1,2,3,None}.png with
            // a procedurally generated 32×32 RGBA texture built from live nifm
            // data.  MakeConnectionIcon renders 4 signal-strength bars (cyan
            // when active, dark-gray when inactive) and overlays a red "X" when
            // the console has no internet connection.  No romfs PNGs are loaded.
            auto tex = MakeConnectionIcon(has_conn, conn_strength);
            if(tex) {
                icon->SetImage(tex);
                // Re-apply 32×32 after SetImage — Plutonium resets rendered
                // size to the texture's natural dimensions on every SetImage call
                // (Cycle K-topiconsfit fix preserved).
                icon->SetWidth(32);
                icon->SetHeight(32);
            }
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

            // Native-pulls task: replaced Battery/{10..100,Charging}.png with a
            // procedurally generated 32×32 RGBA texture built from live psm data.
            // MakeBatteryIcon renders a battery-body outline, a fill bar proportional
            // to battery_level (cyan ≥20%, gray <20%, red <10%), and embeds the
            // charging-bolt glyph (white pixels) when is_charging is true.
            // The separate charging_top_icon PNG is no longer needed and is hidden
            // permanently — the bolt is drawn directly into the battery icon.
            auto tex = MakeBatteryIcon(battery_level, is_charging);
            if(tex) {
                base_top_icon->SetImage(tex);
                // Re-apply 32×32 after SetImage — Plutonium resets rendered
                // size to the texture's natural dimensions on every SetImage call
                // (Cycle K-topiconsfit fix preserved).
                base_top_icon->SetWidth(32);
                base_top_icon->SetHeight(32);
            }
            // charging_top_icon was previously shown/hidden here; the bolt is
            // now embedded in the battery icon above.  Hide permanently so the
            // Plutonium-registered slot is a no-op rather than a dangling ref.
            charging_top_icon->SetVisible(false);
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
                        // K+1 Phase 1: invalidate the Nintendo-classify cache so
                        // the next SetApplicationEntries call reclassifies with the
                        // updated catalog.  Safe to call even when records only
                        // changed attributes (not added/deleted) because the classify
                        // result depends on app_id, not on content.
                        ul::menu::qdesktop::QdDesktopIconsElement::InvalidateNintendoClassifyCache();
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
