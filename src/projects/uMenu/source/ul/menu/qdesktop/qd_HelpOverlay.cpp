// qd_HelpOverlay.cpp — Full-screen help modal implementation (uMenu v1.8.25).
//
// All text textures are pre-rendered in Open() and blitted cheaply in Render().
// Close() frees every texture via pu::ui::render::DeleteTexture, which honours
// the Plutonium LRU cache contract (B41/B42 — callers must use DeleteTexture,
// never SDL_DestroyTexture, for textures returned by RenderText).
//
// Layout summary (1920×1080):
//   y=40           Title bar — "Help — Q OS Menu v1.8.25" (cyan, Large)
//   y=100          Column headers (key | action)
//   y=140..~900    Five sections, each: header (magenta) + body rows (white)
//                  Left column (keys) x=120; right column (actions) x=460
//   y=1020         Footer — "Press any button to close" (lavender, Small)
//
// Background: navy #0E1A33 at 92% alpha (EA), full 1920×1080 SDL rect.

#include <ul/menu/qdesktop/qd_HelpOverlay.hpp>
#include <atomic>
#include <string>

namespace ul::menu::qdesktop {

// ── Palette ──────────────────────────────────────────────────────────────────
// All colours are Q OS palette values specified in the task brief.
// Alpha is 0xFF (fully opaque) for all text textures.
static constexpr pu::ui::Color kColorTitle    { 0x00u, 0xE5u, 0xFFu, 0xFFu };  // cyan
static constexpr pu::ui::Color kColorHeader   { 0xD9u, 0x46u, 0xEFu, 0xFFu };  // magenta
static constexpr pu::ui::Color kColorBody     { 0xFFu, 0xFFu, 0xFFu, 0xFFu };  // white
static constexpr pu::ui::Color kColorFooter   { 0xA7u, 0x8Bu, 0xFAu, 0xFFu };  // lavender

// Background rectangle color components — navy #0E1A33 at 92% alpha (0xEA).
static constexpr u8 kBgR = 0x0Eu;
static constexpr u8 kBgG = 0x1Au;
static constexpr u8 kBgB = 0x33u;
static constexpr u8 kBgA = 0xEAu;

// ── Layout constants ─────────────────────────────────────────────────────────
static constexpr int kScreenW     = 1920;
static constexpr int kScreenH     = 1080;
static constexpr int kColKey      = 120;   // x-origin of key (left) column
static constexpr int kColAction   = 460;   // x-origin of action (right) column
static constexpr int kRowStep     = 38;    // px between body text rows
static constexpr int kHdrStep     = 48;    // px below section header before first row
static constexpr int kSectionGap  = 24;    // extra px between sections
static constexpr int kTitleY      = 40;
static constexpr int kFirstY      = 110;   // y of first section header
static constexpr int kFooterY     = 1020;

// ── Static helpers ────────────────────────────────────────────────────────────

// static
void QdHelpOverlay::MakeText(SDL_Renderer */*r*/,
                              pu::ui::DefaultFontSize font_size,
                              const char *text,
                              pu::ui::Color color,
                              SDL_Texture **out_tex,
                              int *out_w, int *out_h)
{
    *out_tex = nullptr;
    *out_w   = 0;
    *out_h   = 0;
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    SDL_Texture *tex = pu::ui::render::RenderText(
        pu::ui::GetDefaultFont(font_size),
        std::string(text),
        color);
    if (tex == nullptr) {
        return;
    }
    int w = 0, h = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    *out_tex = tex;
    *out_w   = w;
    *out_h   = h;
}

// static
void QdHelpOverlay::FreeTexture(SDL_Texture **tex)
{
    if (tex == nullptr || *tex == nullptr) {
        return;
    }
    pu::ui::render::DeleteTexture(*tex);
    *tex = nullptr;
}

// static
void QdHelpOverlay::Blit(SDL_Renderer *r, SDL_Texture *tex, int x, int y, int w, int h)
{
    if (tex == nullptr || r == nullptr) {
        return;
    }
    SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdHelpOverlay::QdHelpOverlay() = default;

QdHelpOverlay::~QdHelpOverlay()
{
    Close();
}

// ── Open ─────────────────────────────────────────────────────────────────────
// Pre-renders every string into its member texture. Calling Open() when already
// open discards the old textures and rebuilds — safe for resolution/font changes.

void QdHelpOverlay::Open(SDL_Renderer *r)
{
    // Free any previously cached textures first (re-open is safe).
    Close();
    open_ = true;

    // ── Title ─────────────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Large,
             "Help — Q OS Menu v" UL_VERSION,
             kColorTitle,
             &tex_title_, &title_w_, &title_h_);

    // ── Section headers ───────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::MediumLarge,
             "DESKTOP CONTROLS",
             kColorHeader,
             &tex_hdr_desktop_, &hdr_desktop_w_, &hdr_desktop_h_);

    MakeText(r, pu::ui::DefaultFontSize::MediumLarge,
             "LAUNCHPAD CONTROLS",
             kColorHeader,
             &tex_hdr_launchpad_, &hdr_launchpad_w_, &hdr_launchpad_h_);

    MakeText(r, pu::ui::DefaultFontSize::MediumLarge,
             "VAULT (FILE MANAGER)",
             kColorHeader,
             &tex_hdr_vault_, &hdr_vault_w_, &hdr_vault_h_);

    MakeText(r, pu::ui::DefaultFontSize::MediumLarge,
             "LOGIN SCREEN",
             kColorHeader,
             &tex_hdr_login_, &hdr_login_w_, &hdr_login_h_);

    MakeText(r, pu::ui::DefaultFontSize::MediumLarge,
             "HOT CORNER",
             kColorHeader,
             &tex_hdr_hotcorner_, &hdr_hotcorner_w_, &hdr_hotcorner_h_);

    // ── Desktop Controls — 6 rows ─────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium, "D-pad",         kColorBody, &tex_d0k_, &d0k_w_, &d0k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Navigate folders, dock, favorites", kColorBody, &tex_d0a_, &d0a_w_, &d0a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "A",             kColorBody, &tex_d1k_, &d1k_w_, &d1k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Open / launch focused tile", kColorBody, &tex_d1a_, &d1a_w_, &d1a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "B / +",         kColorBody, &tex_d2k_, &d2k_w_, &d2k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Close current screen", kColorBody, &tex_d2a_, &d2a_w_, &d2a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "Y",             kColorBody, &tex_d3k_, &d3k_w_, &d3k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Toggle favorite on focused tile", kColorBody, &tex_d3a_, &d3a_w_, &d3a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "ZR",            kColorBody, &tex_d4k_, &d4k_w_, &d4k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Click whatever the cursor is hovering", kColorBody, &tex_d4a_, &d4a_w_, &d4a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "+ + Share",     kColorBody, &tex_d5k_, &d5k_w_, &d5k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Open this help", kColorBody, &tex_d5a_, &d5a_w_, &d5a_h_);

    // ── Launchpad Controls — 5 rows ───────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium, "D-pad",         kColorBody, &tex_l0k_, &l0k_w_, &l0k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Navigate the grid", kColorBody, &tex_l0a_, &l0a_w_, &l0a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "A",             kColorBody, &tex_l1k_, &l1k_w_, &l1k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Launch focused tile", kColorBody, &tex_l1a_, &l1a_w_, &l1a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "Y",             kColorBody, &tex_l2k_, &l2k_w_, &l2k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Toggle favorites view", kColorBody, &tex_l2a_, &l2a_w_, &l2a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "L / R",         kColorBody, &tex_l3k_, &l3k_w_, &l3k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Previous / next page", kColorBody, &tex_l3a_, &l3a_w_, &l3a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "B / +",         kColorBody, &tex_l4k_, &l4k_w_, &l4k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Return to Desktop", kColorBody, &tex_l4a_, &l4a_w_, &l4a_h_);

    // ── Vault — 4 rows ────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium, "D-pad",         kColorBody, &tex_v0k_, &v0k_w_, &v0k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Navigate files", kColorBody, &tex_v0a_, &v0a_w_, &v0a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "A",             kColorBody, &tex_v1k_, &v1k_w_, &v1k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Open file or folder", kColorBody, &tex_v1a_, &v1a_w_, &v1a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "Y",             kColorBody, &tex_v2k_, &v2k_w_, &v2k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Toggle favorite", kColorBody, &tex_v2a_, &v2a_w_, &v2a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "B / +",         kColorBody, &tex_v3k_, &v3k_w_, &v3k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Close",         kColorBody, &tex_v3a_, &v3a_w_, &v3a_h_);

    // ── Login Screen — 3 rows ─────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Touch tap",     kColorBody, &tex_g0k_, &g0k_w_, &g0k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Select user (250 ms or longer both fire)", kColorBody, &tex_g0a_, &g0a_w_, &g0a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "D-pad",         kColorBody, &tex_g1k_, &g1k_w_, &g1k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Navigate users", kColorBody, &tex_g1a_, &g1a_w_, &g1a_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium, "A",             kColorBody, &tex_g2k_, &g2k_w_, &g2k_h_);
    MakeText(r, pu::ui::DefaultFontSize::Medium, "Confirm",       kColorBody, &tex_g2a_, &g2a_w_, &g2a_h_);

    // ── Hot Corner — single paragraph ─────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium,
             "Tap top-left 60x48 px area to open Launchpad from Desktop, or close Launchpad back to Desktop.",
             kColorBody,
             &tex_hc_body_, &hc_body_w_, &hc_body_h_);

    // ── Footer ─────────────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Small,
             "Press any button to close",
             kColorFooter,
             &tex_footer_, &footer_w_, &footer_h_);
}

// ── Close ─────────────────────────────────────────────────────────────────────
// Frees all textures in declaration order. Uses DeleteTexture, which respects
// the Plutonium LRU cache contract for RenderText-owned pointers.

void QdHelpOverlay::Close()
{
    open_ = false;

    FreeTexture(&tex_title_);

    FreeTexture(&tex_hdr_desktop_);
    FreeTexture(&tex_hdr_launchpad_);
    FreeTexture(&tex_hdr_vault_);
    FreeTexture(&tex_hdr_login_);
    FreeTexture(&tex_hdr_hotcorner_);

    FreeTexture(&tex_d0k_); FreeTexture(&tex_d0a_);
    FreeTexture(&tex_d1k_); FreeTexture(&tex_d1a_);
    FreeTexture(&tex_d2k_); FreeTexture(&tex_d2a_);
    FreeTexture(&tex_d3k_); FreeTexture(&tex_d3a_);
    FreeTexture(&tex_d4k_); FreeTexture(&tex_d4a_);
    FreeTexture(&tex_d5k_); FreeTexture(&tex_d5a_);

    FreeTexture(&tex_l0k_); FreeTexture(&tex_l0a_);
    FreeTexture(&tex_l1k_); FreeTexture(&tex_l1a_);
    FreeTexture(&tex_l2k_); FreeTexture(&tex_l2a_);
    FreeTexture(&tex_l3k_); FreeTexture(&tex_l3a_);
    FreeTexture(&tex_l4k_); FreeTexture(&tex_l4a_);

    FreeTexture(&tex_v0k_); FreeTexture(&tex_v0a_);
    FreeTexture(&tex_v1k_); FreeTexture(&tex_v1a_);
    FreeTexture(&tex_v2k_); FreeTexture(&tex_v2a_);
    FreeTexture(&tex_v3k_); FreeTexture(&tex_v3a_);

    FreeTexture(&tex_g0k_); FreeTexture(&tex_g0a_);
    FreeTexture(&tex_g1k_); FreeTexture(&tex_g1a_);
    FreeTexture(&tex_g2k_); FreeTexture(&tex_g2a_);

    FreeTexture(&tex_hc_body_);
    FreeTexture(&tex_footer_);
}

// ── Render ────────────────────────────────────────────────────────────────────
// Blits the pre-rendered texture cache. No RenderText calls here.
// Layout: two-column grid. Left column starts at kColKey, right at kColAction.
// Each section: header row, then body rows at kRowStep intervals.

void QdHelpOverlay::Render(SDL_Renderer *r)
{
    if (!open_ || r == nullptr) {
        return;
    }

    // ── Background — full-screen navy rectangle ───────────────────────────────
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, kBgR, kBgG, kBgB, kBgA);
    SDL_Rect bg { 0, 0, kScreenW, kScreenH };
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    // ── Title — horizontally centered ─────────────────────────────────────────
    if (tex_title_ != nullptr) {
        const int tx = (kScreenW - title_w_) / 2;
        Blit(r, tex_title_, tx, kTitleY, title_w_, title_h_);
    }

    // ── Separator line under title ─────────────────────────────────────────────
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0x60u);  // dim cyan
        SDL_Rect sep { kColKey, kTitleY + title_h_ + 8, kScreenW - 2 * kColKey, 1 };
        SDL_RenderFillRect(r, &sep);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── Five sections ─────────────────────────────────────────────────────────
    // Each section is: header text, then one or more key/action row pairs.
    // We step y downwards as we go; y_cursor tracks the current top-of-row.

    int y = kFirstY;

    // Helper lambda: emit one key/action pair row and advance y by kRowStep.
    // Declared as a local lambda capturing r and y.
    auto row = [&](SDL_Texture *ktex, int kw, int kh,
                   SDL_Texture *atex, int aw, int ah) {
        Blit(r, ktex, kColKey,    y, kw, kh);
        Blit(r, atex, kColAction, y, aw, ah);
        y += kRowStep;
    };

    // Helper lambda: emit a section header and advance y.
    auto header = [&](SDL_Texture *htex, int hw, int hh) {
        Blit(r, htex, kColKey, y, hw, hh);
        y += kHdrStep;
    };

    // ── Section 1: Desktop Controls ──────────────────────────────────────────
    header(tex_hdr_desktop_, hdr_desktop_w_, hdr_desktop_h_);
    row(tex_d0k_, d0k_w_, d0k_h_, tex_d0a_, d0a_w_, d0a_h_);
    row(tex_d1k_, d1k_w_, d1k_h_, tex_d1a_, d1a_w_, d1a_h_);
    row(tex_d2k_, d2k_w_, d2k_h_, tex_d2a_, d2a_w_, d2a_h_);
    row(tex_d3k_, d3k_w_, d3k_h_, tex_d3a_, d3a_w_, d3a_h_);
    row(tex_d4k_, d4k_w_, d4k_h_, tex_d4a_, d4a_w_, d4a_h_);
    row(tex_d5k_, d5k_w_, d5k_h_, tex_d5a_, d5a_w_, d5a_h_);
    y += kSectionGap;

    // ── Section 2: Launchpad Controls ────────────────────────────────────────
    header(tex_hdr_launchpad_, hdr_launchpad_w_, hdr_launchpad_h_);
    row(tex_l0k_, l0k_w_, l0k_h_, tex_l0a_, l0a_w_, l0a_h_);
    row(tex_l1k_, l1k_w_, l1k_h_, tex_l1a_, l1a_w_, l1a_h_);
    row(tex_l2k_, l2k_w_, l2k_h_, tex_l2a_, l2a_w_, l2a_h_);
    row(tex_l3k_, l3k_w_, l3k_h_, tex_l3a_, l3a_w_, l3a_h_);
    row(tex_l4k_, l4k_w_, l4k_h_, tex_l4a_, l4a_w_, l4a_h_);
    y += kSectionGap;

    // ── Section 3: Vault ─────────────────────────────────────────────────────
    header(tex_hdr_vault_, hdr_vault_w_, hdr_vault_h_);
    row(tex_v0k_, v0k_w_, v0k_h_, tex_v0a_, v0a_w_, v0a_h_);
    row(tex_v1k_, v1k_w_, v1k_h_, tex_v1a_, v1a_w_, v1a_h_);
    row(tex_v2k_, v2k_w_, v2k_h_, tex_v2a_, v2a_w_, v2a_h_);
    row(tex_v3k_, v3k_w_, v3k_h_, tex_v3a_, v3a_w_, v3a_h_);
    y += kSectionGap;

    // ── Section 4: Login Screen ───────────────────────────────────────────────
    header(tex_hdr_login_, hdr_login_w_, hdr_login_h_);
    row(tex_g0k_, g0k_w_, g0k_h_, tex_g0a_, g0a_w_, g0a_h_);
    row(tex_g1k_, g1k_w_, g1k_h_, tex_g1a_, g1a_w_, g1a_h_);
    row(tex_g2k_, g2k_w_, g2k_h_, tex_g2a_, g2a_w_, g2a_h_);
    y += kSectionGap;

    // ── Section 5: Hot Corner ─────────────────────────────────────────────────
    header(tex_hdr_hotcorner_, hdr_hotcorner_w_, hdr_hotcorner_h_);
    // Single paragraph — blitted at the key column, full width.
    Blit(r, tex_hc_body_, kColKey, y, hc_body_w_, hc_body_h_);
    // y not advanced after last section — footer is fixed at kFooterY.

    // ── Footer ────────────────────────────────────────────────────────────────
    if (tex_footer_ != nullptr) {
        const int fx = (kScreenW - footer_w_) / 2;
        Blit(r, tex_footer_, fx, kFooterY, footer_w_, footer_h_);
    }
}

// ── HandleInput ───────────────────────────────────────────────────────────────
// Consumes any button-down event while the overlay is open.

bool QdHelpOverlay::HandleInput(const u64 keys_down)
{
    if (!open_) {
        return false;
    }
    if (keys_down == 0u) {
        return false;
    }
    Close();
    return true;
}

// ── v1.8.25: cross-file help overlay state ────────────────────────────────────
namespace {
    std::atomic_bool g_capture_held{false};
    std::atomic_bool g_help_open_request{false};
}

void SetCaptureHeld(bool held) {
    g_capture_held.store(held, std::memory_order_relaxed);
}

bool IsCaptureHeld() {
    return g_capture_held.load(std::memory_order_relaxed);
}

void RequestHelpOverlayOpen() {
    g_help_open_request.store(true, std::memory_order_release);
}

bool ConsumeHelpOverlayOpenRequest() {
    return g_help_open_request.exchange(false, std::memory_order_acq_rel);
}

}  // namespace ul::menu::qdesktop
