// qd_TerminalLayout.hpp — Live telemetry log tail for Q OS qdesktop (dock slot 1).
// Inherits pu::ui::elm::Element (same pattern as QdVaultLayout / QdTextViewer).
// Renders the last QD_TERMINAL_TAIL_BYTES of QD_TERMINAL_LOG_PATH in a scrollable
// monospace view with auto-refresh every QD_TERMINAL_REFRESH_FRAMES frames.
//
// Input mapping:
//   Up / Down    — scroll ±1 line
//   L  / R       — scroll ±10 lines
//   ZL / ZR      — scroll ±1 page (TERMINAL_VISIBLE_LINES)
//   Y            — force reload from disk
//   A            — copy focused line text to ShowNotification toast
//   B            — return to Main desktop (LoadMenu(MenuType::Main))
#pragma once
#include <pu/Plutonium>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>

namespace ul::menu::qdesktop {

// ── Layout pixel constants ────────────────────────────────────────────────────

/// Path to the telemetry log file that is tailed.
static constexpr const char *QD_TERMINAL_LOG_PATH = "sdmc:/qos-shell/logs/uMenu.0.log";

/// Maximum bytes read from the tail of the log file each refresh.
static constexpr size_t QD_TERMINAL_TAIL_BYTES = 16384;

/// Maximum number of display lines kept in memory after word-wrap.
static constexpr size_t QD_TERMINAL_MAX_LINES = 100;

/// Auto-refresh cadence: reload file from disk every N rendered frames.
static constexpr int QD_TERMINAL_REFRESH_FRAMES = 60;

/// Line wrap column (characters before forced wrap).
static constexpr size_t QD_TERMINAL_WRAP_COLS = 128;

/// Number of simultaneously visible lines in the scrollback area.
static constexpr int TERMINAL_VISIBLE_LINES = 42;

/// Line height in pixels (Small font).
static constexpr s32 TERMINAL_LINE_H = 20;

/// Left gutter width (pixels) — reserved for line numbers.
static constexpr s32 TERMINAL_GUTTER_W = 60;

/// Y pixel of the header bar.
static constexpr s32 TERMINAL_HEADER_H = 40;

/// Y pixel of the footer / status bar.
static constexpr s32 TERMINAL_FOOTER_H = 32;

// ── QdTerminalLayout ──────────────────────────────────────────────────────────

/// Full-screen dock-slot 1 element: live tail of the Q OS telemetry log.
/// Mounted as an Element on the desktop layout via QdDesktopIconsElement.
/// Hidden by default — QdDesktopIconsElement calls SetVisible(true) when slot 1
/// is launched and SetVisible(false) / returns focus when B is pressed.
class QdTerminalLayout : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdTerminalLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdTerminalLayout>(theme);
    }

    explicit QdTerminalLayout(const QdTheme &theme);
    ~QdTerminalLayout();

    // ── Element interface ──────────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return 1920; }
    s32 GetHeight() override { return 1080; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 x, const s32 y) override;

    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────────

    /// Force a reload from disk (also called automatically every REFRESH_FRAMES).
    void Reload();

    /// Reset scroll to the bottom (most-recent lines) and re-load.
    void ResetAndOpen();

private:
    // ── Line cache ─────────────────────────────────────────────────────────────
    // Per-line SDL_Texture* lazily built; evicted when far from view.
    static constexpr int LINE_CACHE_SLACK = 6;

    SDL_Texture *EnsureLineTex(int idx);
    void EvictDistantTextures();
    void FreeAllLineTextures();

    // ── Helpers ────────────────────────────────────────────────────────────────

    /// Split raw_buf_ into lines_ applying QD_TERMINAL_WRAP_COLS hard-wrap.
    /// Keeps at most QD_TERMINAL_MAX_LINES lines (drops oldest).
    void BuildLines();

    /// Clamp scroll_top_ to [0, max valid].
    void ClampScroll();

    // ── State ──────────────────────────────────────────────────────────────────
    QdTheme     theme_;
    std::string raw_buf_;                         ///< raw bytes from last Reload()
    std::vector<std::string> lines_;              ///< word-wrapped display lines
    int         scroll_top_    = 0;               ///< index of first visible line
    int         focus_line_    = 0;               ///< highlighted line for A-copy
    int         frame_counter_ = 0;               ///< counts OnRender calls for auto-refresh
    bool        file_missing_  = false;           ///< true if last fopen() returned nullptr

    // Lazy line texture cache. Key = line index.
    std::unordered_map<int, SDL_Texture *> line_tex_cache_;

    // Static header texture (rebuilt on Reload).
    SDL_Texture *header_tex_  = nullptr;
    // Static footer texture (rebuilt on Reload).
    SDL_Texture *footer_tex_  = nullptr;
};

} // namespace ul::menu::qdesktop
