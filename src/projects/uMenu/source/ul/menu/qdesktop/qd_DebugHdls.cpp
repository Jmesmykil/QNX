// qd_DebugHdls.cpp — HDLS virtual Pro Controller + touch autopilot implementation.
//
// Verified against:
//   /opt/devkitpro/libnx/include/switch/services/hiddbg.h  (yellows8, bundled libnx)
//   /opt/devkitpro/libnx/include/switch/services/hid.h
//
// Struct field names and function signatures used are taken verbatim from those headers.
// Nothing here is guessed.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_DebugHdls.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <cstring>

// Logging no-ops if the engine hasn't defined these (mirrors qd_DebugServer.cpp convention).
#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

QdDebugHdls g_DebugHdls;

// ---------------------------------------------------------------------------
// IsReady
// ---------------------------------------------------------------------------

bool QdDebugHdls::IsReady() const {
    return m_initialized && m_buf_attached && m_dev_attached;
}

// ---------------------------------------------------------------------------
// Ensure  — idempotent lazy init
// ---------------------------------------------------------------------------

bool QdDebugHdls::Ensure() {
#ifndef __SWITCH__
    return false;
#else
    // Step 1: hiddbgInitialize — idempotent guard.
    if (!m_initialized) {
        const Result rc = hiddbgInitialize();
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qd_DebugHdls: hiddbgInitialize failed rc=0x%08X", rc);
            return false;
        }
        m_initialized = true;
        UL_LOG_INFO("qd_DebugHdls: hiddbgInitialize OK");
    }

    // Step 2: hiddbgAttachHdlsWorkBuffer.
    // Signature (hiddbg.h line 408):
    //   Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId *session_id,
    //                                     void *buffer, size_t size);
    // The buffer must be page-aligned (0x1000); m_work_buf is alignas(0x1000).
    // [13.0.0+] the session_id is populated and required for all subsequent calls.
    if (!m_buf_attached) {
        const Result rc = hiddbgAttachHdlsWorkBuffer(&m_session_id,
                                                      m_work_buf,
                                                      sizeof(m_work_buf));
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qd_DebugHdls: hiddbgAttachHdlsWorkBuffer failed rc=0x%08X", rc);
            return false;
        }
        m_buf_attached = true;
        UL_LOG_INFO("qd_DebugHdls: work buffer attached (session_id=0x%llX)",
                    (unsigned long long)m_session_id.id);
    }

    // Step 3: hiddbgAttachHdlsVirtualDevice — register 1 virtual Pro Controller.
    // Signature (hiddbg.h line 466):
    //   Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle *handle,
    //                                        const HiddbgHdlsDeviceInfo *info);
    //
    // HiddbgHdlsDeviceInfo (9.0.0+, hiddbg.h lines 74-82):
    //   u8  deviceType;         // HidDeviceType — use HidDeviceType_FullKey3 (Pro Controller, value=3)
    //   u8  npadInterfaceType;  // HidNpadInterfaceType — use HidNpadInterfaceType_USB (3) for wired Pro
    //   u8  pad[2];
    //   u32 singleColorBody;    // RGBA body colour
    //   u32 singleColorButtons; // RGBA button colour
    //   u32 colorLeftGrip;      // RGBA left grip (9.0.0+)
    //   u32 colorRightGrip;     // RGBA right grip (9.0.0+)
    if (!m_dev_attached) {
        HiddbgHdlsDeviceInfo info = {};
        // HidDeviceType_FullKey3 = 3  (Pro Controller / FullKey, hid.h line 430)
        info.deviceType        = static_cast<u8>(HidDeviceType_FullKey3);
        // HidNpadInterfaceType_USB = 3  (hid.h line 480) — connected via USB, dedicated controller
        info.npadInterfaceType = static_cast<u8>(HidNpadInterfaceType_USB);
        // RGBA colours: standard Switch Pro Controller grey + black buttons
        info.singleColorBody    = 0x323232FF;
        info.singleColorButtons = 0x1A1A1AFF;
        info.colorLeftGrip      = 0x323232FF;
        info.colorRightGrip     = 0x323232FF;

        const Result rc = hiddbgAttachHdlsVirtualDevice(&m_handle, &info);
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qd_DebugHdls: hiddbgAttachHdlsVirtualDevice failed rc=0x%08X", rc);
            return false;
        }
        m_dev_attached = true;
        UL_LOG_INFO("qd_DebugHdls: virtual Pro Controller attached (handle=0x%llX)",
                    (unsigned long long)m_handle.handle);
    }

    return true;
#endif  // __SWITCH__
}

// ---------------------------------------------------------------------------
// SetState  — push button/stick state for the current frame
// ---------------------------------------------------------------------------

bool QdDebugHdls::SetState(u64 buttons, s32 lx, s32 ly, s32 rx, s32 ry) {
#ifndef __SWITCH__
    (void)buttons; (void)lx; (void)ly; (void)rx; (void)ry;
    return false;
#else
    if (!IsReady()) {
        return false;
    }

    // HiddbgHdlsState (12.0.0+, hiddbg.h lines 109-120):
    //   u32 battery_level        — BatteryLevel for main PowerInfo
    //   u32 flags                — BIT(0)=IsPowered, BIT(1)=IsCharging
    //   u64 buttons              — HiddbgNpadButton bitmask; masked 0xfffffffff00fffff by HW
    //   HidAnalogStickState analog_stick_l  — {s32 x, s32 y}
    //   HidAnalogStickState analog_stick_r  — {s32 x, s32 y}
    //   HidVector six_axis_sensor_acceleration
    //   HidVector six_axis_sensor_angle
    //   u32 attribute            — HiddbgHdlsAttribute bitmask
    //   u8  indicator
    //   u8  padding[3]
    HiddbgHdlsState state = {};
    state.battery_level          = 4;          // full battery (range 0-4)
    state.flags                  = BIT(0);     // IsPowered = true
    state.buttons                = buttons;
    state.analog_stick_l.x       = lx;
    state.analog_stick_l.y       = ly;
    state.analog_stick_r.x       = rx;
    state.analog_stick_r.y       = ry;
    // six_axis fields and attribute left zeroed — no virtual gyro/accel needed.

    // Signature (hiddbg.h line 481):
    //   Result hiddbgSetHdlsState(HiddbgHdlsHandle handle, const HiddbgHdlsState *state);
    const Result rc = hiddbgSetHdlsState(m_handle, &state);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_DebugHdls: hiddbgSetHdlsState failed rc=0x%08X", rc);
        return false;
    }
    return true;
#endif  // __SWITCH__
}

// ---------------------------------------------------------------------------
// SetTouch / ClearTouch  — touch-screen autopilot
// ---------------------------------------------------------------------------
// hiddbgSetTouchScreenAutoPilotState IS present in this libnx's hiddbg.h (line 228):
//   Result hiddbgSetTouchScreenAutoPilotState(const HidTouchState *states, s32 count);
// hiddbgUnsetTouchScreenAutoPilotState (line 233):
//   Result hiddbgUnsetTouchScreenAutoPilotState(void);
//
// HidTouchState (hid.h lines 648-658):
//   u64 delta_time, u32 attributes, u32 finger_id,
//   u32 x, u32 y, u32 diameter_x, u32 diameter_y, u32 rotation_angle, u32 reserved

bool QdDebugHdls::SetTouch(s32 x, s32 y) {
#ifndef __SWITCH__
    (void)x; (void)y;
    return false;
#else
    if (!IsReady()) {
        return false;
    }

    HidTouchState touch = {};
    touch.finger_id    = 0;
    touch.x            = static_cast<u32>(x);
    touch.y            = static_cast<u32>(y);
    touch.diameter_x   = 10;   // reasonable finger contact diameter in pixels
    touch.diameter_y   = 10;
    touch.rotation_angle = 0;
    // delta_time = 0: let the kernel fill in the sampling interval.
    // attributes = 0: no special touch attributes.

    const Result rc = hiddbgSetTouchScreenAutoPilotState(&touch, 1);
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_DebugHdls: hiddbgSetTouchScreenAutoPilotState failed rc=0x%08X", rc);
        return false;
    }
    return true;
#endif  // __SWITCH__
}

void QdDebugHdls::ClearTouch() {
#ifdef __SWITCH__
    const Result rc = hiddbgUnsetTouchScreenAutoPilotState();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_DebugHdls: hiddbgUnsetTouchScreenAutoPilotState failed rc=0x%08X", rc);
    }
#endif
}

// ---------------------------------------------------------------------------
// Teardown  — detach device, release buffer, exit; safe if never Ensure()d
// ---------------------------------------------------------------------------

void QdDebugHdls::Teardown() {
#ifdef __SWITCH__
    if (m_dev_attached) {
        // Signature (hiddbg.h line 473):
        //   Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle handle);
        const Result rc = hiddbgDetachHdlsVirtualDevice(m_handle);
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qd_DebugHdls: hiddbgDetachHdlsVirtualDevice failed rc=0x%08X", rc);
        }
        m_dev_attached = false;
        m_handle       = {};
        UL_LOG_INFO("qd_DebugHdls: virtual device detached");
    }

    if (m_buf_attached) {
        // Signature (hiddbg.h line 415):
        //   Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId session_id);
        // Note: takes session_id BY VALUE (not pointer).
        const Result rc = hiddbgReleaseHdlsWorkBuffer(m_session_id);
        if (R_FAILED(rc)) {
            UL_LOG_WARN("qd_DebugHdls: hiddbgReleaseHdlsWorkBuffer failed rc=0x%08X", rc);
        }
        m_buf_attached = false;
        m_session_id   = {};
        UL_LOG_INFO("qd_DebugHdls: work buffer released");
    }

    if (m_initialized) {
        hiddbgExit();
        m_initialized = false;
        UL_LOG_INFO("qd_DebugHdls: hiddbgExit");
    }
#endif  // __SWITCH__
}

}  // namespace ul::menu::qdesktop

#endif  // QDESKTOP_MODE
