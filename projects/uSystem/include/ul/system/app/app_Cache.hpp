
#pragma once
#include <ul/os/os_Applications.hpp>

namespace ul::system::app {

    void InitializeCache(const std::vector<NsExtApplicationRecord> &records);
    bool IsQueryLocked();
    void RequestCacheApplication(const u64 app_id);
    void RequestRemoveApplicationCache(const u64 app_id);

    bool QueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf);
    bool QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size);

    void LoopQueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf);

}
