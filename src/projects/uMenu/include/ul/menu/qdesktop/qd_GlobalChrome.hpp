// qd_GlobalChrome.hpp — persistent top-bar + dock background for v1.9.
//
// QdGlobalChrome owns the translucent top-bar strip (y=0..TOPBAR_H) and the
// dock backdrop (y=kDockNominalTop..1080). It is NOT a Plutonium element; it
// renders directly via SDL to sit below Plutonium's battery/time/connection
// widgets, which remain owned by ui_MainMenuLayout.
//
// Render contract:
//   RenderTopBar(r)  — call before QdDesktopIconsElement::OnRender for correct
//                      Z-order (background behind Plutonium floating elements).
//   RenderDock(r)    — same call site; renders the dock panel backdrop.
//   HandleInput(keys_down, keys_held) — call once per frame before layout input
//                      dispatch to let chrome consume hot-corner input.
//
// Suppression: when the active MenuType is Startup, Lockscreen, or QLockscreen
// all methods are no-ops (lock/startup screens own their own chrome).
//
// Hot-corner delegation: RenderTopBar paints the 96×72 hot-corner widget and
// HandleInput fires the launchpad open request on tap-release inside the zone.
// QdDesktopIconsElement no longer renders or hit-tests the hot corner.
//
// B41/B42 note: QdGlobalChrome renders only SDL_RenderFillRect geometry — no
// RenderText and no SDL_Texture owned here.  Plutonium's battery/time/connection
// elements are untouched.
#pragma once

#ifdef QDESKTOP_MODE

#include <SDL2/SDL.h>
#include <switch.h>

namespace ul::menu::ui { enum class MenuType; }

namespace ul::menu::qdesktop {

class QdGlobalChrome {
public:
    QdGlobalChrome() = default;
    ~QdGlobalChrome() = default;

    // Non-copyable, non-movable (owns no heap; stateless except the menu-type ptr).
    QdGlobalChrome(const QdGlobalChrome&)            = delete;
    QdGlobalChrome& operator=(const QdGlobalChrome&) = delete;

    // Set a pointer to the application's loaded_menu field so Suppress() can
    // check it without coupling this header to ui_MenuApplication.hpp.
    // Must be called once before the first frame (in MenuApplication::OnLoad).
    void SetMenuTypeRef(const ul::menu::ui::MenuType *menu_type_ref);

    // --- render ---

    // Renders the translucent top-bar background strip (y=0..TOPBAR_H_PX=48)
    // and the 96×72 hot-corner widget.  Call once per frame from the
    // Application-level render callback, BEFORE layout elements render.
    void RenderTopBar(SDL_Renderer *r);

    // Renders the translucent dock backdrop (y=kDockNominalTop..1080).
    // Call immediately after RenderTopBar each frame.
    void RenderDock(SDL_Renderer *r);

    // --- input ---

    // Call once per frame before the active layout's OnInput runs.
    // Consumes a touch release inside the hot-corner zone and fires the
    // launchpad open request.  Returns true if input was consumed.
    bool HandleInput(u64 keys_down, u64 keys_held);

private:
    const ul::menu::ui::MenuType *menu_type_ref_ = nullptr;

    // Touch tracking for hot-corner tap: track whether the previous frame
    // had a touch inside the hot-corner so we edge-detect release inside zone.
    bool hc_touch_was_inside_ = false;

    bool IsSuppressed() const;
};

} // namespace ul::menu::qdesktop

#endif // QDESKTOP_MODE
