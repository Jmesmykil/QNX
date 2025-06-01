
#pragma once
#include <ul/smi/smi_Protocol.hpp>
#include <functional>

namespace ul::menu::smi {

    using namespace ul::smi;

    using OnMessageCallback = std::function<void(const MenuMessageContext&)>;

    Result InitializePrivateService();
    void FinalizePrivateService();

    Result QueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf);
    Result QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size);

    void RegisterOnMessageDetect(OnMessageCallback callback, const MenuMessage desired_msg = MenuMessage::Invalid);

}
