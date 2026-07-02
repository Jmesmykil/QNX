
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

    // v2.8.0 — bumped from 1 to 4.
    //
    // Root cause of the post-theme-switch login hang (HW-verified 2026-05-18):
    // when uMenu A dies and uMenu B respawns, B's smGetService(&g_PrivateService,
    // "ulsf:p") races against AMS ServerManager's IPC thread observing A's session
    // closed and running OnNeedsToAccept for the new session.  With only one slot,
    // there is zero headroom for the close+reopen transition.  If B's psrvInitialize
    // lands before A's slot is released, B's IPC sits in the kernel session queue
    // — and although it eventually completes (which is why the user-card displays),
    // subsequent SMI traffic on the half-attached session blocks indefinitely.
    //
    // 4 slots is the smallest value that survives multiple back-to-back theme
    // switches (audit found prior creator memory card: "fork uSystem exhausts HIPC
    // session pool" / 2011-0102 OutOfSessionMemory).  Each slot is ~512 B in the
    // ServerManager allocator, so the memory cost is negligible (~1.5 KB total).
    //
    // The AMS-1.11 clean-exit fix in la_LibraryApplet.cpp:80-92 only drains the
    // LibraryApplet's GetResult slot; the PrivateService session is on a separate
    // pool that this constant governs.
    constexpr size_t MaxPrivateSessions = 4;
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