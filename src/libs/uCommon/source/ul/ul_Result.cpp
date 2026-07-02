#include <ul/ul_Result.hpp>
#include <ul/util/util_Telemetry.hpp>
#include <ul/fs/fs_Stdio.hpp>
#include <ul/os/os_System.hpp>
#include <cstdarg>

extern "C" {

    void diagAbortWithResult(Result rc) {
        UL_RC_LOG_ASSERT("diagAbortWithResult", rc);
        __builtin_unreachable();
    }

}

namespace ul {

    namespace {

        char g_LogPath[FS_MAX_PATH] = {};
        Mutex g_LogLock;

        inline const char *FormatLogKind(const LogKind kind) {
            switch(kind) {
                case LogKind::Information: {
                    return "INFO";
                }
                case LogKind::Warning: {
                    return "WARN";
                }
                case LogKind::Critical: {
                    return "ERROR";
                }
                default: {
                    return "UNK";
                }
            }
        }

        constexpr auto MaxLogFileCount = 10;

        inline void FormatLogPath(char *out_path, const char *proc_name, const u32 log_idx) {
            if(log_idx == 0) {
                snprintf(out_path, FS_MAX_PATH, "%s/log_%s.log", RootPath, proc_name);
            }
            else {
                snprintf(out_path, FS_MAX_PATH, "%s/log_%s_%d.log", RootPath, proc_name, log_idx);
            }
        }

    }

    // Doing this since the process may not even have these initialized (looking at you, uLoader...)
    #define _UL_DO_WITH_FSDEV(...) ({ \
        const auto needs_sm = !serviceIsActive(smGetServiceSession()); \
        const auto needs_fs = !serviceIsActive(fsGetServiceSession()); \
        const auto needs_fsdev = fsdevGetDeviceFileSystem("sdmc") == nullptr; \
        if(!needs_sm || R_SUCCEEDED(smInitialize())) { \
            if(!needs_fs || R_SUCCEEDED(fsInitialize())) { \
                if(!needs_fsdev || R_SUCCEEDED(fsdevMountSdmc())) { \
                    { __VA_ARGS__ } \
                    if(needs_fsdev) { \
                        fsdevUnmountAll(); \
                    } \
                } \
                if(needs_fs) { \
                    fsExit(); \
                } \
            } \
            if(needs_sm) { \
                smExit(); \
            } \
        } \
    })

    void InitializeLogging(const char *proc_name) {
        char tmp_log_path[FS_MAX_PATH] = {};

        _UL_DO_WITH_FSDEV({
            u32 i = MaxLogFileCount - 1;
            while(true) {
                FormatLogPath(g_LogPath, proc_name, i);
                if(fs::ExistsFile(g_LogPath)) {
                    if(i == MaxLogFileCount - 1) {
                        fs::DeleteFile(g_LogPath);
                    }
                    else {
                        FormatLogPath(tmp_log_path, proc_name, i + 1);
                        fs::RenameFile(g_LogPath, tmp_log_path);
                    }
                }

                if(i == 0) {
                    break;
                }
                i--;
            }

            // Loop ends with idx 0 formatted, which is what we want
        });
    }

    void LogImpl(const LogKind kind, const char *log_fmt, ...) {
        // Telemetry path: route through the structured pipeline when available.
        // EmitSync is used for Warning/Critical so the message survives a crash.
        if(::ul::tel::IsInitialized()) {
            va_list tel_args;
            va_start(tel_args, log_fmt);
            char tel_buf[512];
            vsnprintf(tel_buf, sizeof(tel_buf), log_fmt, tel_args);
            va_end(tel_args);
            tel_buf[sizeof(tel_buf) - 1] = '\0';

            switch(kind) {
                case LogKind::Information:
                    ::ul::tel::Emit(::ul::tel::Cat::Generic, ::ul::tel::Sev::Info, "%s", tel_buf);
                    break;
                case LogKind::Warning:
                    ::ul::tel::EmitSync(::ul::tel::Cat::Generic, ::ul::tel::Sev::Warn, "%s", tel_buf);
                    break;
                case LogKind::Critical:
                    ::ul::tel::EmitSync(::ul::tel::Cat::Generic, ::ul::tel::Sev::Crit, "%s", tel_buf);
                    break;
                default:
                    ::ul::tel::Emit(::ul::tel::Cat::Generic, ::ul::tel::Sev::Info, "%s", tel_buf);
                    break;
            }
        }

        // Legacy file path: kept as fallback for early-boot and as the
        // human-readable per-process log at sdmc:/ulaunch/log_<proc>.log.
        ScopedLock lk(g_LogLock);

        if(g_LogPath[0] == '\0') {
            return;
        }

        va_list args;
        va_start(args, log_fmt);
        _UL_DO_WITH_FSDEV({
            auto file = fopen(g_LogPath, "ab+");
            if(file) {
                if(serviceIsActive(timeGetServiceSession_SteadyClock())) {
                    const auto time = os::GetCurrentTime();
                    fprintf(file, "[%02d:%02d:%02d] ", time.h, time.min, time.sec);
                }

                const auto kind_str = FormatLogKind(kind);
                fprintf(file, "[%s] ", kind_str);
                vfprintf(file, log_fmt, args);
                fprintf(file, "\n");
                fflush(file);
                fclose(file);
            }
        });
        va_end(args);
    }

    void AbortImpl(const Result rc) {
        svcBreak(BreakReason_Panic, reinterpret_cast<uintptr_t>(&rc), sizeof(rc));
        __builtin_unreachable();
    }

}
