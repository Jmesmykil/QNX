#include <ul/menu/smi/sf/sf_PrivateService.hpp>
#include <ul/util/util_Scope.hpp>
#include <atomic>

namespace ul::menu::smi::sf {

    namespace {

        inline Result psrvInitialize(Service *srv) {
            u64 pid_placeholder = 0;
            return serviceDispatchIn(srv, 0, pid_placeholder,
                .in_send_pid = true
            );
        }

        inline Result psrvTryPopMessageContext(Service *srv, MenuMessageContext *out_msg_ctx) {
            return serviceDispatch(srv, 1,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { out_msg_ctx, sizeof(MenuMessageContext) } },
            );
        }

        Service g_PrivateService;

        Result InitializePrivateServiceImpl() {
            if(serviceIsActive(&g_PrivateService)) {
                return ResultSuccess;
            }

            UL_RC_TRY(smGetService(&g_PrivateService, sf::PrivateServiceName));
            UL_RC_TRY(psrvInitialize(&g_PrivateService));

            return ResultSuccess;
        }

        void FinalizePrivateServiceImpl() {
            serviceClose(&g_PrivateService);
        }

        inline Result TryPopMessageContext(MenuMessageContext *out_msg_ctx) {
            return psrvTryPopMessageContext(&g_PrivateService, out_msg_ctx);
        }

    }

    namespace {

        bool g_Initialized = false;
        std::atomic_bool g_ReceiverThreadShouldStop = false;
        Thread g_ReceiverThread;
        std::vector<std::pair<OnMessageCallback, MenuMessage>> g_MessageCallbackTable;
        Mutex g_CallbackTableLock = {};

        void MenuMessageReceiverThread(void*) {
            while(true) {
                if(g_ReceiverThreadShouldStop) {
                    break;
                }

                {
                    MenuMessageContext last_msg_ctx;
                    if(R_SUCCEEDED(TryPopMessageContext(&last_msg_ctx))) {
                        UL_LOG_INFO("[PrivateService] Received message of type %d", (u32)last_msg_ctx.msg);
                        ScopedLock lk(g_CallbackTableLock);
                        UL_LOG_INFO("[PrivateService] Dispatching to %zu callbacks...", g_MessageCallbackTable.size());

                        for(const auto &[cb, msg] : g_MessageCallbackTable) {
                            if((msg == MenuMessage::Invalid) || (msg == last_msg_ctx.msg)) {
                                cb(last_msg_ctx);
                            }
                        }
                    }
                }

                svcSleepThread(10'000'000ul);
            }
        }

    }

    Result InitializePrivateService() {
        if(g_Initialized) {
            return ResultSuccess;
        }

        UL_RC_TRY(InitializePrivateServiceImpl());

        g_ReceiverThreadShouldStop = false;
        // Q OS cycle D7 (SP4.12.1): bump receiver-thread stack from 4 KB to
        // 16 KB.  At 4 KB the path through ScopedLock → UL_LOG_INFO →
        // ul::LogImpl → ul::tel::Emit → FormatLine → vsnprintf
        // peaks around 2.0–2.2 KB of stack frames per log call (LogImpl
        // tel_buf[512] + Emit line[SlotSize=512] + FormatLine msg[480] +
        // newlib's _svfprintf_r internal buffers).  With two UL_LOG_INFO
        // calls in the receive loop (lines 63 and 65) and ~1 KB of thread
        // overhead, we sat right on the 4 KB ceiling; SP4.12 hardware
        // boots crashed in _svfprintf_r with SP at 0x32a8993d40 — 704 bytes
        // BELOW the stack region (overflow into unmapped page, fault
        // address = 0).  16 KB matches the libnx default and gives ~12 KB
        // headroom over current peak.
        UL_RC_TRY(threadCreate(&g_ReceiverThread, &MenuMessageReceiverThread, nullptr, nullptr, 0x4000, 49, -2));
        UL_RC_TRY(threadStart(&g_ReceiverThread));

        g_Initialized = true;
        return ResultSuccess;
    }

    void FinalizePrivateService() {
        if(!g_Initialized) {
            return;
        }
        
        g_ReceiverThreadShouldStop = true;
        threadWaitForExit(&g_ReceiverThread);
        threadClose(&g_ReceiverThread);

        FinalizePrivateServiceImpl();
        g_Initialized = false;
    }

    void RegisterOnMessageDetect(OnMessageCallback callback, const MenuMessage desired_msg) {
        ScopedLock lk(g_CallbackTableLock);

        g_MessageCallbackTable.push_back({ callback, desired_msg });
    }

}
