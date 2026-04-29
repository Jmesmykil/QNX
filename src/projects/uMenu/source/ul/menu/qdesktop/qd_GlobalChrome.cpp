// qd_GlobalChrome.cpp — persistent top-bar + dock backdrop for v1.9.
//
// Render contract (see header for full Z-order notes):
//   RenderTopBar — called from the Application-level AddRenderCallback,
//                  BEFORE Plutonium elements fire.  Renders the translucent
//                  top-bar strip and the 96x72 hot-corner widget geometry.
//   RenderDock   — called immediately after RenderTopBar each frame.
//                  Renders the dock panel backdrop only.  Dock slot icons,
//                  magnification, and hit-testing stay in QdDesktopIconsElement.
//   HandleInput  — called once per frame, BEFORE the active layout's OnInput.
//                  Currently returns false (no button-driven chrome actions).
//
// No SDL_Texture is allocated here.  All draws are SDL_RenderFillRect geometry
// (solid / blended rectangles).  This avoids the B41/B42 pu::ui::render::
// RenderText + DeleteTexture lifecycle entirely.

#ifdef QDESKTOP_MODE

#include <ul/menu/qdesktop/qd_GlobalChrome.hpp>
#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>   // g_dev_topbar_enabled, g_dev_dock_enabled
#include <ul/menu/qdesktop/qd_Launchpad.hpp>       // LP_HOTCORNER_W, LP_HOTCORNER_H
#include <ul/menu/ui/ui_MenuApplication.hpp>       // MenuType enum (full definition)

namespace ul::menu::qdesktop {

// ── constants mirrored from qd_DesktopIcons.cpp ──────────────────────────────
// kDockNominalTop and kDockH match the DesktopIcons authoritative values
// (v1.8.3 B35 reverted kDockH to 148; qd_WmConstants.hpp DOCK_H=108 is a
// legacy value and must NOT be used here).

static constexpr int32_t kChromeDockH          = 148;
static constexpr int32_t kChromeDockNominalTop  = 1080 - kChromeDockH;  // 932

// Top-bar height — matches qd_WmConstants.hpp TOPBAR_H and the local
// TOPBAR_H_PX literal in qd_DesktopIcons.cpp.
static constexpr int32_t kChromeTopBarH = 48;

// Hot-corner dimensions — LP_HOTCORNER_W/H are the authoritative values
// (defined in qd_Launchpad.hpp, consumed by DesktopIcons hit-test).
static constexpr int32_t kHcW = LP_HOTCORNER_W;  // 96
static constexpr int32_t kHcH = LP_HOTCORNER_H;  // 72

// ── SetMenuTypeRef ────────────────────────────────────────────────────────────

void QdGlobalChrome::SetMenuTypeRef(const ul::menu::ui::MenuType *menu_type_ref) {
    menu_type_ref_ = menu_type_ref;
}

// ── IsSuppressed ─────────────────────────────────────────────────────────────
// Returns true when the current layout owns its own chrome (lock/startup
// screens).  Launchpad is a full-screen takeover that naturally occludes the
// chrome strips; it does NOT need explicit suppression here.

bool QdGlobalChrome::IsSuppressed() const {
    if (menu_type_ref_ == nullptr) {
        return false;
    }
    switch (*menu_type_ref_) {
        case ul::menu::ui::MenuType::Startup:
        case ul::menu::ui::MenuType::Lockscreen:
        case ul::menu::ui::MenuType::QLockscreen:
            return true;
        default:
            return false;
    }
}

// ── RenderTopBar ──────────────────────────────────────────────────────────────
// Renders at y=0..kChromeTopBarH (the "Bridge").
//   Pass 1: translucent black background strip.
//   Pass 2: 1-px white hairline at the bottom edge of the strip.
//   Pass 3: hot-corner widget geometry (solid dark rectangle + coloured
//           accent borders) at (0,0) kHcW×kHcH.
//
// Called from AddRenderCallback (step 1 of Plutonium's render loop), so it
// renders UNDER Plutonium's battery/time/connection elements (step 7).

void QdGlobalChrome::RenderTopBar(SDL_Renderer *r) {
    if (IsSuppressed()) {
        return;
    }
    if (!g_dev_topbar_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    // -- Pass 1: translucent top-bar background (matches removed DesktopIcons block)
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0xB0u);
    SDL_Rect topbar_bg { 0, 0, 1920, kChromeTopBarH };
    SDL_RenderFillRect(r, &topbar_bg);

    // -- Pass 2: 1-px bottom border hairline
    SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x30u);
    SDL_Rect topbar_border { 0, kChromeTopBarH - 1, 1920, 1 };
    SDL_RenderFillRect(r, &topbar_border);

    // v1.9.2: Hot-corner widget paint (bg + cyan accents + Q-glyph) MOVED to
    // QdDesktopIconsElement::OnRender (end-of-frame, before help overlay).
    // Reason: the chrome's RenderTopBar runs in render-step 1 (before Plutonium
    // elements paint) so desktop content was overpainting the widget, hiding
    // the Q-glyph entirely on v1.9.1 HW test.  The chrome retains the global
    // top-bar strip + 1-px hairline; the widget is desktop-owned now.

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// ── RenderDock ────────────────────────────────────────────────────────────────
// Renders the dock panel backdrop at y=kChromeDockNominalTop..1080 (the
// "Deck").  Dock slot icons, hover magnification, and tap hit-testing remain
// in QdDesktopIconsElement.

void QdGlobalChrome::RenderDock(SDL_Renderer *r) {
    if (IsSuppressed()) {
        return;
    }
    if (!g_dev_dock_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    // Translucent backdrop
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0x00u, 0x00u, 0x00u, 0x60u);
    SDL_Rect dock_panel { 0, kChromeDockNominalTop, 1920, kChromeDockH };
    SDL_RenderFillRect(r, &dock_panel);

    // 1-px top border hairline
    SDL_SetRenderDrawColor(r, 0xFFu, 0xFFu, 0xFFu, 0x40u);
    SDL_Rect dock_border { 0, kChromeDockNominalTop, 1920, 1 };
    SDL_RenderFillRect(r, &dock_border);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// ── HandleInput ───────────────────────────────────────────────────────────────
// Called once per frame, BEFORE the active layout's OnInput runs.
// Hot-corner touch detection is NOT handled here — HandleInput receives key
// bitmasks only (u64), not touch coordinates.  Touch hit-testing for the
// hot corner stays in QdDesktopIconsElement::OnInput (the unchanged path).
// Returns false (does not consume input).

bool QdGlobalChrome::HandleInput([[maybe_unused]] u64 keys_down,
                                 [[maybe_unused]] u64 keys_held) {
    if (IsSuppressed()) {
        return false;
    }
    return false;
}

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
