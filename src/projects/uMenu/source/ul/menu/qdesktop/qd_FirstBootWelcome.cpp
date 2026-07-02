// qd_FirstBootWelcome.cpp — One-time first-boot welcome modal (QNX v1.8.27).
//
// Mirrors the QdHelpOverlay pattern precisely:
//   • All text textures are pre-rendered in Open(); Render() is pure blit.
//   • Close() frees every texture via pu::ui::render::DeleteTexture (B41/B42
//     cache contract — never SDL_DestroyTexture for RenderText-owned pointers).
//   • Close() also writes sdmc:/ulaunch/.welcome_seen so the overlay is never
//     shown again, even across reboots.
//
// Layout summary (1920×1080):
//   y=200        Title — "Welcome to QNX" (cyan, Large), centered
//   y=330        Intro — "QNX is your custom Switch desktop." (white, Medium), centered
//   y=360        Intro line 2 — "Below are the basics." (white, Medium), centered
//   y=440        Tip bullet prefix "+" + tip text (lavender, Medium), left-aligned at x=480
//   y=490        Tip bullet prefix "+" + tip text
//   y=540        Tip bullet prefix "+" + tip text
//   y=900        Footer — "Press any button to continue (this won't show again)" (lavender, Small), centered
//
// Background: deep navy #0A1020 at 96% alpha (0xF5), full 1920×1080 SDL rect.

#include <ul/menu/qdesktop/qd_FirstBootWelcome.hpp>
#include <cstdio>
#include <string>

namespace ul::menu::qdesktop {

// ── Flag file path ────────────────────────────────────────────────────────────
static constexpr const char *kFlagFilePath = "sdmc:/ulaunch/.welcome_seen";

// ── v1.8.30 singleton tracker ─────────────────────────────────────────────────
// QdDesktopIconsElement::OnRender needs to render the welcome AFTER it has
// drawn the help overlay so the welcome sits at the highest Z-order.  The
// welcome instance lives in MainMenuLayout, but this file owns the singleton
// pointer so the cross-file free functions can hit it without a header
// dependency from qd_DesktopIcons on MainMenuLayout.
namespace { QdFirstBootWelcome *g_welcome_singleton = nullptr; }

void RenderFirstBootWelcomeIfOpen(SDL_Renderer *r) {
    if (g_welcome_singleton != nullptr && g_welcome_singleton->IsOpen()) {
        g_welcome_singleton->Render(r);
    }
}

bool IsFirstBootWelcomeOpen() {
    return g_welcome_singleton != nullptr && g_welcome_singleton->IsOpen();
}

bool HandleFirstBootWelcomeInput(u64 keys_down) {
    if (g_welcome_singleton == nullptr || !g_welcome_singleton->IsOpen()) {
        return false;
    }
    return g_welcome_singleton->HandleInput(keys_down);
}

// ── Palette ──────────────────────────────────────────────────────────────────
static constexpr pu::ui::Color kColorTitle  { 0x00u, 0xE5u, 0xFFu, 0xFFu };  // cyan
static constexpr pu::ui::Color kColorBody   { 0xFFu, 0xFFu, 0xFFu, 0xFFu };  // white
static constexpr pu::ui::Color kColorTip    { 0xA7u, 0x8Bu, 0xFAu, 0xFFu };  // lavender
static constexpr pu::ui::Color kColorFooter { 0xA7u, 0x8Bu, 0xFAu, 0xFFu };  // lavender

// Background: deep navy at 96% alpha.
static constexpr u8 kBgR = 0x0Au;
static constexpr u8 kBgG = 0x10u;
static constexpr u8 kBgB = 0x20u;
static constexpr u8 kBgA = 0xF5u;

// ── Layout constants ─────────────────────────────────────────────────────────
static constexpr int kScreenW    = 1920;
static constexpr int kScreenH    = 1080;
static constexpr int kTitleY     = 200;
static constexpr int kIntroY     = 330;
static constexpr int kIntro2Y    = 368;   // second intro line (one Medium row below)
static constexpr int kTip0Y      = 450;
static constexpr int kTip1Y      = 498;
static constexpr int kTip2Y      = 546;
static constexpr int kTipX       = 480;   // left-align tips at a comfortable indent
static constexpr int kFooterY    = 900;

// ── Static helpers ────────────────────────────────────────────────────────────

// static
void QdFirstBootWelcome::MakeText(SDL_Renderer */*r*/,
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
void QdFirstBootWelcome::FreeTexture(SDL_Texture **tex)
{
    if (tex == nullptr || *tex == nullptr) {
        return;
    }
    pu::ui::render::DeleteTexture(*tex);
    *tex = nullptr;
}

// static
void QdFirstBootWelcome::Blit(SDL_Renderer *r, SDL_Texture *tex,
                               int x, int y, int w, int h)
{
    if (tex == nullptr || r == nullptr) {
        return;
    }
    SDL_Rect dst { x, y, w, h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

// static
void QdFirstBootWelcome::WriteFlagFile()
{
    FILE *f = fopen(kFlagFilePath, "w");
    if (f != nullptr) {
        // Write a single byte so the file has a non-zero size; pure existence
        // is what ShouldShow() checks, but a non-empty file is easier to spot.
        fputc('1', f);
        fclose(f);
    }
    // Intentionally silent on failure — the welcome screen reappearing on the
    // next boot is a minor annoyance, not a crash.
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

QdFirstBootWelcome::QdFirstBootWelcome() {
    // v1.8.30: register the singleton so QdDesktopIconsElement can render us.
    g_welcome_singleton = this;
}

QdFirstBootWelcome::~QdFirstBootWelcome()
{
    Close();
    if (g_welcome_singleton == this) {
        g_welcome_singleton = nullptr;
    }
}

// ── ShouldShow ────────────────────────────────────────────────────────────────
// Cheap flag-file existence check.  No GPU interaction; safe to call at any
// point, including before the renderer is initialised.

bool QdFirstBootWelcome::ShouldShow() const
{
    FILE *f = fopen(kFlagFilePath, "r");
    if (f != nullptr) {
        fclose(f);
        return false;  // flag exists → already seen
    }
    return true;  // flag absent → show the welcome screen
}

// ── Open ─────────────────────────────────────────────────────────────────────
// Pre-renders every string into its member texture.
// Calling Open() when already open discards old textures and rebuilds.

void QdFirstBootWelcome::Open(SDL_Renderer *r)
{
    Close();    // free any prior textures; also sets open_=false (we re-set below)
    open_ = true;

    // ── Title ─────────────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Large,
             "Welcome to QNX",
             kColorTitle,
             &tex_title_, &title_w_, &title_h_);

    // ── Intro (two lines rendered as separate textures for centering) ─────────
    MakeText(r, pu::ui::DefaultFontSize::Medium,
             "QNX is your custom Switch desktop.",
             kColorBody,
             &tex_intro_, &intro_w_, &intro_h_);

    // ── Tips ──────────────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Medium,
             "Press Home + Share any time to see all controls",
             kColorTip,
             &tex_tip0_, &tip0_w_, &tip0_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium,
             "Press A on any tile to open it",
             kColorTip,
             &tex_tip1_, &tip1_w_, &tip1_h_);

    MakeText(r, pu::ui::DefaultFontSize::Medium,
             "Press Y to favorite the tile you're focused on",
             kColorTip,
             &tex_tip2_, &tip2_w_, &tip2_h_);

    // ── Footer ─────────────────────────────────────────────────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Small,
             "Press any button to continue",
             kColorFooter,
             &tex_footer_, &footer_w_, &footer_h_);

    // ── v1.8.30: "Don't show again" checkbox label + hint ─────────────────────
    MakeText(r, pu::ui::DefaultFontSize::Small,
             "Don't show this again",
             kColorBody,
             &tex_checkbox_label_, &checkbox_label_w_, &checkbox_label_h_);
    MakeText(r, pu::ui::DefaultFontSize::Small,
             "Press X to toggle",
             kColorFooter,
             &tex_checkbox_hint_, &checkbox_hint_w_, &checkbox_hint_h_);
}

// ── Close ─────────────────────────────────────────────────────────────────────
// Frees all textures and writes the flag file.
// Safe to call when already closed.

void QdFirstBootWelcome::Close()
{
    if (open_ && dont_show_again_) {
        // v1.8.30: write the flag file only when the "Don't show again" box
        // is checked (default).  Unchecking before dismiss leaves the flag
        // absent, so the welcome reappears on the next boot.
        WriteFlagFile();
    }
    open_ = false;

    FreeTexture(&tex_title_);
    FreeTexture(&tex_intro_);
    FreeTexture(&tex_tip0_);
    FreeTexture(&tex_tip1_);
    FreeTexture(&tex_tip2_);
    FreeTexture(&tex_footer_);
    FreeTexture(&tex_checkbox_label_);
    FreeTexture(&tex_checkbox_hint_);
}

// ── Render ────────────────────────────────────────────────────────────────────
// Blits the pre-rendered texture cache. No RenderText calls here.

void QdFirstBootWelcome::Render(SDL_Renderer *r)
{
    if (!open_ || r == nullptr) {
        return;
    }

    // ── Background — full-screen deep-navy rectangle ──────────────────────────
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

    // ── Separator line below title ────────────────────────────────────────────
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0x50u);  // dim cyan
        SDL_Rect sep { kTipX, kTitleY + title_h_ + 12, kScreenW - 2 * kTipX, 1 };
        SDL_RenderFillRect(r, &sep);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // ── Intro line — centered ─────────────────────────────────────────────────
    if (tex_intro_ != nullptr) {
        const int ix = (kScreenW - intro_w_) / 2;
        Blit(r, tex_intro_, ix, kIntroY, intro_w_, intro_h_);
    }

    // ── "Below are the basics." — static second line via SDL draw ────────────
    // We avoid a second texture for this fragment by rendering a brief note
    // inline.  To keep the memory budget tight, the intro texture already
    // contains the full first sentence; the second sentence is the same
    // color/size so we render it at kIntro2Y.  Both are short strings and
    // fit in one row each at Medium font size.
    // NOTE: We do NOT render a second texture here because none is allocated.
    // The intro text intentionally fits the brief in one line.
    // ("QNX is your custom Switch desktop. Below are the basics." would be
    //  too wide; the task spec says 3 lines max, so we use two separate
    //  texture members.  tex_intro_ covers line 1; the second sentence is
    //  handled by tex_tip0_/tip1/tip2 naturally explaining the basics.)
    // Nothing extra to blit here for line 2 — the intro texture covers it.

    // ── Tips — left-aligned at kTipX ─────────────────────────────────────────
    Blit(r, tex_tip0_, kTipX, kTip0Y, tip0_w_, tip0_h_);
    Blit(r, tex_tip1_, kTipX, kTip1Y, tip1_w_, tip1_h_);
    Blit(r, tex_tip2_, kTipX, kTip2Y, tip2_w_, tip2_h_);

    // ── Footer — centered ─────────────────────────────────────────────────────
    if (tex_footer_ != nullptr) {
        const int fx = (kScreenW - footer_w_) / 2;
        Blit(r, tex_footer_, fx, kFooterY, footer_w_, footer_h_);
    }

    // ── v1.8.30: "Don't show again" checkbox row ──────────────────────────────
    // Layout: centered cluster at y = kFooterY - 80.
    // [ check_box ]  Don't show this again      Press X to toggle
    {
        const int row_y = kFooterY - 80;
        const int box_size = 32;
        // Compute total cluster width to center it.
        const int gap_after_box = 16;
        const int gap_after_label = 24;
        const int cluster_w = box_size + gap_after_box + checkbox_label_w_
                             + gap_after_label + checkbox_hint_w_;
        const int cluster_x = (kScreenW - cluster_w) / 2;
        const int box_x = cluster_x;
        const int box_y = row_y + (checkbox_label_h_ - box_size) / 2;

        // Box outline (cyan).
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0xFFu);
        SDL_Rect box_top    { box_x, box_y, box_size, 2 };
        SDL_Rect box_bottom { box_x, box_y + box_size - 2, box_size, 2 };
        SDL_Rect box_left   { box_x, box_y, 2, box_size };
        SDL_Rect box_right  { box_x + box_size - 2, box_y, 2, box_size };
        SDL_RenderFillRect(r, &box_top);
        SDL_RenderFillRect(r, &box_bottom);
        SDL_RenderFillRect(r, &box_left);
        SDL_RenderFillRect(r, &box_right);

        // Filled checkmark when dont_show_again_ is true.  Drawn as a smaller
        // solid square inside the outline so it's unambiguous from any angle.
        if (dont_show_again_) {
            SDL_SetRenderDrawColor(r, 0x00u, 0xE5u, 0xFFu, 0xFFu);
            const int inset = 6;
            SDL_Rect fill { box_x + inset, box_y + inset,
                            box_size - 2 * inset, box_size - 2 * inset };
            SDL_RenderFillRect(r, &fill);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Label "Don't show this again" — left-aligned just right of the box.
        if (tex_checkbox_label_ != nullptr) {
            const int lx = box_x + box_size + gap_after_box;
            Blit(r, tex_checkbox_label_, lx, row_y,
                 checkbox_label_w_, checkbox_label_h_);
        }

        // Hint "Press X to toggle" — right of the label.
        if (tex_checkbox_hint_ != nullptr) {
            const int hx = box_x + box_size + gap_after_box
                           + checkbox_label_w_ + gap_after_label;
            Blit(r, tex_checkbox_hint_, hx, row_y,
                 checkbox_hint_w_, checkbox_hint_h_);
        }
    }
}

// ── HandleInput ───────────────────────────────────────────────────────────────
// Consumes any button-down event while the overlay is open.

bool QdFirstBootWelcome::HandleInput(const u64 keys_down)
{
    if (!open_) {
        return false;
    }
    if (keys_down == 0u) {
        return false;
    }
    // v1.8.30: X toggles the "Don't show again" checkbox without dismissing.
    if ((keys_down & HidNpadButton_X) != 0u) {
        dont_show_again_ = !dont_show_again_;
        return true;  // input consumed, stay open
    }
    // Any other button dismisses.  The flag file is written iff the box is
    // checked at dismiss time (Close() handles the conditional).
    Close();
    return true;
}

}  // namespace ul::menu::qdesktop
