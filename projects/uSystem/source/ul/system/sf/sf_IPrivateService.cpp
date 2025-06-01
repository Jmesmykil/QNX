#include <ul/system/sf/sf_IPrivateService.hpp>
#include <ul/system/la/la_LibraryApplet.hpp>
#include <ul/system/app/app_Cache.hpp>
#include <queue>

extern ul::RecursiveMutex g_MenuMessageQueueLock;
extern std::queue<ul::smi::MenuMessageContext> *g_MenuMessageQueue;

namespace ul::system::sf {

    ::ams::Result PrivateService::Initialize(const ::ams::sf::ClientProcessId &client_pid) {
        if(!this->initialized) {
            u64 program_id = 0;
            UL_RC_TRY(pminfoInitialize());
            UL_RC_TRY(pminfoGetProgramId(&program_id, client_pid.process_id.value));
            pminfoExit();

            const auto last_menu_program_id = la::GetMenuProgramId();
            // If Menu hasn't been launched it's program ID will be 0 (invalid), thus a single (program_id != last_menu_program_id) check isn't enough
            // If any of the IDs is invalid, something unexpected is happening...
            if((last_menu_program_id == 0) || (program_id == 0) || (program_id != last_menu_program_id)) {
                return ResultInvalidProcess;
            }

            this->initialized = true;
        }

        return ResultSuccess;
    }

    ::ams::Result PrivateService::TryPopMessageContext(::ams::sf::Out<MenuMessageContext> &out_msg_ctx) {
        if(!this->initialized) {
            return ResultInvalidProcess;
        }

        ScopedLock lk(g_MenuMessageQueueLock);
        if(g_MenuMessageQueue->empty()) {
            return ResultNoMessagesAvailable;
        }
        else {
            const auto last_msg_ctx = g_MenuMessageQueue->front();
            g_MenuMessageQueue->pop();
            out_msg_ctx.SetValue({ .actual_ctx = last_msg_ctx });
            return ResultSuccess;
        }
    }

    ::ams::Result PrivateService::QueryApplicationNacp(::ams::sf::Out<Nacp> &out_nacp, const u64 app_id) {
        if(!this->initialized) {
            return ResultInvalidProcess;
        }

        if(app::IsQueryLocked()) {
            return ResultApplicationCacheBusy;
        }

        if(!app::QueryApplicationNacp(app_id, reinterpret_cast<NacpStruct*>(out_nacp.GetPointer()))) {
            return ResultApplicationNotCached;
        }

        return ResultSuccess;
    }

    ::ams::Result PrivateService::QueryApplicationIcon(::ams::sf::OutBuffer &out_icon, ::ams::sf::Out<size_t> &out_icon_size, const u64 app_id) {
        if(!this->initialized) {
            return ResultInvalidProcess;
        }

        if(app::IsQueryLocked()) {
            return ResultApplicationCacheBusy;
        }

        size_t icon_size;
        if(!app::QueryApplicationIcon(app_id, out_icon.GetPointer(), out_icon.GetSize(), icon_size)) {
            return ResultApplicationNotCached;
        }

        out_icon_size.SetValue(icon_size);
        return ResultSuccess;
    }

}
