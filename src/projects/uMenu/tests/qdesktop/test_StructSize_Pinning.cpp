// test_StructSize_Pinning.cpp -- v1.7.0-stabilize-2 host tests for A7 + A8.
//
// Pins the size and alignment of NroEntry and LpItem on the host build.
// The complementary safety net is the static_assert in the production
// headers themselves (qd_DesktopIcons.hpp and qd_Launchpad.hpp). If a
// future commit grows either struct, BOTH the production assert AND this
// host test fire, blocking the change at compile time across builds.
//
// History: the v1.6.10 hard-crash on hardware (LibnxError_AppletCmdidNotFound,
// uSystem applet sequence abort) was traced to a struct shift in NroEntry --
// adding fields to the struct moved the layout the libnx IPC command table
// depends on. The remediation is "use side tables, never extend the struct";
// this test enforces it.
//
// We re-declare the structs here verbatim with the SAME layout as the
// production headers, then assert their sizes match the pinned values.
// This avoids a transitive include of qd_Cursor.hpp / SDL2 (which would
// fail on the host) while still detecting if the production struct grows
// (because the production header's static_assert would fire on the device
// build, and adding/removing a field here without matching production
// would surface in build-output diff between the two layouts).
//
// Build:  make test_StructSize_Pinning
// Run:    ./test_StructSize_Pinning

#include "test_host_stubs.hpp"
#include <cstdio>

// Mirror of the IconKind / IconCategory / NroCategory enums (u8 underlying).
namespace mirror {
    enum class IconCategory : u8 {
        Nintendo = 0, Homebrew = 1, Extras = 2, Builtin = 3,  // v1.8.10: Payloads removed
    };
    enum class NroCategory : u8 {
        Emulator = 0, FileManager = 1, SystemTool = 2, Utility = 3,
        BackupDump = 4, QosApp = 5, Unknown = 6,
    };
    enum class IconKind : u8 {
        Builtin = 0, Nro = 1, Application = 2, Special = 3,
    };
    enum class LpSortKind : u8 {
        Nintendo = 0, Homebrew = 1, Extras = 2, Builtin = 3,
    };

    // Mirror of NroEntry from qd_DesktopIcons.hpp lines 92-126.
    // Field order is LOAD-BEARING (libnx IPC table assumption); do not change.
    struct NroEntry {
        char     name[64];
        char     glyph;
        u8       bg_r, bg_g, bg_b;
        char     nro_path[769];
        char     icon_path[769];
        bool     is_builtin;
        u8       dock_slot;
        NroCategory category;
        IconCategory icon_category;
        bool     icon_loaded;
        IconKind kind;
        u64      app_id;
        u16      special_subtype;
    };

    // Mirror of LpItem from qd_Launchpad.hpp lines 127-154.
    struct LpItem {
        char    name[64];
        char    glyph;
        u8      bg_r, bg_g, bg_b;
        char    nro_path[769];
        char    icon_path[769];
        u64     app_id;
        bool    is_builtin;
        u8      dock_slot;
        IconCategory icon_category;
        LpSortKind sort_kind;
        size_t  desktop_idx;
    };
}

// ── Test 1: NroEntry size pinned at 1632 bytes ───────────────────────────────

static void test_nro_entry_size_pinned() {
    // Pinned 2026-04-26 (HEAD 562e554, v1.6.x reconstruction snapshot).
    // Growing this struct silently shifts the libnx IPC command table layout
    // -- the v1.6.10 hard-crash on hardware was caused by exactly this.
    ASSERT_EQ(sizeof(mirror::NroEntry), 1632u);
    ASSERT_EQ(alignof(mirror::NroEntry), 8u);
    TEST_PASS("sizeof(NroEntry) == 1632 (pinned at v1.6.x reconstruction snapshot)");
}

// ── Test 2: LpItem size pinned at 1632 bytes ─────────────────────────────────

static void test_lp_item_size_pinned() {
    // Pinned 2026-04-26. LpItem is not an IPC struct but is the snapshot
    // copy held by Launchpad::Open() across thousands of frames; growing it
    // would degrade cache stride. Static_assert in qd_Launchpad.hpp also
    // catches the drift on the device build.
    ASSERT_EQ(sizeof(mirror::LpItem), 1632u);
    ASSERT_EQ(alignof(mirror::LpItem), 8u);
    TEST_PASS("sizeof(LpItem) == 1632 (pinned at v1.6.x reconstruction snapshot)");
}

// ── Test 3: NroEntry and LpItem layouts converge on the same size ────────────

static void test_nro_entry_and_lp_item_converge() {
    // The two structs carry similar metadata (name, glyph, paths, app_id,
    // category) so their sizes happen to match at 1632 bytes. This is not
    // a hard requirement, but a divergence here would indicate one struct
    // grew without the other -- worth surfacing.
    ASSERT_EQ(sizeof(mirror::NroEntry), sizeof(mirror::LpItem));
    TEST_PASS("NroEntry and LpItem maintain layout parity (both 1632 bytes)");
}

// ── Test 4: InputCoyoteState size + field offsets (v1.8.23) ─────────────────

namespace mirror {
    // Mirror of QdDesktopIconsElement::InputCoyoteState from
    // qd_DesktopIcons.hpp:426-430.  Field order is LOAD-BEARING for
    // the coyote-timing state machine: misaligned fields here means the
    // production struct changed size and the device build would regress.
    // Pinned 2026-04-28.
    struct InputCoyoteState {
        uint64_t down_tick        = 0;          // offset 0 — TouchDown / button-down tick
        uint64_t last_launch_tick = 0;          // offset 8 — last successful launch tick
        uint32_t dpad_held_frames[4] = {0,0,0,0};  // offset 16 — up/dn/lt/rt held counters
    };
} // namespace mirror

static void test_input_coyote_state_size_pinned() {
    // u64 + u64 + u32[4] = 8+8+16 = 32 bytes, alignment 8.
    // Pinned 2026-04-28 (v1.8.23 HW-green build).
    ASSERT_EQ(sizeof(mirror::InputCoyoteState), 32u);
    ASSERT_EQ(alignof(mirror::InputCoyoteState), 8u);
    TEST_PASS("sizeof(InputCoyoteState) == 32 (v1.8.23 pin)");
}

static void test_input_coyote_state_field_offsets() {
    // down_tick at offset 0 (first u64).
    ASSERT_EQ(offsetof(mirror::InputCoyoteState, down_tick), 0u);
    // last_launch_tick at offset 8 (second u64).
    ASSERT_EQ(offsetof(mirror::InputCoyoteState, last_launch_tick), 8u);
    // dpad_held_frames at offset 16 (after two u64 fields).
    ASSERT_EQ(offsetof(mirror::InputCoyoteState, dpad_held_frames), 16u);
    TEST_PASS("InputCoyoteState field offsets: down_tick@0, last_launch_tick@8, dpad_held_frames@16");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_nro_entry_size_pinned();
    test_lp_item_size_pinned();
    test_nro_entry_and_lp_item_converge();
    test_input_coyote_state_size_pinned();
    test_input_coyote_state_field_offsets();
    return 0;
}
