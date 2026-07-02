// qd_DebugCapsScreen.cpp — caps:sc JPEG screen-capture implementation.
//
// Verified header: /opt/devkitpro/libnx/include/switch/services/capssc.h
//
// Exact API used:
//   Result capsscInitialize(void);
//   void   capsscExit(void);
//   Result capsscCaptureJpegScreenShot(u64* out_jpeg_size,
//                                      void* jpeg_buf,
//                                      size_t jpeg_buf_size,
//                                      ViLayerStack layer_stack,
//                                      s64 timeout);
//
// CAPSSC_JPEG_BUFFER_SIZE (0x80000 = 512 KB) is defined in the header;
// that is the size official software uses and the documented maximum for a
// 1280×720 JPEG.  We heap-allocate it so we don't inflate BSS.
//
// ViLayerStack_Default (0) captures the fully composed screen, which is what
// we want for a desktop screenshot.
//
// Timeout: 100 ms expressed in nanoseconds (100'000'000).

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_DebugCapsScreen.hpp>
#include <ul/ul_Result.hpp>

#ifdef __SWITCH__
extern "C" {
#include <switch.h>
}
#endif

#include <cstdlib>
#include <cstring>

// UL_LOG_INFO / UL_LOG_WARN come from ul_Result.hpp (included above).
// Guard with no-ops in case this TU is ever built without the full ul headers
// (e.g. unit-test stub builds on the host).
#ifndef UL_LOG_INFO
#define UL_LOG_INFO(fmt, ...) do {} while (0)
#endif
#ifndef UL_LOG_WARN
#define UL_LOG_WARN(fmt, ...) do {} while (0)
#endif

namespace ul::menu::qdesktop {

bool CaptureScreenJpeg(std::vector<u8> &out) {
#ifdef __SWITCH__
    // Open the caps:sc service.  capsscInitialize is documented [2.0.0+].
    const Result init_rc = capsscInitialize();
    if (R_FAILED(init_rc)) {
        UL_LOG_WARN("CaptureScreenJpeg: capsscInitialize rc=0x%08X",
                    static_cast<unsigned>(init_rc));
        return false;
    }

    // Allocate the JPEG output buffer.
    // CAPSSC_JPEG_BUFFER_SIZE == 0x80000 (512 KB); this is the size official
    // Switch software passes and the documented maximum for a 1280×720 capture.
    constexpr size_t kBufSize = CAPSSC_JPEG_BUFFER_SIZE; // 0x80000
    void *jpeg_buf = std::malloc(kBufSize);
    if (jpeg_buf == nullptr) {
        UL_LOG_WARN("CaptureScreenJpeg: malloc(%zu) failed", kBufSize);
        capsscExit();
        return false;
    }

    // Capture.
    //   layer_stack = ViLayerStack_Default (0) — the fully composed display.
    //   timeout     = 100 ms in nanoseconds, the standard value used by Nintendo.
    u64 jpeg_size = 0;
    const Result cap_rc = capsscCaptureJpegScreenShot(
        &jpeg_size,
        jpeg_buf,
        kBufSize,
        ViLayerStack_Default,
        100'000'000LL   // 100 ms
    );

    if (R_FAILED(cap_rc)) {
        UL_LOG_WARN("CaptureScreenJpeg: capsscCaptureJpegScreenShot rc=0x%08X",
                    static_cast<unsigned>(cap_rc));
        std::free(jpeg_buf);
        capsscExit();
        return false;
    }

    if (jpeg_size == 0 || jpeg_size > kBufSize) {
        UL_LOG_WARN("CaptureScreenJpeg: bad out_jpeg_size=%llu (buf=%zu)",
                    static_cast<unsigned long long>(jpeg_size), kBufSize);
        std::free(jpeg_buf);
        capsscExit();
        return false;
    }

    // Copy the JPEG bytes into the caller's vector and release the heap buffer.
    out.assign(static_cast<const u8 *>(jpeg_buf),
               static_cast<const u8 *>(jpeg_buf) + jpeg_size);

    std::free(jpeg_buf);
    capsscExit();

    UL_LOG_INFO("CaptureScreenJpeg: ok, %llu bytes",
                static_cast<unsigned long long>(jpeg_size));
    return true;

#else
    // Host / non-Switch build: not supported.
    (void)out;
    return false;
#endif // __SWITCH__
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
