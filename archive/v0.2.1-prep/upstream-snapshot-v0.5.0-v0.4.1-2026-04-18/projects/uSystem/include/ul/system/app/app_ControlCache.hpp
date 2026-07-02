
#pragma once
#include <ul/os/os_Applications.hpp>
#include <ul/smi/sf/sf_Private.hpp>

// QOS-PATCH-010: fw 20.0.0 slashed the applet-pool steal cap from 40MB to 14MB.
// uLaunch v1.2.0 targeted 40MB for its NACP+icon in-RAM cache, causing pgl error
// 0xC880 (Module 128, Description 100 ResultOutOfMemory) at svcCreateProcess.
// Cap in-RAM cache to MaxCachedApplications entries (~10MB) via FIFO eviction.
// 2026-04-18

namespace ul::system::app {

    // NACP fields that we care about

    struct ApplicationNacpMisc {
        char display_version[0x10];
        u8 video_capture;
        u64 save_data_owner_id;
        u64 user_account_save_data_size;
        u64 user_account_save_data_journal_size;
        u64 device_save_data_size;
        u64 device_save_data_journal_size;
        u64 temporary_storage_size;
        u64 cache_storage_size;
        u64 cache_storage_journal_size;
        u64 bcat_delivery_cache_storage_size;
        u64 bcat_delivery_cache_storage_journal_size;
    };

    void InitializeControlCache(const std::vector<NsExtApplicationRecord> &records);

    bool IsQueryLocked();
    void RequestCacheApplication(const u64 app_id);
    void RequestRemoveApplicationCache(const u64 app_id);

    bool QueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc);
    bool LoopQueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc);

}
