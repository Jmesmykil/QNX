#include <ul/system/sf/sf_IpcManager.hpp>
#include <ul/system/smi/sf/sf_IPrivateService.hpp>
#include <ul/system/sf/sf_IPublicService.hpp>
#include <ul/util/util_Size.hpp>

using namespace ul::util::size;

namespace ul::system::sf {

    namespace {

        ServerManager g_Manager;

        constexpr size_t IpcManagerThreadStackSize = 32_KB;
        ::ams::os::ThreadType g_ManagerThread;
        alignas(::ams::os::ThreadStackAlignment) u8 g_ManagerThreadStack[IpcManagerThreadStackSize];
        
        ::ams::os::Mutex g_ManagerAllocatorLock(false);

        constexpr size_t ManagerAllocatorHeapSize = 32_KB;
        alignas(0x40) constinit u8 g_ManagerAllocatorHeap[ManagerAllocatorHeapSize];
        ::ams::lmem::HeapHandle g_ManagerAllocatorHeapHandle;
        Allocator g_ManagerAllocator;

        void IpcManagerThread(void*) {
            ::ams::os::SetThreadNamePointer(::ams::os::GetCurrentThread(), "ul.system.sf.IpcManager");

            UL_RC_ASSERT(g_Manager.RegisterServer(Port_PrivateService, PrivateName, MaxPrivateSessions));
            UL_RC_ASSERT(g_Manager.RegisterServer(Port_PublicService, PublicName, MaxPublicSessions));

            g_Manager.LoopProcess();
        }

        void InitializeHeap() {
            g_ManagerAllocatorHeapHandle = ::ams::lmem::CreateExpHeap(g_ManagerAllocatorHeap, sizeof(g_ManagerAllocatorHeap), ::ams::lmem::CreateOption_None);
            g_ManagerAllocator.Attach(g_ManagerAllocatorHeapHandle);
        }

        template<typename Impl, typename T, typename ...Args>
        auto MakeShared(Args &&...args) {
            ScopedLock lk(g_ManagerAllocatorLock);
            return ObjectFactory::CreateSharedEmplaced<Impl, T>(std::addressof(g_ManagerAllocator), std::forward<Args>(args)...);
        }

    }

    ::ams::Result ServerManager::OnNeedsToAccept(int port_index, Server *server) {
        switch(port_index) {
            case Port_PrivateService: {
                return this->AcceptImpl(server, MakeShared<::ams::ul::system::smi::sf::IPrivateService, smi::sf::PrivateService>());
            }
            case Port_PublicService: {
                return this->AcceptImpl(server, MakeShared<::ams::ul::system::sf::IPublicService, sf::PublicService>());
            }
            AMS_UNREACHABLE_DEFAULT_CASE();
        }
    }

    Result Initialize() {
        InitializeHeap();
        UL_RC_TRY(::ams::os::CreateThread(&g_ManagerThread, &IpcManagerThread, nullptr, g_ManagerThreadStack, sizeof(g_ManagerThreadStack), 10));
        ::ams::os::StartThread(&g_ManagerThread);

        return ResultSuccess;
    }

    ams::sf::EmplacedRef<::ams::fssrv::sf::IFileSystem, ::ams::fssrv::impl::FileSystemInterfaceAdapter> MakeSharedFileSystem(std::shared_ptr<::ams::fs::fsa::IFileSystem> &&fs) {
        return MakeShared<::ams::fssrv::sf::IFileSystem, ::ams::fssrv::impl::FileSystemInterfaceAdapter>(std::move(fs), false);
    }

    ::ams::Result RegisterSession(const ::ams::os::NativeHandle session_handle, ::ams::sf::cmif::ServiceObjectHolder &&obj) {
        return g_Manager.RegisterSession(session_handle, std::move(obj));
    }

}
