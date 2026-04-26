#include "qos_smi_client.hpp"

// uCommon SMI infrastructure (IPC layer — no SDL2 / Plutonium dependency)
#include <ul/menu/smi/sf/sf_PrivateService.hpp>   // InitializePrivateService / FinalizePrivateService / RegisterOnMessageDetect
#include <ul/menu/smi/smi_Commands.hpp>             // LaunchApplication, LaunchHomebrewApplication, UpdateMenuPaths
#include <ul/menu/am/am_LibraryAppletUtils.hpp>     // ReadStartMode

// libnx
#include <switch.h>

// C standard I/O for diagnostic logging to SD card
#include <cstdio>
#include <atomic>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Log a one-line message to sdmc:/switch/qos-menu-init.log.
// Using a fresh fopen/fclose per call so that partial writes never corrupt
// earlier entries.  Acceptable overhead — this is init/error path only.
void LogSd(const char *msg) {
    FILE *f = fopen("sdmc:/switch/qos-menu-init.log", "a");
    if(f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

// Pending-message pump state: we use a lightweight flag to avoid calling
// TryPopMessageContext from the main thread while the receiver thread is
// also running.  In v0.7 the receiver thread IS the message pump; the
// public ProcessPendingMessages() is a no-op poll that returns whether the
// receiver thread is still running and healthy.
bool g_InitDone = false;

// Track how many messages have been dispatched since Init() for the
// ProcessPendingMessages() return value.  The receiver thread increments
// this; ProcessPendingMessages() snapshots and resets it.
std::atomic<u32> g_MessageCount{0};

// Callback registered with RegisterOnMessageDetect(ANY) so we can count
// dispatched messages for the ProcessPendingMessages() return value.
void CountingCallback(const ul::smi::MenuMessageContext &) {
    ++g_MessageCount;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace qos::smi {

bool Init() {
    if(g_InitDone) {
        return true;
    }

    const Result rc = ul::menu::smi::sf::InitializePrivateService();
    if(R_FAILED(rc)) {
        LogSd("[SMI] InitializePrivateService failed");
        return false;
    }

    // Register a catch-all callback that increments g_MessageCount so that
    // ProcessPendingMessages() can report whether anything was dispatched.
    ul::menu::smi::sf::RegisterOnMessageDetect(
        &CountingCallback,
        ul::smi::MenuMessage::Invalid  // Invalid = any message
    );

    g_InitDone = true;
    LogSd("[SMI] Init OK");
    return true;
}

void Shutdown() {
    if(!g_InitDone) {
        return;
    }
    ul::menu::smi::sf::FinalizePrivateService();
    g_InitDone = false;
    LogSd("[SMI] Shutdown OK");
}

bool LaunchApplication(uint64_t application_id) {
    if(!g_InitDone) {
        LogSd("[SMI] LaunchApplication called before Init");
        return false;
    }

    const Result rc = ul::menu::smi::LaunchApplication(static_cast<u64>(application_id));
    if(R_FAILED(rc)) {
        LogSd("[SMI] LaunchApplication failed");
        return false;
    }
    LogSd("[SMI] LaunchApplication accepted");
    return true;
}

bool LaunchHomebrewByPath(const char *nro_path) {
    if(!g_InitDone || !nro_path) {
        LogSd("[SMI] LaunchHomebrewByPath invalid args");
        return false;
    }

    // ul::menu::smi::LaunchHomebrewApplication takes std::string; convert here
    // so the public wrapper stays plain-C.  The empty argv string is correct —
    // uSystem populates argv from the NRO header if none is provided.
    const Result rc = ul::menu::smi::LaunchHomebrewApplication(
        std::string(nro_path),
        std::string("")
    );
    if(R_FAILED(rc)) {
        LogSd("[SMI] LaunchHomebrewByPath failed");
        return false;
    }
    LogSd("[SMI] LaunchHomebrewByPath accepted");
    return true;
}

bool ProcessPendingMessages() {
    if(!g_InitDone) {
        return false;
    }
    // The private-service receiver thread pumps messages on its own 10 ms
    // cadence.  This function's contract is non-blocking: snapshot the counter
    // and report whether anything was dispatched since the last call.
    const u32 count = g_MessageCount;
    g_MessageCount = 0;
    return count > 0;
}

bool NotifyLayoutReady() {
    if(!g_InitDone) {
        LogSd("[SMI] NotifyLayoutReady called before Init");
        return false;
    }

    // v0.6.x convention: call UpdateMenuPaths with empty paths to signal that
    // the menu layout is alive.  uSystem uses the receipt of this command to
    // confirm the applet is responsive and ready for input routing.
    char empty[FS_MAX_PATH] = {};
    const Result rc = ul::menu::smi::UpdateMenuPaths(empty, empty);
    if(R_FAILED(rc)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[SMI] NotifyLayoutReady (UpdateMenuPaths) failed rc=0x%X", rc);
        LogSd(buf);
        return false;
    }
    LogSd("[SMI] NotifyLayoutReady OK");
    return true;
}

int QueryStartMode() {
    ul::smi::MenuStartMode mode = ul::smi::MenuStartMode::Invalid;
    const Result rc = ul::menu::am::ReadStartMode(mode);
    if(R_FAILED(rc)) {
        LogSd("[SMI] QueryStartMode failed");
        return -1;
    }
    return static_cast<int>(mode);
}

} // namespace qos::smi
