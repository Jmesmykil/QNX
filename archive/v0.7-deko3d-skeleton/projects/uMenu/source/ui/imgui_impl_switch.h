// imgui_impl_switch.h — ImGui Switch HID input backend for Q OS uMenu v0.7
// Wraps hidGetNpadStateHandheld for LibraryApplet usage.
// HOME button detection: ImGui_ImplSwitch_NewFrame() returns false to signal exit.
#pragma once

/// \brief Initialize Switch HID backend.
/// Must be called after ImGui::CreateContext() and ImGui_ImplDeko3d_Init().
bool ImGui_ImplSwitch_Init();

/// \brief Shut down Switch HID backend.
void ImGui_ImplSwitch_Shutdown();

/// \brief Poll input and feed into ImGui IO for the current frame.
/// \return false if the HOME/Plus button was pressed and the applet should exit.
bool ImGui_ImplSwitch_NewFrame();
