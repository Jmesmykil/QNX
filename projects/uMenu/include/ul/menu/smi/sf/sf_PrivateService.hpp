
#pragma once
#include <ul/smi/smi_Protocol.hpp>
#include <ul/smi/sf/sf_Private.hpp>

namespace ul::menu::smi::sf {

    using namespace ul::smi;
    using namespace ul::smi::sf;

    using OnMessageCallback = std::function<void(const MenuMessageContext&)>;

    Result InitializePrivateService();
    void FinalizePrivateService();

    Result QueryApplicationNacpMetadata(const u64 app_id, NacpMetadata *out_nacp_metadata);
    Result QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size);

    void RegisterOnMessageDetect(OnMessageCallback callback, const MenuMessage desired_msg = MenuMessage::Invalid);

}
