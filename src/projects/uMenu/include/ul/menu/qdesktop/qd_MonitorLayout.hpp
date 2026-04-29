// qd_MonitorLayout.hpp — System stats panel for Q OS qdesktop (dock slot 2).
// Inherits pu::ui::elm::Element (same pattern as QdVaultLayout / QdTextViewer).
// 2×3 grid of stat tiles (Thermal, Power, Network, System, Bluetooth, Uptime).
// Stats are refreshed every QD_MONITOR_REFRESH_FRAMES frames (~0.5 s at 60 fps).
//
// libnx APIs used:
//   tsGetTemperature(TsLocation_Internal, &millicelsius)  — SOC temp
//   psmGetBatteryChargePercentage + psmGetChargerType     — battery / charger
//   nifmGetCurrentIpAddress + nifmGetInternetConnectionStatus — network
//   svcGetInfo(InfoType_UsedMemorySize / TotalMemorySize) — RAM
//   svcGetSystemTick()                                    — FPS counter + uptime
//   bt::GetConnectedAudioDevice()                         — BT device name
//
// Input mapping:
//   B — return to Main desktop (LoadMenu(MenuType::Main))
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <switch.h>

namespace ul::menu::qdesktop {

// ── Layout pixel constants ────────────────────────────────────────────────────

/// Auto-refresh cadence: re-query all stats every N rendered frames.
static constexpr int QD_MONITOR_REFRESH_FRAMES = 30;

/// Number of stat tiles: 6 (Thermal, Power, Network, System, Bluetooth, Uptime).
static constexpr int QD_MONITOR_TILE_COUNT = 6;

/// Tegra X1 system tick frequency (Hz).
static constexpr u64 QD_MONITOR_TICK_HZ = 19200000ULL;

// Tile grid geometry (fits inside body between topbar and dock).
static constexpr s32 QD_MONITOR_TILE_COLS = 3;
static constexpr s32 QD_MONITOR_TILE_ROWS = 2;
static constexpr s32 QD_MONITOR_TILE_W    = 580;
static constexpr s32 QD_MONITOR_TILE_H    = 380;
static constexpr s32 QD_MONITOR_TILE_GAP  =  20;

// Body origin (below 48 px topbar, centred horizontally).
static constexpr s32 QD_MONITOR_BODY_TOP  =  80;  // a bit of padding below topbar
static constexpr s32 QD_MONITOR_GRID_X    = (1920 - QD_MONITOR_TILE_COLS * QD_MONITOR_TILE_W
                                              - (QD_MONITOR_TILE_COLS - 1) * QD_MONITOR_TILE_GAP) / 2;

// ── QdMonitorLayout ───────────────────────────────────────────────────────────

/// Full-screen dock-slot 2 element: real-time system stats panel.
/// All libnx service sessions are opened in the constructor and closed in the
/// destructor — the layout is kept alive for the lifetime of the desktop session.
class QdMonitorLayout : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdMonitorLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdMonitorLayout>(theme);
    }

    explicit QdMonitorLayout(const QdTheme &theme);
    ~QdMonitorLayout();

    // ── Element interface ──────────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

private:
    // ── Stat record (refreshed every REFRESH_FRAMES) ──────────────────────────
    struct Stats {
        // Thermal
        float soc_celsius;       ///< SOC internal temp in °C (from tsGetTemperature)
        bool  ts_ok;             ///< true if tsGetTemperature succeeded

        // Power
        u32   battery_pct;       ///< 0-100 (from psmGetBatteryChargePercentage)
        PsmChargerType charger;  ///< charger type (from psmGetChargerType)
        bool  psm_ok;            ///< true if psm calls succeeded

        // Network
        char  ip_str[20];        ///< "A.B.C.D" or "disconnected"
        NifmInternetConnectionStatus net_status;
        bool  nifm_ok;           ///< true if nifm calls succeeded

        // System (memory)
        u64   mem_used;          ///< bytes (svcGetInfo InfoType_UsedMemorySize)
        u64   mem_total;         ///< bytes (svcGetInfo InfoType_TotalMemorySize)
        bool  mem_ok;            ///< true if svcGetInfo succeeded for both

        // FPS (computed between refreshes)
        float fps;               ///< frames per second measured over last second

        // Bluetooth
        char  bt_name[256];      ///< connected device friendly name or "(none)"

        // Uptime
        u64   uptime_seconds;    ///< seconds since boot (svcGetSystemTick / QD_MONITOR_TICK_HZ)
    };

    // ── Private helpers ────────────────────────────────────────────────────────

    /// Query all stats via libnx and populate stats_.
    void RefreshStats();

    /// Render one tile at pixel position (tx, ty).
    void RenderTile(SDL_Renderer *r, s32 tx, s32 ty, const char *title,
                    const char *line1, const char *line2,
                    pu::ui::Color title_color, bool ok) const;

    // ── State ──────────────────────────────────────────────────────────────────
    QdTheme theme_;
    Stats   stats_       = {};
    int     frame_ctr_   = 0;  ///< frame counter for refresh cadence

    // FPS tracking
    u64     fps_tick_last_   = 0;  ///< svcGetSystemTick value at last FPS reset
    int     fps_frame_count_ = 0;  ///< frames counted since last FPS reset

    // libnx service init flags (set in ctor, cleared in dtor).
    bool    ts_inited_   = false;
    bool    psm_inited_  = false;
    bool    nifm_inited_ = false;

    // Bottom hint bar — rendered once in ctor, freed in dtor.
    SDL_Texture *hint_bar_tex_;
};

} // namespace ul::menu::qdesktop
