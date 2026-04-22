
#pragma once
#include <stratosphere.hpp>
#include <ul/system/smi/smi_SystemProtocol.hpp>
#include <ul/smi/sf/sf_Private.hpp>

namespace ul::system::smi::sf {

    using namespace ul::smi;
    using namespace ul::smi::sf;

    struct SfMenuMessageContext : MenuMessageContext, ::ams::sf::LargeData, ::ams::sf::PrefersMapAliasTransferMode {};
    struct SfNacpMetadata : NacpMetadata, ::ams::sf::LargeData, ::ams::sf::PrefersMapAliasTransferMode {};

}

#define UL_SYSTEM_SMI_SF_I_PRIVATE_SERVICE_INTERFACE_INFO(C, H) \
    AMS_SF_METHOD_INFO(C, H, 0, Result, Initialize, (const ::ams::sf::ClientProcessId &client_pid), (client_pid)) \
    AMS_SF_METHOD_INFO(C, H, 1, Result, TryPopMessageContext, (::ams::sf::Out<::ul::system::smi::sf::SfMenuMessageContext> &out_msg), (out_msg)) \

AMS_SF_DEFINE_INTERFACE(ams::ul::system::smi::sf, IPrivateService, UL_SYSTEM_SMI_SF_I_PRIVATE_SERVICE_INTERFACE_INFO, 0xCAFEBABE)

namespace ul::system::smi::sf {

    class PrivateService {
        private:
            bool initialized;

        public:
            PrivateService() : initialized(false) {}

            ::ams::Result Initialize(const ::ams::sf::ClientProcessId &client_pid);
            ::ams::Result TryPopMessageContext(::ams::sf::Out<SfMenuMessageContext> &out_msg);
    };
    static_assert(::ams::ul::system::smi::sf::IsIPrivateService<PrivateService>);

}
