// qd_MonitorLayout.cpp — System stats panel implementation (dock slot 2).
// All stats are sourced from real libnx APIs:
//   ts*    — SOC temperature (millicelsius → °C)
//   psm*   — battery percentage + charger type
//   nifm*  — IP address + internet connection status
//   svcGetInfo — RAM used / total
//   svcGetSystemTick — FPS counter + uptime
//   bt::GetConnectedAudioDevice — Bluetooth device name
#include <ul/menu/qdesktop/qd_MonitorLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/bt/bt_Manager.hpp>
#include <ul/ul_Log.hpp>
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
// libnx service headers
#include <switch/services/ts.h>
#include <switch/services/psm.h>
#include <switch/services/nifm.h>
#include <switch/kernel/svc.h>

// Global menu application instance (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdMonitorLayout::QdMonitorLayout(const QdTheme &theme)
    : theme_(theme)
{
    // Open libnx service sessions. Failures are non-fatal; individual stat
    // queries check the init flags and report "N/A" on failure.

    if (R_SUCCEEDED(tsInitialize())) {
        ts_inited_ = true;
    } else {
        UL_LOG_WARN("qdesktop:monitor: tsInitialize failed — SOC temp unavailable");
    }

    if (R_SUCCEEDED(psmInitialize())) {
        psm_inited_ = true;
    } else {
        UL_LOG_WARN("qdesktop:monitor: psmInitialize failed — battery info unavailable");
    }

    if (R_SUCCEEDED(nifmInitialize(NifmClientType_User))) {
        nifm_inited_ = true;
    } else {
        UL_LOG_WARN("qdesktop:monitor: nifmInitialize failed — network info unavailable");
    }

    // Seed FPS tracking with the current tick so first measurement isn't zero.
    fps_tick_last_   = svcGetSystemTick();
    fps_frame_count_ = 0;

    // Initial stat query so the first frame has data.
    RefreshStats();
}

QdMonitorLayout::~QdMonitorLayout() {
    if (nifm_inited_) { nifmExit();  nifm_inited_ = false; }
    if (psm_inited_)  { psmExit();   psm_inited_  = false; }
    if (ts_inited_)   { tsExit();    ts_inited_   = false; }
}

// ── RefreshStats ──────────────────────────────────────────────────────────────

void QdMonitorLayout::RefreshStats() {
    // ── Thermal ──────────────────────────────────────────────────────────────
    if (ts_inited_) {
        float millicelsius = 0.0f;
        const Result rc = tsGetTemperature(TsLocation_Internal, &millicelsius);
        if (R_SUCCEEDED(rc)) {
            stats_.soc_celsius = millicelsius / 1000.0f;
            stats_.ts_ok = true;
        } else {
            stats_.soc_celsius = 0.0f;
            stats_.ts_ok = false;
            UL_LOG_WARN("qdesktop:monitor: tsGetTemperature failed: 0x%08X", rc);
        }
    } else {
        stats_.soc_celsius = 0.0f;
        stats_.ts_ok = false;
    }

    // ── Power ─────────────────────────────────────────────────────────────────
    if (psm_inited_) {
        u32 pct = 0;
        PsmChargerType charger = PsmChargerType_Unconnected;
        const Result rc1 = psmGetBatteryChargePercentage(&pct);
        const Result rc2 = psmGetChargerType(&charger);
        if (R_SUCCEEDED(rc1) && R_SUCCEEDED(rc2)) {
            stats_.battery_pct = pct;
            stats_.charger     = charger;
            stats_.psm_ok      = true;
        } else {
            stats_.battery_pct = 0;
            stats_.charger     = PsmChargerType_Unconnected;
            stats_.psm_ok      = false;
            UL_LOG_WARN("qdesktop:monitor: psm query failed: rc1=0x%08X rc2=0x%08X", rc1, rc2);
        }
    } else {
        stats_.battery_pct = 0;
        stats_.charger     = PsmChargerType_Unconnected;
        stats_.psm_ok      = false;
    }

    // ── Network ───────────────────────────────────────────────────────────────
    if (nifm_inited_) {
        u32 ip_u32 = 0;
        NifmInternetConnectionStatus conn_status = NifmInternetConnectionStatus_ConnectingUnknown1;
        const Result rc1 = nifmGetCurrentIpAddress(&ip_u32);
        // nifmGetInternetConnectionStatus returns NifmInternetConnectionType + NifmInternetConnectionStatus
        NifmInternetConnectionType conn_type = NifmInternetConnectionType_Invalid;
        u8 signal_str = 0;
        const Result rc2 = nifmGetInternetConnectionStatus(&conn_type, &signal_str, &conn_status);
        if (R_SUCCEEDED(rc1) && ip_u32 != 0) {
            snprintf(stats_.ip_str, sizeof(stats_.ip_str),
                     "%u.%u.%u.%u",
                     (ip_u32 >>  0) & 0xFF,
                     (ip_u32 >>  8) & 0xFF,
                     (ip_u32 >> 16) & 0xFF,
                     (ip_u32 >> 24) & 0xFF);
            stats_.nifm_ok = true;
        } else {
            snprintf(stats_.ip_str, sizeof(stats_.ip_str), "disconnected");
            stats_.nifm_ok = (R_SUCCEEDED(rc1));  // partial: ip 0 is still valid call
        }
        stats_.net_status = R_SUCCEEDED(rc2) ? conn_status
                                              : NifmInternetConnectionStatus_ConnectingUnknown1;
    } else {
        snprintf(stats_.ip_str, sizeof(stats_.ip_str), "N/A");
        stats_.net_status = NifmInternetConnectionStatus_ConnectingUnknown1;
        stats_.nifm_ok    = false;
    }

    // ── System memory ─────────────────────────────────────────────────────────
    u64 mem_used  = 0;
    u64 mem_total = 0;
    const Result mem_rc1 = svcGetInfo(&mem_used,  InfoType_UsedMemorySize,  INVALID_HANDLE, 0);
    const Result mem_rc2 = svcGetInfo(&mem_total, InfoType_TotalMemorySize, INVALID_HANDLE, 0);
    if (R_SUCCEEDED(mem_rc1) && R_SUCCEEDED(mem_rc2)) {
        stats_.mem_used  = mem_used;
        stats_.mem_total = mem_total;
        stats_.mem_ok    = true;
    } else {
        stats_.mem_used  = 0;
        stats_.mem_total = 0;
        stats_.mem_ok    = false;
        UL_LOG_WARN("qdesktop:monitor: svcGetInfo mem failed: rc1=0x%08X rc2=0x%08X",
                    mem_rc1, mem_rc2);
    }

    // ── Uptime ────────────────────────────────────────────────────────────────
    const u64 now_tick   = svcGetSystemTick();
    stats_.uptime_seconds = now_tick / QD_MONITOR_TICK_HZ;

    // ── Bluetooth ─────────────────────────────────────────────────────────────
    const BtmAudioDevice bt_dev = ul::menu::bt::GetConnectedAudioDevice();
    // Check if the device address is the null sentinel {0,0,0,0,0,0}.
    static constexpr BtdrvAddress kNullAddr = {};
    if (memcmp(&bt_dev.addr, &kNullAddr, sizeof(BtdrvAddress)) != 0 && bt_dev.name[0] != '\0') {
        // Copy safely — bt_dev.name is char[249] in libnx BtmAudioDevice.
        snprintf(stats_.bt_name, sizeof(stats_.bt_name), "%s", bt_dev.name);
    } else {
        snprintf(stats_.bt_name, sizeof(stats_.bt_name), "(none)");
    }
}

// ── RenderTile ────────────────────────────────────────────────────────────────

void QdMonitorLayout::RenderTile(SDL_Renderer *r, s32 tx, s32 ty,
                                  const char *title, const char *line1, const char *line2,
                                  pu::ui::Color title_color, bool ok) const
{
    // Tile background.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.surface_glass.r, theme_.surface_glass.g, theme_.surface_glass.b, 0xD8u);
    SDL_Rect tile_rect { tx, ty, QD_MONITOR_TILE_W, QD_MONITOR_TILE_H };
    SDL_RenderFillRect(r, &tile_rect);

    // Border.
    SDL_SetRenderDrawColor(r,
        theme_.focus_ring.r, theme_.focus_ring.g, theme_.focus_ring.b, 0x40u);
    SDL_RenderDrawRect(r, &tile_rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Error indicator strip at top if not ok.
    if (!ok) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0xF8u, 0x71u, 0x71u, 0x80u);
        SDL_Rect err_strip { tx, ty, QD_MONITOR_TILE_W, 4 };
        SDL_RenderFillRect(r, &err_strip);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // Title text.
    SDL_Texture *title_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string(title),
        title_color,
        static_cast<u32>(QD_MONITOR_TILE_W - 16));
    if (title_tex != nullptr) {
        int tw = 0, th = 0;
        SDL_QueryTexture(title_tex, nullptr, nullptr, &tw, &th);
        SDL_Rect tdst { tx + 12, ty + 12, tw, th };
        SDL_RenderCopy(r, title_tex, nullptr, &tdst);
        SDL_DestroyTexture(title_tex);
    }

    // Line1 — main value (larger / brighter).
    SDL_Texture *l1_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
        std::string(line1),
        ok ? theme_.text_primary : theme_.text_secondary,
        static_cast<u32>(QD_MONITOR_TILE_W - 16));
    if (l1_tex != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(l1_tex, nullptr, nullptr, &lw, &lh);
        // Centre vertically in the lower part of the tile.
        const s32 l1y = ty + QD_MONITOR_TILE_H / 2 - lh - 4;
        SDL_Rect l1dst { tx + 12, l1y, lw, lh };
        SDL_RenderCopy(r, l1_tex, nullptr, &l1dst);
        SDL_DestroyTexture(l1_tex);
    }

    // Line2 — secondary value / sub-label (dimmer).
    if (line2 != nullptr && line2[0] != '\0') {
        SDL_Texture *l2_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string(line2),
            theme_.text_secondary,
            static_cast<u32>(QD_MONITOR_TILE_W - 16));
        if (l2_tex != nullptr) {
            int lw = 0, lh = 0;
            SDL_QueryTexture(l2_tex, nullptr, nullptr, &lw, &lh);
            const s32 l2y = ty + QD_MONITOR_TILE_H / 2 + 4;
            SDL_Rect l2dst { tx + 12, l2y, lw, lh };
            SDL_RenderCopy(r, l2_tex, nullptr, &l2dst);
            SDL_DestroyTexture(l2_tex);
        }
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdMonitorLayout::OnRender(pu::ui::render::Renderer::Ref & /*drawer*/,
                                const s32 origin_x, const s32 origin_y)
{
    // ── FPS tracking ──────────────────────────────────────────────────────────
    ++fps_frame_count_;
    const u64 now_tick = svcGetSystemTick();
    const u64 elapsed  = now_tick - fps_tick_last_;
    if (elapsed >= QD_MONITOR_TICK_HZ) {
        // One or more seconds have elapsed — compute fps.
        const float elapsed_secs = static_cast<float>(elapsed) / static_cast<float>(QD_MONITOR_TICK_HZ);
        stats_.fps       = static_cast<float>(fps_frame_count_) / elapsed_secs;
        fps_frame_count_ = 0;
        fps_tick_last_   = now_tick;
    }

    // ── Auto-refresh stats ────────────────────────────────────────────────────
    ++frame_ctr_;
    if (frame_ctr_ >= QD_MONITOR_REFRESH_FRAMES) {
        frame_ctr_ = 0;
        RefreshStats();
    }

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const s32 ax = origin_x;
    const s32 ay = origin_y;

    // ── 1. Full-screen background ─────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x06u, 0x06u, 0x10u, 0xF4u);
    SDL_Rect bg { ax, ay, 1920, 1080 };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 2. Header bar ─────────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g, theme_.topbar_bg.b, 0xF0u);
    SDL_Rect hbar { ax, ay, 1920, 48 };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Header title.
    {
        SDL_Texture *title_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string("Monitor  —  System Stats  [B] Back"),
            theme_.accent,
            static_cast<u32>(1920 - 16));
        if (title_tex != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect tdst { ax + 8, ay + (48 - th) / 2, tw, th };
            SDL_RenderCopy(r, title_tex, nullptr, &tdst);
            SDL_DestroyTexture(title_tex);
        }
    }

    // ── 3. Six stat tiles ─────────────────────────────────────────────────────

    // Tile 0: Thermal
    {
        char l1[64];
        if (stats_.ts_ok) {
            snprintf(l1, sizeof(l1), "%.1f °C", static_cast<double>(stats_.soc_celsius));
        } else {
            snprintf(l1, sizeof(l1), "N/A");
        }
        const s32 tx = ax + QD_MONITOR_GRID_X;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, tx, ty,
                   "Thermal (SOC)", l1, "tsGetTemperature(TsLocation_Internal)",
                   theme_.button_close, stats_.ts_ok);
    }

    // Tile 1: Power
    {
        char l1[64];
        char l2[64];
        if (stats_.psm_ok) {
            snprintf(l1, sizeof(l1), "%u %%", (unsigned)stats_.battery_pct);
            const char *charger_str;
            switch (stats_.charger) {
                case PsmChargerType_Unconnected: charger_str = "No charger"; break;
                case PsmChargerType_EnoughPower: charger_str = "AC (enough power)"; break;
                case PsmChargerType_LowPower:    charger_str = "AC (low power)"; break;
                default:                         charger_str = "AC (unknown type)"; break;
            }
            snprintf(l2, sizeof(l2), "%s", charger_str);
        } else {
            snprintf(l1, sizeof(l1), "N/A");
            snprintf(l2, sizeof(l2), "psm unavailable");
        }
        const s32 tx = ax + QD_MONITOR_GRID_X + QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, tx, ty, "Power (Battery)", l1, l2,
                   theme_.button_minimize, stats_.psm_ok);
    }

    // Tile 2: Network
    {
        char l2[64];
        if (stats_.nifm_ok) {
            const char *status_str;
            switch (stats_.net_status) {
                case NifmInternetConnectionStatus_Connected:
                    status_str = "Connected"; break;
                case NifmInternetConnectionStatus_ConnectingUnknown1:
                case NifmInternetConnectionStatus_ConnectingUnknown2:
                case NifmInternetConnectionStatus_ConnectingUnknown3:
                    status_str = "Connecting…"; break;
                default:
                    status_str = "Unknown"; break;
            }
            snprintf(l2, sizeof(l2), "%s", status_str);
        } else {
            snprintf(l2, sizeof(l2), "nifm unavailable");
        }
        const s32 tx = ax + QD_MONITOR_GRID_X + (QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP) * 2;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, tx, ty, "Network", stats_.ip_str, l2,
                   theme_.accent, stats_.nifm_ok);
    }

    // Tile 3: System (RAM + FPS)
    {
        char l1[64];
        char l2[64];
        if (stats_.mem_ok) {
            const float used_mb  = static_cast<float>(stats_.mem_used)  / (1024.0f * 1024.0f);
            const float total_mb = static_cast<float>(stats_.mem_total) / (1024.0f * 1024.0f);
            snprintf(l1, sizeof(l1), "%.0f / %.0f MiB", (double)used_mb, (double)total_mb);
        } else {
            snprintf(l1, sizeof(l1), "RAM: N/A");
        }
        snprintf(l2, sizeof(l2), "%.1f FPS", (double)stats_.fps);
        const s32 tx = ax + QD_MONITOR_GRID_X;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        RenderTile(r, tx, ty, "System (RAM / FPS)", l1, l2,
                   theme_.button_maximize, stats_.mem_ok);
    }

    // Tile 4: Bluetooth
    {
        const s32 tx = ax + QD_MONITOR_GRID_X + QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        const bool bt_connected = (stats_.bt_name[0] != '\0' &&
                                   strcmp(stats_.bt_name, "(none)") != 0);
        RenderTile(r, tx, ty, "Bluetooth (Audio)", stats_.bt_name,
                   bt_connected ? "connected" : "no device paired/connected",
                   bt_connected ? theme_.button_maximize : theme_.text_secondary,
                   bt_connected);
    }

    // Tile 5: Uptime
    {
        char l1[64];
        const u64 uptime = stats_.uptime_seconds;
        const u64 secs   = uptime % 60;
        const u64 mins   = (uptime / 60) % 60;
        const u64 hours  = (uptime / 3600) % 24;
        const u64 days   = uptime / 86400;
        if (days > 0) {
            snprintf(l1, sizeof(l1), "%llud %lluh %llum %llus",
                     (unsigned long long)days, (unsigned long long)hours,
                     (unsigned long long)mins, (unsigned long long)secs);
        } else {
            snprintf(l1, sizeof(l1), "%lluh %llum %llus",
                     (unsigned long long)hours,
                     (unsigned long long)mins, (unsigned long long)secs);
        }
        const s32 tx = ax + QD_MONITOR_GRID_X + (QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP) * 2;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        RenderTile(r, tx, ty, "Uptime", l1, "svcGetSystemTick / 19.2 MHz",
                   theme_.text_secondary, true);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdMonitorLayout::OnInput(const u64 keys_down, const u64 /*keys_up*/,
                               const u64 /*keys_held*/,
                               const pu::ui::TouchPoint /*touch_pos*/)
{
    if (keys_down & HidNpadButton_B) {
        if (g_MenuApplication) {
            g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        }
    }
}

} // namespace ul::menu::qdesktop
