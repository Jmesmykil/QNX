
#pragma once
#include <ul/loader/loader_TargetTypes.hpp>
#include <ul/menu/smi/smi_MenuProtocol.hpp>

namespace ul::menu::smi {

    inline Result SetSelectedUser(const AccountUid &user_id) {
        return SendCommand(SystemMessage::SetSelectedUser,
            [&](ScopedStorageWriter &writer) {
                writer.Push(user_id);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }
    
    inline Result LaunchApplication(const u64 app_id) {
        return SendCommand(SystemMessage::LaunchApplication,
            [&](ScopedStorageWriter &writer) {
                writer.Push(app_id);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result ResumeApplication() {
        return SendCommand(SystemMessage::ResumeApplication,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result TerminateApplication() {
        return SendCommand(SystemMessage::TerminateApplication,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result LaunchHomebrewLibraryApplet(const std::string &nro_path, const std::string &nro_argv) {
        const auto target_ipt = loader::TargetInput::Create(nro_path, nro_argv, false, "");

        return SendCommand(SystemMessage::LaunchHomebrewLibraryApplet,
            [&](ScopedStorageWriter &writer) {
                writer.Push(target_ipt);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    // v3.1 Phase 2 Task 3 Slice 4 (architectural inversion):
    //
    // Server registers the ECS hijack only (ldr:shell uMenu doesn't have).
    // Server pushes back the resolved AppletId so uMenu can call
    // appletCreateLibraryAppletSelf locally — uMenu is the foreground LA
    // and AM permits that path, unlike the original Slice 2 design which
    // tried to launch from background uSystem and hung.
    //
    // After this returns success, the caller should:
    //   AppletHolder h;
    //   appletCreateLibraryAppletSelf(&h, (AppletId)out_applet_id, LibAppletMode_BackgroundIndirect);
    //   libappletPushInData(&h, &target_input, sizeof(target_input));
    //   appletHolderStart(&h);
    //   u64 handle = 0;
    //   appletHolderGetIndirectLayerConsumerHandle(&h, &handle);
    //   …then drive viGetIndirectLayerImageMap(handle) per frame.
    //
    // The caller is responsible for the target_input it pushed (it's the same
    // one the caller passed in here — no need to re-fetch).
    inline Result LaunchHomebrewWindowedLibraryApplet(const std::string &nro_path, const std::string &nro_argv, u32 &out_applet_id) {
        const auto target_ipt = loader::TargetInput::Create(nro_path, nro_argv, false, "");
        out_applet_id = 0;  // clean state if SendCommand fails before reader runs

        return SendCommand(SystemMessage::LaunchHomebrewWindowedLibraryApplet,
            [&](ScopedStorageWriter &writer) {
                writer.Push(target_ipt);
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return reader.Pop(out_applet_id);
            }
        );
    }

    inline Result LaunchHomebrewApplication(const std::string &nro_path, const std::string &nro_argv) {
        const auto target_ipt = loader::TargetInput::Create(nro_path, nro_argv, false, "");

        return SendCommand(SystemMessage::LaunchHomebrewApplication,
            [&](ScopedStorageWriter &writer) {
                writer.Push(target_ipt);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result ChooseHomebrew() {
        return SendCommand(SystemMessage::ChooseHomebrew,
            [&](ScopedStorageWriter &writer) {
                // ...
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenWebPage(const char(&url)[500]) {
        return SendCommand(SystemMessage::OpenWebPage,
            [&](ScopedStorageWriter &writer) {
                writer.PushData(url, sizeof(url));
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenAlbum() {
        return SendCommand(SystemMessage::OpenAlbum,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result RestartMenu(const bool reload_theme_cache) {
        return SendCommand(SystemMessage::RestartMenu,
            [&](ScopedStorageWriter &writer) {
                writer.Push(reload_theme_cache);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result ReloadConfig() {
        return SendCommand(SystemMessage::ReloadConfig,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result UpdateMenuPaths(const char (&menu_fs_path)[FS_MAX_PATH], const char (&menu_path)[FS_MAX_PATH]) {
        return SendCommand(SystemMessage::UpdateMenuPaths,
            [&](ScopedStorageWriter &writer) {
                writer.PushData(menu_fs_path, sizeof(menu_fs_path));
                writer.PushData(menu_path, sizeof(menu_path));
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result UpdateMenuIndex(const u32 menu_index) {
        return SendCommand(SystemMessage::UpdateMenuIndex,
            [&](ScopedStorageWriter &writer) {
                writer.Push(menu_index);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenUserPage() {
        return SendCommand(SystemMessage::OpenUserPage,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenMiiEdit() {
        return SendCommand(SystemMessage::OpenMiiEdit,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenAddUser() {
        return SendCommand(SystemMessage::OpenAddUser,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result OpenNetConnect() {
        return SendCommand(SystemMessage::OpenNetConnect,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result ListAddedApplications(const u32 count, u64 *out_app_buf) {
        return SendCommand(SystemMessage::ListAddedApplications,
            [&](ScopedStorageWriter &writer) {
                writer.Push(count);
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                reader.PopData(out_app_buf, sizeof(u64) * count);
                return ResultSuccess;
            }
        );
    }

    inline Result ListDeletedApplications(const u32 count, u64 *out_app_buf) {
        return SendCommand(SystemMessage::ListDeletedApplications,
            [&](ScopedStorageWriter &writer) {
                writer.Push(count);
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                reader.PopData(out_app_buf, sizeof(u64) * count);
                return ResultSuccess;
            }
        );
    }

    inline Result OpenCabinet(const NfpLaStartParamTypeForAmiiboSettings type) {
        return SendCommand(SystemMessage::OpenCabinet,
            [&](ScopedStorageWriter &writer) {
                writer.Push((u8)type);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result StartVerifyApplication(const u64 app_id) {
        return SendCommand(SystemMessage::StartVerifyApplication,
            [&](ScopedStorageWriter &writer) {
                writer.Push(app_id);
                return ResultSuccess;
            },
            [](ScopedStorageReader &reader) {
                // ...
                return ResultSuccess;
            }
        );
    }

    inline Result ListInVerifyApplications(const u32 count, u64 *out_app_buf) {
        return SendCommand(SystemMessage::ListInVerifyApplications,
            [&](ScopedStorageWriter &writer) {
                writer.Push(count);
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                reader.PopData(out_app_buf, sizeof(u64) * count);
                return ResultSuccess;
            }
        );
    }

    inline Result NotifyWarnedAboutOutdatedTheme() {
        return SendCommand(SystemMessage::NotifyWarnedAboutOutdatedTheme,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return ResultSuccess;
            }
        );
    }

    inline Result TerminateMenu() {
        return SendCommand(SystemMessage::TerminateMenu,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return ResultSuccess;
            }
        );
    }

    inline Result OpenControllerKeyRemapping(const u32 style_set, const HidNpadJoyHoldType hold_type) {
        return SendCommand(SystemMessage::OpenControllerKeyRemapping,
            [&](ScopedStorageWriter &writer) {
                writer.Push(style_set);
                writer.Push(hold_type);
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return ResultSuccess;
            }
        );
    }

    // v2.3.6: Toggle the qlaunch override + reboot.  uSystem renames
    // /atmosphere/contents/0100000000001000/exefs.nsp <-> .nsp.disabled
    // and calls appletRequestToReboot().  When stock qlaunch is active,
    // calling this again restores uSystem.
    inline Result RebootToStockQlaunch() {
        return SendCommand(SystemMessage::RebootToStockQlaunch,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return ResultSuccess;
            }
        );
    }

    // v3.8.x SELF-HEAL WATCHDOG (2026-06-20):
    // Send a liveness heartbeat to uSystem.  Called by uMenu's render loop
    // every ~1800 frames (~30s at 60fps).  uSystem records the arrival tick
    // and will force-recover uMenu if the heartbeat goes stale.
    //
    // Fire-and-forget: we use SendCommand but do NOT block on the reply —
    // the writer and reader lambdas are both no-ops.  If the IPC fails
    // (uSystem is temporarily busy) we simply miss one beat; the watchdog
    // timeout is 30s so a single missed beat at 30s cadence is safe.
    inline Result SendHeartbeat() {
        return SendCommand(SystemMessage::Heartbeat,
            [&](ScopedStorageWriter &writer) {
                return ResultSuccess;
            },
            [&](ScopedStorageReader &reader) {
                return ResultSuccess;
            }
        );
    }

}
