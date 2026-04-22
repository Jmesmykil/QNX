// imgui_impl_switch.cpp — ImGui Switch HID input backend for Q OS uMenu v0.7
// Adapted from ftpd/mtheall (MIT) — stripped to LibraryApplet handheld mode.
// Updated for ImGui 1.92.x API (io.AddKeyEvent / io.AddKeyAnalogEvent; no NavInputs[]).
// uMenu runs as AppletType_LibraryApplet; we use hidGetNpadStatesHandheld only.

#include "imgui_impl_switch.h"
#include "../imgui/imgui.h"
#include <switch.h>

// ──────────────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────────────

static constexpr float FB_W = 1280.0f;
static constexpr float FB_H = 720.0f;

// Analog stick dead-zone (normalised 0..1 mapped from -32767..32767)
static constexpr float STICK_DEAD = 0.20f;

// ──────────────────────────────────────────────────────────────────────────────
// Module state
// ──────────────────────────────────────────────────────────────────────────────

static bool                g_Initialised = false;
static HidNpadHandheldState g_PrevPad    = {};

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static inline float StickAxis(s32 raw)
{
    return static_cast<float>(raw) / 32767.0f;
}

// Apply dead zone: returns clamped [-1,1] value if outside zone, else 0.
static inline float DeadZone(float v)
{
    if (v > STICK_DEAD)  return (v - STICK_DEAD) / (1.0f - STICK_DEAD);
    if (v < -STICK_DEAD) return (v + STICK_DEAD) / (1.0f - STICK_DEAD);
    return 0.0f;
}

// Emit a boolean gamepad key event.
static inline void NavKey(ImGuiIO &io, ImGuiKey key, bool pressed)
{
    io.AddKeyEvent(key, pressed);
}

// Emit an analog gamepad key event (ImGuiKey_GamepadLStick* etc.)
static inline void NavAxis(ImGuiIO &io, ImGuiKey key, float value)
{
    // AddKeyAnalogEvent maps to the key's analog lane; negative side needs its own key.
    io.AddKeyAnalogEvent(key, value > 0.0f, value > 0.0f ? value : 0.0f);
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

bool ImGui_ImplSwitch_Init()
{
    if (g_Initialised)
        return true;

    hidInitializeTouchScreen();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_switch";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    g_Initialised = true;
    return true;
}

void ImGui_ImplSwitch_Shutdown()
{
    g_Initialised = false;
    g_PrevPad     = {};
}

bool ImGui_ImplSwitch_NewFrame()
{
    ImGuiIO &io = ImGui::GetIO();

    // ── Display size ─────────────────────────────────────────────────────────
    io.DisplaySize             = ImVec2(FB_W, FB_H);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // ── Delta time ───────────────────────────────────────────────────────────
    static u64 s_LastTick = 0;
    u64 now = armGetSystemTick();
    if (s_LastTick == 0)
        s_LastTick = now;
    u64 delta_ticks = now - s_LastTick;
    s_LastTick      = now;
    // Erista: 19.2 MHz system tick
    float dt = static_cast<float>(delta_ticks) / 19200000.0f;
    if (dt <= 0.0f || dt > 0.25f)
        dt = 1.0f / 60.0f;
    io.DeltaTime = dt;

    // ── Touch input ──────────────────────────────────────────────────────────
    HidTouchScreenState touch = {};
    hidGetTouchScreenStates(&touch, 1);
    if (touch.count > 0) {
        io.AddMousePosEvent(
            static_cast<float>(touch.touches[0].x),
            static_cast<float>(touch.touches[0].y));
        io.AddMouseButtonEvent(0, true);
    } else {
        io.AddMouseButtonEvent(0, false);
    }

    // ── NPAD Handheld ────────────────────────────────────────────────────────
    HidNpadHandheldState pad = {};
    hidGetNpadStatesHandheld(HidNpadIdType_Handheld, &pad, 1);

    u64 held    = pad.buttons;
    u64 prev    = g_PrevPad.buttons;
    g_PrevPad   = pad;

    // ── HOME / Plus → exit applet ────────────────────────────────────────────
    // HOME is caught by appletMainLoop() returning false.
    // Plus (START-equivalent) is our explicit "exit skeleton" shortcut.
    if (held & HidNpadButton_Plus)
        return false;

    // ── Gamepad navigation mapping — ImGui 1.87+ key event API ───────────────
    // Nintendo Switch → ImGui gamepad layout (Nintendo face-button convention):
    //   A = confirm/down  → GamepadFaceDown
    //   B = cancel/right  → GamepadFaceRight
    //   X = menu/left     → GamepadFaceLeft
    //   Y = up            → GamepadFaceUp
    NavKey(io, ImGuiKey_GamepadFaceDown,  held & HidNpadButton_A);
    NavKey(io, ImGuiKey_GamepadFaceRight, held & HidNpadButton_B);
    NavKey(io, ImGuiKey_GamepadFaceLeft,  held & HidNpadButton_X);
    NavKey(io, ImGuiKey_GamepadFaceUp,    held & HidNpadButton_Y);
    NavKey(io, ImGuiKey_GamepadDpadLeft,  held & HidNpadButton_Left);
    NavKey(io, ImGuiKey_GamepadDpadRight, held & HidNpadButton_Right);
    NavKey(io, ImGuiKey_GamepadDpadUp,    held & HidNpadButton_Up);
    NavKey(io, ImGuiKey_GamepadDpadDown,  held & HidNpadButton_Down);
    NavKey(io, ImGuiKey_GamepadL1,        held & HidNpadButton_L);
    NavKey(io, ImGuiKey_GamepadR1,        held & HidNpadButton_R);
    NavKey(io, ImGuiKey_GamepadL2,        held & HidNpadButton_ZL);
    NavKey(io, ImGuiKey_GamepadR2,        held & HidNpadButton_ZR);
    NavKey(io, ImGuiKey_GamepadStart,     held & HidNpadButton_Minus);

    // Left stick analog
    float lx = DeadZone(StickAxis(pad.analog_stick_l.x));
    float ly = DeadZone(StickAxis(pad.analog_stick_l.y));

    NavAxis(io, ImGuiKey_GamepadLStickLeft,  lx < 0.0f ? -lx : 0.0f);
    NavAxis(io, ImGuiKey_GamepadLStickRight, lx > 0.0f ?  lx : 0.0f);
    NavAxis(io, ImGuiKey_GamepadLStickUp,    ly > 0.0f ?  ly : 0.0f);
    NavAxis(io, ImGuiKey_GamepadLStickDown,  ly < 0.0f ? -ly : 0.0f);

    (void)prev;  // reserved for future key-press events

    return true;
}
