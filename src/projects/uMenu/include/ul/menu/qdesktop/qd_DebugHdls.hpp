// qd_DebugHdls.hpp — System-wide synthetic input via hid:dbg HDLS (virtual Pro Controller).
//
// Exposes a virtual Pro Controller that both uMenu and running games see.
// Also exposes touch-screen autopilot (hiddbgSetTouchScreenAutoPilotState is present in
// this libnx's hiddbg.h).
//
// Runtime requirement: "hid:dbg" must be listed in the NPDM service access — handled
// externally.  This module only calls the API; it will log + return false on failure
// rather than crash if the permission is missing.
//
// Usage (per-frame, UI thread):
//   g_DebugHdls.Ensure();                             // once, or call every frame (idempotent)
//   g_DebugHdls.SetState(HidNpadButton_A, 0, 0, 0, 0); // press A this frame
//   g_DebugHdls.SetTouch(640, 360);                   // optional: inject a touch point
//   g_DebugHdls.ClearTouch();                         // release touch
//   // … at teardown:
//   g_DebugHdls.Teardown();

#ifdef QDESKTOP_MODE
#pragma once

#include <switch.h>

namespace ul::menu::qdesktop {

class QdDebugHdls {
public:
    // Lazy init: hiddbgInitialize + attach work buffer + attach 1 virtual Pro Controller.
    // Idempotent — safe to call every frame.  Returns false on any failure.
    bool Ensure();

    // Detach virtual device + release work buffer + hiddbgExit.
    // Safe to call even if Ensure() was never called or previously failed.
    void Teardown();

    // Set the virtual controller state for THIS frame.
    // Call once per frame from the UI thread.
    //   buttons — HidNpadButton bitmask of held buttons this frame (u64; also accepts
    //             HiddbgNpadButton bits for Home/Capture).
    //   lx, ly  — Left stick axes, -0x8000 .. 0x7FFF (0 = center).
    //   rx, ry  — Right stick axes, -0x8000 .. 0x7FFF (0 = center).
    // Returns false if !IsReady().
    bool SetState(u64 buttons, s32 lx, s32 ly, s32 rx, s32 ry);

    // Inject a single-finger touch at (x, y) in screen coordinates.
    // Uses hiddbgSetTouchScreenAutoPilotState (present in this libnx's hiddbg.h).
    // Returns false if !IsReady().
    bool SetTouch(s32 x, s32 y);

    // Release the injected touch.
    void ClearTouch();

    bool IsReady() const;

private:
    // 0x1000-aligned transfer-memory work buffer required by hiddbgAttachHdlsWorkBuffer.
    // The IPC command internally wraps this in a TransferMemory object; the buffer must
    // remain valid and page-aligned for the lifetime of the session.
    static constexpr size_t kWorkBufSize = 0x1000;

    alignas(0x1000) u8      m_work_buf[kWorkBufSize] = {};

    HiddbgHdlsSessionId     m_session_id    = {};
    HiddbgHdlsHandle        m_handle        = {};

    bool m_initialized  = false;  // hiddbgInitialize succeeded
    bool m_buf_attached = false;  // hiddbgAttachHdlsWorkBuffer succeeded
    bool m_dev_attached = false;  // hiddbgAttachHdlsVirtualDevice succeeded
};

extern QdDebugHdls g_DebugHdls;

}  // namespace ul::menu::qdesktop

#endif  // QDESKTOP_MODE
