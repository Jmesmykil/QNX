// qd_DebugObserve.cpp — Read-only system-observation snapshot for the debug HTTP route.
//
// Every service block is isolated: init → read → exit.  A failed init emits a
// degraded value (null / 0) for that block and continues; nothing aborts the
// whole snapshot.  No side-effects; all calls are read-only.
//
// JSON is assembled with snprintf into a fixed local buffer, then returned as
// std::string.  Values are numbers or short ASCII; no JSON-escape hazards.
//
// Verified symbol inventory (against /opt/devkitpro/libnx/include/switch/services/):
//
//   ts.h     : tsInitialize / tsExit / tsGetTemperature(TsLocation)
//              NOTE: TsLocation_Internal = PCB, TsLocation_External = SoC
//              (comment in header says "External = TMP451 External: SoC").
//
//   psm.h    : psmInitialize / psmExit / psmGetBatteryChargePercentage /
//              psmGetChargerType / psmIsEnoughPowerSupplied
//
//   wlaninf.h: wlaninfInitialize / wlaninfExit / wlaninfGetRSSI / wlaninfGetState
//              NOTE: wlaninfInitialize documented as [1.0.0-14.1.2]; safe on Erista.
//
//   set.h    : setsysInitialize / setsysExit / setsysGetFirmwareVersion /
//              setInitialize / setExit / setGetRegionCode / setGetSystemLanguage
//
//   set.h    : setcalInitialize / setcalExit /
//              setcalGetSerialNumber(SetCalSerialNumber*)  — SetCalSerialNumber = SetSysSerialNumber, char[0x18]
//              setcalGetWirelessLanMacAddress(SetCalMacAddress*)  — SetCalMacAddress: u8 addr[6]
//              setcalGetBdAddress(SetCalBdAddress*)              — SetCalBdAddress:   u8 bd_addr[6]
//
//   ns.h     : nsInitialize / nsExit / nsListApplicationRecord
//
//   pm.h     : pmdmntInitialize / pmdmntExit / pmdmntGetApplicationProcessId
//              pminfoInitialize / pminfoExit / pminfoGetProgramId
//
//   acc.h    : accountInitialize(AccountServiceType_Application) / accountExit /
//              accountGetUserCount(s32*)
//
// SKIPPED fields / reasons:
//   - tsSessionGetTemperature / tsOpenSession: [10.0.0+] / [8.0.0+] — not needed when
//     tsGetTemperature works fine for our FW range.
//   - psmIsEnoughPowerSupplied: verified in header; wired below.
//   - BT MAC via setcalGetBdAddress: verified; wired below as "mac_bt".
//   - wlaninfInitialize deprecation [1.0.0-14.1.2]: Erista ships <= 18.x, still works.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_DebugObserve.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <cstdio>
#include <cstring>
#include <string>

// UL_LOG no-op fallback (mirrors qd_DebugServer.cpp pattern).
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

#ifdef __SWITCH__

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Format a 6-byte MAC/BD-address as "XX:XX:XX:XX:XX:XX" into buf[18].
static void FormatMac(const u8 addr[6], char buf[18]) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

// ---------------------------------------------------------------------------
// Per-service read functions — each opens, reads, closes.
// Returns true on success; on failure leaves outputs at their zero-init values.
// ---------------------------------------------------------------------------

struct ThermalData { s32 pcb = 0; s32 soc = 0; bool ok = false; };
static ThermalData ReadThermal() {
    ThermalData d;
    if (R_FAILED(tsInitialize())) {
        UL_LOG_WARN("observe: tsInitialize failed");
        return d;
    }
    // TsLocation_Internal = PCB, TsLocation_External = SoC (see ts.h comment).
    const bool pcb_ok = R_SUCCEEDED(tsGetTemperature(TsLocation_Internal,  &d.pcb));
    const bool soc_ok = R_SUCCEEDED(tsGetTemperature(TsLocation_External,  &d.soc));
    tsExit();
    d.ok = pcb_ok || soc_ok;
    return d;
}

struct BatteryData { u32 pct = 0; s32 charger = 0; bool enough = false; bool ok = false; };
static BatteryData ReadBattery() {
    BatteryData d;
    if (R_FAILED(psmInitialize())) {
        UL_LOG_WARN("observe: psmInitialize failed");
        return d;
    }
    PsmChargerType ct = PsmChargerType_Unconnected;
    bool enough = false;
    const bool pct_ok     = R_SUCCEEDED(psmGetBatteryChargePercentage(&d.pct));
    const bool ct_ok      = R_SUCCEEDED(psmGetChargerType(&ct));
    const bool enough_ok  = R_SUCCEEDED(psmIsEnoughPowerSupplied(&enough));
    psmExit();
    d.charger = static_cast<s32>(ct);
    d.enough  = enough;
    d.ok      = pct_ok || ct_ok || enough_ok;
    return d;
}

struct WifiData { s32 rssi = 0; s32 state = 0; bool ok = false; };
static WifiData ReadWifi() {
    WifiData d;
    if (R_FAILED(wlaninfInitialize())) {
        UL_LOG_WARN("observe: wlaninfInitialize failed");
        return d;
    }
    WlanInfState ws = WlanInfState_NotConnected;
    const bool rssi_ok  = R_SUCCEEDED(wlaninfGetRSSI(&d.rssi));
    const bool state_ok = R_SUCCEEDED(wlaninfGetState(&ws));
    wlaninfExit();
    d.state = static_cast<s32>(ws);
    d.ok    = rssi_ok || state_ok;
    return d;
}

struct FwData { char display[0x18]; bool ok = false;
    FwData() { display[0] = '\0'; }
};
static FwData ReadFirmware() {
    FwData d;
    if (R_FAILED(setsysInitialize())) {
        UL_LOG_WARN("observe: setsysInitialize failed");
        return d;
    }
    SetSysFirmwareVersion fv;
    __builtin_memset(&fv, 0, sizeof(fv));
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fv))) {
        // display_version is char[0x18], null-terminated by the system.
        __builtin_strncpy(d.display, fv.display_version, sizeof(d.display) - 1);
        d.display[sizeof(d.display) - 1] = '\0';
        d.ok = true;
    }
    setsysExit();
    return d;
}

struct LangRegionData { u64 lang_code = 0; s32 region = 0; bool ok = false; };
static LangRegionData ReadLangRegion() {
    LangRegionData d;
    if (R_FAILED(setInitialize())) {
        UL_LOG_WARN("observe: setInitialize failed");
        return d;
    }
    SetRegion region = SetRegion_JPN;
    const bool lang_ok   = R_SUCCEEDED(setGetSystemLanguage(&d.lang_code));
    const bool region_ok = R_SUCCEEDED(setGetRegionCode(&region));
    setExit();
    d.region = static_cast<s32>(region);
    d.ok     = lang_ok || region_ok;
    return d;
}

struct CalData {
    char serial[0x18];
    char mac_wifi[18];   // "XX:XX:XX:XX:XX:XX" + NUL
    char mac_bt[18];
    bool ok = false;
    CalData() { serial[0] = '\0'; mac_wifi[0] = '\0'; mac_bt[0] = '\0'; }
};
static CalData ReadCal() {
    CalData d;
    if (R_FAILED(setcalInitialize())) {
        UL_LOG_WARN("observe: setcalInitialize failed");
        return d;
    }
    bool any = false;

    SetCalSerialNumber sn;
    __builtin_memset(&sn, 0, sizeof(sn));
    if (R_SUCCEEDED(setcalGetSerialNumber(&sn))) {
        __builtin_strncpy(d.serial, sn.number, sizeof(d.serial) - 1);
        d.serial[sizeof(d.serial) - 1] = '\0';
        any = true;
    }

    SetCalMacAddress mac_wl;
    __builtin_memset(&mac_wl, 0, sizeof(mac_wl));
    if (R_SUCCEEDED(setcalGetWirelessLanMacAddress(&mac_wl))) {
        FormatMac(mac_wl.addr, d.mac_wifi);
        any = true;
    }

    SetCalBdAddress mac_bt;
    __builtin_memset(&mac_bt, 0, sizeof(mac_bt));
    if (R_SUCCEEDED(setcalGetBdAddress(&mac_bt))) {
        FormatMac(mac_bt.bd_addr, d.mac_bt);
        any = true;
    }

    setcalExit();
    d.ok = any;
    return d;
}

struct TitleData { s32 count = 0; bool ok = false; };
static TitleData ReadTitles() {
    TitleData d;
    if (R_FAILED(nsInitialize())) {
        UL_LOG_WARN("observe: nsInitialize failed");
        return d;
    }
    // Pull up to 32 records just to count; we never store or expose IDs.
    static constexpr s32 kBatch = 32;
    NsApplicationRecord buf[kBatch];
    s32 total = 0;
    s32 offset = 0;
    s32 got = 0;
    do {
        got = 0;
        if (R_FAILED(nsListApplicationRecord(buf, kBatch, offset, &got))) break;
        total += got;
        offset += got;
    } while (got == kBatch);
    nsExit();
    d.count = total;
    d.ok    = true;
    return d;
}

struct FgProcData { u64 pid = 0; u64 program_id = 0; bool ok = false; };
static FgProcData ReadFgProc() {
    FgProcData d;
    // pmdmnt gives us the foreground application PID.
    if (R_FAILED(pmdmntInitialize())) {
        UL_LOG_WARN("observe: pmdmntInitialize failed");
        return d;
    }
    u64 pid = 0;
    const bool pid_ok = R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid));
    pmdmntExit();
    if (!pid_ok || pid == 0) return d;
    d.pid = pid;

    // Then resolve PID -> program_id via pm:info.
    if (R_FAILED(pminfoInitialize())) {
        // Have the PID at least; report without program_id.
        d.ok = true;
        return d;
    }
    u64 prog_id = 0;
    if (R_SUCCEEDED(pminfoGetProgramId(&prog_id, pid))) {
        d.program_id = prog_id;
    }
    pminfoExit();
    d.ok = true;
    return d;
}

struct AccountData { s32 count = 0; bool ok = false; };
static AccountData ReadAccounts() {
    AccountData d;
    // Use Application service type (acc:u0) — least privilege.
    if (R_FAILED(accountInitialize(AccountServiceType_Application))) {
        UL_LOG_WARN("observe: accountInitialize failed");
        return d;
    }
    s32 cnt = 0;
    if (R_SUCCEEDED(accountGetUserCount(&cnt))) {
        d.count = cnt;
        d.ok    = true;
    }
    accountExit();
    return d;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
std::string BuildObserveJson() {
    // Collect all blocks — each is independent.
    const auto th  = ReadThermal();
    const auto bat = ReadBattery();
    const auto wf  = ReadWifi();
    const auto fw  = ReadFirmware();
    const auto lr  = ReadLangRegion();
    const auto cal = ReadCal();
    const auto ns  = ReadTitles();
    const auto fg  = ReadFgProc();
    const auto acc = ReadAccounts();

    // Build JSON by hand — values are plain numbers or short ASCII serials/MACs.
    // Buffer: ~400 bytes is ample; 1024 is a comfortable ceiling.
    char buf[1024];
    int pos = 0;

    // temp_c
    if (th.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "{\"temp_c\":{\"pcb\":%d,\"soc\":%d}",
                        static_cast<int>(th.pcb), static_cast<int>(th.soc));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "{\"temp_c\":null");
    }

    // battery
    if (bat.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"battery\":{\"pct\":%u,\"charger\":%d,\"enough\":%s}",
                        static_cast<unsigned>(bat.pct),
                        static_cast<int>(bat.charger),
                        bat.enough ? "true" : "false");
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"battery\":null");
    }

    // wifi
    if (wf.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"wifi\":{\"rssi\":%d,\"state\":%d}",
                        static_cast<int>(wf.rssi), static_cast<int>(wf.state));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"wifi\":null");
    }

    // fw — display_version is short ASCII ("18.1.0" etc.), no escape needed.
    if (fw.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"fw\":\"%s\"", fw.display);
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"fw\":null");
    }

    // lang / region (lang_code is a u64 bitmask; emit raw so the host can decode)
    if (lr.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"lang\":%llu,\"region\":%d",
                        static_cast<unsigned long long>(lr.lang_code),
                        static_cast<int>(lr.region));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"lang\":null,\"region\":null");
    }

    // cal — serial and MAC addresses are PII; stripped from the unauthenticated
    // /observe response (4.1 info-leak fix).  cal.ok still controls the "cal"
    // field so callers can detect whether the cal service was reachable.
    pos += snprintf(buf + pos, sizeof(buf) - pos,
                    ",\"cal\":%s", cal.ok ? "true" : "null");

    // titles
    if (ns.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"titles\":%d", static_cast<int>(ns.count));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"titles\":null");
    }

    // users
    if (acc.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"users\":%d", static_cast<int>(acc.count));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",\"users\":null");
    }

    // fg_pid / fg_prog_id
    if (fg.ok) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"fg_pid\":%llu,\"fg_prog_id\":\"%016llX\"",
                        static_cast<unsigned long long>(fg.pid),
                        static_cast<unsigned long long>(fg.program_id));
    } else {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        ",\"fg_pid\":null,\"fg_prog_id\":null");
    }

    // close
    if (pos < static_cast<int>(sizeof(buf)) - 1) {
        buf[pos++] = '}';
        buf[pos]   = '\0';
    } else {
        // Overflow guard (should not happen with the current field set).
        buf[sizeof(buf) - 2] = '}';
        buf[sizeof(buf) - 1] = '\0';
    }

    return std::string(buf);
}

#else // !__SWITCH__

// Host-side stub so translation units that include the header still link.
std::string BuildObserveJson() {
    return "{\"error\":\"not_switch\"}";
}

#endif // __SWITCH__

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
