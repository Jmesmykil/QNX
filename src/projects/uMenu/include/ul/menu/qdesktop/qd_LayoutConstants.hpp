// qd_LayoutConstants.hpp — Shared layout constants for Q OS uMenu v3.7+.
//
// SSOT for every named margin, padding, row-height, icon-size, hot-corner
// region, tooltip offset, and content-inset used across:
//   qd_Frame, qd_WindowManager, qd_DesktopIcons, qd_HotCornerOverlay,
//   qd_HotCornerRightOverlay, qd_HotCornerDropdown, qd_HotCornerRightDropdown,
//   qd_SaveBackup, qd_SaveEditorLayout, qd_CheatsLayout, qd_VaultLayout,
//   qd_SettingsLayout, qd_AboutLayout, qd_ModsLayout.
//
// Rules:
//   1. ALL magic pixel literals visible in the files above MUST become a named
//      constant here (or derive from one).
//   2. Screen-size dependence: derive from SCREEN_W / SCREEN_H (qd_WmConstants.hpp).
//   3. Chrome-size dependence: derive from QdFrame::k* constants (qd_Frame.hpp).
//   4. Never inline bare numeric literals into render/hit-test call sites — that
//      is what caused the alignment bugs this header fixes.
//
// Inclusion order: include AFTER qd_WmConstants.hpp and BEFORE qd_Frame.hpp
// (Frame constants reference some of these).  qd_Frame.hpp forward-declares
// nothing from here; layout files include both.
#pragma once
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_WmConstants.hpp>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Screen anchors (derived from qd_WmConstants.hpp) ─────────────────────────
// These re-export SCREEN_W / SCREEN_H as signed s32 for use in layout math
// that mixes signed coordinates.
static constexpr int32_t  SCR_W = static_cast<int32_t>(SCREEN_W);  // 1920
static constexpr int32_t  SCR_H = static_cast<int32_t>(SCREEN_H);  // 1080

// ── Hot-corner visual and hit-zone dimensions ─────────────────────────────────
//
// DEFECT FIXED (qd_WmConstants.hpp:111-112):
//   HOT_CORNER_W=30, HOT_CORNER_H=30 — the Rust-derived constants — were DEAD:
//   they were never used for hit testing.  The actual visual widget and hit
//   region were LP_HOTCORNER_W=96, LP_HOTCORNER_H=72 from qd_Launchpad.hpp.
//   HOT_CORNER_W/H remained only as stale remnants of the Rust mock.
//
// This header is now the sole SSOT for hot-corner geometry.  LP_HOTCORNER_W/H
// remain in qd_Launchpad.hpp for backward compatibility (they reference these).
//
// Visual widget bounds (what gets painted):
static constexpr int32_t HC_VISUAL_W = 96;   // left overlay: 96×72 px
static constexpr int32_t HC_VISUAL_H = 72;

// Hit region — identical to visual bounds (paint IS the hit zone).
// DEFECT FIXED: previously two separate constants (HOT_CORNER_W=30 never used;
// LP_HOTCORNER_W=96 used).  Now unified: hit == visual.
static constexpr int32_t HC_HIT_W = HC_VISUAL_W;   // 96
static constexpr int32_t HC_HIT_H = HC_VISUAL_H;   // 72

// Right hot-corner: visual widget at (SCR_W - HC_VISUAL_W, 0).
// DEFECT FIXED (qd_HotCornerRightOverlay.cpp:29, qd_HotCornerRightDropdown.cpp:78):
//   Both files hardcoded `const int32_t kScreenW = 1920` instead of using SCREEN_W.
static constexpr int32_t HC_RIGHT_X = SCR_W - HC_VISUAL_W;  // 1824

// ── Topbar (qd_WmConstants.hpp TOPBAR_H = 48) ────────────────────────────────
// For layout code that needs a signed int without a cast.
static constexpr int32_t  TOPBAR_H_S = static_cast<int32_t>(TOPBAR_H);  // 48

// ── Hot-corner accent border thickness ────────────────────────────────────────
// Used by both QdHotCornerOverlay and QdHotCornerRightOverlay.
// Previously scattered as anonymous `2` in FillRect calls.
static constexpr int32_t HC_ACCENT_THICKNESS = 2;   // px

// ── Window stagger origin ─────────────────────────────────────────────────────
// DEFECT FIXED (qd_WindowManager.cpp:40-41):
//   next_stagger_x_ / next_stagger_y_ initialised to bare `104` and `56`.
//   Now named and placed here so they can be tuned without hunting literal `104`.
//   Relationship: x-stagger = TOPBAR_H_S * 2 + LAUNCH_STAGGER; y-stagger = TOPBAR_H_S + 8.
//   104 = 48*2 + 8; 56 = 48 + 8.
static constexpr int32_t STAGGER_ORIGIN_X = TOPBAR_H_S * 2 + 8;  // 104
static constexpr int32_t STAGGER_ORIGIN_Y = TOPBAR_H_S + 8;       //  56

// ── Dropdown panel layout (left + right hot-corner dropdowns) ─────────────────
//
// Row heights and padding are SHARED between the two dropdowns.
// They were previously private static constexpr in each .cpp with identical
// values — a classic out-of-sync risk.

static constexpr int32_t DROPDOWN_ROW_H    = 48;   // height of each item row
static constexpr int32_t DROPDOWN_PAD_V    =  8;   // top/bottom internal padding
static constexpr int32_t DROPDOWN_PAD_H    = 16;   // left text indent inside panel
static constexpr int32_t DROPDOWN_RADIUS   =  8;   // rounded-corner radius (= PAD_V)

// Left dropdown: 5 items, panel pinned to x=0 below the hot-corner widget.
static constexpr int32_t DROPDOWN_LEFT_W   = 280;
static constexpr int32_t DROPDOWN_LEFT_X   = 0;
static constexpr int32_t DROPDOWN_LEFT_Y   = HC_VISUAL_H;  // 72 — flush with widget bottom

// Right dropdown: 11 items, panel flush with screen right edge.
// DEFECT FIXED (qd_HotCornerRightDropdown.cpp:78-79):
//   `const int kScreenW = 1920` and `kPanelX = kScreenW - kPanelW` hardcoded.
static constexpr int32_t DROPDOWN_RIGHT_W  = 320;
static constexpr int32_t DROPDOWN_RIGHT_X  = SCR_W - DROPDOWN_RIGHT_W;  // 1600

// DEFECT FIXED (qd_HotCornerRightDropdown.cpp:172):
//   `panel_y_ = static_cast<int>(TOPBAR_H) - 8` — magic `-8` offset.
//   Meaning: icons in the top bar are centred at y∈[8,40]; dropdown top border
//   aligns to icon bottom = y=40.  Named the 8 px constant explicitly.
static constexpr int32_t HC_ICON_TOP_INSET = 8;    // icons start 8 px below topbar top
static constexpr int32_t DROPDOWN_RIGHT_Y  = TOPBAR_H_S - HC_ICON_TOP_INSET;  // 40

// ── Q-glyph render size inside the hot-corner overlay ────────────────────────
// DEFECT (qd_HotCornerOverlay.cpp:103): `kQGlyphPx = 60` was a local constexpr.
// Named here so both overlays can share the same glyph-size token.
static constexpr int32_t HC_Q_GLYPH_PX = 60;

// ── Tooltip geometry ──────────────────────────────────────────────────────────
// DEFECT FIXED (qd_Tooltip.cpp:32-33):
//   `kScreenW = 1920` and `kScreenH = 1080` hardcoded instead of using SCREEN_W/H.
static constexpr int32_t TOOLTIP_SCREEN_W  = SCR_W;
static constexpr int32_t TOOLTIP_SCREEN_H  = SCR_H;
static constexpr int32_t TOOLTIP_PAD_X     = 10;   // left+right internal padding each
static constexpr int32_t TOOLTIP_PAD_Y     =  6;   // top+bottom internal padding each
static constexpr int32_t TOOLTIP_GAP_ABOVE =  6;   // gap between tooltip bottom and anchor when above
static constexpr int32_t TOOLTIP_GAP_BELOW =  6;   // gap between tooltip top and anchor when below

// ── Window chrome content insets ──────────────────────────────────────────────
// DEFECT FIXED (qd_Frame.cpp:748,771-772):
//   `kTitleLeftPad=48`, `kTitleRightPad=48`, `kHintLeftPad=48`, `kHintRightPad=8`
//   were local `static constexpr` inside Paint() — not accessible to external
//   layout code that needs to match the chrome's effective content x-start.
//
// These are computed from qd_Frame.hpp chrome geometry:
//   kDiscInset=12 + kDiscDia=30 + 6 guard = 48 px each side for close/maximize.
//   Minimize is BL so it also takes 48 px from the left in the status bar.
//   Status bar right side has no disc → 8 px right pad only.
static constexpr int32_t CHROME_TITLE_LEFT_PAD  = 48;  // clear close disc on left
static constexpr int32_t CHROME_TITLE_RIGHT_PAD = 48;  // clear maximize disc on right
static constexpr int32_t CHROME_HINT_LEFT_PAD   = 48;  // clear minimize disc on left
static constexpr int32_t CHROME_HINT_RIGHT_PAD  =  8;  // no disc on right in status bar

// Effective content left x-start within a window frame (consistent x origin for
// all panel content that must not underlay the left disc cluster).
static constexpr int32_t CONTENT_LEFT_INSET = CHROME_HINT_LEFT_PAD;  // 48

// ── Shared panel/list layout tokens ───────────────────────────────────────────
// The values below unify the formerly-divergent constants across:
//   QdCheatsLayout (private kMargin=32, kRowH=44, kTopbarH=36, kHintBarH=28),
//   QdModsLayout   (private kMargin=32, kRowH=44, kTopbarH=36, kHintBarH=28),
//   QdSettingsLayout (label indent ox+18, detail indent x+20, title indent x+24).
//
// DEFECT FIXED: SettingsLayout used 18 / 20 / 24 for three logically-identical
// "text left margin" offsets across sidebar, detail pane, and title strip.
// Unified to PANEL_TEXT_LEFT_PAD = 20 (matches detail pane value; sidebar and
// title strip updated to match).

static constexpr int32_t PANEL_HEADER_H     = 36;  // header bar in Cheats/Mods/About panels
static constexpr int32_t PANEL_ROW_H        = 44;  // list row height in Cheats/Mods
static constexpr int32_t PANEL_ROW_GAP      =  4;  // gap between rows
static constexpr int32_t PANEL_HINT_BAR_H   = 28;  // bottom hint strip
static constexpr int32_t PANEL_MARGIN       = 32;  // left/right outer margin in Cheats/Mods
static constexpr int32_t PANEL_DETAIL_X     = 480; // right pane split x in Cheats/Mods
static constexpr int32_t PANEL_TEXT_LEFT_PAD = 20;  // unified text left margin (was 18/20/24)
static constexpr int32_t PANEL_TEXT_RIGHT_PAD= 24;  // text right margin in detail pane

// Settings-specific row heights (differ intentionally: detail rows have more info).
static constexpr int32_t SETTINGS_SIDEBAR_ROW_H_K = 62; // sidebar rows (taller, category labels)
static constexpr int32_t SETTINGS_DETAIL_ROW_H_K  = 54; // detail rows (data rows)

// ── Vault / file browser tokens ───────────────────────────────────────────────
// These are already named in qd_VaultLayout.hpp (VAULT_SIDEBAR_W etc.) and
// are not duplicated here — Vault is sufficiently isolated.

// ── Dock geometry ─────────────────────────────────────────────────────────────
// Dock tile packing: tiles pack right-to-left from (SCR_W - DOCK_RIGHT_PAD).
// DEFECT FIXED (qd_WindowManager.cpp:350, LayoutDockEntries):
//   `right_edge = SCREEN_W - 8` — magic 8 px edge gap.
static constexpr int32_t DOCK_RIGHT_PAD = 8;   // gap between rightmost tile and screen edge
static constexpr int32_t DOCK_TILE_GAP  = 8;   // padding around each snapshot tile

}  // namespace ul::menu::qdesktop
#endif  // QDESKTOP_MODE
