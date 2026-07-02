// qd_MonitorLayout.hpp — System stats panel for Q OS qdesktop (dock slot 2).
// Inherits QdContentElement (passive renderer contract — see qd_ContentElement.hpp).
// 2×3 grid of stat tiles (Thermal, Power, Network, System, Bluetooth, Uptime).
// Stats are refreshed every QD_MONITOR_REFRESH_FRAMES frames (~0.5 s at 60 fps).
//
// libnx APIs used:
//   ts:        SoC + PCB temp.  Session-API (tsOpenSession + tsSessionGetTemperature)
//              preferred (FW 10.0.0+) because the legacy wrappers
//              (tsGetTemperature / tsGetTemperatureMilliC) were removed in
//              HorizonOS 14.0.0 and silently return 0 / unsupported on modern
//              firmware.  Legacy wrappers are tried as fallbacks for older HW.
//              NOTE: libnx defines TsLocation_Internal = TMP451 *PCB* sensor
//              and TsLocation_External = TMP451 *SoC* sensor (counter-intuitive
//              naming — see ts.h comments).  We label rows by sensor function
//              (SoC / PCB), not by the libnx enum suffix.
//   psmGetBatteryChargePercentage + psmGetChargerType     — battery / charger
//   nifmGetCurrentIpAddress + nifmGetInternetConnectionStatus — network
//   svcGetInfo(InfoType_UsedMemorySize / TotalMemorySize) — RAM
//   svcGetSystemTick()                                    — FPS counter + uptime
//   bt::GetConnectedAudioDevice()                         — BT device name
//   clkrstOpenSession + clkrstGetClockRate                 — CPU/GPU clocks on FW 8.0.0+
//   pcvGetClockRate(PcvModule_CpuBus / PcvModule_GPU)     — CPU/GPU clocks legacy fallback (FW ≤7.0.1)
//
// Input mapping:
//   B — return to Main desktop (LoadMenu(MenuType::Main))
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_ContentElement.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <switch.h>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Layout pixel constants ────────────────────────────────────────────────────

/// Auto-refresh cadence: re-query all stats every N rendered frames.
static constexpr int QD_MONITOR_REFRESH_FRAMES = 30;

/// Number of stat tiles: 8 (Thermal/SoC, Power, Network, System, Bluetooth,
/// Uptime, Clocks, Thermal/PCB).  QD_MONITOR_MAX_TILES cache slots cover all 8.
static constexpr int QD_MONITOR_TILE_COUNT = 8;

/// Tegra X1 system tick frequency (Hz).
static constexpr u64 QD_MONITOR_TICK_HZ = 19200000ULL;

// Tile grid geometry — fits inside the 780×618 natural canvas (W7-TILE-FIX).
// W7-TILE-FIX: natural canvas updated from 800×900 to 780×618 so the uniform-
// scale factor is governed by width (1278÷780≈1.64) rather than height, giving
// each tile a visual width of ~393px — enough for the longest subtitle strings
// ("tsSessionGetTemperature(SoC)", "no device paired/connected", etc.).
//
//   Natural canvas: 780 × 618
//   Scale at DEFAULT_WIN_W/H (1280×800, viewport 1278×715):
//     width-bound:  1278 ÷ 780 = 1.638
//     height-bound: 715  ÷ 618 = 1.157  ← binding axis (uniform-fit)
//   Visual tile width  = 240 × 1.157 = 278 px
//   Text wrap_width    = (240−16) × 1.157 = 259 px  → longest subtitle fits
//
//   3 cols × 240 + 2 gaps × 16 = 752  → grid_x = (780 − 752) / 2 = 14
//   3 rows × 160 + 2 gaps × 16 = 512  → body_top=56, hint_bar_h=50
//   Total natural height: 56 + 512 + 50 = 618 px  (uniform; no extra margin)
static constexpr s32 QD_MONITOR_TILE_COLS = 3;
static constexpr s32 QD_MONITOR_TILE_ROWS = 3;
static constexpr s32 QD_MONITOR_TILE_W    = 240;
static constexpr s32 QD_MONITOR_TILE_H    = 160;
static constexpr s32 QD_MONITOR_TILE_GAP  =  16;

// Body origin (below 48 px topbar, centred horizontally inside natural canvas).
static constexpr s32 QD_MONITOR_BODY_TOP  =  56;  // ~8 px padding below topbar
// (Legacy QD_MONITOR_GRID_X removed: OnRender computes grid_x from
// GetNaturalW() at runtime so the layout stays self-consistent if the
// natural width changes.)

// ── QdMonitorLayout ───────────────────────────────────────────────────────────

/// Full-screen dock-slot 2 element: real-time system stats panel.
/// All libnx service sessions are opened in the constructor and closed in the
/// destructor — the layout is kept alive for the lifetime of the desktop session.
class QdMonitorLayout : public QdContentElement {
public:
    using Ref = std::shared_ptr<QdMonitorLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdMonitorLayout>(theme);
    }

    explicit QdMonitorLayout(const QdTheme &theme);
    ~QdMonitorLayout();

    // ── QdContentElement interface ─────────────────────────────────────────────
    // W7-TILE-FIX: canvas updated to 780×618 (see tile-grid constant comment).
    // Height 618 = 56 (header) + 3×160 (tiles) + 2×16 (gaps) + 50 (hint bar).
    s32 GetNaturalW() const override { return 780; }
    s32 GetNaturalH() const override { return 618; }
    // W7-TILE-FIX: keep uniform-fit so all 3 tile rows are always fully visible.
    // At the new 780×618 canvas the binding axis is height (scale≈1.157) not
    // width, so tiles render at ~278 px wide — enough for all subtitle strings.
    bool PrefersWidthBoundScale() const override { return false; }

    // ── Element interface ──────────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return GetNaturalW(); }
    s32 GetHeight() override { return GetNaturalH(); }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public API ────────────────────────────────────────────────────────────

    /// Refresh stats from libnx service APIs.  Called via on_tick (gated by
    /// WindowState::Normal) so service calls never fire during minimize snapshots.
    /// Same pattern as QdSettingsElement::Refresh() and QdAboutElement::Refresh().
    void Refresh() { RefreshStats(); frame_ctr_ = 0; }

private:
    // ── Stat record (refreshed every REFRESH_FRAMES) ──────────────────────────
    struct Stats {
        // Thermal — SoC = TsLocation_External, PCB = TsLocation_Internal.
        // Last-attempt rc is stored so the render path can show "rc=0x%X" in
        // place of the value when the query fails (creator-pull diagnostics).
        float soc_celsius;       ///< SoC temp in °C (TsLocation_External)
        bool  ts_ok;             ///< true if SoC temperature query succeeded
        u32   ts_soc_last_rc;    ///< last rc from SoC temperature probe (0 on success)
        float pcb_celsius;       ///< PCB temp in °C (TsLocation_Internal)
        bool  ts_pcb_ok;         ///< true if PCB temperature query succeeded
        u32   ts_pcb_last_rc;    ///< last rc from PCB temperature probe (0 on success)

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

        // CPU / GPU clocks (Sphaira-absorb sprint 1 — pcvGetClockRate)
        u32   cpu_hz;            ///< CPU clock in Hz (0 if unavailable)
        u32   gpu_hz;            ///< GPU clock in Hz (0 if unavailable)
        bool  pcv_ok;            ///< true if at least one pcv query succeeded

    };

    // ── Private helpers ────────────────────────────────────────────────────────

    /// Query all stats via libnx and populate stats_.
    void RefreshStats();

    /// Render one tile at pixel position (tx, ty).
    /// tile_idx identifies the cache slot (0–QD_MONITOR_MAX_TILES-1).
    /// W5-PERF-HOTSPOTS #1 (P0-A): non-const to allow updating cached textures.
    void RenderTile(SDL_Renderer *r, int tile_idx, s32 tx, s32 ty,
                    const char *title, const char *line1, const char *line2,
                    pu::ui::Color title_color, bool ok);

    // ── State ──────────────────────────────────────────────────────────────────

    QdTheme theme_;
    Stats   stats_       = {};
    int     frame_ctr_   = 0;  ///< frame counter for refresh cadence

    // FPS tracking
    u64     fps_tick_last_   = 0;  ///< svcGetSystemTick value at last FPS reset
    int     fps_frame_count_ = 0;  ///< frames counted since last FPS reset

    // libnx service init flags (set in ctor, cleared in dtor).
    bool    ts_inited_     = false;
    // ts session-API state (FW 10.0.0+).  Sessions are opened once and reused
    // across refreshes so we don't pay the IPC cost per-frame.  Closed in dtor.
    bool    ts_sess_soc_open_  = false;  ///< true if TsDeviceCode_LocationExternal session is open
    bool    ts_sess_pcb_open_  = false;  ///< true if TsDeviceCode_LocationInternal session is open
    bool    ts_use_session_    = false;  ///< true → prefer session API (FW 10.0.0+)
    TsSession ts_sess_soc_     = {};     ///< handle for SoC (External) device-code session
    TsSession ts_sess_pcb_     = {};     ///< handle for PCB (Internal) device-code session
    bool    psm_inited_    = false;
    bool    nifm_inited_   = false;
    bool    pcv_inited_    = false;  ///< pcvInitialize() succeeded (legacy CPU/GPU clocks, FW ≤7.0.1)
    bool    clkrst_inited_ = false;  ///< clkrstInitialize() succeeded (CPU/GPU clocks, FW 8.0.0+)

    // W5-PERF-HOTSPOTS #8 (P0-D): clkrst sessions hoisted from per-refresh to ctor/dtor.
    // Eliminates 4 IPC round-trips (2× open + 2× close) per 0.5 s refresh cycle.
    bool          clkrst_sess_cpu_open_ = false;  ///< true when sess_cpu_ is open
    bool          clkrst_sess_gpu_open_ = false;  ///< true when sess_gpu_ is open
    ClkrstSession clkrst_sess_cpu_      = {};     ///< persistent CPU clock session
    ClkrstSession clkrst_sess_gpu_      = {};     ///< persistent GPU clock session

    // W5-PERF-HOTSPOTS #1/#2 (P0-A): per-tile text texture cache for RenderTile().
    // Eliminates 3 RenderText+DeleteTexture pairs per tile per frame (~1440/s at 60 Hz × 8 tiles).
    // Each slot caches one SDL_Texture* and the last string that produced it.
    // On render: compare current string against last_*[]; if changed, DeleteTexture(old),
    // RenderText(new), memcpy new string into shadow; otherwise blit the cached texture.
    // Freed in FreeAllTileTextures() called from dtor.
    static constexpr int QD_MONITOR_MAX_TILES = 8;  // tiles rendered (tiles 0-7 currently)
    static constexpr int QD_MONITOR_TEX_BUF   = 64;
    SDL_Texture *tile_title_tex_[QD_MONITOR_MAX_TILES] = {};
    SDL_Texture *tile_line1_tex_[QD_MONITOR_MAX_TILES] = {};
    SDL_Texture *tile_line2_tex_[QD_MONITOR_MAX_TILES] = {};
    char tile_last_title_[QD_MONITOR_MAX_TILES][QD_MONITOR_TEX_BUF] = {};
    char tile_last_line1_[QD_MONITOR_MAX_TILES][QD_MONITOR_TEX_BUF] = {};
    char tile_last_line2_[QD_MONITOR_MAX_TILES][QD_MONITOR_TEX_BUF] = {};
    // W6-LEDGER: per-tile ledger handles for tracking (kind=Texture).
    uint64_t tile_title_lh_[QD_MONITOR_MAX_TILES] = {};
    uint64_t tile_line1_lh_[QD_MONITOR_MAX_TILES] = {};
    uint64_t tile_line2_lh_[QD_MONITOR_MAX_TILES] = {};
    // Cached header title texture (constant string — built once in ctor, freed in dtor).
    // W5-PERF-HOTSPOTS #1 (P0-A): header text was re-rasterised every frame.
    SDL_Texture *header_title_tex_ = nullptr;
    int          header_title_w_   = 0;
    int          header_title_h_   = 0;

    // Release all per-tile and header cached textures.  Called from dtor.
    void FreeAllTileTextures();

    // Bottom hint bar — rendered once in ctor, freed in dtor.
    SDL_Texture *hint_bar_tex_;

    // ── Resources view (W6-LEDGER Part C) ──────────────────────────────────────
    // View mode: false = Stats tiles (default), true = Resources view.
    bool          view_resources_  = false;
    // Snapshot of the ledger, refreshed once per second.
    QdResourceLedger::Snapshot res_snap_ = {};
    uint64_t      res_snap_tick_   = 0;  ///< svcGetSystemTick at last snapshot

    // Per-kind row text texture cache (same UpdateCachedTexture pattern as tiles).
    static constexpr int QD_RES_KIND_COUNT = static_cast<int>(QdResKind::Count);
    static constexpr int QD_RES_ROW_BUF    = 96;  ///< shadow-string buffer size
    SDL_Texture *res_row_tex_[QD_RES_KIND_COUNT]          = {};
    char         res_row_last_[QD_RES_KIND_COUNT][QD_RES_ROW_BUF] = {};
    uint64_t     res_row_lh_[QD_RES_KIND_COUNT]           = {};
    // Total and caption rows.
    SDL_Texture *res_total_tex_   = nullptr;
    SDL_Texture *res_caption_tex_ = nullptr;
    char         res_total_last_[QD_RES_ROW_BUF]  = {};
    uint64_t     res_total_lh_    = 0;
    // View-toggle hint bar texture — rebuilt when view changes.
    SDL_Texture *hint_bar_res_tex_ = nullptr;
    // Y-button dump notification (shown for ~120 frames after Y press).
    SDL_Texture *dump_notif_tex_ = nullptr;
    int          dump_notif_frames_ = 0;

    // Release Resources view textures.  Called from dtor.
    void FreeResViewTextures();

    // Render the Resources view into the current frame.
    void RenderResourcesView(SDL_Renderer *r, s32 ax, s32 ay);
};

} // namespace ul::menu::qdesktop
