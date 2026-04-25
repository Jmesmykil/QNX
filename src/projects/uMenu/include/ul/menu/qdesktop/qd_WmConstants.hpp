// qd_WmConstants.hpp — WM pixel + count constants and focus-model types for SP3.
// Ported from tools/mock-nro-desktop-gui/src/wm.rs (constants section + focus model).
//
// SP3-F09: ALL 25 pixel constants remapped ×1.5 (1280×720 → 1920×1080).
// SP3-F10: Count constants (DOCK_SLOT_COUNT, WORKSPACE_COUNT,
//          WORKSPACE_SLIDE_FRAMES) are NOT remapped — they are item/frame counts.
// SP3-F11: StackString → callers use char buf[] + snprintf (no heap, no std::string).
// SP3-F12: FocusLevel::Window { window_idx } → tagged union with Kind enum.
// SP3-F13: FocusElement.neighbors[4] initialised to -1 sentinel (AB-15).
// AB-09:   No std::variant / std::visit.
// AB-12:   SCREEN_W = 1920, SCREEN_H = 1080. Literals 1280 / 720 forbidden.
// AB-15:   -1 sentinel for missing neighbours (not 0).
#pragma once
#include <pu/Plutonium>
#include <cstdint>

namespace ul::menu::qdesktop {

// ── Screen dimensions (×1.5 remapped) ────────────────────────────────────────

static constexpr uint32_t SCREEN_W = 1920;   // Rust: 1280
static constexpr uint32_t SCREEN_H = 1080;   // Rust:  720

// ── The Bridge (top bar) ──────────────────────────────────────────────────────

static constexpr uint32_t TOPBAR_H = 48;     // Rust: 32

// ── The Deck (dock) ───────────────────────────────────────────────────────────

static constexpr uint32_t DOCK_H             = 108;  // Rust: 72
static constexpr uint32_t DOCK_PADDING_BOTTOM =  12;  // Rust:  8
static constexpr uint32_t DOCK_SLOT_SIZE      =  84;  // Rust: 56
static constexpr uint32_t DOCK_SLOT_GAP       =  18;  // Rust: 12

// ── Count constants — NOT remapped (SP3-F10) ─────────────────────────────────

/// Number of dock slots (item count — no pixel remap).
static constexpr uint32_t DOCK_SLOT_COUNT        = 6;   // unchanged
/// Number of virtual workspaces (item count — no pixel remap).
static constexpr uint32_t WORKSPACE_COUNT        = 9;   // unchanged
/// Workspace slide animation duration in frames (frame count — no pixel remap).
static constexpr uint32_t WORKSPACE_SLIDE_FRAMES = 12;  // unchanged

// ── Title bar and traffic lights ──────────────────────────────────────────────

static constexpr uint32_t TITLEBAR_H          =  42;  // Rust: 28
static constexpr uint32_t TRAFFIC_RADIUS       =  11;  // Rust:  7 (10.5 → 11, round up)
static constexpr int32_t  TRAFFIC_GAP          =  33;  // Rust: 22
static constexpr int32_t  TRAFFIC_LEFT_OFFSET  =  21;  // Rust: 14
static constexpr int32_t  TRAFFIC_Y_OFFSET     =  21;  // Rust: 14
static constexpr int32_t  TRAFFIC_HIT_SLOP     =   6;  // Rust:  4

// ── Resize grip ───────────────────────────────────────────────────────────────

static constexpr uint32_t GRIP_SIZE = 18;   // Rust: 12

// ── Min/max window dimensions ─────────────────────────────────────────────────

static constexpr uint32_t WIN_MIN_W    = 300;  // Rust: 200
static constexpr uint32_t WIN_MIN_H    = 180;  // Rust: 120
static constexpr int32_t  MAX_INSET_X  =  24;  // Rust: 16

// ── Window sizing / stagger ───────────────────────────────────────────────────

static constexpr int32_t  LAUNCH_STAGGER  =  36;   // Rust: 24
static constexpr uint32_t DEFAULT_WIN_W   = 780;   // Rust: 520
static constexpr uint32_t DEFAULT_WIN_H   = 480;   // Rust: 320

// ── Focus ring ────────────────────────────────────────────────────────────────

static constexpr uint32_t FOCUS_RING_THICKNESS = 3;   // Rust: 2

// ── Cursor start position (×1.5 remapped, = SCREEN_W/2, SCREEN_H/2) ─────────

static constexpr int32_t CURSOR_START_X = static_cast<int32_t>(SCREEN_W / 2);  // 960
static constexpr int32_t CURSOR_START_Y = static_cast<int32_t>(SCREEN_H / 2);  // 540

// ── Hot corner dimensions ─────────────────────────────────────────────────────

static constexpr uint32_t HOT_CORNER_W = 30;  // Rust: 20
static constexpr uint32_t HOT_CORNER_H = 30;  // Rust: 20

// ── Focus-surface enum ────────────────────────────────────────────────────────

/// Which top-level surface currently owns D-pad input.
/// Mirrors wm.rs FocusSurface (v1.1.3 finder-style surface focus).
enum class FocusSurface : uint8_t {
    Desktop,       ///< D-pad navigates the desktop icon grid.
    Dock,          ///< D-pad navigates the dock slots.
    Window,        ///< D-pad navigates the focused window's controls.
    CommandPanel,  ///< D-pad is owned by the command panel overlay.
    Cursor,        ///< D-pad drives the software cursor (reserved).
};

// ── Directional enum (for neighbour navigation) ───────────────────────────────

/// Cardinal direction for D-pad / neighbour lookup.
/// neighbors[0]=Up, [1]=Down, [2]=Left, [3]=Right (matching wm.rs comments).
enum class Dir : uint8_t {
    Up    = 0,
    Down  = 1,
    Left  = 2,
    Right = 3,
};

// ── FocusLevel — SP3-F12 tagged union ────────────────────────────────────────

/// The three navigation levels described in NEXT-v0.14-DPAD-SPEC.md.
/// SP3-F12: Rust Window { window_idx: usize } struct variant → tagged union.
struct FocusLevel {
    enum class Kind : uint8_t {
        Screen,  ///< Level 0 — dock / window row cycling.
        Window,  ///< Level 1 — within-window element navigation.
    } kind = Kind::Screen;

    /// Valid only when kind == Kind::Window.
    size_t window_idx = 0;
};

inline FocusLevel focus_level_screen() {
    FocusLevel fl;
    fl.kind       = FocusLevel::Kind::Screen;
    fl.window_idx = 0;
    return fl;
}

inline FocusLevel focus_level_window(size_t idx) {
    FocusLevel fl;
    fl.kind       = FocusLevel::Kind::Window;
    fl.window_idx = idx;
    return fl;
}

// ── FocusElement — SP3-F13 sentinel array ────────────────────────────────────

/// Identifies a focusable element inside a window's control tree.
/// Values are logical; each window type maps them to screen regions.
enum class FocusElementId : uint8_t {
    VaultSidebar,
    VaultFileList,
    VaultViewModeBar,
    WindowClose,
    WindowMin,
    WindowMax,
    WindowBody,
};

/// A focusable element with its directional adjacency table.
/// SP3-F13: neighbors uses int32_t with -1 = no neighbour (AB-15).
/// Index: [0]=Up, [1]=Down, [2]=Left, [3]=Right  (matches Dir enum).
struct FocusElement {
    FocusElementId id;
    /// -1 means no neighbour in that direction (AB-15: never use 0 as sentinel).
    int32_t neighbors[4];

    /// Construct with all neighbours absent.
    static FocusElement make(FocusElementId eid) {
        FocusElement fe;
        fe.id          = eid;
        fe.neighbors[0] = -1;
        fe.neighbors[1] = -1;
        fe.neighbors[2] = -1;
        fe.neighbors[3] = -1;
        return fe;
    }
};

} // namespace ul::menu::qdesktop
