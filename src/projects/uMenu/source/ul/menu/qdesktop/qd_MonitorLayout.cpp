// qd_MonitorLayout.cpp — System stats panel implementation (dock slot 2).
// All stats are sourced from real libnx APIs:
//   ts*    — SOC temperature (millicelsius → °C)
//   psm*   — battery percentage + charger type
//   nifm*  — IP address + internet connection status
//   svcGetInfo — RAM used / total
//   svcGetSystemTick — FPS counter + uptime
//   bt::GetConnectedAudioDevice — Bluetooth device name
#include <ul/menu/qdesktop/qd_MonitorLayout.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>  // v2.6.0 — full-screen bg reads g_QdTheme.desktop_bg
#include <ul/menu/qdesktop/qd_PerfLogger.hpp>  // W7: verbose perf toggle (ZR)
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/bt/bt_Manager.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstdint>
// libnx service headers
#include <switch/services/ts.h>
#include <switch/services/psm.h>
#include <switch/services/nifm.h>
#include <switch/kernel/svc.h>
#include <switch/services/pcv.h>     // PcvModuleId_CpuBus / PcvModuleId_GPU
#include <switch/services/clkrst.h>  // clkrst* — CPU/GPU clocks (FW 8.0.0+ replacement for pcvGetClockRate)

// Global menu application instance (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdMonitorLayout::QdMonitorLayout(const QdTheme &theme)
    : theme_(theme),
      hint_bar_tex_(nullptr)
{
    // W5-PERF-HOTSPOTS #1 (P0-A): zero all per-tile cache slots.
    for (int i = 0; i < QD_MONITOR_MAX_TILES; ++i) {
        tile_title_tex_[i] = nullptr;
        tile_line1_tex_[i] = nullptr;
        tile_line2_tex_[i] = nullptr;
        tile_last_title_[i][0] = '\0';
        tile_last_line1_[i][0] = '\0';
        tile_last_line2_[i][0] = '\0';
    }
    // Open libnx service sessions. Failures are non-fatal; individual stat
    // queries check the init flags and report "N/A" on failure.

    // ── ts (temperature) service ─────────────────────────────────────────
    // FW 14.0.0 removed the legacy temperature wrappers (tsGetTemperature /
    // tsGetTemperatureMilliC); modern firmware exposes only the session API
    // (tsOpenSession + tsSessionGetTemperature).  Strategy:
    //   1. tsInitialize() — opens the "ts" service IPC.
    //   2. If hosversionAtLeast(10,0,0), open two persistent TsSessions
    //      (LocationExternal = SoC, LocationInternal = PCB) and prefer them.
    //   3. RefreshStats falls back to legacy wrappers on older firmware or
    //      whenever a session call returns a non-success rc.
    // We log every rc so the creator can pull the log and decode failures.
    {
        const Result rc_ts = tsInitialize();
        if (R_SUCCEEDED(rc_ts)) {
            ts_inited_ = true;
            if (hosversionAtLeast(10, 0, 0)) {
                const Result rc_soc = tsOpenSession(&ts_sess_soc_, TsDeviceCode_LocationExternal);
                if (R_SUCCEEDED(rc_soc)) {
                    ts_sess_soc_open_ = true;
                } else {
                    UL_LOG_WARN("qdesktop:monitor: tsOpenSession(SoC/External) rc=0x%08X", rc_soc);
                }
                const Result rc_pcb = tsOpenSession(&ts_sess_pcb_, TsDeviceCode_LocationInternal);
                if (R_SUCCEEDED(rc_pcb)) {
                    ts_sess_pcb_open_ = true;
                } else {
                    UL_LOG_WARN("qdesktop:monitor: tsOpenSession(PCB/Internal) rc=0x%08X", rc_pcb);
                }
                // Only prefer the session path if at least one device-code
                // session opened; otherwise stay on legacy wrappers below.
                ts_use_session_ = (ts_sess_soc_open_ || ts_sess_pcb_open_);
            }
        } else {
            UL_LOG_WARN("qdesktop:monitor: tsInitialize rc=0x%08X — temperatures unavailable", rc_ts);
        }
    }

    // W15-A FIX: __appInit() in main.cpp already initialized psm and nifm
    // (System service type) at process boot.  Calling psmInitialize /
    // nifmInitialize here just bumps libnx's refcount — wastes a session
    // slot and, in nifm's case, opens a User-mode session in PARALLEL to
    // the boot-time System-mode session (type mismatch).  Reuse the
    // process-lifetime sessions; mark our flags true so the Refresh paths
    // run.  The matching psmExit/nifmExit in our dtor would have decremented
    // the refcount to zero prematurely — also dropped (psm_inited_/nifm_inited_
    // are now informational only).
    psm_inited_  = true;   // owned by __appInit, not us
    nifm_inited_ = true;   // owned by __appInit, not us

    // Sphaira-absorb sprint 1: CPU/GPU clock service.
    // pcvGetClockRate is only available on [1.0.0-7.0.1]; on 8.0.0+ the service
    // was split and clkrst* must be used.  Try clkrst first (modern path); fall
    // back to legacy pcv if clkrst is not available.
    if (R_SUCCEEDED(clkrstInitialize())) {
        clkrst_inited_ = true;
        // W5-PERF-HOTSPOTS Tier-A (P0-D): hoist ClkrstSession open to ctor so
        // RefreshStats no longer pays 4 IPC round-trips (open+close×2) per cycle.
        if (R_SUCCEEDED(clkrstOpenSession(&clkrst_sess_cpu_, PcvModuleId_CpuBus, 3))) {
            clkrst_sess_cpu_open_ = true;
        } else {
            UL_LOG_WARN("qdesktop:monitor: clkrstOpenSession(CPU) in ctor failed — will retry in RefreshStats");
        }
        if (R_SUCCEEDED(clkrstOpenSession(&clkrst_sess_gpu_, PcvModuleId_GPU, 3))) {
            clkrst_sess_gpu_open_ = true;
        } else {
            UL_LOG_WARN("qdesktop:monitor: clkrstOpenSession(GPU) in ctor failed — will retry in RefreshStats");
        }
    } else if (R_SUCCEEDED(pcvInitialize())) {
        pcv_inited_ = true;
    } else {
        UL_LOG_WARN("qdesktop:monitor: clkrst/pcv init failed — CPU/GPU clocks unavailable");
    }

    // Seed FPS tracking with the current tick so first measurement isn't zero.
    fps_tick_last_   = svcGetSystemTick();
    fps_frame_count_ = 0;

    // Initial stat query so the first frame has data.
    RefreshStats();

    // Build the bottom hint bar once; freed in the destructor.
    // W7: ZR added to toggle verbose perf logging.
    const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
    hint_bar_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("B/+ Close  \xe2\x80\xa2  L/R Resources  \xe2\x80\xa2  Y Ledger  \xe2\x80\xa2  X Verbose"),
        hint_col);
    // W6-LEDGER (Part C): Resources view hint bar (built separately for toggling).
    hint_bar_res_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("B/+ Close  \xe2\x80\xa2  L/R Stats  \xe2\x80\xa2  Y Ledger  \xe2\x80\xa2  X Verbose"),
        hint_col);

    // W5-PERF-HOTSPOTS #1 (P0-A): cache the constant header title once in ctor.
    // OnRender previously RenderText'd this every frame (~60 Hz × ~50 µs = 3 ms/s).
    header_title_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("Monitor  \xe2\x80\x94  System Stats  [B] Back"),
        theme_.accent,
        static_cast<u32>(GetNaturalW() - 16));
    if (header_title_tex_ != nullptr) {
        SDL_QueryTexture(header_title_tex_, nullptr, nullptr, &header_title_w_, &header_title_h_);
    }
}

QdMonitorLayout::~QdMonitorLayout() {
    if (hint_bar_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_tex_);
        hint_bar_tex_ = nullptr;
    }
    if (hint_bar_res_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_res_tex_);
        hint_bar_res_tex_ = nullptr;
    }
    // W5-PERF-HOTSPOTS #1 (P0-A): free cached header + per-tile textures.
    FreeAllTileTextures();
    // W5-PERF-HOTSPOTS Tier-A (P0-D): close hoisted ClkrstSessions before clkrstExit.
    if (clkrst_sess_cpu_open_) { clkrstCloseSession(&clkrst_sess_cpu_); clkrst_sess_cpu_open_ = false; }
    if (clkrst_sess_gpu_open_) { clkrstCloseSession(&clkrst_sess_gpu_); clkrst_sess_gpu_open_ = false; }
    if (clkrst_inited_) { clkrstExit(); clkrst_inited_ = false; }
    if (pcv_inited_)    { pcvExit();    pcv_inited_    = false; }
    // W15-A FIX: psm and nifm are process-lifetime sessions owned by __appInit
    // — Monitor's ctor no longer calls *Initialize for them, so we must NOT
    // call *Exit either (would decrement refcount → invalidate session for
    // every other caller).  nifm_inited_/psm_inited_ are kept as informational
    // flags but they're always true and never trigger Exit here.
    // Close persistent ts sessions before tsExit (libnx requires this order).
    if (ts_sess_soc_open_) { tsSessionClose(&ts_sess_soc_); ts_sess_soc_open_ = false; }
    if (ts_sess_pcb_open_) { tsSessionClose(&ts_sess_pcb_); ts_sess_pcb_open_ = false; }
    if (ts_inited_)     { tsExit();     ts_inited_     = false; }
}

void QdMonitorLayout::FreeAllTileTextures() {
    // W5-PERF-HOTSPOTS #1 (P0-A): release all cached per-tile and header textures.
    if (header_title_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(header_title_tex_);
        header_title_tex_ = nullptr;
        header_title_w_   = 0;
        header_title_h_   = 0;
    }
    for (int i = 0; i < QD_MONITOR_MAX_TILES; ++i) {
        if (tile_title_tex_[i] != nullptr) {
            // W6-LEDGER: untrack before free.
            UL_LEDGER_UNTRACK(tile_title_lh_[i]);
            tile_title_lh_[i] = 0;
            pu::ui::render::DeleteTexture(tile_title_tex_[i]);
            tile_title_tex_[i] = nullptr;
        }
        if (tile_line1_tex_[i] != nullptr) {
            UL_LEDGER_UNTRACK(tile_line1_lh_[i]);
            tile_line1_lh_[i] = 0;
            pu::ui::render::DeleteTexture(tile_line1_tex_[i]);
            tile_line1_tex_[i] = nullptr;
        }
        if (tile_line2_tex_[i] != nullptr) {
            UL_LEDGER_UNTRACK(tile_line2_lh_[i]);
            tile_line2_lh_[i] = 0;
            pu::ui::render::DeleteTexture(tile_line2_tex_[i]);
            tile_line2_tex_[i] = nullptr;
        }
        tile_last_title_[i][0] = '\0';
        tile_last_line1_[i][0] = '\0';
        tile_last_line2_[i][0] = '\0';
    }
    // W6-LEDGER: release Resources view textures.
    FreeResViewTextures();
}

// ── RefreshStats ──────────────────────────────────────────────────────────────

void QdMonitorLayout::RefreshStats() {
    // ── Thermal (SoC = TsLocation_External, PCB = TsLocation_Internal) ──────
    // libnx ts.h labels are counter-intuitive:
    //   TsLocation_Internal = 0 → "TMP451 Internal: PCB"
    //   TsLocation_External = 1 → "TMP451 External: SoC"
    // Earlier code mislabeled the row as "SOC" while querying Internal (PCB).
    //
    // Probe order on each refresh:
    //   1. Session API (preferred on FW 10.0.0+) — returns float °C directly.
    //   2. tsGetTemperatureMilliC — FW 1.0.0-13.2.1, returns s32 millicelsius.
    //   3. tsGetTemperature       — all FW (per header), returns s32 celsius.
    // On every failure we record the last rc into stats_ so the render path
    // can show "N/A (rc=0x%X)" for in-field decode.
    auto probe_temp = [&](TsSession *sess, bool sess_open,
                          TsLocation legacy_loc, float &out_c, bool &out_ok,
                          u32 &out_rc, const char *tag) {
        out_c  = 0.0f;
        out_ok = false;
        out_rc = 0;
        if (!ts_inited_) return;
        // 1. Session API.
        if (ts_use_session_ && sess_open && sess != nullptr) {
            float f = 0.0f;
            const Result rc = tsSessionGetTemperature(sess, &f);
            if (R_SUCCEEDED(rc)) {
                out_c  = f;
                out_ok = true;
                return;
            }
            out_rc = rc;
            UL_LOG_WARN("qdesktop:monitor: tsSessionGetTemperature(%s) rc=0x%08X", tag, rc);
        }
        // 2. Legacy millicelsius wrapper.
        {
            s32 milli = 0;
            const Result rc_mc = tsGetTemperatureMilliC(legacy_loc, &milli);
            if (R_SUCCEEDED(rc_mc)) {
                out_c  = static_cast<float>(milli) / 1000.0f;
                out_ok = true;
                return;
            }
            out_rc = rc_mc;
        }
        // 3. Legacy celsius wrapper.
        {
            s32 deg = 0;
            const Result rc = tsGetTemperature(legacy_loc, &deg);
            if (R_SUCCEEDED(rc)) {
                out_c  = static_cast<float>(deg);
                out_ok = true;
                return;
            }
            out_rc = rc;
            UL_LOG_WARN("qdesktop:monitor: ts %s legacy probes failed (last rc=0x%08X)", tag, rc);
        }
    };
    probe_temp(&ts_sess_soc_, ts_sess_soc_open_, TsLocation_External,
               stats_.soc_celsius, stats_.ts_ok,     stats_.ts_soc_last_rc, "SoC");
    probe_temp(&ts_sess_pcb_, ts_sess_pcb_open_, TsLocation_Internal,
               stats_.pcb_celsius, stats_.ts_pcb_ok, stats_.ts_pcb_last_rc, "PCB");

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
        NifmInternetConnectionType conn_type = NifmInternetConnectionType_WiFi;
        u32 signal_str = 0;
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
    // InfoType_UsedMemorySize (7) and InfoType_TotalMemorySize (6) are process-
    // scoped queries.  They require a valid process handle — CUR_PROCESS_HANDLE
    // (0xFFFF8001) is the pseudo-handle for the calling process.  INVALID_HANDLE
    // caused svcGetInfo to fail (wrong handle class), setting mem_ok=false and
    // displaying "RAM: N/A" on hardware.  Fixed: use CUR_PROCESS_HANDLE.
    u64 mem_used  = 0;
    u64 mem_total = 0;
    const Result mem_rc1 = svcGetInfo(&mem_used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
    const Result mem_rc2 = svcGetInfo(&mem_total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
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

    // ── CPU / GPU clocks (Sphaira-absorb sprint 1) ───────────────────────────
    // pcvGetClockRate is only available on [1.0.0-7.0.1].  On every modern
    // Switch (FW 8.0.0+), the pcv module was split and the per-module clock
    // queries live on clkrst.  Modern path: clkrstOpenSession +
    // clkrstGetClockRate.  Legacy path (pcv) is kept as a fallback for older
    // firmware so unit-test boards still report something.
    stats_.cpu_hz = 0;
    stats_.gpu_hz = 0;
    stats_.pcv_ok = false;
    if (clkrst_inited_) {
        // W5-PERF-HOTSPOTS Tier-A (P0-D): use persistent sessions opened in ctor;
        // no open/close IPC per refresh.  If a session failed to open in ctor,
        // fall through to the pcv legacy path below (any_ok stays false).
        bool any_ok = false;
        if (clkrst_sess_cpu_open_) {
            u32 hz = 0;
            if (R_SUCCEEDED(clkrstGetClockRate(&clkrst_sess_cpu_, &hz))) {
                stats_.cpu_hz = hz;
                any_ok        = true;
            }
        }
        if (clkrst_sess_gpu_open_) {
            u32 hz = 0;
            if (R_SUCCEEDED(clkrstGetClockRate(&clkrst_sess_gpu_, &hz))) {
                stats_.gpu_hz = hz;
                any_ok        = true;
            }
        }
        stats_.pcv_ok = any_ok;
        if (!any_ok && (clkrst_sess_cpu_open_ || clkrst_sess_gpu_open_)) {
            UL_LOG_WARN("qdesktop:monitor: clkrstGetClockRate failed for both CPU and GPU");
        }
    } else if (pcv_inited_) {
        u32 cpu_hz = 0;
        u32 gpu_hz = 0;
        const Result rc_cpu = pcvGetClockRate(PcvModule_CpuBus, &cpu_hz);
        const Result rc_gpu = pcvGetClockRate(PcvModule_GPU,    &gpu_hz);
        if (R_SUCCEEDED(rc_cpu) || R_SUCCEEDED(rc_gpu)) {
            stats_.cpu_hz = R_SUCCEEDED(rc_cpu) ? cpu_hz : 0;
            stats_.gpu_hz = R_SUCCEEDED(rc_gpu) ? gpu_hz : 0;
            stats_.pcv_ok = true;
        } else {
            UL_LOG_WARN("qdesktop:monitor: pcvGetClockRate failed: cpu=0x%08X gpu=0x%08X",
                        rc_cpu, rc_gpu);
        }
    }

    // (External/PCB temperature is sampled by the probe_temp helper above
    // alongside the SoC sensor — see "Thermal" block earlier in RefreshStats.)

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

// W5-PERF-HOTSPOTS #1 (P0-A): per-tile texture caching eliminates 3 alloc+free
// pairs per tile per frame (~1440/s with 8 tiles at 60 Hz).
// Helper: update a cached texture slot when the source string changes.
// kMonitorTexBuf matches QdMonitorLayout::QD_MONITOR_TEX_BUF (private — use
// file-scope constant to avoid access from a non-member function).
static constexpr int kMonitorTexBuf = 64;
static void UpdateCachedTexture(SDL_Texture *&tex, char *last_str, const char *new_str,
                                const std::string &font_path, const pu::ui::Color &color,
                                u32 wrap_width,
                                uint64_t *lh_ptr = nullptr,
                                const char *lh_tag = nullptr)
{
    if (strncmp(last_str, new_str, kMonitorTexBuf - 1) != 0) {
        if (tex != nullptr) {
            // W6-LEDGER: untrack old texture.
            if (lh_ptr && *lh_ptr != 0) {
                UL_LEDGER_UNTRACK(*lh_ptr);
                *lh_ptr = 0;
            }
            pu::ui::render::DeleteTexture(tex);
            tex = nullptr;
        }
        if (new_str != nullptr && new_str[0] != '\0') {
            tex = pu::ui::render::RenderText(font_path, std::string(new_str), color, wrap_width);
            // W6-LEDGER: track new texture.
            if (lh_ptr && tex != nullptr) {
                int tw = 0, th = 0;
                SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                const size_t tex_bytes = (tw > 0 && th > 0)
                    ? static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u : 0u;
                *lh_ptr = UL_LEDGER_TRACK(
                    QdResKind::Texture,
                    lh_tag ? lh_tag : "mon:tile",
                    tex_bytes);
            }
        }
        strncpy(last_str, new_str != nullptr ? new_str : "", kMonitorTexBuf - 1);
        last_str[kMonitorTexBuf - 1] = '\0';
    }
}

void QdMonitorLayout::RenderTile(SDL_Renderer *r, int tile_idx, s32 tx, s32 ty,
                                  const char *title, const char *line1, const char *line2,
                                  pu::ui::Color title_color, bool ok)
{
    // Guard: clamp tile_idx in case caller exceeds QD_MONITOR_MAX_TILES.
    if (tile_idx < 0 || tile_idx >= QD_MONITOR_MAX_TILES) {
        tile_idx = 0;
    }

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

    // Title text — update cache if string changed, then blit.
    {
        char lh_tag[32];
        snprintf(lh_tag, sizeof(lh_tag), "mon:tile_%d_t", tile_idx);
        UpdateCachedTexture(tile_title_tex_[tile_idx], tile_last_title_[tile_idx],
                            title,
                            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                            title_color,
                            static_cast<u32>(QD_MONITOR_TILE_W - 16),
                            &tile_title_lh_[tile_idx], lh_tag);
    }
    if (tile_title_tex_[tile_idx] != nullptr) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tile_title_tex_[tile_idx], nullptr, nullptr, &tw, &th);
        SDL_Rect tdst { tx + 12, ty + 12, tw, th };
        SDL_RenderCopy(r, tile_title_tex_[tile_idx], nullptr, &tdst);
    }

    // Line1 — main value (larger / brighter).
    const pu::ui::Color l1_color = ok ? theme_.text_primary : theme_.text_secondary;
    {
        char lh_tag[32];
        snprintf(lh_tag, sizeof(lh_tag), "mon:tile_%d_1", tile_idx);
        UpdateCachedTexture(tile_line1_tex_[tile_idx], tile_last_line1_[tile_idx],
                            line1,
                            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium),
                            l1_color,
                            static_cast<u32>(QD_MONITOR_TILE_W - 16),
                            &tile_line1_lh_[tile_idx], lh_tag);
    }
    if (tile_line1_tex_[tile_idx] != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(tile_line1_tex_[tile_idx], nullptr, nullptr, &lw, &lh);
        const s32 l1y = ty + QD_MONITOR_TILE_H / 2 - lh - 4;
        SDL_Rect l1dst { tx + 12, l1y, lw, lh };
        SDL_RenderCopy(r, tile_line1_tex_[tile_idx], nullptr, &l1dst);
    }

    // Line2 — secondary value / sub-label (dimmer).
    const char *line2_safe = (line2 != nullptr) ? line2 : "";
    {
        char lh_tag[32];
        snprintf(lh_tag, sizeof(lh_tag), "mon:tile_%d_2", tile_idx);
        UpdateCachedTexture(tile_line2_tex_[tile_idx], tile_last_line2_[tile_idx],
                            line2_safe,
                            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
                            theme_.text_secondary,
                            static_cast<u32>(QD_MONITOR_TILE_W - 16),
                            &tile_line2_lh_[tile_idx], lh_tag);
    }
    if (tile_line2_tex_[tile_idx] != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(tile_line2_tex_[tile_idx], nullptr, nullptr, &lw, &lh);
        const s32 l2y = ty + QD_MONITOR_TILE_H / 2 + 4;
        SDL_Rect l2dst { tx + 12, l2y, lw, lh };
        SDL_RenderCopy(r, tile_line2_tex_[tile_idx], nullptr, &l2dst);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdMonitorLayout::OnRender(pu::ui::render::Renderer::Ref &/*drawer*/,
                                s32 origin_x, s32 origin_y)
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

    // Auto-refresh is driven by on_tick (gated by WindowState::Normal) — see
    // Refresh() in the header and OpenMonitorWindow in qd_DesktopIcons_WmBridge.cpp.
    // Calling RefreshStats() here directly would trigger libnx service calls during
    // MinimizeWindow's snapshot capture (state != Normal), crashing Atmosphère.

    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const s32 ax = origin_x;
    const s32 ay = origin_y;

    // ── 1. Full-screen background ─────────────────────────────────────────────
    // v2.6.0 — theme-aware: was hardcoded near-black.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    {
        const auto &db = ::ul::menu::qdesktop::g_QdTheme.desktop_bg;
        SDL_SetRenderDrawColor(r, db.r, db.g, db.b, 0xF4u);
    }
    SDL_Rect bg { ax, ay, GetNaturalW(), GetNaturalH() };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── 2. Header bar ─────────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g, theme_.topbar_bg.b, 0xF0u);
    SDL_Rect hbar { ax, ay, GetNaturalW(), 48 };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // Header title — W5-PERF-HOTSPOTS #1 (P0-A): blit cached texture built in ctor.
    if (header_title_tex_ != nullptr) {
        SDL_Rect tdst { ax + 8, ay + (48 - header_title_h_) / 2, header_title_w_, header_title_h_ };
        SDL_RenderCopy(r, header_title_tex_, nullptr, &tdst);
    }

    // ── 3. Route to Stats or Resources view ─────────────────────────────────────
    if (view_resources_) {
        RenderResourcesView(r, ax, ay);
        // W10-BUG2: hint bar moved to window chrome (SetHintText in WmBridge).
        // Do NOT render hint_bar_res_tex_ here — the window bottom bar shows it.
        // Dump notification overlay.
        if (dump_notif_frames_ > 0 && dump_notif_tex_ != nullptr) {
            --dump_notif_frames_;
            int nw = 0, nh = 0;
            SDL_QueryTexture(dump_notif_tex_, nullptr, nullptr, &nw, &nh);
            const s32 nx = ax + (GetNaturalW() - nw) / 2;
            const s32 ny = ay + GetNaturalH() / 2 - nh / 2;
            SDL_Rect ndst { nx, ny, nw, nh };
            SDL_RenderCopy(r, dump_notif_tex_, nullptr, &ndst);
        } else if (dump_notif_frames_ == 0 && dump_notif_tex_ != nullptr) {
            pu::ui::render::DeleteTexture(dump_notif_tex_);
            dump_notif_tex_ = nullptr;
        }
        return;  // skip Stats tile section
    }

    // ── 3b. Six stat tiles ────────────────────────────────────────────────────
    // Compute tile grid X at runtime so windowed mode centres correctly.
    const s32 grid_x = (GetNaturalW() - QD_MONITOR_TILE_COLS * QD_MONITOR_TILE_W
                        - (QD_MONITOR_TILE_COLS - 1) * QD_MONITOR_TILE_GAP) / 2;

    // Tile 0: Thermal (SoC = TsLocation_External in libnx).
    // Failure displays "N/A (rc=0x%X)" so the creator can decode in the log.
    {
        char l1[64];
        if (stats_.ts_ok) {
            snprintf(l1, sizeof(l1), "%.1f \xC2\xB0""C", static_cast<double>(stats_.soc_celsius));
        } else if (stats_.ts_soc_last_rc != 0) {
            snprintf(l1, sizeof(l1), "N/A (rc=0x%08X)", stats_.ts_soc_last_rc);
        } else {
            snprintf(l1, sizeof(l1), "N/A");
        }
        const s32 tx = ax + grid_x;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, 0, tx, ty,
                   "Thermal (SoC)", l1,
                   ts_use_session_ ? "tsSessionGetTemperature(SoC)"
                                   : "tsGetTemperature(External=SoC)",
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
        const s32 tx = ax + grid_x + QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, 1, tx, ty, "Power (Battery)", l1, l2,
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
        const s32 tx = ax + grid_x + (QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP) * 2;
        const s32 ty = ay + QD_MONITOR_BODY_TOP;
        RenderTile(r, 2, tx, ty, "Network", stats_.ip_str, l2,
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
        const s32 tx = ax + grid_x;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        RenderTile(r, 3, tx, ty, "System (RAM / FPS)", l1, l2,
                   theme_.button_maximize, stats_.mem_ok);
    }

    // Tile 4: Bluetooth
    {
        const s32 tx = ax + grid_x + QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        const bool bt_connected = (stats_.bt_name[0] != '\0' &&
                                   strcmp(stats_.bt_name, "(none)") != 0);
        RenderTile(r, 4, tx, ty, "Bluetooth (Audio)", stats_.bt_name,
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
        const s32 tx = ax + grid_x + (QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP) * 2;
        const s32 ty = ay + QD_MONITOR_BODY_TOP + QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP;
        RenderTile(r, 5, tx, ty, "Uptime", l1, "svcGetSystemTick / 19.2 MHz",
                   theme_.text_secondary, true);
    }

    // ── Sphaira-absorb sprint 1 — Row 3: CPU/GPU clocks + external temp ──────
    // Tile 6: CPU / GPU clocks
    {
        char l1[64];
        char l2[64];
        if (stats_.pcv_ok) {
            const u32 cpu_mhz = stats_.cpu_hz / 1000000u;
            const u32 gpu_mhz = stats_.gpu_hz / 1000000u;
            snprintf(l1, sizeof(l1), "CPU: %u MHz", cpu_mhz);
            snprintf(l2, sizeof(l2), "GPU: %u MHz", gpu_mhz);
        } else {
            snprintf(l1, sizeof(l1), "CPU: N/A");
            snprintf(l2, sizeof(l2), "GPU: N/A");
        }
        const s32 tx = ax + grid_x;
        const s32 ty = ay + QD_MONITOR_BODY_TOP
                       + (QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP) * 2;
        RenderTile(r, 6, tx, ty, "Clocks (pcvGetClockRate)", l1, l2,
                   theme_.accent, stats_.pcv_ok);
    }

    // Tile 7: Thermal (PCB = TsLocation_Internal in libnx).
    // Was "Temp (Ext) + RAM" combo prior to W3-MONTEMP; split into a dedicated
    // PCB sensor row per creator request ("gpu, cpu, temp, and thermal").
    // RAM-free is now shown only on Tile 3 (System); duplicating it here was
    // wasting the second value-line.  Line 2 explains the API path so a log
    // pull tells us which probe succeeded.
    {
        char l1[64];
        char l2[64];
        if (stats_.ts_pcb_ok) {
            snprintf(l1, sizeof(l1), "%.1f \xC2\xB0""C", (double)stats_.pcb_celsius);
        } else if (stats_.ts_pcb_last_rc != 0) {
            snprintf(l1, sizeof(l1), "N/A (rc=0x%08X)", stats_.ts_pcb_last_rc);
        } else {
            snprintf(l1, sizeof(l1), "N/A");
        }
        snprintf(l2, sizeof(l2), "%s",
                 ts_use_session_ ? "tsSessionGetTemperature(PCB)"
                                 : "tsGetTemperature(Internal=PCB)");
        const s32 tx = ax + grid_x + QD_MONITOR_TILE_W + QD_MONITOR_TILE_GAP;
        const s32 ty = ay + QD_MONITOR_BODY_TOP
                       + (QD_MONITOR_TILE_H + QD_MONITOR_TILE_GAP) * 2;
        RenderTile(r, 7, tx, ty, "Thermal (PCB)", l1, l2,
                   theme_.button_close, stats_.ts_pcb_ok);
    }

    // W10-BUG2: hint bar moved to window chrome (SetHintText in WmBridge).
    // Do NOT render hint_bar_tex_ here — the window bottom bar shows it.
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

    // W6-LEDGER (Part C): L / R to toggle between Stats and Resources view.
    if ((keys_down & HidNpadButton_L) || (keys_down & HidNpadButton_R)) {
        view_resources_ = !view_resources_;
        // Force snapshot refresh on view switch.
        res_snap_tick_ = 0;
    }

    // W6-LEDGER (Part D): Y button — dump live ledger to log and show notification.
    if (keys_down & HidNpadButton_Y) {
        QdResourceLedger::Instance().DumpLive();
        // Build or rebuild the notification texture.
        if (dump_notif_tex_ != nullptr) {
            pu::ui::render::DeleteTexture(dump_notif_tex_);
            dump_notif_tex_ = nullptr;
        }
        const pu::ui::Color notif_col { 0xA0u, 0xFFu, 0xA0u, 0xFFu };
        dump_notif_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string("Ledger dumped to log_uMenu.log"),
            notif_col);
        dump_notif_frames_ = 120;  // ~2 s at 60 fps
    }

    // W7: X button — toggle verbose perf logging and show notification.
    // W10: moved from ZR → X.  ZR is the global cursor-click button at the
    // desktop layer (qd_DesktopIcons.cpp) and is NOT in QdWindow::nav_mask, so
    // a windowed Monitor never saw the ZR keypress — the verbose-toggle never
    // fired and the notification never appeared.  X is in nav_mask and has no
    // other Monitor binding.
    if (keys_down & HidNpadButton_X) {
        auto &perf = QdPerfLogger::Instance();
        perf.SetVerbose(!perf.IsVerbose());
        perf.StampEvent("verbose-toggle");
        if (dump_notif_tex_ != nullptr) {
            pu::ui::render::DeleteTexture(dump_notif_tex_);
            dump_notif_tex_ = nullptr;
        }
        const pu::ui::Color notif_col { 0xFFu, 0xE0u, 0x80u, 0xFFu };
        const std::string msg = perf.IsVerbose()
            ? std::string("Perf verbose ON")
            : std::string("Perf verbose OFF");
        dump_notif_tex_ = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            msg, notif_col);
        dump_notif_frames_ = 120;  // ~2 s at 60 fps
    }
}

// ── FreeResViewTextures ───────────────────────────────────────────────────────

void QdMonitorLayout::FreeResViewTextures() {
    for (int k = 0; k < QD_RES_KIND_COUNT; ++k) {
        if (res_row_tex_[k] != nullptr) {
            UL_LEDGER_UNTRACK(res_row_lh_[k]);
            res_row_lh_[k] = 0;
            pu::ui::render::DeleteTexture(res_row_tex_[k]);
            res_row_tex_[k] = nullptr;
            res_row_last_[k][0] = '\0';
        }
    }
    if (res_total_tex_ != nullptr) {
        UL_LEDGER_UNTRACK(res_total_lh_);
        res_total_lh_ = 0;
        pu::ui::render::DeleteTexture(res_total_tex_);
        res_total_tex_ = nullptr;
        res_total_last_[0] = '\0';
    }
    if (res_caption_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(res_caption_tex_);
        res_caption_tex_ = nullptr;
    }
    if (dump_notif_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(dump_notif_tex_);
        dump_notif_tex_ = nullptr;
        dump_notif_frames_ = 0;
    }
}

// ── RenderResourcesView ───────────────────────────────────────────────────────

// Format bytes as "N KiB" or "N.N MiB"; returns "—" if bytes == 0.
static void FormatBytes(char *buf, size_t buf_len, size_t bytes) {
    if (bytes == 0) {
        snprintf(buf, buf_len, "\xe2\x80\x94");  // U+2014 em-dash
        return;
    }
    if (bytes < 1024 * 1024u) {
        // KiB with one decimal
        const double kib = static_cast<double>(bytes) / 1024.0;
        snprintf(buf, buf_len, "%.1f KiB", kib);
    } else {
        // MiB with one decimal
        const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        snprintf(buf, buf_len, "%.1f MiB", mib);
    }
}

static const char* ResKindLabel(int k) {
    switch (k) {
        case 0: return "Textures";
        case 1: return "Surfaces";
        case 2: return "Services";
        case 3: return "Sessions";
        case 4: return "Threads";
        case 5: return "FileHandles";
        case 6: return "Windows";
        case 7: return "Minimized snaps";
        case 8: return "Icon cache";
        case 9: return "Sfx";
        default: return "?";
    }
}

void QdMonitorLayout::RenderResourcesView(SDL_Renderer *r, s32 ax, s32 ay) {
    // ── Refresh snapshot once per second ──────────────────────────────────────
    static constexpr uint64_t kSnapIntervalTicks = 19200000ULL;  // 1 s
    const uint64_t now_tick = svcGetSystemTick();
    if (res_snap_tick_ == 0 || (now_tick - res_snap_tick_) >= kSnapIntervalTicks) {
        res_snap_     = QdResourceLedger::Instance().GetSnapshot();
        res_snap_tick_ = now_tick;
    }

    // ── Layout constants ───────────────────────────────────────────────────────
    const s32 col_x     = ax + 16;
    const s32 row_h     = 28;
    const s32 body_top  = ay + QD_MONITOR_BODY_TOP;
    const s32 row_w     = GetNaturalW() - 32;
    const pu::ui::Color  col_label  = theme_.text_secondary;
    const pu::ui::Color  col_value  = theme_.text_primary;
    const std::string   &font_small = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

    // ── Draw each kind row ────────────────────────────────────────────────────
    for (int k = 0; k < QD_RES_KIND_COUNT; ++k) {
        const auto &cs = res_snap_.per_kind[k];

        // Format the row string: "Label   live=N  bytes=X   total_ever=N"
        char bytes_buf[24];
        FormatBytes(bytes_buf, sizeof(bytes_buf), cs.bytes_live);

        char row_str[QD_RES_ROW_BUF];
        snprintf(row_str, sizeof(row_str),
                 "%-16s  live=%-5u  bytes=%-12s  total=%u",
                 ResKindLabel(k),
                 (unsigned)cs.count_live,
                 bytes_buf,
                 (unsigned)cs.count_total_ever);

        // Update cached texture using the same UpdateCachedTexture helper.
        {
            char lh_tag[32];
            snprintf(lh_tag, sizeof(lh_tag), "mon:res_row_%d", k);
            UpdateCachedTexture(res_row_tex_[k], res_row_last_[k],
                                row_str, font_small,
                                (cs.count_live > 0) ? col_value : col_label,
                                static_cast<u32>(row_w),
                                &res_row_lh_[k], lh_tag);
        }

        const s32 ry = body_top + k * row_h;
        if (res_row_tex_[k] != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(res_row_tex_[k], nullptr, nullptr, &tw, &th);
            SDL_Rect dst { col_x, ry + (row_h - th) / 2, tw, th };
            SDL_RenderCopy(r, res_row_tex_[k], nullptr, &dst);
        }
    }

    // ── Divider line ──────────────────────────────────────────────────────────
    const s32 div_y = body_top + QD_RES_KIND_COUNT * row_h + 4;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.grid_line.r, theme_.grid_line.g, theme_.grid_line.b, 0x80u);
    SDL_RenderDrawLine(r, ax + 8, div_y, ax + GetNaturalW() - 8, div_y);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Total row ─────────────────────────────────────────────────────────────
    {
        char total_bytes_buf[24];
        FormatBytes(total_bytes_buf, sizeof(total_bytes_buf), res_snap_.total_bytes);

        const uint64_t uptime = res_snap_.uptime_sec;
        const uint64_t secs  = uptime % 60;
        const uint64_t mins  = (uptime / 60) % 60;
        const uint64_t hours = (uptime / 3600) % 24;
        char total_str[QD_RES_ROW_BUF];
        snprintf(total_str, sizeof(total_str),
                 "Live: %u resources, %s tracked.  Uptime: %02llu:%02llu:%02llu",
                 (unsigned)res_snap_.total_live,
                 total_bytes_buf,
                 (unsigned long long)hours,
                 (unsigned long long)mins,
                 (unsigned long long)secs);

        UpdateCachedTexture(res_total_tex_, res_total_last_,
                            total_str, font_small, col_value,
                            static_cast<u32>(row_w),
                            &res_total_lh_, "mon:res_total");

        const s32 ty = div_y + 8;
        if (res_total_tex_ != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(res_total_tex_, nullptr, nullptr, &tw, &th);
            SDL_Rect dst { col_x, ty, tw, th };
            SDL_RenderCopy(r, res_total_tex_, nullptr, &dst);
        }
    }

    // ── Caption ───────────────────────────────────────────────────────────────
    if (res_caption_tex_ == nullptr) {
        const pu::ui::Color cap_col { 0x66u, 0x66u, 0x88u, 0xFFu };
        res_caption_tex_ = pu::ui::render::RenderText(
            font_small,
            std::string("(tracked subset \xe2\x80\x94 see log_uMenu.log \"ledger\" tag for full live dump)"),
            cap_col);
    }
    if (res_caption_tex_ != nullptr) {
        int cw = 0, ch = 0;
        SDL_QueryTexture(res_caption_tex_, nullptr, nullptr, &cw, &ch);
        const s32 cy = div_y + 8 + 30;
        SDL_Rect cdst { col_x, cy, cw, ch };
        SDL_RenderCopy(r, res_caption_tex_, nullptr, &cdst);
    }
}

} // namespace ul::menu::qdesktop
