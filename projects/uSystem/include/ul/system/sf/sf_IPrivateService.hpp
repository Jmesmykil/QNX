
#pragma once
#include <stratosphere.hpp>
#include <ul/system/smi/smi_SystemProtocol.hpp>

namespace ul::system::sf {

    struct MenuMessageContext : ::ams::sf::LargeData, ::ams::sf::PrefersMapAliasTransferMode {
        smi::MenuMessageContext actual_ctx;
    };

    struct Nacp : ::ams::sf::LargeData, ::ams::sf::PrefersMapAliasTransferMode {
        NacpStruct actual_nacp;
    };

}

#define UL_SYSTEM_SF_I_PRIVATE_SERVICE_INTERFACE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 0, Result, Initialize, (const ::ams::sf::ClientProcessId &client_pid), (client_pid)) \
    AMS_SF_METHOD_INFO(C, H, 1, Result, TryPopMessageContext, (::ams::sf::Out<::ul::system::sf::MenuMessageContext> &out_msg), (out_msg)) \
    AMS_SF_METHOD_INFO(C, H, 2, Result, QueryApplicationNacp, (::ams::sf::Out<::ul::system::sf::Nacp> &out_nacp, const u64 app_id), (out_nacp, app_id)) \
    AMS_SF_METHOD_INFO(C, H, 3, Result, QueryApplicationIcon, (::ams::sf::OutBuffer &out_icon, ::ams::sf::Out<size_t> &out_icon_size, const u64 app_id), (out_icon, out_icon_size, app_id))

AMS_SF_DEFINE_INTERFACE(ams::ul::system::sf, IPrivateService, UL_SYSTEM_SF_I_PRIVATE_SERVICE_INTERFACE_INFO, 0xCAFEBABE)

namespace ul::system::sf {

    class PrivateService {
        private:
            bool initialized;

        public:
            PrivateService() : initialized(false) {}

            ::ams::Result Initialize(const ::ams::sf::ClientProcessId &client_pid);
            ::ams::Result TryPopMessageContext(::ams::sf::Out<MenuMessageContext> &out_msg);
            ::ams::Result QueryApplicationNacp(::ams::sf::Out<Nacp> &out_nacp, const u64 app_id);
            ::ams::Result QueryApplicationIcon(::ams::sf::OutBuffer &out_icon, ::ams::sf::Out<size_t> &out_icon_size, const u64 app_id);
    };
    static_assert(::ams::ul::system::sf::IsIPrivateService<PrivateService>);

}
