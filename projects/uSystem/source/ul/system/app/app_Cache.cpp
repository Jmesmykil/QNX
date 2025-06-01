#include <ul/system/app/app_Cache.hpp>
#include <ul/ul_Result.hpp>
#include <ul/util/util_Size.hpp>
#include <queue>
#include <algorithm>
#include <cstring>

using namespace ul::util::size;

namespace ul::system::app {

    namespace {

        std::queue<u64> *g_PendingApplicationCacheQueue;
        ul::Mutex g_PendingApplicationCacheQueueLock;

        std::queue<u64> *g_PendingApplicationRemovalCacheQueue;
        ul::Mutex g_PendingApplicationRemovalCacheQueueLock;

        std::vector<u64> g_ApplicationIdCache;
        ul::Mutex g_ApplicationIdCacheLock;

        std::vector<NsApplicationControlData> g_ApplicationControlDataCache;
        ul::Mutex g_ApplicationControlDataCacheLock;

        Thread g_ApplicationControlDataCacheThread;
        constexpr size_t g_ApplicationControlDataCacheThreadStackSize = 4_KB;

        void LogCacheMemoryUsage() {
            const auto total_size = (sizeof(NsApplicationControlData) + sizeof(u64)) * g_ApplicationIdCache.size();
            UL_LOG_INFO("[ApplicationControlDataCache] Cache memory usage: %f MB", total_size / 1024.0f / 1024.0f);
        }

        void RemoveNextApplicationCache() {
            ScopedLock lock(g_PendingApplicationRemovalCacheQueueLock);

            if(!g_PendingApplicationRemovalCacheQueue->empty()) {
                UL_LOG_INFO("[ApplicationControlDataCache] Pending remove applications: %zu", g_PendingApplicationRemovalCacheQueue->size());
                const auto app_id = g_PendingApplicationRemovalCacheQueue->front();
                g_PendingApplicationRemovalCacheQueue->pop();

                ScopedLock lock2(g_ApplicationIdCacheLock);
                const auto app_it = std::find_if(g_ApplicationIdCache.begin(), g_ApplicationIdCache.end(), [app_id](const u64 id) {
                    return id == app_id;
                });
                if(app_it != g_ApplicationIdCache.end()) {
                    const auto index = std::distance(g_ApplicationIdCache.begin(), app_it);
                    {
                        ScopedLock lock3(g_ApplicationControlDataCacheLock);
                        g_ApplicationControlDataCache.erase(g_ApplicationControlDataCache.begin() + index);
                    }
                    g_ApplicationIdCache.erase(app_it);

                    UL_LOG_INFO("[ApplicationControlDataCache] Removed application cache for application ID 0x%016lX", app_id);
                    LogCacheMemoryUsage();
                }
                else {
                    UL_LOG_WARN("[ApplicationControlDataCache] Failed to remove application cache for application ID 0x%016lX, not found in cache", app_id);
                }
            }
        }

        void AddNextApplicationCache() {
            ScopedLock lock(g_PendingApplicationCacheQueueLock);

            if(!g_PendingApplicationCacheQueue->empty()) {
                UL_LOG_INFO("[ApplicationControlDataCache] Pending add applications: %zu", g_PendingApplicationCacheQueue->size());
                const auto app_id = g_PendingApplicationCacheQueue->front();

                {
                    ScopedLock lock2(g_ApplicationControlDataCacheLock);
                    ScopedLock lock3(g_ApplicationIdCacheLock);

                    g_ApplicationIdCache.push_back(app_id);
                    auto &control_data = g_ApplicationControlDataCache.emplace_back();
                    size_t tmp_size;
                    
                    u32 tries = 0;
                    while(true) {
                        const auto start_tick = armGetSystemTick();
                        const auto rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, std::addressof(control_data), sizeof(NsApplicationControlData), &tmp_size);
                        const auto end_tick = armGetSystemTick();
                        const auto elapsed_time_ms = armTicksToNs(end_tick - start_tick) / 1'000'000;
                        if(R_FAILED(rc)) {
                            UL_LOG_WARN("[ApplicationControlDataCache] Failed to get control data for application ID 0x%016lX, rc: 0x%X (elapsed time: %ld ms)", app_id, rc, elapsed_time_ms);
                            if(tries >= 50) {
                                UL_LOG_WARN("[ApplicationControlDataCache] Failed to get control data for application ID 0x%016lX after 50 tries, giving up", app_id);
                                g_ApplicationIdCache.pop_back();
                                g_ApplicationControlDataCache.pop_back();
                                break;
                            }
                        }
                        else {
                            UL_LOG_INFO("[ApplicationControlDataCache] Added application cache for application ID 0x%016lX (elapsed time: %ld ms)", app_id, elapsed_time_ms);
                            LogCacheMemoryUsage();
                            break;
                        }

                        tries++;
                    }
                }

                g_PendingApplicationCacheQueue->pop();
            }
        }

        void ApplicationControlDataCacheMain(void*) {
            UL_LOG_INFO("[ApplicationControlDataCache] alive!");

            while(true) {
                RemoveNextApplicationCache();
                AddNextApplicationCache();

                svcSleepThread(1'000'000);
            }
        }

    }

    void InitializeCache(const std::vector<NsExtApplicationRecord> &records) {
        g_PendingApplicationCacheQueue = new std::queue<u64>();
        g_PendingApplicationRemovalCacheQueue = new std::queue<u64>();

        for(const auto &record : records) {
            g_PendingApplicationCacheQueue->push(record.id);
        }

        UL_RC_ASSERT(threadCreate(&g_ApplicationControlDataCacheThread, ApplicationControlDataCacheMain, nullptr, nullptr, g_ApplicationControlDataCacheThreadStackSize, 28, -2));
        UL_RC_ASSERT(threadStart(&g_ApplicationControlDataCacheThread));
    }

    bool IsQueryLocked() {
        return g_ApplicationControlDataCacheLock.IsLocked() || g_ApplicationIdCacheLock.IsLocked();
    }

    void RequestCacheApplication(const u64 app_id) {
        ScopedLock lock(g_PendingApplicationCacheQueueLock);
        g_PendingApplicationCacheQueue->push(app_id);
    }

    void RequestRemoveApplicationCache(const u64 app_id) {
        ScopedLock lock(g_PendingApplicationRemovalCacheQueueLock);
        g_PendingApplicationRemovalCacheQueue->push(app_id);
    }

    // Blazingly fast compared to 20.x NS commands

    bool QueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf) {
        ScopedLock lock(g_ApplicationControlDataCacheLock);
        ScopedLock lock2(g_ApplicationIdCacheLock);

        const auto app_it = std::find_if(g_ApplicationIdCache.begin(), g_ApplicationIdCache.end(), [app_id](const u64 id) {
            return id == app_id;
        });
        if(app_it != g_ApplicationIdCache.end()) {
            const auto index = std::distance(g_ApplicationIdCache.begin(), app_it);
            memcpy(out_nacp_buf, std::addressof(g_ApplicationControlDataCache[index].nacp), sizeof(NacpStruct));
            return true;
        }

        return false;
    }

    bool QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size) {
        ScopedLock lock(g_ApplicationControlDataCacheLock);
        ScopedLock lock2(g_ApplicationIdCacheLock);

        const auto app_it = std::find_if(g_ApplicationIdCache.begin(), g_ApplicationIdCache.end(), [app_id](const u64 id) {
            return id == app_id;
        });
        if(app_it != g_ApplicationIdCache.end()) {
            const auto index = std::distance(g_ApplicationIdCache.begin(), app_it);
            const auto copy_size = std::min(icon_size, sizeof(g_ApplicationControlDataCache[index].icon));
            memcpy(out_icon_buf, std::addressof(g_ApplicationControlDataCache[index].icon), copy_size);
            out_icon_size = copy_size;
            return true;
        }

        return false;
    }

    void LoopQueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf) {
        while(true) {
            if(IsQueryLocked()) {
                continue;
            }

            if(QueryApplicationNacp(app_id, out_nacp_buf)) {
                break;
            }
            else {
                UL_LOG_WARN("Failed to query NACP for application ID 0x%016lX!", app_id);
            }

            svcSleepThread(10'000'000);
        }
    }

}
