// qd_FirstBootWelcome.hpp — One-time first-boot welcome modal for QNX v1.8.27.
//
// Shown exactly once: when the flag file sdmc:/ulaunch/.welcome_seen is absent.
// The caller checks ShouldShow() before calling Open(); Open() lazy-allocates
// all textures so there is zero boot-path cost when the flag is already present.
// Close() frees textures AND writes the flag file so the overlay never reappears.
//
// Lifecycle (mirrors QdHelpOverlay exactly):
//   ShouldShow() — stat(sdmc:/ulaunch/.welcome_seen); returns true if absent
//   Open(r)      — pre-renders text textures; sets open_=true
//   Render(r)    — blits cached textures; no RenderText per frame
//   HandleInput(keys_down) — any non-zero press → Close() + return true
//   Close()      — frees textures + writes flag file; sets open_=false
//   ~QdFirstBootWelcome() — calls Close()
//
// Wire-in spec (ui_MainMenuLayout — do NOT do this here):
//   Add `qdesktop::QdFirstBootWelcome first_boot_welcome_;` as private member
//   in the #ifdef QDESKTOP_MODE block of ui_MainMenuLayout.hpp.
//
//   In Initialize() (after qdesktop_icons is live), under #ifdef QDESKTOP_MODE:
//     if (first_boot_welcome_.ShouldShow()) {
//         first_boot_welcome_.Open(renderer);
//     }
//
//   In OnMenuUpdate (or wherever the renderer is accessible on first frame):
//     Actually Open() needs the renderer. Wire it through the OnRender path.
//     See ui_MainMenuLayout.cpp for the exact wiring.
//
//   OnInput / OnRender (QDESKTOP_MODE block):
//     if (first_boot_welcome_.IsOpen()) {
//         if (first_boot_welcome_.HandleInput(keys_down)) return;
//     }
//     ...
//     if (first_boot_welcome_.IsOpen()) first_boot_welcome_.Render(renderer);
#pragma once

#include <SDL2/SDL.h>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <pu/ui/ui_Types.hpp>
#include <switch.h>

namespace ul::menu::qdesktop {

// ── QdFirstBootWelcome ────────────────────────────────────────────────────────
//
// Full-screen modal overlay shown only on the user's very first uMenu boot.
// All textures are allocated in Open() and freed in Close() to keep the boot
// path free of GPU allocations when the flag file already exists.
//
// Memory budget: 9 SDL_Texture* members. Total well under 4 KB GPU.
// Background rectangle is painted with SDL primitives (no texture).
class QdFirstBootWelcome {
public:
    QdFirstBootWelcome();
    ~QdFirstBootWelcome();

    // Non-copyable, non-movable (owns SDL textures).
    QdFirstBootWelcome(const QdFirstBootWelcome&) = delete;
    QdFirstBootWelcome& operator=(const QdFirstBootWelcome&) = delete;
    QdFirstBootWelcome(QdFirstBootWelcome&&) = delete;
    QdFirstBootWelcome& operator=(QdFirstBootWelcome&&) = delete;

    // Returns true if sdmc:/ulaunch/.welcome_seen does NOT exist.
    // Cheap: single fopen check. Safe to call before renderer is ready.
    bool ShouldShow() const;

    // Returns whether the overlay is currently visible.
    bool IsOpen() const { return open_; }

    // Pre-renders all text textures and sets open_=true.
    // Safe to call when already open — re-renders (handles resolution changes).
    void Open(SDL_Renderer *r);

    // Frees all cached textures, writes the flag file, and sets open_=false.
    // Safe to call when already closed.
    void Close();

    // Blits all cached textures onto r. No-op if !open_.
    // Call at the very end of the parent element's OnRender, after all layers.
    void Render(SDL_Renderer *r);

    // If open_ and keys_down is non-zero, calls Close() and returns true.
    // Returns false if not open or no button was pressed.
    bool HandleInput(u64 keys_down);

private:
    // ── Helpers ──────────────────────────────────────────────────────────────
    static void MakeText(SDL_Renderer *r,
                         pu::ui::DefaultFontSize font_size,
                         const char *text,
                         pu::ui::Color color,
                         SDL_Texture **out_tex,
                         int *out_w, int *out_h);

    static void FreeTexture(SDL_Texture **tex);

    static void Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h);

    // Writes the flag file sdmc:/ulaunch/.welcome_seen.
    // No-op and silent on failure (flag is advisory; failure doesn't crash).
    static void WriteFlagFile();

    // ── Visibility ───────────────────────────────────────────────────────────
    bool open_ = false;

    // ── Title ─────────────────────────────────────────────────────────────────
    SDL_Texture *tex_title_  = nullptr;  int title_w_  = 0, title_h_  = 0;

    // ── Intro paragraph ───────────────────────────────────────────────────────
    SDL_Texture *tex_intro_  = nullptr;  int intro_w_  = 0, intro_h_  = 0;

    // ── Three keybind tip rows ────────────────────────────────────────────────
    SDL_Texture *tex_tip0_   = nullptr;  int tip0_w_   = 0, tip0_h_   = 0;
    SDL_Texture *tex_tip1_   = nullptr;  int tip1_w_   = 0, tip1_h_   = 0;
    SDL_Texture *tex_tip2_   = nullptr;  int tip2_w_   = 0, tip2_h_   = 0;

    // ── Footer ─────────────────────────────────────────────────────────────────
    SDL_Texture *tex_footer_ = nullptr;  int footer_w_ = 0, footer_h_ = 0;

    // ── v1.8.30: "Don't show again" checkbox ──────────────────────────────────
    // Default to TRUE (sticky-by-default — most users want one-time-only).
    // X button toggles this state.  When the user presses any other button to
    // dismiss, Close() writes the flag file iff dont_show_again_ is true.
    bool dont_show_again_ = true;
    SDL_Texture *tex_checkbox_label_ = nullptr;
    int  checkbox_label_w_ = 0, checkbox_label_h_ = 0;
    SDL_Texture *tex_checkbox_hint_  = nullptr;
    int  checkbox_hint_w_  = 0, checkbox_hint_h_  = 0;
};

// ── v1.8.30 cross-file render hook ────────────────────────────────────────────
// QdDesktopIconsElement::OnRender calls this after the help overlay so the
// welcome paints on top of everything.  No-op when no instance is alive or
// when the instance is closed.  Mirrors the QdHelpOverlay free-function
// pattern (RequestHelpOverlayOpen / IsCaptureHeld).
void RenderFirstBootWelcomeIfOpen(SDL_Renderer *r);
bool IsFirstBootWelcomeOpen();
bool HandleFirstBootWelcomeInput(u64 keys_down);

}  // namespace ul::menu::qdesktop
