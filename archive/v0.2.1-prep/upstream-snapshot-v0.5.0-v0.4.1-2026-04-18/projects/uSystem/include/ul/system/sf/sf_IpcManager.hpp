
#pragma once
#include <stratosphere.hpp>
#include <stratosphere/fssrv/fssrv_interface_adapters.hpp>
#include <ul/sf/sf_Public.hpp>
#include <ul/smi/sf/sf_Private.hpp>

namespace ul::system::sf {

    using namespace ul::sf;
    using namespace ul::smi::sf;

    // Note: domains and pointer buffer are required since ECS sessions will make use of them (like normal fs interfaces)

    using Allocator = ::ams::sf::ExpHeapAllocator;
    using ObjectFactory = ::ams::sf::ObjectFactory<::ams::sf::ExpHeapAllocator::Policy>;

    struct ServerOptions {
        static constexpr size_t PointerBufferSize = 0x800;
        static constexpr size_t MaxDomains = 0x40;
        static constexpr size_t MaxDomainObjects = 0x100;
        static constexpr bool CanDeferInvokeRequest = false;
        static constexpr bool CanManageMitmServers = false;
    };

    enum Port {
        Port_PrivateService,
        Port_PublicService,

        Port_Count
    };

    constexpr size_t MaxPrivateSessions = 1;
    constexpr ::ams::sm::ServiceName PrivateName = ::ams::sm::ServiceName::Encode(PrivateServiceName);

    constexpr size_t MaxPublicSessions = 32;
    constexpr ::ams::sm::ServiceName PublicName = ::ams::sm::ServiceName::Encode(PublicServiceName);

    constexpr size_t MaxEcsExtraSessions = 5;
    constexpr size_t MaxSessions = MaxPrivateSessions + MaxEcsExtraSessions;

    class ServerManager final : public ::ams::sf::hipc::ServerManager<Port_Count, ServerOptions, MaxSessions> {
        private:
            virtual ::ams::Result OnNeedsToAccept(int port_index, Server *server) override;
    };

    Result Initialize();
    ams::sf::EmplacedRef<::ams::fssrv::sf::IFileSystem, ::ams::fssrv::impl::FileSystemInterfaceAdapter> MakeSharedFileSystem(std::shared_ptr<::ams::fs::fsa::IFileSystem> &&fs);
    ::ams::Result RegisterSession(const ::ams::os::NativeHandle session_handle, ::ams::sf::cmif::ServiceObjectHolder &&obj);

}