#include <ul/system/smi/sf/sf_IPrivateService.hpp>
#include <ul/system/la/la_LibraryApplet.hpp>
#include <ul/system/app/app_ControlCache.hpp>
#include <ul/util/util_Scope.hpp>
#include <queue>

extern ul::RecursiveMutex g_MenuMessageQueueLock;
extern std::queue<ul::smi::MenuMessageContext> *g_MenuMessageQueue;

namespace ul::system::smi::sf {

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

    ::ams::Result PrivateService::TryPopMessageContext(::ams::sf::Out<SfMenuMessageContext> &out_msg_ctx) {
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
            out_msg_ctx.SetValue({ last_msg_ctx });
            return ResultSuccess;
        }
    }

    ::ams::Result PrivateService::QueryApplicationNacpMetadata(::ams::sf::Out<SfNacpMetadata> &out_nacp, const u64 app_id) {
        if(!this->initialized) {
            return ResultInvalidProcess;
        }

        UL_LOG_INFO("PrivateService::QueryApplicationNacpMetadata (app_id: 0x%016lX)", app_id);
        
        if(app::IsQueryLocked()) {
            return ResultApplicationCacheBusy;
        }

        NacpMetadata meta;
        if(!app::QueryApplicationNacp(app_id, meta)) {
            return ResultApplicationNotCached;
        }

        app::ApplicationNacpMisc nacp_misc;
        if(!app::QueryApplicationNacpMisc(app_id, nacp_misc)) {
            return ResultApplicationNotCached;
        }

        auto out_name = out_nacp.GetPointer()->name;
        strncpy(out_name, meta.name, sizeof(NacpMetadata::name) - 1);
        out_name[sizeof(NacpMetadata::name) - 1] = '\0';

        auto out_author = out_nacp.GetPointer()->author;
        strncpy(out_author, meta.author, sizeof(NacpMetadata::author) - 1);
        out_author[sizeof(NacpMetadata::author) - 1] = '\0';

        auto out_display_version = out_nacp.GetPointer()->display_version;
        strncpy(out_display_version, nacp_misc.display_version, sizeof(NacpMetadata::display_version) - 1);
        out_display_version[sizeof(NacpMetadata::display_version) - 1] = '\0';

        return ResultSuccess;
    }

    ::ams::Result PrivateService::QueryApplicationIcon(::ams::sf::OutBuffer &out_icon, ::ams::sf::Out<size_t> &out_icon_size, const u64 app_id) {
        if(!this->initialized) {
            return ResultInvalidProcess;
        }

        UL_LOG_INFO("PrivateService::QueryApplicationIcon (app_id: 0x%016lX)", app_id);

        if(app::IsQueryLocked()) {
            return ResultApplicationCacheBusy;
        }

        size_t actual_icon_size;
        if(!app::QueryApplicationIcon(app_id, out_icon.GetPointer(), out_icon.GetSize(), actual_icon_size)) {
            return ResultApplicationNotCached;
        }
        out_icon_size.SetValue(actual_icon_size);

        return ResultSuccess;
    }

}
