// qd_Theme.hpp — Q OS desktop color token system for uMenu C++ port.
// Ported verbatim from tools/mock-nro-desktop-gui/src/theme.rs (v1.1.12).
// All RGB values are authoritative; do not derive from doc 33 JSON defaults.
#pragma once
#include <pu/Plutonium>

namespace ul::menu::qdesktop {

// ── Color helpers ──────────────────────────────────────────────────────────

// Convenience: create a fully-opaque Color.
static inline constexpr pu::ui::Color Rgb(u8 r, u8 g, u8 b) {
    return pu::ui::Color(r, g, b, 0xFF);
}

// Create a Color with explicit alpha.
static inline constexpr pu::ui::Color Rgba(u8 r, u8 g, u8 b, u8 a) {
    return pu::ui::Color(r, g, b, a);
}

// ── QdTheme ────────────────────────────────────────────────────────────────

// 17 color tokens ported from theme.rs.
// Field names follow the Rust token names exactly (snake_case).
struct QdTheme {
    pu::ui::Color desktop_bg;         // (0x0A,0x0A,0x14) — from theme.rs DESKTOP_BG
    pu::ui::Color surface_glass;      // (0x12,0x12,0x2A) — from theme.rs SURFACE_GLASS
    pu::ui::Color topbar_bg;          // (0x0C,0x0C,0x20) — from theme.rs TOPBAR_BG
    pu::ui::Color dock_bg;            // (0x10,0x10,0x2A) — from theme.rs DOCK_BG
    pu::ui::Color accent;             // (0x7D,0xD3,0xFC) — from theme.rs ACCENT
    pu::ui::Color text_primary;       // (0xE0,0xE0,0xF0) — from theme.rs TEXT_PRIMARY
    pu::ui::Color text_secondary;     // (0x88,0x88,0xAA) — from theme.rs TEXT_SECONDARY
    pu::ui::Color focus_ring;         // (0x7C,0xC5,0xFF) — from theme.rs FOCUS_RING
    pu::ui::Color button_close;       // (0xF8,0x71,0x71) — from theme.rs BUTTON_CLOSE
    pu::ui::Color button_minimize;    // (0xFB,0xBF,0x24) — from theme.rs BUTTON_MINIMIZE
    pu::ui::Color button_maximize;    // (0x4A,0xDE,0x80) — from theme.rs BUTTON_MAXIMIZE
    pu::ui::Color cursor_fill;        // (0xF5,0xF5,0xFF) — from theme.rs CURSOR_FILL
    pu::ui::Color cursor_outline;     // (0x05,0x05,0x10) — from theme.rs CURSOR_OUTLINE
    pu::ui::Color cursor_right_click; // (0xE5,0x4B,0x4B) — from theme.rs CURSOR_RIGHT_CLICK
    // TITLEBAR_INACTIVE and BUTTON_RESTORE are absent from theme.rs;
    // derived to match the visual spec: inactive=dimmed surface, restore=same as maximize.
    pu::ui::Color titlebar_inactive;  // (0x18,0x18,0x30) — derived; no theme.rs token
    pu::ui::Color button_restore;     // (0x4A,0xDE,0x80) — same as button_maximize
    pu::ui::Color grid_line;          // (0x18,0x18,0x32) — from wallpaper.rs GRID color

    // Factory: Dark Liquid Glass theme (default, matches all Rust reference values).
    static QdTheme DarkLiquidGlass() {
        QdTheme t;
        t.desktop_bg         = Rgb(0x0A, 0x0A, 0x14);
        t.surface_glass      = Rgb(0x12, 0x12, 0x2A);
        t.topbar_bg          = Rgb(0x0C, 0x0C, 0x20);
        t.dock_bg            = Rgb(0x10, 0x10, 0x2A);
        t.accent             = Rgb(0x7D, 0xD3, 0xFC);
        t.text_primary       = Rgb(0xE0, 0xE0, 0xF0);
        t.text_secondary     = Rgb(0x88, 0x88, 0xAA);
        t.focus_ring         = Rgb(0x7C, 0xC5, 0xFF);
        t.button_close       = Rgb(0xF8, 0x71, 0x71);
        t.button_minimize    = Rgb(0xFB, 0xBF, 0x24);
        t.button_maximize    = Rgb(0x4A, 0xDE, 0x80);
        t.cursor_fill        = Rgb(0xF5, 0xF5, 0xFF);
        t.cursor_outline     = Rgb(0x05, 0x05, 0x10);
        t.cursor_right_click = Rgb(0xE5, 0x4B, 0x4B);
        t.titlebar_inactive  = Rgb(0x18, 0x18, 0x30);
        t.button_restore     = Rgb(0x4A, 0xDE, 0x80);
        t.grid_line          = Rgb(0x18, 0x18, 0x32);
        return t;
    }

};

} // namespace ul::menu::qdesktop
