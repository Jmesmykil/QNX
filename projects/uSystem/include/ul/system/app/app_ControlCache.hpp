
#pragma once
#include <ul/os/os_Applications.hpp>
#include <ul/smi/sf/sf_Private.hpp>

namespace ul::system::app {

    // NACP fields that nxtc does not cache but we still use (and thus cache ourselves independently)

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

    bool QueryApplicationNacp(const u64 app_id, smi::sf::NacpMetadata &out_nacp);
    bool QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_buf_size, size_t &out_actual_icon_size);
    bool QueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc);

    bool LoopQueryApplicationNacpMisc(const u64 app_id, ApplicationNacpMisc &out_nacp_misc);

}
