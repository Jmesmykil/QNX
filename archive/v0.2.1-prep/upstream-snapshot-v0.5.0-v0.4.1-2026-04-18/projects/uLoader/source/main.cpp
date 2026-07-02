#include <ul/loader/loader_SelfProcess.hpp>
#include <ul/loader/loader_Target.hpp>
#include <ul/loader/loader_Input.hpp>
#include <ul/loader/loader_ProgramIdUtils.hpp>
#include <ul/ul_Result.hpp>
#include <ul/util/util_Scope.hpp>
#include <ul/util/util_Size.hpp>

using namespace ul::util::size;

namespace {

    constexpr auto HbloaderSettingsSectionName = "hbloader";

    template<typename T>
    inline Result GetHbloaderSetting(const char *key, T &out_value) {
        u64 setting_size;
        UL_RC_TRY(setsysGetSettingsItemValue(HbloaderSettingsSectionName, key, std::addressof(out_value), sizeof(out_value), &setting_size));

        if(setting_size != sizeof(out_value)) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        
        return ul::ResultSuccess;
    }

    constexpr size_t HeapSize = 64_KB;
    u8 g_Heap[HeapSize] = {};

}

extern "C" {

    u32 __nx_applet_exit_mode = 2;

    u32 __nx_fs_num_sessions = 1;
    u32 __nx_fsdev_direntry_cache_size = 1;
    bool __nx_fsdev_support_cwd = false;

    extern u8 *fake_heap_start;
    extern u8 *fake_heap_end;

    void __libnx_initheap() {
        fake_heap_start = g_Heap;
        fake_heap_end = g_Heap + HeapSize;
    }

    void __appInit() {}
    void __appExit() {}

}

int main() {
    UL_RC_ASSERT(smInitialize());

    UL_RC_ASSERT(fsInitialize());
    UL_RC_ASSERT(fsdevMountSdmc());

    ul::InitializeLogging("uLoader");

    UL_RC_ASSERT(setsysInitialize());
    
    SetSysFirmwareVersion fw_ver;
    UL_RC_ASSERT(setsysGetFirmwareVersion(&fw_ver));
    // Atmosphere is always assumed to be present (was used to launch us actually :P)
    hosversionSet(MAKEHOSVERSION(fw_ver.major, fw_ver.minor, fw_ver.micro) | BIT(31));

    u64 applet_heap_size;
    UL_RC_ASSERT(GetHbloaderSetting("applet_heap_size", applet_heap_size));
    u64 applet_heap_reservation_size;
    UL_RC_ASSERT(GetHbloaderSetting("applet_heap_reservation_size", applet_heap_reservation_size));

    setsysExit();

    u64 self_program_id;
    UL_RC_ASSERT(ul::loader::GetSelfProgramId(self_program_id));
    ul::loader::DetermineSelfAppletType(self_program_id);

    ul::loader::TargetInput target_ipt;
    UL_RC_ASSERT(ul::loader::ReadTargetInput(target_ipt));

    UL_LOG_INFO("Targetting '%s' with argv '%s' (once: %d)", target_ipt.nro_path, target_ipt.nro_argv, target_ipt.target_once);

    fsdevUnmountAll();
    fsExit();
    smExit();

    ul::loader::Target(target_ipt, applet_heap_size, applet_heap_reservation_size);
}
