// test_LoginChime.cpp -- v1.7.0-stabilize-2 host tests for REC-01.
//
// Models the StartPlayBgm() gate semantics from
// src/projects/uMenu/source/ul/menu/ui/ui_MenuApplication.cpp. The fix is in
// production code; these tests pin the invariants the fix establishes:
//
//   1. The gate (g_login_chime_played) arms BEFORE the bgm-null check,
//      so the chime never replays when the audio resource is unavailable
//      on first attempt.
//   2. The gate prevents re-play on subsequent Startup re-entries.
//   3. The gate has no effect on non-Startup menus -- they are free to
//      repeat their bgm without restriction.
//
// We can't link MenuApplication directly (it depends on Plutonium runtime,
// libnx, Mix_*, etc.). Instead we model the StartPlayBgm() control flow
// with a function-pointer indirection over the audio API, then verify the
// control flow matches the v1.7.0-stabilize-2 patch.
//
// Build:  make test_LoginChime
// Run:    ./test_LoginChime

#include "test_host_stubs.hpp"
#include <cstdio>

// We cannot truly reset a function-static across test cases without
// process restart. So instead of mirroring the production
// `static bool g_login_chime_played` (which would fuse all tests into one
// state), we model the gate as a STRUCT-level state passed in. This lets
// each test start from a fresh gate while still exercising the exact
// patched control-flow ordering: ARM-gate-FIRST, then bgm null-check.

namespace {
// Simulated MenuType (only Startup is meaningful for the chime gate).
enum class MenuType : int { Main = 0, Startup = 1, Settings = 2 };
} // namespace

namespace v2 {

struct ChimeGate {
    bool played = false;

    // Patched-pattern start_play_bgm: arm gate before bgm check.
    void start_play_bgm(MenuType menu, bool bgm_present, int &out_play_count) {
        if (menu == MenuType::Startup && played) {
            return;
        }
        if (menu == MenuType::Startup) {
            played = true;  // arm BEFORE the bgm null check
        }
        if (bgm_present) {
            ++out_play_count;
        }
    }
};

} // namespace v2

// ── Test 1: gate arms even when bgm pointer is null ──────────────────────────

static void test_gate_arms_when_bgm_unavailable() {
    v2::ChimeGate gate;
    int play_count = 0;
    // First Startup entry with bgm UNAVAILABLE (e.g., audio file failed to load).
    gate.start_play_bgm(MenuType::Startup, /*bgm_present=*/false, play_count);
    ASSERT_TRUE(gate.played);     // gate IS armed even though no audio played
    ASSERT_EQ(play_count, 0);     // no audio call made

    // Now bgm becomes available (audio file loads on a later attempt).
    gate.start_play_bgm(MenuType::Startup, /*bgm_present=*/true, play_count);
    // Critical: the gate prevents the chime from playing now, even though
    // audio is suddenly available. This is the REC-01 fix: bgm-not-ready
    // on first attempt cannot trigger a delayed chime later.
    ASSERT_EQ(play_count, 0);
    TEST_PASS("REC-01 gate arms when bgm.bgm == nullptr (no delayed chime)");
}

// ── Test 2: gate arms when bgm is valid and prevents re-play ─────────────────

static void test_gate_arms_when_bgm_valid_and_prevents_replay() {
    v2::ChimeGate gate;
    int play_count = 0;
    gate.start_play_bgm(MenuType::Startup, /*bgm_present=*/true, play_count);
    ASSERT_TRUE(gate.played);
    ASSERT_EQ(play_count, 1);

    // Re-enter Startup (e.g., lockscreen cycle, return-from-Settings).
    gate.start_play_bgm(MenuType::Startup, /*bgm_present=*/true, play_count);
    ASSERT_EQ(play_count, 1);  // still 1; chime did NOT replay
    TEST_PASS("gate arms when bgm valid and prevents re-play on subsequent Startup");
}

// ── Test 3: non-Startup menus are unaffected ─────────────────────────────────

static void test_non_startup_menu_unaffected() {
    v2::ChimeGate gate;
    int play_count = 0;
    // Settings menu plays its own bgm; the gate does NOT block it.
    gate.start_play_bgm(MenuType::Settings, /*bgm_present=*/true, play_count);
    ASSERT_FALSE(gate.played);   // gate stays un-armed (only Startup arms it)
    ASSERT_EQ(play_count, 1);
    // Settings re-entry plays again (no gate).
    gate.start_play_bgm(MenuType::Settings, /*bgm_present=*/true, play_count);
    ASSERT_EQ(play_count, 2);
    TEST_PASS("non-Startup menus play bgm freely; gate does not interfere");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    test_gate_arms_when_bgm_unavailable();
    test_gate_arms_when_bgm_valid_and_prevents_replay();
    test_non_startup_menu_unaffected();
    return 0;
}
