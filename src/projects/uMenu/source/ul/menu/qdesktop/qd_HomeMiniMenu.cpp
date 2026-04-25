// qd_HomeMiniMenu.cpp — implementation of qd_HomeMiniMenu.hpp.
//
// Toggle storage: file-static std::atomic<bool> with default-true. Reads
// from render code use memory_order_relaxed (toggle is a hint, not a
// safety boundary — the render path tolerates one stale frame). The
// dialog write uses memory_order_release so the next render pass picks
// up the change immediately.
//
// UI: pu::ui::Application::CreateShowDialog wrapped in a re-show loop. Each
// loop iteration rebuilds the option vector with the current toggle state
// rendered into the option label (e.g. "Wallpaper [ON]"). Selecting a
// toggle option flips the bool and re-shows; selecting an action option
// dispatches and exits. The "Close" option is wired as the dialog's cancel
// option per CreateShowDialog's `use_last_opt_as_cancel`.

#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/ul_Result.hpp>

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

    // ── Toggles (definitions of the externs in the header) ────────────────
    std::atomic<bool> g_dev_wallpaper_enabled        { true };
    std::atomic<bool> g_dev_brand_fade_enabled       { true };
    std::atomic<bool> g_dev_dock_enabled             { true };
    std::atomic<bool> g_dev_icons_enabled            { true };
    std::atomic<bool> g_dev_topbar_enabled           { true };
    std::atomic<bool> g_dev_volume_policy_enabled    { true };

    namespace {

        // Returns "ON" / "OFF" string for the toggle's current state.
        const char *StateStr(const std::atomic<bool> &t) {
            return t.load(std::memory_order_acquire) ? "ON" : "OFF";
        }

        // Builds the option list for the dialog. Order matters: the index
        // returned from DisplayDialog is mapped back to a toggle/action below.
        std::vector<std::string> BuildOptions() {
            std::vector<std::string> opts;
            opts.reserve(9);
            opts.push_back(std::string("Wallpaper      [") + StateStr(g_dev_wallpaper_enabled)     + "]");
            opts.push_back(std::string("Brand fade     [") + StateStr(g_dev_brand_fade_enabled)    + "]");
            opts.push_back(std::string("Dock           [") + StateStr(g_dev_dock_enabled)          + "]");
            opts.push_back(std::string("Desktop icons  [") + StateStr(g_dev_icons_enabled)         + "]");
            opts.push_back(std::string("Top bar        [") + StateStr(g_dev_topbar_enabled)        + "]");
            opts.push_back(std::string("Volume policy  [") + StateStr(g_dev_volume_policy_enabled) + "]");
            opts.push_back(std::string("Restart uMenu"));            // index 6
            opts.push_back(std::string("Reload theme"));             // index 7
            opts.push_back(std::string("Close"));                    // index 8 = cancel
            return opts;
        }

    }  // namespace

    void ShowDevMenu() {
        if (g_MenuApplication == nullptr) {
            UL_LOG_WARN("qdesktop: ShowDevMenu — g_MenuApplication null, skipping");
            return;
        }

        UL_LOG_INFO("qdesktop: ShowDevMenu opened");

        bool keep_open = true;
        while (keep_open) {
            const auto opts = BuildOptions();
            const auto opt = g_MenuApplication->CreateShowDialog(
                "Q OS — Dev Menu",
                "Press a toggle to flip ON ↔ OFF. Some changes apply on next "
                "render frame; others (e.g. icons) require Restart uMenu to "
                "fully take effect.",
                opts,
                /*use_last_opt_as_cancel=*/true,
                /*icon=*/{}
            );

            // CreateShowDialog returns:
            //   -1 if user cancelled (B button or "Close")
            //   -2 if dialog had no Ok option (defensive; treated as close)
            //   >=0 otherwise = picked option index
            if (opt < 0) {
                UL_LOG_INFO("qdesktop: ShowDevMenu — cancelled / closed");
                break;
            }

            switch (opt) {
                case 0:
                    g_dev_wallpaper_enabled.store(
                        !g_dev_wallpaper_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle wallpaper -> %s",
                                StateStr(g_dev_wallpaper_enabled));
                    break;
                case 1:
                    g_dev_brand_fade_enabled.store(
                        !g_dev_brand_fade_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle brand_fade -> %s",
                                StateStr(g_dev_brand_fade_enabled));
                    break;
                case 2:
                    g_dev_dock_enabled.store(
                        !g_dev_dock_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle dock -> %s",
                                StateStr(g_dev_dock_enabled));
                    break;
                case 3:
                    g_dev_icons_enabled.store(
                        !g_dev_icons_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle icons -> %s",
                                StateStr(g_dev_icons_enabled));
                    break;
                case 4:
                    g_dev_topbar_enabled.store(
                        !g_dev_topbar_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle topbar -> %s",
                                StateStr(g_dev_topbar_enabled));
                    break;
                case 5:
                    g_dev_volume_policy_enabled.store(
                        !g_dev_volume_policy_enabled.load(std::memory_order_acquire),
                        std::memory_order_release);
                    UL_LOG_INFO("qdesktop: dev toggle volume_policy -> %s",
                                StateStr(g_dev_volume_policy_enabled));
                    break;
                case 6: {
                    // Restart uMenu — uses the existing SMI RestartMenu path,
                    // identical to what the upstream Settings panel does.
                    // reload_theme_cache=true so the post-restart boot picks
                    // up any theme files that were swapped on the SD.
                    UL_LOG_INFO("qdesktop: dev action — Restart uMenu");
                    const Result rc = ::ul::menu::smi::RestartMenu(true);
                    if (R_FAILED(rc)) {
                        UL_LOG_WARN("qdesktop: RestartMenu rc=0x%X — staying open",
                                    static_cast<unsigned>(rc));
                    }
                    keep_open = false;
                    break;
                }
                case 7: {
                    // Reload theme — refresh the procedural assets that depend
                    // on theme constants. The brand fade texture is the only
                    // one currently cacheable; release it so the next fade
                    // re-runs WritePixel and picks up any future palette tweak.
                    UL_LOG_INFO("qdesktop: dev action — Reload theme (clear brand fade cache)");
                    // Forward-declared symbol from qd_Transition.cpp:
                    extern void ReleaseBrandFadeTexture();
                    ReleaseBrandFadeTexture();
                    g_MenuApplication->ShowNotification(
                        "Brand fade cache cleared — next transition regenerates",
                        2000);
                    break;
                }
                case 8:
                    // "Close" — also bound as cancel; reaching it via the
                    // cancel option produces opt = -1 above.  This case is
                    // defensive (some dialog implementations report the
                    // cancel index as a positive return).
                    keep_open = false;
                    break;
                default:
                    UL_LOG_WARN("qdesktop: ShowDevMenu unexpected opt=%d — closing", opt);
                    keep_open = false;
                    break;
            }
        }

        UL_LOG_INFO("qdesktop: ShowDevMenu closed");
    }

}  // namespace ul::menu::qdesktop
