// qd_NintendoAppsLayout.cpp — 4×2 tile grid of Nintendo built-in app launchers.
//
// Natural canvas: 780×480 px.
// Tile grid: 4 cols × 2 rows, tile_w=175, tile_h=150, gap=12.
//   4×175 + 3×12 = 736 px → grid_x computed at runtime from GetNaturalW()
//                            so centring is self-consistent if canvas ever changes.
//   2×150 + 12   = 312 px → fits in 374 px available (480 - 56 body_top - 50 hint bar).
//
// Touch input: content-local coordinates pre-translated by QdWindow.
// Tap on tile i calls kNintendoApps[i].launch(), which blocks until the applet exits.
// No on_tick needed — launchers are synchronous (SDL event loop is not re-entered).
#include <ul/menu/qdesktop/qd_NintendoAppsLayout.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>  // v2.6.0 — full-screen bg reads g_QdTheme.desktop_bg
#include <ul/menu/ui/ui_MenuApplication.hpp>
#include <ul/menu/ui/ui_Common.hpp>      // TryFindLoadImage (theme-aware PNG loader)
#include <ul/ul_Result.hpp>             // UL_LOG_INFO / UL_LOG_WARN (album browser)
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>             // IMG_Load_RW — decode album JPEGs
#include <switch.h>                     // caps:a album-accessor service
#include <cstring>
#include <cstdio>
#include <algorithm>

// Global menu application instance (defined in main.cpp).
extern ul::menu::ui::MenuApplication::Ref g_MenuApplication;

namespace ul::menu::qdesktop {

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdNintendoAppsLayout::QdNintendoAppsLayout(const QdTheme &theme)
    : theme_(theme),
      hovered_idx_(-1),
      hint_bar_tex_(nullptr)
{
    // Build the bottom hint bar once; freed in the destructor.
    const pu::ui::Color hint_col { 0x99u, 0x99u, 0xBBu, 0xFFu };
    hint_bar_tex_ = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string("Tap a tile to open"),
        hint_col);
}

QdNintendoAppsLayout::~QdNintendoAppsLayout() {
    if (hint_bar_tex_ != nullptr) {
        pu::ui::render::DeleteTexture(hint_bar_tex_);
        hint_bar_tex_ = nullptr;
    }
    // Release every cached tile icon.  nullptr values are sentinels for
    // "load failed" — skip them.  pu::ui::render::DeleteTexture handles
    // SDL_DestroyTexture and the LRU bookkeeping in one call.
    for (auto &kv : icon_tex_cache_) {
        if (kv.second != nullptr) {
            pu::ui::render::DeleteTexture(kv.second);
        }
    }
    icon_tex_cache_.clear();

    // Release any decoded album screenshot + grid thumbnails still held open.
    FreeAlbumImage();
    FreeAlbumThumbs();
}

// ── TileRect ──────────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::TileRect(int i, s32 &tx, s32 &ty) const {
    // Row-major layout.  Grid X centred at runtime from GetNaturalW().
    const s32 grid_x = (GetNaturalW()
                        - QD_NA_TILE_COLS * QD_NA_TILE_W
                        - (QD_NA_TILE_COLS - 1) * QD_NA_TILE_GAP) / 2;
    const int col = i % QD_NA_TILE_COLS;
    const int row = i / QD_NA_TILE_COLS;
    tx = grid_x + col * (QD_NA_TILE_W + QD_NA_TILE_GAP);
    ty = QD_NA_BODY_TOP + row * (QD_NA_TILE_H + QD_NA_TILE_GAP);
}

// ── RenderTile ────────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::RenderTile(SDL_Renderer *r,
                                       s32 tx, s32 ty,
                                       const NintendoApp &app,
                                       bool hovered) const
{
    // ── Resolve the tile icon (lazy-load + cache by icon_path pointer) ───
    // Cache miss ⇒ try TryFindLoadImage (theme-aware, tries .png/.jpg in
    // active theme, then default theme).  Store the result — including a
    // nullptr sentinel for failed loads — so we never retry on later frames.
    SDL_Texture *icon_tex = nullptr;
    if (app.icon_path != nullptr) {
        auto it = icon_tex_cache_.find(app.icon_path);
        if (it == icon_tex_cache_.end()) {
            SDL_Texture *loaded = ::ul::menu::ui::TryFindLoadImage(
                std::string(app.icon_path));
            icon_tex_cache_[app.icon_path] = loaded;  // may be nullptr
            icon_tex = loaded;
        } else {
            icon_tex = it->second;
        }
    }

    // ── Tile background ──────────────────────────────────────────────────
    // When the icon loaded, dim the solid color way down so the icon reads
    // cleanly; the color still tints the corners enough to keep tiles
    // distinguishable.  When the icon is missing, retain the original solid
    // brightness so the label stays legible against the colored panel.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    {
        u8 alpha;
        if (icon_tex != nullptr) {
            alpha = hovered ? 0x80u : 0x50u;
        } else {
            alpha = hovered ? 0xF0u : 0xC0u;
        }
        SDL_SetRenderDrawColor(r, app.color.r, app.color.g, app.color.b, alpha);
        SDL_Rect tile_rect { tx, ty, QD_NA_TILE_W, QD_NA_TILE_H };
        SDL_RenderFillRect(r, &tile_rect);
    }

    // Border — cyan focus ring when hovered, dim otherwise.
    {
        const pu::ui::Color &border = hovered ? theme_.focus_ring : theme_.text_secondary;
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, hovered ? 0xFFu : 0x40u);
        SDL_Rect tile_rect { tx, ty, QD_NA_TILE_W, QD_NA_TILE_H };
        SDL_RenderDrawRect(r, &tile_rect);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Icon (if loaded), centred above the label, ~70% of tile area ────
    // Aspect-preserving fit into a 70%-tile square; label remains the
    // bottom strip so the layout is identical for icon and no-icon tiles.
    constexpr s32 kLabelStripH = 28;  // reserved height at tile bottom for label
    const s32 icon_box_w = (QD_NA_TILE_W * 70) / 100;
    const s32 icon_box_h = ((QD_NA_TILE_H - kLabelStripH) * 80) / 100;
    if (icon_tex != nullptr) {
        int iw = 0, ih = 0;
        SDL_QueryTexture(icon_tex, nullptr, nullptr, &iw, &ih);
        if (iw > 0 && ih > 0) {
            // Aspect-preserving fit.
            const float sx = static_cast<float>(icon_box_w) / static_cast<float>(iw);
            const float sy = static_cast<float>(icon_box_h) / static_cast<float>(ih);
            const float s  = (sx < sy) ? sx : sy;
            const s32 dw = static_cast<s32>(static_cast<float>(iw) * s);
            const s32 dh = static_cast<s32>(static_cast<float>(ih) * s);
            const s32 ix = tx + (QD_NA_TILE_W - dw) / 2;
            const s32 iy = ty + ((QD_NA_TILE_H - kLabelStripH) - dh) / 2;
            SDL_Rect idst { ix, iy, dw, dh };
            SDL_RenderCopy(r, icon_tex, nullptr, &idst);
        }
    }

    // ── Label ─────────────────────────────────────────────────────────────
    // When an icon is rendered, place the label in the bottom strip below
    // the icon.  When no icon (nullptr icon_path or load failed), keep the
    // legacy centered-label render path so the visual layout is preserved
    // for the three icon-less slots (Profile, Keyboard, Error Info).
    SDL_Texture *label_tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
        std::string(app.label),
        theme_.text_primary,
        static_cast<u32>(QD_NA_TILE_W - 8));
    if (label_tex != nullptr) {
        int lw = 0, lh = 0;
        SDL_QueryTexture(label_tex, nullptr, nullptr, &lw, &lh);
        const s32 lx = tx + (QD_NA_TILE_W - lw) / 2;
        s32 ly;
        if (icon_tex != nullptr) {
            // Bottom strip: vertically centre the label inside the
            // reserved kLabelStripH band.
            ly = ty + (QD_NA_TILE_H - kLabelStripH) + (kLabelStripH - lh) / 2;
        } else {
            // Legacy centred render — preserves no-icon look exactly.
            ly = ty + (QD_NA_TILE_H - lh) / 2;
        }
        SDL_Rect ldst { lx, ly, lw, lh };
        SDL_RenderCopy(r, label_tex, nullptr, &ldst);
        pu::ui::render::DeleteTexture(label_tex);
    }
}

// ── OnRender ──────────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::OnRender(pu::ui::render::Renderer::Ref &/*drawer*/,
                                     s32 origin_x, s32 origin_y)
{
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        return;
    }

    const s32 ax = origin_x;
    const s32 ay = origin_y;

    // ── 1. Full-screen background ─────────────────────────────────────────────
    // v2.6.0 — theme-aware: was hardcoded near-black.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    {
        const auto &db = ::ul::menu::qdesktop::g_QdTheme.desktop_bg;
        SDL_SetRenderDrawColor(r, db.r, db.g, db.b, 0xF4u);
    }
    SDL_Rect bg { ax, ay, GetNaturalW(), GetNaturalH() };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Album browser takes over the whole content area while open ─────────────
    if (album_mode_ == AlbumMode::List)  { RenderAlbumList(r, ax, ay);  return; }
    if (album_mode_ == AlbumMode::Image) { RenderAlbumImage(r, ax, ay); return; }

    // ── 2. Header bar ─────────────────────────────────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,
        theme_.topbar_bg.r, theme_.topbar_bg.g, theme_.topbar_bg.b, 0xF0u);
    SDL_Rect hbar { ax, ay, GetNaturalW(), 48 };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    {
        SDL_Texture *title_tex = pu::ui::render::RenderText(
            pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small),
            std::string("Nintendo Apps"),
            theme_.accent,
            static_cast<u32>(GetNaturalW() - 16));
        if (title_tex != nullptr) {
            int tw = 0, th = 0;
            SDL_QueryTexture(title_tex, nullptr, nullptr, &tw, &th);
            SDL_Rect tdst { ax + 8, ay + (48 - th) / 2, tw, th };
            SDL_RenderCopy(r, title_tex, nullptr, &tdst);
            pu::ui::render::DeleteTexture(title_tex);
        }
    }

    // ── 3. Tile grid ──────────────────────────────────────────────────────────
    for (int i = 0; i < static_cast<int>(kNintendoAppCount); ++i) {
        s32 tx = 0, ty = 0;
        TileRect(i, tx, ty);
        RenderTile(r, ax + tx, ay + ty, kNintendoApps[i], hovered_idx_ == i);
    }

    // ── 4. Bottom hint bar ────────────────────────────────────────────────────
    if (hint_bar_tex_ != nullptr) {
        int hw = 0, hh = 0;
        SDL_QueryTexture(hint_bar_tex_, nullptr, nullptr, &hw, &hh);
        const s32 hx = ax + (GetNaturalW() - hw) / 2;
        const s32 hy = ay + GetNaturalH() - 8 - hh;
        SDL_Rect hdst { hx, hy, hw, hh };
        SDL_RenderCopy(r, hint_bar_tex_, nullptr, &hdst);
    }
}

// ── OnInput ───────────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::OnInput(const u64 keys_down, const u64 /*keys_up*/,
                                    const u64 /*keys_held*/,
                                    const pu::ui::TouchPoint touch_pos)
{
    // While the album browser is open it owns all input (D-pad / A / X + touch);
    // the tile grid is hidden underneath.
    if (album_mode_ != AlbumMode::Tiles) {
        AlbumInput(keys_down, touch_pos);
        return;
    }

    // touch_pos is already in content-local natural coordinates (pre-translated by QdWindow).
    // Hit-test each tile.  On tap (touch_pos.valid == true), call the matching launcher.
    if (touch_pos.IsEmpty()) {
        // Mouse hover — update hovered_idx_ for visual feedback without launching.
        hovered_idx_ = -1;
        for (int i = 0; i < static_cast<int>(kNintendoAppCount); ++i) {
            s32 tx = 0, ty = 0;
            TileRect(i, tx, ty);
            if (touch_pos.x >= tx && touch_pos.x < tx + QD_NA_TILE_W &&
                touch_pos.y >= ty && touch_pos.y < ty + QD_NA_TILE_H)
            {
                hovered_idx_ = i;
                break;
            }
        }
        return;
    }

    // Finger-down tap — find which tile was hit and invoke its launcher.
    for (int i = 0; i < static_cast<int>(kNintendoAppCount); ++i) {
        s32 tx = 0, ty = 0;
        TileRect(i, tx, ty);
        if (touch_pos.x >= tx && touch_pos.x < tx + QD_NA_TILE_W &&
            touch_pos.y >= ty && touch_pos.y < ty + QD_NA_TILE_H)
        {
            hovered_idx_ = i;
            // Two tiles can't be plain library-applet launches:
            //  • Album   — PhotoViewer (0x15) is hijacked to hbl here, so we
            //              open the in-window caps:a screenshot browser instead.
            //  • Settings— stock System Settings is rendered inside qlaunch
            //              (which Q OS replaces) and the standalone "set" applet
            //              is devkit-only (not on retail), so there is nothing
            //              to launch; open the windowed Q OS Settings in-place
            //              (no reboot), matching how uLaunch routes "Settings".
            if (kNintendoApps[i].launch == &LaunchAlbum) {
                OpenAlbumBrowser();
            } else if (kNintendoApps[i].launch == &LaunchSystemSettings) {
                if (on_open_settings_) {
                    on_open_settings_();
                } else {
                    kNintendoApps[i].launch();  // safety fallback
                }
            } else {
                kNintendoApps[i].launch();
            }
            // After the applet/browser returns, clear hover so the tile doesn't
            // stay highlighted.
            hovered_idx_ = -1;
            return;
        }
    }

    // Tap outside all tiles — clear hover.
    hovered_idx_ = -1;
}

// ── Album browser (caps:a screenshot viewer) ───────────────────────────────────
//
// PhotoViewer (AppletId 0x15) is hijacked to hbl on this setup, so the stock
// Album applet cannot be launched.  Instead the Album tile opens this in-window
// browser: caps:a (the Album Accessor service, granted by uMenu's "*" SAC) is
// used to enumerate every screenshot on SD + NAND and load the selected JPEG,
// which SDL_image decodes into a texture shown fit-to-window.

namespace {

// Draw one line of small text at (x, y); mirrors the label path in RenderTile.
// Returns the rendered height in px (0 on failure).  wrap == 0 ⇒ no wrapping.
int DrawAlbumText(SDL_Renderer *r, const std::string &s, s32 x, s32 y,
                  const pu::ui::Color &col, u32 wrap)
{
    SDL_Texture *t = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small), s, col, wrap);
    if (t == nullptr) {
        return 0;
    }
    int w = 0, h = 0;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    pu::ui::render::DeleteTexture(t);
    return h;
}

constexpr s32 kAlbumRowH    = 30;   // list row height (px, natural)
constexpr s32 kAlbumHdrH    = 48;   // list header bar height
constexpr s32 kAlbumHintH   = 26;   // bottom hint strip height

}  // namespace

// ── OpenAlbumBrowser ───────────────────────────────────────────────────────────

void QdNintendoAppsLayout::OpenAlbumBrowser() {
    UL_LOG_INFO("qd_NintendoApps: opening album browser (caps:a)");
    album_items_.clear();
    album_sel_ = 0;
    album_top_ = 0;
    album_status_.clear();
    FreeAlbumImage();

    const Result rc = capsaInitialize();
    if (R_FAILED(rc)) {
        UL_LOG_WARN("qd_NintendoApps: capsaInitialize failed: 0x%08X", static_cast<unsigned>(rc));
        album_status_ = "Album service unavailable.";
        album_mode_ = AlbumMode::List;
        return;
    }

    // Enumerate both storages — SD first (where most user shots live), then NAND.
    const CapsAlbumStorage storages[2] = { CapsAlbumStorage_Sd, CapsAlbumStorage_Nand };
    for (const CapsAlbumStorage st : storages) {
        u64 count = 0;
        if (R_FAILED(capsaGetAlbumFileCount(st, &count)) || count == 0) {
            continue;
        }
        if (count > 2048) {
            count = 2048;  // sane upper bound on one storage
        }
        std::vector<CapsAlbumEntry> entries(static_cast<size_t>(count));
        u64 got = 0;
        if (R_FAILED(capsaGetAlbumFileList(st, &got, entries.data(), count))) {
            continue;
        }
        if (got > count) {
            got = count;
        }
        for (u64 i = 0; i < got; ++i) {
            const CapsAlbumEntry &e = entries[static_cast<size_t>(i)];
            // Screenshots only — movies would need a video player we don't have.
            if (e.file_id.content != CapsAlbumFileContents_ScreenShot &&
                e.file_id.content != CapsAlbumFileContents_ExtraScreenShot) {
                continue;
            }
            AlbumItem it{};
            it.file_id = e.file_id;
            it.size    = e.size;
            const CapsAlbumFileDateTime &d = e.file_id.datetime;
            std::snprintf(it.label, sizeof(it.label),
                          "%04u-%02u-%02u %02u:%02u:%02u",
                          static_cast<unsigned>(d.year),  static_cast<unsigned>(d.month),
                          static_cast<unsigned>(d.day),   static_cast<unsigned>(d.hour),
                          static_cast<unsigned>(d.minute),static_cast<unsigned>(d.second));
            album_items_.push_back(it);
        }
    }
    capsaExit();

    // The list API returns chronological order (oldest first); show newest first.
    std::reverse(album_items_.begin(), album_items_.end());

    if (album_items_.empty()) {
        album_status_ = "No screenshots found.";
    }
    UL_LOG_INFO("qd_NintendoApps: album browser listed %zu screenshots", album_items_.size());
    album_mode_ = AlbumMode::List;
}

// ── CloseAlbumBrowser ──────────────────────────────────────────────────────────

void QdNintendoAppsLayout::CloseAlbumBrowser() {
    FreeAlbumImage();
    FreeAlbumThumbs();          // free grid thumbnail textures before dropping items
    album_items_.clear();
    album_items_.shrink_to_fit();
    album_sel_ = 0;
    album_top_ = 0;
    album_status_.clear();
    album_mode_ = AlbumMode::Tiles;
}

// ── FreeAlbumImage ─────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::FreeAlbumImage() {
    if (album_img_tex_ != nullptr) {
        SDL_DestroyTexture(album_img_tex_);
        album_img_tex_ = nullptr;
    }
    album_img_w_ = 0;
    album_img_h_ = 0;
}

// ── EnsureAlbumThumb / FreeAlbumThumbs (grid previews) ──────────────────────────
// Lazily decode a 320x180 RGBA thumbnail for one item and cache it as a texture.
// Self-contained caps:a init/exit per decode (mirrors LoadAlbumImage's pattern);
// only called for VISIBLE grid cells, so the per-decode cost is bounded.

void QdNintendoAppsLayout::EnsureAlbumThumb(SDL_Renderer *r, int idx) {
    if (r == nullptr || idx < 0 || idx >= static_cast<int>(album_items_.size())) {
        return;
    }
    AlbumItem &it = album_items_[static_cast<size_t>(idx)];
    if (it.thumb_tex != nullptr || it.thumb_failed) {
        return;
    }
    // 320x180 RGBA out + JPEG-decode scratch.  Static (reused; UI thread only).
    static u8 s_thumb_rgba[320 * 180 * 4];
    static u8 s_thumb_work[0x16000];

    if (R_FAILED(capsaInitialize())) { it.thumb_failed = true; return; }
    u64 tw = 0, th = 0;
    const Result rc = capsaLoadAlbumScreenShotThumbnailImage(
        &tw, &th, &it.file_id,
        s_thumb_rgba, sizeof(s_thumb_rgba),
        s_thumb_work, sizeof(s_thumb_work));
    capsaExit();
    if (R_FAILED(rc) || tw == 0 || th == 0) {
        it.thumb_failed = true;
        return;
    }
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC,
                                         static_cast<int>(tw), static_cast<int>(th));
    if (tex == nullptr) { it.thumb_failed = true; return; }
    SDL_UpdateTexture(tex, nullptr, s_thumb_rgba, static_cast<int>(tw) * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    it.thumb_tex = tex;
}

void QdNintendoAppsLayout::FreeAlbumThumbs() {
    for (auto &it : album_items_) {
        if (it.thumb_tex != nullptr) {
            SDL_DestroyTexture(it.thumb_tex);
            it.thumb_tex = nullptr;
        }
        it.thumb_failed = false;
    }
}

// ── GetBottomHint — feeds the WINDOW's chrome status bar (no in-content hint) ────
// B is hierarchical-back: it pops one level (Image -> grid -> Tiles) and only
// CLOSES the window at the top (Tiles).  Hints reflect that per mode.
std::string QdNintendoAppsLayout::GetBottomHint() const {
    switch (album_mode_) {
        case AlbumMode::List:  return "D-pad / Touch: Select   A: View   B: Back";
        case AlbumMode::Image: return "Left / Right: Prev / Next   Touch / B: Back";
        case AlbumMode::Tiles:
        default:               return "A: Launch   B: Close";
    }
}

// ── OnBackRequested — hierarchical B (matches the X-back exactly) ────────────────
// QdWindow calls this when B is pressed, BEFORE closing the window.  Pop one level
// and return true (keep the window open); return false at the top so it closes.
bool QdNintendoAppsLayout::OnBackRequested() {
    switch (album_mode_) {
        case AlbumMode::Image:
            FreeAlbumImage();
            album_status_.clear();
            album_mode_ = AlbumMode::List;   // back to the thumbnail grid
            return true;
        case AlbumMode::List:
            CloseAlbumBrowser();             // back to the Nintendo Apps tiles
            return true;
        case AlbumMode::Tiles:
        default:
            return false;                    // top level — let the window close
    }
}

// ── GetDebugState ─────────────────────────────────────────────────────────────

std::string QdNintendoAppsLayout::GetDebugState() const {
    switch (album_mode_) {
        case AlbumMode::Tiles:
            return "album:Tiles";
        case AlbumMode::List:
            return "album:List:sel=" + std::to_string(album_sel_);
        case AlbumMode::Image:
            return "album:Image:sel=" + std::to_string(album_sel_);
        default:
            return "album:?";
    }
}

// ── LoadAlbumImage ─────────────────────────────────────────────────────────────

bool QdNintendoAppsLayout::LoadAlbumImage(int idx) {
    FreeAlbumImage();
    if (idx < 0 || idx >= static_cast<int>(album_items_.size())) {
        return false;
    }
    const AlbumItem &it = album_items_[static_cast<size_t>(idx)];

    const Result rc_init = capsaInitialize();
    if (R_FAILED(rc_init)) {
        album_status_ = "Album service unavailable.";
        return false;
    }

    u64 fsize = it.size;
    if (fsize == 0) {
        capsaGetAlbumFileSize(&it.file_id, &fsize);
    }
    // Screenshots are well under 1 MiB; cap at 8 MiB to bound the allocation.
    if (fsize == 0 || fsize > 8u * 1024u * 1024u) {
        capsaExit();
        album_status_ = "Screenshot too large to load.";
        return false;
    }

    std::vector<u8> buf(static_cast<size_t>(fsize));
    u64 out_size = 0;
    const Result rc_load = capsaLoadAlbumFile(&it.file_id, &out_size, buf.data(), buf.size());
    capsaExit();
    if (R_FAILED(rc_load) || out_size == 0) {
        UL_LOG_WARN("qd_NintendoApps: capsaLoadAlbumFile failed rc=0x%08X out=%llu",
                    static_cast<unsigned>(rc_load), static_cast<unsigned long long>(out_size));
        album_status_ = "Could not load screenshot.";
        return false;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(buf.data(), static_cast<int>(out_size));
    if (rw == nullptr) {
        album_status_ = "Decode init failed.";
        return false;
    }
    SDL_Surface *raw = IMG_Load_RW(rw, /*freesrc=*/1);  // frees rw regardless
    if (raw == nullptr) {
        UL_LOG_WARN("qd_NintendoApps: IMG_Load_RW failed: %s", IMG_GetError());
        album_status_ = "Decode failed.";
        return false;
    }
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);
    if (conv == nullptr) {
        album_status_ = "Convert failed.";
        return false;
    }
    SDL_Renderer *r = pu::ui::render::GetMainRenderer();
    if (r == nullptr) {
        SDL_FreeSurface(conv);
        album_status_ = "No renderer.";
        return false;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, conv);
    album_img_w_ = conv->w;
    album_img_h_ = conv->h;
    SDL_FreeSurface(conv);
    if (tex == nullptr) {
        UL_LOG_WARN("qd_NintendoApps: CreateTextureFromSurface failed: %s", SDL_GetError());
        album_status_ = "Texture upload failed.";
        album_img_w_ = album_img_h_ = 0;
        return false;
    }
    album_img_tex_ = tex;
    album_status_.clear();
    UL_LOG_INFO("qd_NintendoApps: album image %d loaded %dx%d", idx, album_img_w_, album_img_h_);
    return true;
}

// ── RenderAlbumList ────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::RenderAlbumList(SDL_Renderer *r, s32 ax, s32 ay) {
    // Header bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g, theme_.topbar_bg.b, 0xF0u);
    SDL_Rect hbar { ax, ay, GetNaturalW(), kAlbumHdrH };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    char title[56];
    std::snprintf(title, sizeof(title), "Album  —  %d screenshot%s",
                  static_cast<int>(album_items_.size()),
                  album_items_.size() == 1 ? "" : "s");
    DrawAlbumText(r, title, ax + 8, ay + 16, theme_.accent,
                  static_cast<u32>(GetNaturalW() - 16));

    const s32 body_top = ay + kAlbumHdrH + 6;
    if (album_items_.empty()) {
        DrawAlbumText(r, album_status_.empty() ? "No screenshots found." : album_status_,
                      ax + 12, body_top, theme_.text_secondary,
                      static_cast<u32>(GetNaturalW() - 24));
    } else {
        // ── Thumbnail grid (lazy-decode visible cells; free off-window) ─────────
        const s32 nat_w  = GetNaturalW();
        const s32 margin = 16, gap = 12;
        const int cols   = 4;                                   // 4-wide preview grid
        const s32 cellW  = (nat_w - 2 * margin - (cols - 1) * gap) / cols;
        const s32 cellH  = cellW * 9 / 16;                       // 16:9 previews
        const s32 grid_h = GetNaturalH() - kAlbumHdrH - kAlbumHintH - 12;
        int rows_vis = (grid_h + gap) / (cellH + gap);
        if (rows_vis < 1) rows_vis = 1;

        const int n          = static_cast<int>(album_items_.size());
        const int total_rows = (n + cols - 1) / cols;
        if (album_top_ > total_rows - rows_vis) album_top_ = total_rows - rows_vis;
        if (album_top_ < 0) album_top_ = 0;

        // Bound memory: free thumbnails more than one row outside the window.
        const int keep_lo = (album_top_ - 1) * cols;
        const int keep_hi = (album_top_ + rows_vis + 1) * cols;
        for (int i = 0; i < n; ++i) {
            AlbumItem &fi = album_items_[static_cast<size_t>(i)];
            if ((i < keep_lo || i >= keep_hi) && fi.thumb_tex) {
                SDL_DestroyTexture(fi.thumb_tex);
                fi.thumb_tex = nullptr;
                fi.thumb_failed = false;   // allow re-decode if it scrolls back
            }
        }

        for (int vr = 0; vr < rows_vis; ++vr) {
            const int row = album_top_ + vr;
            for (int c = 0; c < cols; ++c) {
                const int idx = row * cols + c;
                if (idx >= n) break;
                const s32 cx = ax + margin + c * (cellW + gap);
                const s32 cy = body_top + vr * (cellH + gap);
                const SDL_Rect cell { cx, cy, cellW, cellH };

                EnsureAlbumThumb(r, idx);
                AlbumItem &it = album_items_[static_cast<size_t>(idx)];
                if (it.thumb_tex != nullptr) {
                    SDL_RenderCopy(r, it.thumb_tex, nullptr, &cell);
                } else {
                    SDL_SetRenderDrawColor(r, theme_.titlebar_inactive.r,
                        theme_.titlebar_inactive.g, theme_.titlebar_inactive.b, 255);
                    SDL_RenderFillRect(r, &cell);
                }
                if (idx == album_sel_) {
                    SDL_SetRenderDrawColor(r, theme_.accent.r, theme_.accent.g,
                                           theme_.accent.b, 255);
                    for (int t = 0; t < 3; ++t) {
                        SDL_Rect rr { cell.x - t, cell.y - t, cell.w + 2 * t, cell.h + 2 * t };
                        SDL_RenderDrawRect(r, &rr);
                    }
                }
            }
        }
    }

    // Hint shown in the WINDOW's chrome status bar (GetBottomHint via on_tick) —
    // not drawn in-content, so the album matches every other window.
}

// ── RenderAlbumImage ───────────────────────────────────────────────────────────

void QdNintendoAppsLayout::RenderAlbumImage(SDL_Renderer *r, s32 ax, s32 ay) const {
    constexpr s32 hdr_h = 32;

    // Header bar.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, theme_.topbar_bg.r, theme_.topbar_bg.g, theme_.topbar_bg.b, 0xF0u);
    SDL_Rect hbar { ax, ay, GetNaturalW(), hdr_h };
    SDL_RenderFillRect(r, &hbar);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    char title[56];
    const char *stamp =
        (album_sel_ >= 0 && album_sel_ < static_cast<int>(album_items_.size()))
            ? album_items_[static_cast<size_t>(album_sel_)].label : "";
    std::snprintf(title, sizeof(title), "%s   (%d/%d)", stamp, album_sel_ + 1,
                  static_cast<int>(album_items_.size()));
    DrawAlbumText(r, title, ax + 8, ay + 8, theme_.accent,
                  static_cast<u32>(GetNaturalW() - 16));

    // Image area (between header and hint).
    const s32 area_x = ax;
    const s32 area_y = ay + hdr_h;
    const s32 area_w = GetNaturalW();
    const s32 area_h = GetNaturalH() - hdr_h - kAlbumHintH;

    if (album_img_tex_ != nullptr && album_img_w_ > 0 && album_img_h_ > 0) {
        const float fx = static_cast<float>(area_w) / static_cast<float>(album_img_w_);
        const float fy = static_cast<float>(area_h) / static_cast<float>(album_img_h_);
        const float fs = (fx < fy) ? fx : fy;
        const s32 dw = static_cast<s32>(static_cast<float>(album_img_w_) * fs);
        const s32 dh = static_cast<s32>(static_cast<float>(album_img_h_) * fs);
        SDL_Rect dst { area_x + (area_w - dw) / 2, area_y + (area_h - dh) / 2, dw, dh };
        SDL_RenderCopy(r, album_img_tex_, nullptr, &dst);
    } else {
        DrawAlbumText(r, album_status_.empty() ? "Loading..." : album_status_,
                      area_x + 12, area_y + 12, theme_.text_primary,
                      static_cast<u32>(area_w - 24));
    }

    // Hint shown in the WINDOW's chrome status bar (GetBottomHint via on_tick).
}

// ── AlbumInput ─────────────────────────────────────────────────────────────────

void QdNintendoAppsLayout::AlbumInput(u64 keys_down, const pu::ui::TouchPoint &touch) {
    // Touch debounce — act only on a fresh touch-down, once per touch.  A tap that
    // switches mode (Image→List or List→Image) must NOT also be handled by the
    // destination mode on the same held touch (that bled through and opened a file
    // underneath the image).
    const bool touch_now   = !touch.IsEmpty();
    if (!touch_now) album_touch_latched_ = false;
    const bool touch_fresh = touch_now && !album_touch_latched_;

    if (album_mode_ == AlbumMode::List) {
        const int n = static_cast<int>(album_items_.size());

        // Grid geometry — MUST match RenderAlbumList.
        const s32 nat_w  = GetNaturalW();
        const s32 margin = 16, gap = 12;
        const int cols   = 4;
        const s32 cellW  = (nat_w - 2 * margin - (cols - 1) * gap) / cols;
        const s32 cellH  = cellW * 9 / 16;
        const s32 body_top = kAlbumHdrH + 6;
        const s32 grid_h   = GetNaturalH() - kAlbumHdrH - kAlbumHintH - 12;
        int rows_vis = (grid_h + gap) / (cellH + gap);
        if (rows_vis < 1) rows_vis = 1;

        // Touch: tap a visible cell to open it (reject taps that land in the gap).
        if (touch_fresh && n > 0) {
            const s32 rel_x = static_cast<s32>(touch.x) - margin;
            const s32 rel_y = static_cast<s32>(touch.y) - body_top;
            if (rel_x >= 0 && rel_y >= 0) {
                const int c  = rel_x / (cellW + gap);
                const int vr = rel_y / (cellH + gap);
                const bool in_cell = (rel_x % (cellW + gap)) < cellW &&
                                     (rel_y % (cellH + gap)) < cellH;
                if (in_cell && c >= 0 && c < cols && vr >= 0 && vr < rows_vis) {
                    const int idx = (album_top_ + vr) * cols + c;
                    if (idx >= 0 && idx < n) {
                        album_sel_ = idx;
                        LoadAlbumImage(idx);             // sets album_status_ on failure
                        album_mode_ = AlbumMode::Image;
                        album_touch_latched_ = true;     // this touch is consumed
                    }
                }
            }
            return;
        }

        if (keys_down & HidNpadButton_X) {
            CloseAlbumBrowser();
            return;
        }
        if (n == 0) {
            return;
        }
        // Grid navigation (Left/Right within a row, Up/Down across rows).
        if ((keys_down & HidNpadButton_Right) && album_sel_ < n - 1 &&
            (album_sel_ % cols) != cols - 1) {
            album_sel_++;
        }
        if ((keys_down & HidNpadButton_Left) && (album_sel_ % cols) != 0) {
            album_sel_--;
        }
        if ((keys_down & HidNpadButton_Down) && album_sel_ + cols < n) {
            album_sel_ += cols;
        }
        if ((keys_down & HidNpadButton_Up) && album_sel_ - cols >= 0) {
            album_sel_ -= cols;
        }
        // Keep the selected row visible (scroll by rows).
        const int sel_row = album_sel_ / cols;
        if (sel_row < album_top_)             album_top_ = sel_row;
        if (sel_row >= album_top_ + rows_vis) album_top_ = sel_row - rows_vis + 1;
        if (album_top_ < 0) album_top_ = 0;

        if (keys_down & HidNpadButton_A) {
            LoadAlbumImage(album_sel_);                  // sets album_status_ on failure
            album_mode_ = AlbumMode::Image;
        }
        return;
    }

    if (album_mode_ == AlbumMode::Image) {
        const int n = static_cast<int>(album_items_.size());
        if (keys_down & HidNpadButton_X) {
            FreeAlbumImage();
            album_status_.clear();
            album_mode_ = AlbumMode::List;
            return;
        }
        if ((keys_down & HidNpadButton_Right) && album_sel_ < n - 1) {
            album_sel_++;
            LoadAlbumImage(album_sel_);
        }
        if ((keys_down & HidNpadButton_Left) && album_sel_ > 0) {
            album_sel_--;
            LoadAlbumImage(album_sel_);
        }
        // Touch anywhere returns to the list (debounced — this tap is consumed and
        // will NOT fall through to the List handler to open a file underneath).
        if (touch_fresh) {
            FreeAlbumImage();
            album_status_.clear();
            album_mode_ = AlbumMode::List;
            album_touch_latched_ = true;
        }
        return;
    }
}

} // namespace ul::menu::qdesktop
