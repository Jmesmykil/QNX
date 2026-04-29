// qd_MonitorHostLayout.cpp — see qd_MonitorHostLayout.hpp for the why.

#include <ul/menu/qdesktop/qd_MonitorHostLayout.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/ul_Result.hpp>

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

    QdMonitorHostLayout::QdMonitorHostLayout(QdMonitorLayout::Ref monitor_element)
        : monitor_element_(monitor_element) {
        this->SetBackgroundColor({ 0, 0, 0, 255 });
        this->Add(this->monitor_element_);
        // v1.9.7: hot-corner overlay painted above the monitor panel.
        this->overlay_ = QdHotCornerOverlay::New();
        this->Add(this->overlay_);
    }

    void QdMonitorHostLayout::OnMenuInput(const u64 keys_down,
                                          const u64 keys_up,
                                          const u64 keys_held,
                                          const pu::ui::TouchPoint touch_pos) {
        // QdMonitorLayout (the Element) handles its own input via its OnInput
        // override; the base pu::ui::Layout::OnInput dispatch already routes here
        // through the child Element list, so OnMenuInput stays empty for the host.
        (void)keys_down;
        (void)keys_up;
        (void)keys_held;
        (void)touch_pos;
    }

    bool QdMonitorHostLayout::OnHomeButtonPress() {
        UL_LOG_INFO("monitor: OnHomeButtonPress -> returning to MainMenu");
        g_MenuApplication->LoadMenu(ul::menu::ui::MenuType::Main);
        return true;
    }

    void QdMonitorHostLayout::LoadSfx() {
        // Monitor has no per-layout sfx today.  When sfx are added, load them here.
    }

    void QdMonitorHostLayout::DisposeSfx() {
        // Mirrors LoadSfx; kept symmetric for future sfx work.
    }

}
