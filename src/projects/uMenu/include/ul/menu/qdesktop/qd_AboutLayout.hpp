// qd_AboutLayout.hpp — Q OS-native About panel for the qdesktop "A" (About) dock tile.
// Replaces uLaunch's ShowAboutDialog popup. Slot-4 destination for dock dispatch.
// Inherits pu::ui::elm::Element (same pattern as QdVaultLayout / QdDesktopIconsElement).
// Full-screen overlay: procedural "Q OS" logo (no romfs PNG) + system info rows.
// Live libnx queries for battery/charger; cached g_GlobalSettings for firmware/serial.
#pragma once
#include <pu/Plutonium>
#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_Theme.hpp>
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <ul/menu/qdesktop/qd_HotCornerOverlay.hpp>
#include <SDL2/SDL.h>

namespace ul::menu::qdesktop {

// ── About panel pixel constants ───────────────────────────────────────────────
// 1920×1080 layout space (×1.5 from Rust 1280×720 reference).

/// Usable vertical body (between topbar and dock).
static constexpr s32 ABOUT_BODY_TOP  = TOPBAR_H;             // 48
static constexpr s32 ABOUT_BODY_H    = SCREEN_H - TOPBAR_H - DOCK_H; // 924

/// Card panel dimensions (centred in body).
static constexpr s32 ABOUT_CARD_W    = 1400;
static constexpr s32 ABOUT_CARD_H    = 800;
static constexpr s32 ABOUT_CARD_X    = (SCREEN_W - ABOUT_CARD_W) / 2; // 260
static constexpr s32 ABOUT_CARD_Y    = ABOUT_BODY_TOP + (ABOUT_BODY_H - ABOUT_CARD_H) / 2; // 110

/// Procedural logo panel (left of card).
static constexpr s32 ABOUT_LOGO_SIZE = 200;
static constexpr s32 ABOUT_LOGO_X    = ABOUT_CARD_X + 48;
static constexpr s32 ABOUT_LOGO_Y    = ABOUT_CARD_Y + 48;

/// Info column origin (right of logo panel).
static constexpr s32 ABOUT_INFO_X    = ABOUT_LOGO_X + ABOUT_LOGO_SIZE + 48;
static constexpr s32 ABOUT_INFO_Y    = ABOUT_CARD_Y + 48;
static constexpr s32 ABOUT_ROW_H     = 38;  ///< vertical stride per info row

/// Number of displayable info rows.
static constexpr size_t ABOUT_ROW_COUNT = 14;

// ── QdAboutElement ────────────────────────────────────────────────────────────

/// Full-screen element that draws the About panel.
/// Mounted directly on its host layout; the host layout fills the screen.
class QdAboutElement : public pu::ui::elm::Element {
public:
    using Ref = std::shared_ptr<QdAboutElement>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdAboutElement>(theme);
    }

    explicit QdAboutElement(const QdTheme &theme);
    ~QdAboutElement();

    // ── Element interface ──────────────────────────────────────────────────
    s32 GetX()      override { return 0; }
    s32 GetY()      override { return 0; }
    s32 GetWidth()  override { return content_w_; }
    s32 GetHeight() override { return content_h_; }

    void OnRender(pu::ui::render::Renderer::Ref &drawer,
                  s32 x, s32 y) override;

    void OnInput(u64 keys_down, u64 keys_up, u64 keys_held,
                 pu::ui::TouchPoint touch_pos) override;

    // ── Public API ─────────────────────────────────────────────────────────

    /// Resize the element to the given pixel dimensions.
    /// Called by QdWindow::New() to fit the layout inside a window.
    /// Default: 1920 × 1080 (full-screen, used when not in a window).
    void SetContentSize(s32 w, s32 h) {
        content_w_ = (w > 0) ? w : 1920;
        content_h_ = (h > 0) ? h : 1080;
    }

    /// Poll libnx for live values (battery, charger, operation mode, active
    /// user) and rebuild every cached text texture.  Called once on panel
    /// open and optionally on Y-button press.
    void Refresh();

private:
    // ── Info row descriptor ────────────────────────────────────────────────

    struct Row {
        char label[48];    ///< left-column text  (e.g. "Firmware")
        char value[128];   ///< right-column text (e.g. "18.1.0")
        SDL_Texture *label_tex; ///< rasterised label; nullptr until first Refresh
        SDL_Texture *value_tex; ///< rasterised value; nullptr until first Refresh
    };

    // ── State ──────────────────────────────────────────────────────────────

    // v1.10: window-content dimensions (default full-screen 1920×1080;
    // overridden by SetContentSize() when QdWindow embeds this element).
    s32 content_w_ = static_cast<s32>(SCREEN_W);
    s32 content_h_ = static_cast<s32>(SCREEN_H);

    QdTheme        theme_;
    Row            rows_[ABOUT_ROW_COUNT];

    /// Section heading textures ("Q OS Build", "Hardware", "Network", "Power").
    static constexpr size_t SECTION_COUNT = 4;
    SDL_Texture   *section_tex_[SECTION_COUNT];
    /// Cached "Q OS" logo centre label texture (rendered once in ctor).
    SDL_Texture   *logo_tex_;
    /// Cached footer hint texture ("B  Back    Y  Refresh").
    SDL_Texture   *footer_tex_;
    /// Cached bottom-of-screen hint bar texture (rendered once per Refresh).
    SDL_Texture   *hint_bar_tex_;

    bool           refreshed_;   ///< true once Refresh() has completed

    // ── Runtime geometry (computed from content_w_/content_h_) ────────────────

    /// All layout positions recomputed per frame from content dimensions.
    /// Keeps RenderCard/RenderRows/RenderSection independent of SCREEN_W/H.
    struct AboutGeo {
        s32 card_w, card_h;
        s32 card_x, card_y;
        s32 logo_size;
        s32 logo_x, logo_y;
        s32 info_x, info_y;
        s32 row_h;
        // Section-end right edge for horizontal rule.
        s32 rule_right;
    };

    /// Compute AboutGeo from content_w_ / content_h_.
    AboutGeo ComputeGeo() const;

    // ── Private helpers ────────────────────────────────────────────────────

    /// Destroy all per-row SDL_Texture* objects.
    void FreeRowTextures();

    /// Destroy section + logo + footer textures.
    void FreeStaticTextures();

    /// Render a single SDL_Texture* at pixel position, querying its size.
    static void BlitTex(SDL_Renderer *r, SDL_Texture *tex, s32 x, s32 y);

    /// Render the frosted-glass card background and the procedural logo panel.
    void RenderCard(SDL_Renderer *r, const AboutGeo &geo) const;

    /// Render the info rows using cached textures.
    void RenderRows(SDL_Renderer *r, const AboutGeo &geo);

    /// Render section divider line + heading texture at the given y position.
    void RenderSection(SDL_Renderer *r, size_t sec_idx, s32 y,
                       const AboutGeo &geo) const;

    /// Map SetSysProductModel integer to a human-readable string.
    static const char *ProductModelString(s32 model);
};

// ── QdAboutLayout ─────────────────────────────────────────────────────────────

/// Host layout for QdAboutElement.
/// Subclasses IMenuLayout so OnMessage()'s static_pointer_cast<IMenuLayout>
/// is type-safe — a bare pu::ui::Layout here would Data-Abort at 0x0 on any
/// smi::MenuMessage (HOME, GameCardMountFailure, SdCardEjected, etc.).
/// See qd_VaultHostLayout.hpp for the canonical crash chain description.
class QdAboutLayout : public ul::menu::ui::IMenuLayout {
public:
    using Ref = std::shared_ptr<QdAboutLayout>;

    static Ref New(const QdTheme &theme) {
        return std::make_shared<QdAboutLayout>(theme);
    }

    explicit QdAboutLayout(const QdTheme &theme);
    ~QdAboutLayout();

    // ── IMenuLayout pure-virtual obligations ─────────────────────────────────

    void OnMenuInput(const u64 keys_down,
                     const u64 keys_up,
                     const u64 keys_held,
                     const pu::ui::TouchPoint touch_pos) override;

    // Return to the main desktop on HOME and consume the message.
    bool OnHomeButtonPress() override;

    // About has no per-layout sfx — intentional no-ops; not stubs.
    // Input and rendering go through the child QdAboutElement directly.
    void LoadSfx() override;
    void DisposeSfx() override;

    /// Forward Refresh() call to the inner element.
    void Refresh();

private:
    QdAboutElement::Ref about_element_;
    QdHotCornerOverlay::Ref overlay_;
};

} // namespace ul::menu::qdesktop
