
#pragma once
#include <switch.h>
#include <switch-ipcext.h>
#include <vector>
#include <string>
#include <unistd.h>
#include <cstdio>

namespace ul {

    constexpr const char RootPath[] = "sdmc:/qos-shell";
    constexpr const char DefaultLanguage[] = "en";

    constexpr const char SysMMCConfigPath[] = "sdmc:/qos-shell/sys-config.cfg";
    constexpr const char EmuMMCConfigPath[] = "sdmc:/qos-shell/emu-config.cfg";

    constexpr const char ThemesPath[] = "sdmc:/qos-shell/themes";
    constexpr const char DefaultThemePath[] = "romfs:/default";

    constexpr const char MenuLanguagesPath[] = "sdmc:/qos-shell/lang/uMenu";
    constexpr const char MenuRomfsFile[] = "sdmc:/qos-shell/bin/uMenu/romfs.bin";

    constexpr const char BuiltinMenuLanguagesPath[] = "romfs:/lang";

    constexpr const char RootCachePath[] = "sdmc:/qos-shell/cache";
    constexpr const char HomebrewCachePath[] = "sdmc:/qos-shell/cache/hb";
    constexpr const char ApplicationCachePath[] = "sdmc:/qos-shell/cache/app";
    constexpr const char ThemePreviewCachePath[] = "sdmc:/qos-shell/cache/preview";
    constexpr const char ActiveThemeCachePath[] = "sdmc:/qos-shell/cache/active";

    constexpr const char ManagerLanguagesPath[] = "sdmc:/qos-shell/lang/uManager";

    constexpr const char HbmenuPath[] = "sdmc:/hbmenu.nro";
    constexpr const char ManagerPath[] = "sdmc:/switch/uManager.nro";
    constexpr const char RootHomebrewPath[] = "sdmc:/switch";

    constexpr const char *ImageFormatList[] = {
        "png",
        "jpg",
        "jpeg",
        "webp"
    };

    //////////////////////

    constexpr const char PreV100MenuPath[] = "sdmc:/qos-shell/entries";
    constexpr const char PreV100ApplicationCachePath[] = "sdmc:/qos-shell/titles";
    constexpr const char PreV100HomebrewCachePath[] = "sdmc:/qos-shell/nro";
    constexpr const char PreV100AccountCachePath[] = "sdmc:/qos-shell/user";

    constexpr const char V100_V110MenuPath[] = "sdmc:/qos-shell/menu";

    constexpr const char PreV120AccountCachePath[] = "sdmc:/qos-shell/cache/acc";
    constexpr const char PreV120ApplicationCachePath[] = "sdmc:/qos-shell/cache/app";

    constexpr const char PreV120ConfigPath[] = "sdmc:/qos-shell/config.cfg";

    //////////////////////

    class Mutex {
        private:
            ::Mutex mutex;

        public:
            constexpr Mutex() : mutex() {}

            inline void Lock() {
                mutexLock(&this->mutex);
            }

            inline void Unlock() {
                mutexUnlock(&this->mutex);
            }

            inline bool TryLock() {
                return mutexTryLock(&this->mutex);
            }

            inline bool IsLocked() {
                return mutexIsLockedByCurrentThread(&this->mutex);
            }
    };

    class RecursiveMutex {
        private:
            ::RMutex mutex;

        public:
            constexpr RecursiveMutex() : mutex() {}

            inline void Lock() {
                rmutexLock(&this->mutex);
            }

            inline void Unlock() {
                rmutexUnlock(&this->mutex);
            }

            inline bool TryLock() {
                return rmutexTryLock(&this->mutex);
            }
    };

    template<typename T>
    class ScopedLock {
        private:
            T &lock;
        
        public:
            ScopedLock(T &lock) : lock(lock) {
                lock.Lock();
            }

            ~ScopedLock() {
                this->lock.Unlock();
            }
    };

    struct Version {
        u8 major;
        u8 minor;
        u8 micro;

        inline constexpr bool Equals(const Version &other) {
            return (this->major == other.major) && (this->minor == other.minor) && (this->micro == other.micro);
        }

        inline std::string Format() {
            return std::to_string(static_cast<u32>(this->major)) + "." + std::to_string(static_cast<u32>(this->minor)) + "." + std::to_string(static_cast<u32>(this->micro));
        }
    };

    constexpr Version CurrentVersion = {
        .major = UL_MAJOR,
        .minor = UL_MINOR,
        .micro = UL_MICRO
    };

}
