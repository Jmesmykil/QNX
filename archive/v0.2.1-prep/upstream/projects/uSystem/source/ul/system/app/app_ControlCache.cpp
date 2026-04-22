#include <ul/system/app/app_ControlCache.hpp>
#include <ul/ul_Result.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/util/util_Size.hpp>
#include <queue>
#include <deque>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <ul/fs/fs_Stdio.hpp>

using namespace ul::util::size;

namespace ul::system::app {

    namespace {

        SetLanguage g_SystemLanguage;

        std::queue<u64> *g_PendingApplicationCacheQueue;
        ul::Mutex g_PendingApplicationCacheQueueLock;

        std::queue<u64> *g_PendingApplicationRemovalCacheQueue;
        ul::Mutex g_PendingApplicationRemovalCacheQueueLock;

        std::unordered_map<u64, ApplicationNacpMisc> *g_ApplicationCache;
        ul::Mutex g_ApplicationCacheLock;

        // QOS-PATCH-010: fw 20.0.0 applet-pool cap is 14MB. At ~200KB per entry
        // (sizeof(ApplicationNacpMisc) + NsApplicationControlData icon headroom),
        // 50 entries cap peak in-RAM usage at ~10MB, leaving 4MB headroom.
        // Older entries are evicted FIFO via g_ApplicationCacheInsertionOrder.
        // Disk cache (icon .jpg + .nacp.meta) is NOT evicted — only the in-RAM map.
        constexpr size_t MaxCachedApplications = 50;

        // Tracks insertion order for FIFO eviction. Parallel to g_ApplicationCache.
        std::deque<u64> *g_ApplicationCacheInsertionOrder;

        Thread g_ApplicationControlCacheThread;
        constexpr size_t ApplicationControlCacheThreadStackSize = 64_KB;

        constexpr size_t PerApplicationCacheUsage = sizeof(ApplicationNacpMisc) + sizeof(u64);
        constexpr double PerApplicationCacheUsageMb = PerApplicationCacheUsage / 1024.0f / 1024.0f;

        constexpr size_t LoopQueryMaxRetryCount = 50;

        void LogCacheMemoryUsage() {
            const auto total_size = PerApplicationCacheUsage * g_ApplicationCache->size();
            UL_LOG_INFO("[ApplicationControlCache] Cache memory usage: %f MB", total_size / 1024.0f / 1024.0f);
        }

        void RemoveNextApplicationCache() {
            ScopedLock lock(g_PendingApplicationRemovalCacheQueueLock);

            if(!g_PendingApplicationRemovalCacheQueue->empty()) {
                UL_LOG_INFO("[ApplicationControlCache] Pending remove applications: %zu", g_PendingApplicationRemovalCacheQueue->size());
                const auto app_id = g_PendingApplicationRemovalCacheQueue->front();
                g_PendingApplicationRemovalCacheQueue->pop();

                ScopedLock lock2(g_ApplicationCacheLock);
                const auto app_it = std::find_if(g_ApplicationCache->begin(), g_ApplicationCache->end(), [app_id](const auto &entry) {
                    return entry.first == app_id;
                });
                if(app_it != g_ApplicationCache->end()) {
                    g_ApplicationCache->erase(app_it);
                    // QOS-PATCH-010: keep insertion-order deque in sync with explicit removals.
                    const auto ord_it = std::find(g_ApplicationCacheInsertionOrder->begin(), g_ApplicationCacheInsertionOrder->end(), app_id);
                    if(ord_it != g_ApplicationCacheInsertionOrder->end()) {
                        g_ApplicationCacheInsertionOrder->erase(ord_it);
                    }
                    UL_LOG_INFO("[ApplicationControlCache] Removed application cache for application ID 0x%016lX", app_id);
                    LogCacheMemoryUsage();
                }
                else {
                    UL_LOG_WARN("[ApplicationControlCache] Failed to remove application cache for application ID 0x%016lX, not found in cache", app_id);
                }
            }
        }

        inline Result GetApplicationControlData(const u64 app_id, NsApplicationControlData *out_data, size_t &out_icon_size) {
            size_t got_size;
            const auto rc = nsextGetApplicationControlData(NsApplicationControlSource_Storage, app_id, out_data, sizeof(*out_data), &got_size);
            if(R_SUCCEEDED(rc)) {
                UL_LOG_INFO("[ApplicationControlCache] Got from legacy NS application control data for application ID 0x%016lX", app_id);
                UL_ASSERT_TRUE(got_size <= sizeof(*out_data));
                out_icon_size = got_size - sizeof(NacpStruct);
            }
            return rc;
        }

        bool CacheApplicationIcon(const u64 app_id, const u8 *icon_buf, const size_t icon_buf_size) {
            const auto path = fs::JoinPath(ApplicationCachePath, util::FormatProgramId(app_id) + ".jpg");
            return fs::WriteFile(path, icon_buf, icon_buf_size, true);
        }

        bool CacheApplicationNacpMetadata(const u64 app_id, const smi::sf::NacpMetadata &nacp_metadata) {
            const auto path = fs::JoinPath(ApplicationCachePath, util::FormatProgramId(app_id) + ".nacp.meta");
            return fs::WriteFile(path, &nacp_metadata, sizeof(nacp_metadata), true);
        }

        void AddNextApplicationCache() {
            ScopedLock lock(g_PendingApplicationCacheQueueLock);

            if(!g_PendingApplicationCacheQueue->empty()) {
                UL_LOG_INFO("[ApplicationControlCache] Pending add applications: %zu", g_PendingApplicationCacheQueue->size());
                const auto app_id = g_PendingApplicationCacheQueue->front();

                {
                    ScopedLock lock2(g_ApplicationCacheLock);

                    // QOS-PATCH-010: Enforce MaxCachedApplications FIFO eviction.
                    // Evict oldest entries until we are under the cap.
                    while(g_ApplicationCache->size() >= MaxCachedApplications) {
                        if(!g_ApplicationCacheInsertionOrder->empty()) {
                            const auto oldest_id = g_ApplicationCacheInsertionOrder->front();
                            g_ApplicationCacheInsertionOrder->pop_front();
                            g_ApplicationCache->erase(oldest_id);
                            UL_LOG_INFO("[ApplicationControlCache] FIFO evict app ID 0x%016lX (cap %zu)", oldest_id, MaxCachedApplications);
                        }
                        else {
                            // Insertion order deque is empty but map is not — clear the map to recover.
                            UL_LOG_WARN("[ApplicationControlCache] Insertion order deque empty but cache at cap; clearing map");
                            g_ApplicationCache->clear();
                            break;
                        }
                    }

                    auto &nacp_misc = g_ApplicationCache->operator[](app_id);

                    u32 tries = 0;
                    size_t icon_size;
                    while(true) {
                        const auto start_tick = armGetSystemTick();
                        auto control_data = new NsApplicationControlData();
                        UL_ON_SCOPE_EXIT({
                            delete control_data;
                        });

                        const auto rc = GetApplicationControlData(app_id, control_data, icon_size);
                        const auto end_tick = armGetSystemTick();
                        const auto elapsed_time_ms = armTicksToNs(end_tick - start_tick) / 1'000'000;
                        if(R_FAILED(rc)) {
                            UL_LOG_WARN("[ApplicationControlCache] Failed to get control data for application ID 0x%016lX: %s (elapsed time: %ld ms)", app_id, util::FormatResultDisplay(rc).c_str(), elapsed_time_ms);
                            if(tries >= 50) {
                                UL_LOG_WARN("[ApplicationControlCache] Failed to get control data for application ID 0x%016lX after 50 tries, giving up", app_id);
                                g_ApplicationCache->erase(app_id);
                                break;
                            }
                        }
                        else {
                            UL_LOG_WARN("[ApplicationControlCache] Got control data for application ID 0x%016lX (elapsed time: %ld ms), icon size: %zu bytes", app_id, elapsed_time_ms, icon_size);

                            smi::sf::NacpMetadata meta = {};
                            NacpLanguageEntry *langentry;
                            nacpGetLanguageEntry(&control_data->nacp, &langentry);
                            if(langentry == nullptr) {
                                UL_LOG_WARN("[ApplicationControlCache] Failed to get language entry for application ID 0x%016lX, cannot cache", app_id);
                                g_ApplicationCache->erase(app_id);
                                break;
                            }
                            memcpy(meta.name, langentry->name, sizeof(meta.name));
                            memcpy(meta.author, langentry->author, sizeof(meta.author));
                            memcpy(meta.display_version, control_data->nacp.display_version, sizeof(meta.display_version));

                            // Cache icon + NACP
                            if(!CacheApplicationNacpMetadata(app_id, meta)) {
                                UL_LOG_WARN("[ApplicationControlCache] Failed to cache NACP for application ID 0x%016lX", app_id);
                            }
                            else if(!CacheApplicationIcon(app_id, control_data->icon, icon_size)) {
                                UL_LOG_WARN("[ApplicationControlCache] Failed to cache icon for application ID 0x%016lX", app_id);
                            }
                            else {
                                UL_LOG_INFO("[ApplicationControlCache] Made NACP + icon cache for application ID 0x%016lX (icon size: %zu bytes)", app_id, icon_size);
                            }

                            // Populate our misc cache
                            memcpy(nacp_misc.display_version, control_data->nacp.display_version, sizeof(nacp_misc.display_version));
                            nacp_misc.video_capture = control_data->nacp.video_capture;
                            nacp_misc.save_data_owner_id = control_data->nacp.save_data_owner_id;
                            nacp_misc.user_account_save_data_size = control_data->nacp.user_account_save_data_size;
                            nacp_misc.user_account_save_data_journal_size = control_data->nacp.user_account_save_data_journal_size;
                            nacp_misc.device_save_data_size = control_data->nacp.device_save_data_size;
                            nacp_misc.device_save_data_journal_size = control_data->nacp.device_save_data_journal_size;
                            nacp_misc.temporary_storage_size = control_data->nacp.temporary_storage_size;
                            nacp_misc.cache_storage_size = control_data->nacp.cache_storage_size;
                            nacp_misc.cache_storage_journal_size = control_data->nacp.cache_storage_journal_size;
                            nacp_misc.bcat_delivery_cache_storage_size = control_data->nacp.bcat_delivery_cache_storage_size;
                            UL_LOG_INFO("[ApplicationControlCache] Added application misc cache for application ID 0x%016lX (elapsed time: %ld ms)", app_id, elapsed_time_ms);
                            // QOS-PATCH-010: record insertion order for FIFO eviction.
                            g_ApplicationCacheInsertionOrder->push_back(app_id);
                            LogCacheMemoryUsage();
                            break;
                        }

                        tries++;
                    }
                }

                g_PendingApplicationCacheQueue->pop();
            }
        }

        void ApplicationControlCacheMain(void*) {
            UL_LOG_INFO("[ApplicationControlCache] alive!");

            while(true) {
                RemoveNextApplicationCache();
                AddNextApplicationCache();

                svcSleepThread(10'000'000);
            }
        }

    }

    void InitializeControlCache(const std::vector<NsExtApplicationRecord> &records) {
        fs::CreateDirectory(ApplicationCachePath);

        UL_RC_ASSERT(setInitialize());
        u64 lang_code;
        UL_RC_ASSERT(setGetSystemLanguage(&lang_code));
        UL_RC_ASSERT(setMakeLanguage(lang_code, &g_SystemLanguage));
        setExit();

        UL_RC_ASSERT(ncmInitialize());

        UL_LOG_INFO("[ApplicationControlCache] Initializing control cache with %zu records (cache usage per application: %f MB)", records.size(), PerApplicationCacheUsageMb);

        g_PendingApplicationCacheQueue = new std::queue<u64>();
        g_PendingApplicationRemovalCacheQueue = new std::queue<u64>();
        g_ApplicationCache = new std::unordered_map<u64, ApplicationNacpMisc>();
        g_ApplicationCacheInsertionOrder = new std::deque<u64>(); // QOS-PATCH-010

        for(const auto &record : records) {
            g_PendingApplicationCacheQueue->push(record.id);
        }

        UL_RC_ASSERT(threadCreate(&g_ApplicationControlCacheThread, ApplicationControlCacheMain, nullptr, nullptr, ApplicationControlCacheThreadStackSize, 0x1F, -2));
        UL_RC_ASSERT(threadStart(&g_ApplicationControlCacheThread));
    }

    bool IsQueryLocked() {
        return g_ApplicationCacheLock.IsLocked();
    }

    void RequestCacheApplication(const u64 app_id) {
        ScopedLock lock(g_PendingApplicationCacheQueueLock);
        g_PendingApplicationCacheQueue->push(app_id);
    }

    void RequestRemoveApplicationCache(const u64 app_id) {
        ScopedLock lock(g_PendingApplicationRemovalCacheQueueLock);
        g_PendingApplicationRemovalCacheQueue->push(app_id);
    }

    bool QueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc) {
        ScopedLock lock(g_ApplicationCacheLock);

        const auto app_it = std::find_if(g_ApplicationCache->begin(), g_ApplicationCache->end(), [app_id](const auto &entry) {
            return entry.first == app_id;
        });
        if(app_it != g_ApplicationCache->end()) {
            out_nacp_misc = app_it->second;
            return true;
        }

        return false;
    }

    bool LoopQueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc) {
        u32 i = 0;
        while(true) {
            if(IsQueryLocked()) {
                continue;
            }

            if(QueryApplicationNacpMisc(app_id, out_nacp_misc)) {
                return true;
            }
            else {
                UL_LOG_WARN("Failed to query misc NACP fields for application ID 0x%016lX!", app_id);
                i++;
                if(i >= LoopQueryMaxRetryCount) {
                    UL_LOG_WARN("Max retry count reached for application ID 0x%016lX!", app_id);
                    return false;
                }
            }

            svcSleepThread(10'000'000);
        }
    }

}
