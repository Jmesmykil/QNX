// qd_HelpOverlay.hpp — Full-screen help/instructions modal for uMenu v1.8.25.
//
// Triggered by pressing Plus + Capture simultaneously on the Desktop.
// The overlay is flat (no Plutonium element inheritance), owned by
// QdDesktopIconsElement, and wired in by the main thread after delivery.
//
// Lifecycle:
//   Open(renderer) — pre-renders all text textures into members; sets open_=true
//   Render(renderer) — blits all cached textures; no RenderText per frame
//   HandleInput(keys_down) — if open_, any button press calls Close() and returns true
//   Close() — frees all textures via pu::ui::render::DeleteTexture; sets open_=false
//   ~QdHelpOverlay() — calls Close()
//
// Wire-in spec (for main thread — do NOT do this here):
//   qd_DesktopIcons.hpp:  add  `QdHelpOverlay help_overlay_;`  as a private member.
//   qd_DesktopIcons.cpp:  #include <ul/menu/qdesktop/qd_HelpOverlay.hpp>
//   OnInput trigger combo: (keys_held & HidNpadButton_Plus) && (keys_down & HidNpadButton_Capture)
//     Rationale: Home button is NOT interceptable by apps on Switch. Capture
//     (Share button) IS exposed as HidNpadButton_Capture via libnx. Plus is the
//     natural companion (menu/start button). Simultaneous detection uses keys_held
//     for Plus (already held) + keys_down for Capture (the edge-trigger key).
//   OnInput, before other handlers:
//     if (help_overlay_.IsOpen()) {
//         if (help_overlay_.HandleInput(keys_down)) return;
//     }
//     if ((keys_held & HidNpadButton_Plus) && (keys_down & HidNpadButton_Capture)) {
//         help_overlay_.Open(renderer);
//         return;
//     }
//   OnRender, at the very end (after dock/folders/favorites):
//     if (help_overlay_.IsOpen()) help_overlay_.Render(renderer);
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>

namespace ul::menu::qdesktop {

// ── QdHelpOverlay ────────────────────────────────────────────────────────────
//
// Full-screen modal overlay that renders a five-section key-reference card.
// All textures are allocated on Open() and freed on Close() to avoid any
// per-frame RenderText calls. The overlay consumes all button input while open.
//
// Memory budget: ~22 SDL_Texture* members, each a short text string (40-200 B
// GPU memory). Total well under 8 KB GPU. Background rectangle is painted with
// SDL primitives (no texture).
class QdHelpOverlay {
public:
    QdHelpOverlay();
    ~QdHelpOverlay();

    // Non-copyable, non-movable (owns SDL textures).
    QdHelpOverlay(const QdHelpOverlay&) = delete;
    QdHelpOverlay& operator=(const QdHelpOverlay&) = delete;
    QdHelpOverlay(QdHelpOverlay&&) = delete;
    QdHelpOverlay& operator=(QdHelpOverlay&&) = delete;

    // Returns whether the overlay is currently visible.
    bool IsOpen() const { return open_; }

    // Pre-renders all text textures and sets open_=true.
    // Must be called with a valid SDL_Renderer (the same one used for Render).
    // Safe to call when already open — re-renders (handles resolution changes).
    void Open(SDL_Renderer *r);

    // Frees all cached textures and sets open_=false.
    // Safe to call when already closed.
    void Close();

    // Blits all cached textures onto r. No-op if !open_.
    // Call at the end of the parent element's OnRender, after all other layers.
    void Render(SDL_Renderer *r);

    // If open_ and keys_down is non-zero, calls Close() and returns true
    // (input consumed). Returns false if not open or no button was pressed.
    bool HandleInput(u64 keys_down);

private:
    // ── Helpers ──────────────────────────────────────────────────────────────
    // Renders a string with the given font size and color, stores result in
    // *out_tex, and queries its pixel dimensions into *out_w / *out_h.
    // If rendering fails, *out_tex is left nullptr and dimensions are zeroed.
    static void MakeText(SDL_Renderer *r,
                         pu::ui::DefaultFontSize font_size,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);

    // Frees a single texture pointer via pu::ui::render::DeleteTexture and
    // nulls the pointer. Safe if *tex is already nullptr.
    static void FreeTexture(SDL_Texture **tex);

    // Blits tex at (x, y) on r. No-op if tex is nullptr.
    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // ── Visibility ───────────────────────────────────────────────────────────
    bool open_ = false;

    // ── Title ─────────────────────────────────────────────────────────────────
    SDL_Texture *tex_title_ = nullptr;
    int title_w_ = 0, title_h_ = 0;

    // ── Section headers (5) ───────────────────────────────────────────────────
    SDL_Texture *tex_hdr_desktop_  = nullptr;  int hdr_desktop_w_  = 0, hdr_desktop_h_  = 0;
    SDL_Texture *tex_hdr_launchpad_= nullptr;  int hdr_launchpad_w_= 0, hdr_launchpad_h_= 0;
    SDL_Texture *tex_hdr_vault_    = nullptr;  int hdr_vault_w_    = 0, hdr_vault_h_    = 0;
    SDL_Texture *tex_hdr_login_    = nullptr;  int hdr_login_w_    = 0, hdr_login_h_    = 0;
    SDL_Texture *tex_hdr_hotcorner_= nullptr;  int hdr_hotcorner_w_= 0, hdr_hotcorner_h_= 0;

    // ── Desktop Controls rows (6 rows, key + action each) ─────────────────────
    SDL_Texture *tex_d0k_ = nullptr; int d0k_w_ = 0, d0k_h_ = 0;  // "D-pad"
    SDL_Texture *tex_d0a_ = nullptr; int d0a_w_ = 0, d0a_h_ = 0;  // "Navigate folders, dock, favorites"
    SDL_Texture *tex_d1k_ = nullptr; int d1k_w_ = 0, d1k_h_ = 0;  // "A"
    SDL_Texture *tex_d1a_ = nullptr; int d1a_w_ = 0, d1a_h_ = 0;  // "Open / launch focused tile"
    SDL_Texture *tex_d2k_ = nullptr; int d2k_w_ = 0, d2k_h_ = 0;  // "B / +"
    SDL_Texture *tex_d2a_ = nullptr; int d2a_w_ = 0, d2a_h_ = 0;  // "Close current screen"
    SDL_Texture *tex_d3k_ = nullptr; int d3k_w_ = 0, d3k_h_ = 0;  // "Y"
    SDL_Texture *tex_d3a_ = nullptr; int d3a_w_ = 0, d3a_h_ = 0;  // "Toggle favorite on focused tile"
    SDL_Texture *tex_d4k_ = nullptr; int d4k_w_ = 0, d4k_h_ = 0;  // "ZR"
    SDL_Texture *tex_d4a_ = nullptr; int d4a_w_ = 0, d4a_h_ = 0;  // "Click whatever the cursor is hovering"
    SDL_Texture *tex_d5k_ = nullptr; int d5k_w_ = 0, d5k_h_ = 0;  // "+ + Share"
    SDL_Texture *tex_d5a_ = nullptr; int d5a_w_ = 0, d5a_h_ = 0;  // "Open this help"

    // ── Launchpad Controls rows (5) ───────────────────────────────────────────
    SDL_Texture *tex_l0k_ = nullptr; int l0k_w_ = 0, l0k_h_ = 0;
    SDL_Texture *tex_l0a_ = nullptr; int l0a_w_ = 0, l0a_h_ = 0;
    SDL_Texture *tex_l1k_ = nullptr; int l1k_w_ = 0, l1k_h_ = 0;
    SDL_Texture *tex_l1a_ = nullptr; int l1a_w_ = 0, l1a_h_ = 0;
    SDL_Texture *tex_l2k_ = nullptr; int l2k_w_ = 0, l2k_h_ = 0;
    SDL_Texture *tex_l2a_ = nullptr; int l2a_w_ = 0, l2a_h_ = 0;
    SDL_Texture *tex_l3k_ = nullptr; int l3k_w_ = 0, l3k_h_ = 0;
    SDL_Texture *tex_l3a_ = nullptr; int l3a_w_ = 0, l3a_h_ = 0;
    SDL_Texture *tex_l4k_ = nullptr; int l4k_w_ = 0, l4k_h_ = 0;
    SDL_Texture *tex_l4a_ = nullptr; int l4a_w_ = 0, l4a_h_ = 0;

    // ── Vault rows (4) ────────────────────────────────────────────────────────
    SDL_Texture *tex_v0k_ = nullptr; int v0k_w_ = 0, v0k_h_ = 0;
    SDL_Texture *tex_v0a_ = nullptr; int v0a_w_ = 0, v0a_h_ = 0;
    SDL_Texture *tex_v1k_ = nullptr; int v1k_w_ = 0, v1k_h_ = 0;
    SDL_Texture *tex_v1a_ = nullptr; int v1a_w_ = 0, v1a_h_ = 0;
    SDL_Texture *tex_v2k_ = nullptr; int v2k_w_ = 0, v2k_h_ = 0;
    SDL_Texture *tex_v2a_ = nullptr; int v2a_w_ = 0, v2a_h_ = 0;
    SDL_Texture *tex_v3k_ = nullptr; int v3k_w_ = 0, v3k_h_ = 0;
    SDL_Texture *tex_v3a_ = nullptr; int v3a_w_ = 0, v3a_h_ = 0;

    // ── Login Screen rows (3) ─────────────────────────────────────────────────
    SDL_Texture *tex_g0k_ = nullptr; int g0k_w_ = 0, g0k_h_ = 0;
    SDL_Texture *tex_g0a_ = nullptr; int g0a_w_ = 0, g0a_h_ = 0;
    SDL_Texture *tex_g1k_ = nullptr; int g1k_w_ = 0, g1k_h_ = 0;
    SDL_Texture *tex_g1a_ = nullptr; int g1a_w_ = 0, g1a_h_ = 0;
    SDL_Texture *tex_g2k_ = nullptr; int g2k_w_ = 0, g2k_h_ = 0;
    SDL_Texture *tex_g2a_ = nullptr; int g2a_w_ = 0, g2a_h_ = 0;

    // ── Hot Corner paragraph ───────────────────────────────────────────────────
    SDL_Texture *tex_hc_body_ = nullptr; int hc_body_w_ = 0, hc_body_h_ = 0;

    // ── Footer ─────────────────────────────────────────────────────────────────
    SDL_Texture *tex_footer_ = nullptr; int footer_w_ = 0, footer_h_ = 0;
};

// v1.8.25: Cross-file help overlay state. Home + Capture (Share) trigger needs
// coordination between OnInput (sees keys_held every frame) and
// MainMenuLayout::OnHomeButtonPress (async event handler — reads the flag,
// raises an open request that next OnInput consumes).
void SetCaptureHeld(bool held);
bool IsCaptureHeld();
void RequestHelpOverlayOpen();
bool ConsumeHelpOverlayOpenRequest();

}  // namespace ul::menu::qdesktop
