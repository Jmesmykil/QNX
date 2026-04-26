// qd_TextViewer.hpp — Full-screen scrollable text viewer overlay for Q OS qdesktop.
// Renders .log/.txt/.toml/.json/.md files in a monospace gutter + body layout.
// Memory-bounded: max 1 MiB read, files larger are truncated with a footer line.
// Lazy-cached per-line textures evicted as scroll moves out of visible range.
// Input: D-pad up/down = 1 line; ZL/ZR = 1 page (24 lines); B closes via Close().
#pragma once
#include <pu/Plutonium>
#include <pu/sdl2/sdl2_Types.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace ul::menu::qdesktop {

class QdTextViewer : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdTextViewer>;

    // Viewer dimensions — always full screen.
    static constexpr s32 VIEWER_W = 1920;
    static constexpr s32 VIEWER_H = 1080;

    // Layout constants.
    static constexpr s32 GUTTER_W       = 72;   // 5 chars wide at ~14px each + pad
    static constexpr s32 CONTENT_X      = GUTTER_W + 8;
    static constexpr s32 CONTENT_W      = VIEWER_W - CONTENT_X - 8;
    static constexpr s32 LINE_H         = 22;    // approximate line height for Small font
    static constexpr s32 TOP_PAD        = 8;
    static constexpr s32 VISIBLE_LINES  = 24;    // (VIEWER_H - TOP_PAD*2) / LINE_H ~ 48; keep 24 for page unit + header

    // File read cap (bytes).
    static constexpr size_t MAX_READ_BYTES = 1u * 1024u * 1024u;  // 1 MiB
    // Line wrap column.
    static constexpr size_t WRAP_COLS = 120;

    static Ref New(const QdTheme &theme);

    QdTextViewer(const QdTheme &theme);
    ~QdTextViewer();

    // Load file at path. Returns true if opened successfully (0-byte is also ok).
    // Replaces any previously loaded file.
    bool LoadFile(const char *path);

    // Close the viewer — caller checks IsOpen() to remove from layout.
    void Close();

    // Whether the viewer is currently visible / open.
    bool IsOpen() const { return open_; }

    // ── pu::ui::elm::Element interface ───────────────────────────────────────
    s32 GetX() override { return 0; }
    s32 GetY() override { return 0; }
    s32 GetWidth()  override { return VIEWER_W; }
    s32 GetHeight() override { return VIEWER_H; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  const s32 origin_x, const s32 origin_y) override;
    void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held,
                 const pu::ui::TouchPoint touch_pos) override;

private:
    // Split raw_text_ into wrapped lines stored in lines_.
    // Each line starts with WRAP_COLS wrap applied.
    void BuildLines();

    // Ensure the texture for line index idx is rendered.
    // Returns nullptr if idx is out of range.
    SDL_Texture *EnsureLineTex(int idx);

    // Free all cached line textures.
    void FreeAllTextures();

    // Free line textures that are far from the current view window.
    // Keeps [scroll_top_ - CACHE_SLACK, scroll_top_ + VISIBLE_LINES + CACHE_SLACK].
    void EvictDistantTextures();

    static constexpr int CACHE_SLACK = 8;  // lines above/below view kept warm

    QdTheme     theme_;
    bool        open_       = false;
    std::string filename_;          // basename for display in header
    bool        truncated_  = false; // true when file was capped at MAX_READ_BYTES
    std::string raw_text_;           // raw bytes read from file (UTF-8 assumed)
    std::vector<std::string> lines_; // word-wrapped lines

    int scroll_top_  = 0;  // index of the first visible line

    // Per-line texture cache. Key = line index.
    std::unordered_map<int, SDL_Texture*> line_tex_cache_;

    // Header texture showing filename.
    SDL_Texture *header_tex_  = nullptr;
    // Footer texture showing truncation notice (may be nullptr).
    SDL_Texture *footer_tex_  = nullptr;
};

} // namespace ul::menu::qdesktop
