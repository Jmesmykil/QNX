// qd_HomeMiniMenu.cpp — Dev/debug toggle panel for Q OS qdesktop.
//
// Toggle storage: file-static std::atomic<bool> with default-true. Reads
// from render code use memory_order_relaxed (toggle is a hint, not a safety
// boundary — the render path tolerates one stale frame). The menu write uses
// memory_order_release so the next render pass picks up the change.
//
// UI: Custom SDL-drawn vertical list overlay rendered via
//     g_MenuApplication->CallForRenderWithRenderOver().
//
// Controls:
//   D-pad UP / DOWN     — move cursor (wraps)
//   D-pad LEFT / RIGHT  — also move cursor (compatibility with one-hand hold)
//   A                   — activate highlighted item
//   B                   — close menu without action
//   Touch tap           — tap a row to activate it immediately
//
// Layout (all in Plutonium render coordinates, 1920×1080):
//   Panel  640 × (TITLE_H + SUBTITLE_H + 9*ROW_H + BOTTOM_PAD) px, centered
//   Title  28 px from top of panel
//   Rows   start at TITLE_H + SUBTITLE_H below panel top; ROW_H each
//   Selected row gets a filled accent-coloured background rect + border

#include <ul/menu/qdesktop/qd_HomeMiniMenu.hpp>
#include <ul/menu/qdesktop/qd_Transition.hpp>
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/smi/smi_Commands.hpp>
#include <ul/ul_Result.hpp>
#include <pu/ui/render/render_Renderer.hpp>
#include <pu/ui/render/render_SDL2.hpp>
#include <SDL2/SDL.h>
#include <array>
#include <string>

extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

    // ── Toggles (definitions of the externs declared in the header) ──────────
    std::atomic<bool> g_dev_wallpaper_enabled        { true };
    std::atomic<bool> g_dev_brand_fade_enabled       { true };
    std::atomic<bool> g_dev_dock_enabled             { true };
    std::atomic<bool> g_dev_icons_enabled            { true };
    std::atomic<bool> g_dev_topbar_enabled           { true };
    std::atomic<bool> g_dev_volume_policy_enabled    { true };

    // ── Private implementation ────────────────────────────────────────────────

    namespace {

        // Returns "ON" / "OFF" for the current state of a toggle.
        const char *StateStr(const std::atomic<bool> &t) {
            return t.load(std::memory_order_acquire) ? "ON" : "OFF";
        }

        // ── Layout constants (1920×1080 render coordinates) ──────────────────

        static constexpr s32 PANEL_W       = 640;
        static constexpr s32 ROW_H         = 72;       // height of each list item
        static constexpr s32 TITLE_H       = 80;       // title bar height
        static constexpr s32 SUBTITLE_H    = 44;       // subtitle / hint text height
        static constexpr s32 BOTTOM_PAD    = 24;       // padding below last row
        static constexpr int NUM_ITEMS     = 9;        // total rows including Close

        // Computed panel height.
        static constexpr s32 PANEL_H = TITLE_H + SUBTITLE_H + NUM_ITEMS * ROW_H + BOTTOM_PAD;

        // Panel top-left corner (centred on 1920×1080).
        static constexpr s32 PANEL_X = (1920 - PANEL_W) / 2;   // = 640
        static constexpr s32 PANEL_Y = (1080 - PANEL_H) / 2;

        // Row list starts after title + subtitle.
        static constexpr s32 ROWS_TOP = PANEL_Y + TITLE_H + SUBTITLE_H;

        // Row text left padding.
        static constexpr s32 TEXT_PAD = 28;

        // ── Theme colours (matching QdTheme::DarkLiquidGlass) ────────────────
        //
        // These are duplicated here as constexpr so qd_HomeMiniMenu.cpp does
        // not need to depend on qd_Theme.hpp (avoids a header pull that dragged
        // in SDL2 types in a file that previously had no SDL2 dependency).
        //
        // If the theme is updated, keep these in sync with qd_Theme.hpp.

        static constexpr pu::ui::Color CLR_PANEL_BG   = { 0x08, 0x08, 0x1A, 0xEE }; // near-black
        static constexpr pu::ui::Color CLR_PANEL_BDR  = { 0x7C, 0xC5, 0xFF, 0x60 }; // focus_ring dimmed
        static constexpr pu::ui::Color CLR_TITLE_BG   = { 0x12, 0x12, 0x2A, 0xFF }; // surface_glass
        static constexpr pu::ui::Color CLR_TITLE_TEXT = { 0xE0, 0xE0, 0xF0, 0xFF }; // text_primary
        static constexpr pu::ui::Color CLR_HINT_TEXT  = { 0x88, 0x88, 0xAA, 0xFF }; // text_secondary
        static constexpr pu::ui::Color CLR_ROW_TEXT   = { 0xE0, 0xE0, 0xF0, 0xFF }; // text_primary
        static constexpr pu::ui::Color CLR_ROW_SEL_BG = { 0x7D, 0xD3, 0xFC, 0x28 }; // accent tinted
        static constexpr pu::ui::Color CLR_ROW_SEL_BD = { 0x7C, 0xC5, 0xFF, 0xA0 }; // focus_ring
        static constexpr pu::ui::Color CLR_CLOSE_TEXT = { 0xFF, 0x7A, 0x7A, 0xFF }; // warm red close
        static constexpr pu::ui::Color CLR_SCREEN_DIM = { 0x00, 0x00, 0x00, 0x90 }; // background fade

        // ── Item descriptors ─────────────────────────────────────────────────

        enum class ItemKind { Toggle, Action };

        struct Item {
            const char  *label_base;    // base label (without state suffix)
            ItemKind     kind;
            std::atomic<bool> *toggle;  // non-null when kind == Toggle
        };

        // Constructs the 9 menu items.  We need to capture pointers to the
        // atomic bools defined above; these are stable (file-static lifetime).
        static Item g_items[NUM_ITEMS] = {
            { "Wallpaper",      ItemKind::Toggle, &g_dev_wallpaper_enabled     },
            { "Brand fade",     ItemKind::Toggle, &g_dev_brand_fade_enabled    },
            { "Dock",           ItemKind::Toggle, &g_dev_dock_enabled          },
            { "Desktop icons",  ItemKind::Toggle, &g_dev_icons_enabled         },
            { "Top bar",        ItemKind::Toggle, &g_dev_topbar_enabled        },
            { "Volume policy",  ItemKind::Toggle, &g_dev_volume_policy_enabled },
            { "Restart uMenu",  ItemKind::Action, nullptr                      },
            { "Reload theme",   ItemKind::Action, nullptr                      },
            { "Close",          ItemKind::Action, nullptr                      },  // index 8
        };

        // ── Rendering helpers ─────────────────────────────────────────────────

        // Builds the display label for item i, e.g. "Dock  [ON]".
        // Returns a std::string so the texture can be built per-frame only when
        // state changes (the caller re-creates textures each frame for simplicity
        // given the low frame-rate of an overlay menu).
        std::string ItemLabel(int i) {
            if (g_items[i].kind == ItemKind::Toggle) {
                std::string s = g_items[i].label_base;
                s += "  [";
                s += StateStr(*g_items[i].toggle);
                s += "]";
                return s;
            }
            return g_items[i].label_base;
        }

        // Fill a coloured rectangle via the raw SDL renderer.
        // Plutonium's Renderer::RenderRectangleFill does not expose blending
        // mode control, so we use SDL directly for the translucent elements.
        void FillRect(SDL_Renderer *sdl_r,
                      s32 x, s32 y, s32 w, s32 h,
                      const pu::ui::Color &c)
        {
            SDL_SetRenderDrawBlendMode(sdl_r, c.a < 0xFF ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(sdl_r, c.r, c.g, c.b, c.a);
            const SDL_Rect rect { x, y, w, h };
            SDL_RenderFillRect(sdl_r, &rect);
        }

        // Draw a 1-pixel border rectangle.
        void DrawRect(SDL_Renderer *sdl_r,
                      s32 x, s32 y, s32 w, s32 h,
                      const pu::ui::Color &c)
        {
            SDL_SetRenderDrawBlendMode(sdl_r, c.a < 0xFF ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(sdl_r, c.r, c.g, c.b, c.a);
            const SDL_Rect rect { x, y, w, h };
            SDL_RenderDrawRect(sdl_r, &rect);
        }

        // ── Activate the item at index i ──────────────────────────────────────
        //
        // Returns true  if the menu should close after this action.
        // Returns false if the menu should stay open (toggle — redraw same frame).
        bool ActivateItem(int i) {
            if (i < 0 || i >= NUM_ITEMS) { return true; }

            if (g_items[i].kind == ItemKind::Toggle) {
                // Flip the toggle; menu stays open so the state change is visible.
                std::atomic<bool> &flag = *g_items[i].toggle;
                const bool next = !flag.load(std::memory_order_acquire);
                flag.store(next, std::memory_order_release);
                UL_LOG_INFO("qdesktop: dev toggle '%s' -> %s",
                            g_items[i].label_base, next ? "ON" : "OFF");
                return false;  // keep menu open
            }

            // Action items.
            switch (i) {
                case 6: {
                    UL_LOG_INFO("qdesktop: dev action — Restart uMenu");
                    const Result rc = ::ul::menu::smi::RestartMenu(true);
                    if (R_FAILED(rc)) {
                        UL_LOG_WARN("qdesktop: RestartMenu rc=0x%X — staying open",
                                    static_cast<unsigned>(rc));
                        return false;
                    }
                    return true;
                }
                case 7: {
                    UL_LOG_INFO("qdesktop: dev action — Reload theme (clear brand fade cache)");
                    // Defined in qd_Transition.cpp (ul::menu::qdesktop namespace).
                    ReleaseBrandFadeTexture();
                    g_MenuApplication->ShowNotification(
                        "Brand fade cache cleared — next transition regenerates",
                        2000);
                    return false;  // keep open so user sees the notification
                }
                case 8:
                default:
                    return true;  // Close
            }
        }

    }  // namespace

    // ── ShowDevMenu ───────────────────────────────────────────────────────────

    void ShowDevMenu() {
        if (g_MenuApplication == nullptr) {
            UL_LOG_WARN("qdesktop: ShowDevMenu — g_MenuApplication null, skipping");
            return;
        }

        UL_LOG_INFO("qdesktop: ShowDevMenu opened (custom SDL overlay)");

        int cursor = 8;     // default to "Close" (index 8) — A from default safely dismisses.
                            // (was 0 = "Wallpaper" toggle which silently flipped wallpaper OFF
                            //  on Home double-press + A — user-reported "login wallpaper gone")
        bool close = false; // set true to exit the loop

        // Touch state machine: edge-trigger so a finger held on a row doesn't
        // repeatedly fire.  touch_was_active tracks whether a finger was down
        // last frame; on a rising edge we record the hit row; on a falling edge
        // (lift) we activate it if the finger didn't move off the row.
        bool touch_was_active = false;
        int  touch_hit_row    = -1;

        // Pre-build the font references once — they are pointers into Plutonium's
        // font cache and remain valid for the duration of the overlay.
        const std::string &font_title    = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
        const std::string &font_subtitle = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
        const std::string &font_item     = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

        // ── A3-OPT-1: pre-build static textures once before the loop ─────────
        //
        // Title and hint text never change; render once and reuse.
        // Per-item label textures are rebuilt only when text or cursor position
        // changes (toggle state flip or cursor move), not every frame.
        //
        // Cached item state: each slot stores the last-rendered label string AND
        // the cursor index at render time (needed because the row colour is
        // cursor-position-dependent).  On any change the slot is rebuilt.

        // Title texture — invariant for the lifetime of the overlay.
        pu::sdl2::Texture t_title_cached = pu::ui::render::RenderText(
            font_title, "Q OS  Dev Menu", CLR_TITLE_TEXT);
        s32 t_title_w = 0, t_title_h = 0;
        if (t_title_cached != nullptr) {
            t_title_w = pu::ui::render::GetTextureWidth(t_title_cached);
            t_title_h = pu::ui::render::GetTextureHeight(t_title_cached);
        }

        // Hint texture — invariant for the lifetime of the overlay.
        pu::sdl2::Texture t_hint_cached = pu::ui::render::RenderText(
            font_subtitle,
            "A/tap = toggle | B = close | icon+dock changes need Restart",
            CLR_HINT_TEXT);
        s32 t_hint_w = 0, t_hint_h = 0;
        if (t_hint_cached != nullptr) {
            t_hint_w = pu::ui::render::GetTextureWidth(t_hint_cached);
            t_hint_h = pu::ui::render::GetTextureHeight(t_hint_cached);
        }

        // Per-item label cache — one slot per row.
        struct ItemTexCache {
            pu::sdl2::Texture tex  = nullptr;
            std::string       text = {};   // last rendered label string
            int               cur  = -1;  // cursor index at render time (colour key)
            s32               h    = 0;   // texture height (cached for layout)
        };
        ItemTexCache item_cache[NUM_ITEMS];

        // Helper: (re)build a single label cache slot.
        auto RebuildItemTex = [&](int i, int cur_idx) {
            if (item_cache[i].tex != nullptr) {
                pu::ui::render::DeleteTexture(item_cache[i].tex);
                item_cache[i].tex = nullptr;
            }
            const std::string lbl = ItemLabel(i);
            const pu::ui::Color &text_clr = (i == 8) ? CLR_CLOSE_TEXT : CLR_ROW_TEXT;
            item_cache[i].tex  = pu::ui::render::RenderText(
                font_item, lbl, text_clr, PANEL_W - 2 * TEXT_PAD);
            item_cache[i].text = lbl;
            item_cache[i].cur  = cur_idx;
            item_cache[i].h    = (item_cache[i].tex != nullptr)
                ? pu::ui::render::GetTextureHeight(item_cache[i].tex) : 0;
        };

        // Build all item textures for the initial cursor position.
        for (int i = 0; i < NUM_ITEMS; ++i) {
            RebuildItemTex(i, cursor);
        }
        // ─────────────────────────────────────────────────────────────────────

        while (!close) {
            const auto ok = g_MenuApplication->CallForRenderWithRenderOver(
                [&](pu::ui::render::Renderer::Ref &drawer) -> bool {

                    // ── Input ─────────────────────────────────────────────────
                    const u64 keys_down = g_MenuApplication->GetButtonsDown();

                    // Track whether the cursor moved this frame — if so we must
                    // invalidate ALL item textures (row colour depends on cursor).
                    const int prev_cursor = cursor;

                    // D-pad up / left — move cursor up (wraps).
                    if (keys_down & (HidNpadButton_AnyUp | HidNpadButton_AnyLeft)) {
                        cursor = (cursor + NUM_ITEMS - 1) % NUM_ITEMS;
                    }

                    // D-pad down / right — move cursor down (wraps).
                    if (keys_down & (HidNpadButton_AnyDown | HidNpadButton_AnyRight)) {
                        cursor = (cursor + 1) % NUM_ITEMS;
                    }

                    // A — activate.
                    if (keys_down & HidNpadButton_A) {
                        if (ActivateItem(cursor)) {
                            close = true;
                            return false;  // stop this render frame
                        }
                        // If the item keeps the menu open (toggle), fall through
                        // to redraw so the updated state is visible immediately.
                    }

                    // B — close without action.
                    if (keys_down & HidNpadButton_B) {
                        UL_LOG_INFO("qdesktop: ShowDevMenu — B pressed, closing");
                        close = true;
                        return false;
                    }

                    // ── Touch ─────────────────────────────────────────────────
                    const HidTouchScreenState tch = g_MenuApplication->GetTouchState();
                    const bool touch_active = (tch.count > 0);

                    if (touch_active) {
                        // Scale the raw touch coordinates to the 1920×1080
                        // render surface (same transform the Dialog uses).
                        const s32 tx = (s32)((double)tch.touches[0].x
                                              * pu::ui::render::ScreenFactor);
                        const s32 ty = (s32)((double)tch.touches[0].y
                                              * pu::ui::render::ScreenFactor);

                        // Determine which row (if any) the touch falls in.
                        const s32 rows_bottom = ROWS_TOP + NUM_ITEMS * ROW_H;
                        int hit = -1;
                        if (tx >= PANEL_X && tx < PANEL_X + PANEL_W &&
                            ty >= ROWS_TOP && ty < rows_bottom) {
                            hit = (ty - ROWS_TOP) / ROW_H;
                            if (hit < 0 || hit >= NUM_ITEMS) { hit = -1; }
                        }

                        if (!touch_was_active) {
                            // Rising edge — record the row under the finger.
                            touch_hit_row = hit;
                        }
                        // While held, visually highlight the row under finger.
                        if (hit >= 0) { cursor = hit; }
                        touch_was_active = true;
                    } else {
                        // Falling edge — finger lifted (tch.count == 0).
                        // Activate the row that was recorded on the rising edge.
                        if (touch_was_active && touch_hit_row >= 0) {
                            if (ActivateItem(touch_hit_row)) {
                                close = true;
                                touch_was_active = false;
                                touch_hit_row    = -1;
                                return false;
                            }
                        }
                        touch_was_active = false;
                        touch_hit_row    = -1;
                    }

                    // ── A3-OPT-1: dirty-check and selectively rebuild label textures ──
                    //
                    // Rebuild is needed when:
                    //   (a) cursor moved — row colour is cursor-position-dependent
                    //       (RF from OPTIMIZATION_FINDINGS.md: "include cursor index
                    //        in dirty check, rebuild on cursor move")
                    //   (b) a toggle item's text changed (ActivateItem flipped state)
                    //
                    // Case (a): cursor moved → invalidate every slot (all colours change).
                    // Case (b): per-slot text mismatch → rebuild that slot only.
                    const bool cursor_moved = (cursor != prev_cursor);
                    for (int i = 0; i < NUM_ITEMS; ++i) {
                        const std::string lbl = ItemLabel(i);
                        if (cursor_moved || item_cache[i].text != lbl ||
                            item_cache[i].cur != cursor) {
                            RebuildItemTex(i, cursor);
                        }
                    }
                    // ─────────────────────────────────────────────────────────

                    // ── Draw ──────────────────────────────────────────────────
                    SDL_Renderer *sdl_r = pu::ui::render::GetMainRenderer();

                    // Full-screen dim overlay.
                    FillRect(sdl_r, 0, 0, 1920, 1080, CLR_SCREEN_DIM);

                    // Panel background.
                    FillRect(sdl_r, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, CLR_PANEL_BG);
                    DrawRect(sdl_r, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, CLR_PANEL_BDR);

                    // Title bar background.
                    FillRect(sdl_r, PANEL_X, PANEL_Y, PANEL_W, TITLE_H, CLR_TITLE_BG);

                    // Title text — cached; no RenderText/DeleteTexture per frame.
                    if (t_title_cached != nullptr) {
                        const s32 tx = PANEL_X + (PANEL_W - t_title_w) / 2;
                        const s32 ty = PANEL_Y + (TITLE_H - t_title_h) / 2;
                        drawer->RenderTexture(t_title_cached, tx, ty);
                    }

                    // Subtitle / hint text — cached; no RenderText/DeleteTexture per frame.
                    if (t_hint_cached != nullptr) {
                        const s32 tx = PANEL_X + (PANEL_W - t_hint_w) / 2;
                        const s32 ty = PANEL_Y + TITLE_H + (SUBTITLE_H - t_hint_h) / 2;
                        drawer->RenderTexture(t_hint_cached, tx, ty);
                    }

                    // Thin separator line between title area and rows.
                    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(sdl_r,
                        CLR_PANEL_BDR.r, CLR_PANEL_BDR.g, CLR_PANEL_BDR.b, 0x60u);
                    SDL_RenderDrawLine(sdl_r,
                        PANEL_X,           PANEL_Y + TITLE_H + SUBTITLE_H,
                        PANEL_X + PANEL_W, PANEL_Y + TITLE_H + SUBTITLE_H);

                    // Item rows.
                    for (int i = 0; i < NUM_ITEMS; ++i) {
                        const s32 row_y = ROWS_TOP + i * ROW_H;
                        const bool is_sel = (i == cursor);

                        // Selection highlight.
                        if (is_sel) {
                            FillRect(sdl_r, PANEL_X + 4, row_y, PANEL_W - 8, ROW_H,
                                     CLR_ROW_SEL_BG);
                            DrawRect(sdl_r, PANEL_X + 4, row_y, PANEL_W - 8, ROW_H,
                                     CLR_ROW_SEL_BD);
                        }

                        // Item label — use cached texture (rebuilt above when dirty).
                        if (item_cache[i].tex != nullptr) {
                            const s32 ty = row_y + (ROW_H - item_cache[i].h) / 2;
                            drawer->RenderTexture(item_cache[i].tex, PANEL_X + TEXT_PAD, ty);
                        }

                        // Row separator (thin dim line).
                        if (i < NUM_ITEMS - 1) {
                            SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
                            SDL_SetRenderDrawColor(sdl_r, 0x40, 0x40, 0x60, 0x60u);
                            SDL_RenderDrawLine(sdl_r,
                                PANEL_X + TEXT_PAD,           row_y + ROW_H,
                                PANEL_X + PANEL_W - TEXT_PAD, row_y + ROW_H);
                        }
                    }

                    return !close;  // continue loop unless close was set
                }
            );

            if (!ok) {
                // Application says to stop the loop (power button, suspend, etc.)
                break;
            }
        }

        // ── A3-OPT-1: release all cached textures on loop exit ────────────────
        if (t_title_cached != nullptr) {
            pu::ui::render::DeleteTexture(t_title_cached);
            t_title_cached = nullptr;
        }
        if (t_hint_cached != nullptr) {
            pu::ui::render::DeleteTexture(t_hint_cached);
            t_hint_cached = nullptr;
        }
        for (int i = 0; i < NUM_ITEMS; ++i) {
            if (item_cache[i].tex != nullptr) {
                pu::ui::render::DeleteTexture(item_cache[i].tex);
                item_cache[i].tex = nullptr;
            }
        }
        // ─────────────────────────────────────────────────────────────────────

        UL_LOG_INFO("qdesktop: ShowDevMenu closed");
    }

}  // namespace ul::menu::qdesktop
