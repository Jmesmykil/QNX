#include <ul/menu/smi/smi_PrivateService.hpp>
#include <ul/sf/sf_Base.hpp>
#include <ul/util/util_Scope.hpp>
#include <atomic>

namespace ul::menu::smi {

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

        inline Result psrvQueryApplicationNacp(Service *srv, NacpStruct *out_nacp_buf, const u64 app_id) {
            return serviceDispatchIn(srv, 2, app_id,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { out_nacp_buf, sizeof(NacpStruct) } },
            );
        }

        inline Result psrvQueryApplicationIcon(Service *srv, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size, const u64 app_id) {
            size_t got_icon_size;
            UL_RC_TRY(serviceDispatchInOut(srv, 3, app_id, got_icon_size,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { out_icon_buf, icon_size } }
            ));
            out_icon_size = got_icon_size;
            return ResultSuccess;
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
                        ScopedLock lk(g_CallbackTableLock);

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
        UL_RC_TRY(threadCreate(&g_ReceiverThread, &MenuMessageReceiverThread, nullptr, nullptr, 0x1000, 49, -2));
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

    Result QueryApplicationNacp(const u64 app_id, NacpStruct *out_nacp_buf) {
        return psrvQueryApplicationNacp(&g_PrivateService, out_nacp_buf, app_id);
    }

    Result QueryApplicationIcon(const u64 app_id, u8 *out_icon_buf, const size_t icon_size, size_t &out_icon_size) {
        return psrvQueryApplicationIcon(&g_PrivateService, out_icon_buf, icon_size, out_icon_size, app_id);
    }

    void RegisterOnMessageDetect(OnMessageCallback callback, const MenuMessage desired_msg) {
        ScopedLock lk(g_CallbackTableLock);

        g_MessageCallbackTable.push_back({ callback, desired_msg });
    }

}
