// test_QdIconCategory.cpp — Host-side unit tests for qd_IconCategory Classify().
// Created: 2026-04-24T00:00:00Z (SP1 audit fix F-09/F-10/F-11 — link real production .cpp)
//
// This test TU deliberately links the REAL qd_IconCategory.cpp rather than
// re-implementing its logic inline.  That is the fix for findings F-09/F-10/F-11:
// at least one test file must link an actual production TU so that divergence
// between inline copies and the real implementation is caught at compile time.
//
// qd_IconCategory.cpp depends only on <cstring> and <cctype> — no SDL2, no
// libnx, no Plutonium runtime — so it compiles cleanly on the macOS/Linux host.
//
// Build (single-TU via Makefile):
//   $(CXX) $(CXXFLAGS) $(INCLUDES) test_QdIconCategory.cpp \
//       ../../source/ul/menu/qdesktop/qd_IconCategory.cpp \
//       -o test_QdIconCategory
//
// This also exercises every code path in Classify(): Pass-1 (NACP match),
// Pass-2 (stem match), and the Unknown fallback.

#include "test_host_stubs.hpp"
#include <ul/menu/qdesktop/qd_IconCategory.hpp>

using namespace ul::menu::qdesktop;

// ── Pass-1 (NACP name) tests ─────────────────────────────────────────────────

static void test_nacp_exact_retroarch() {
    // "RetroArch" in NACP name → Emulator, glyph 'E'.
    CategoryResult r = Classify("RetroArch", "something_else");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Emulator));
    ASSERT_EQ(r.glyph, 'E');
    ASSERT_EQ(r.r, 60u);
    ASSERT_EQ(r.g, 100u);
    ASSERT_EQ(r.b, 160u);
    TEST_PASS("nacp_exact_retroarch");
}

static void test_nacp_case_insensitive_goldleaf() {
    // "GOLDLEAF" (uppercase) in NACP name → FileManager, glyph 'F'.
    CategoryResult r = Classify("GOLDLEAF", "something_else");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::FileManager));
    ASSERT_EQ(r.glyph, 'F');
    TEST_PASS("nacp_case_insensitive_goldleaf");
}

static void test_nacp_substring_lockpick_rcm() {
    // NACP contains "lockpick" as substring → SystemTool.
    CategoryResult r = Classify("Lockpick_RCM v1.9", "rcm");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::SystemTool));
    ASSERT_EQ(r.glyph, 'S');
    TEST_PASS("nacp_substring_lockpick_rcm");
}

static void test_nacp_qos_app() {
    // "q os" in NACP name → QosApp, glyph 'Q'.
    CategoryResult r = Classify("Q OS Desktop v0.11", "unknown_stem");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::QosApp));
    ASSERT_EQ(r.glyph, 'Q');
    ASSERT_EQ(r.r, 30u);
    ASSERT_EQ(r.g, 80u);
    ASSERT_EQ(r.b, 150u);
    TEST_PASS("nacp_qos_app");
}

static void test_nacp_nxdumptool() {
    // "nxdumptool" in NACP → BackupDump, glyph 'B'.
    CategoryResult r = Classify("nxdumptool", "zzz_no_match");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::BackupDump));
    ASSERT_EQ(r.glyph, 'B');
    ASSERT_EQ(r.r, 140u);
    ASSERT_EQ(r.g, 70u);
    ASSERT_EQ(r.b, 50u);
    TEST_PASS("nacp_nxdumptool");
}

static void test_nacp_hbmenu_utility() {
    // "hbmenu" / "homebrew menu" in NACP → Utility, glyph 'U'.
    CategoryResult r = Classify("Homebrew Menu", "hbmenu");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Utility));
    ASSERT_EQ(r.glyph, 'U');
    TEST_PASS("nacp_hbmenu_utility");
}

// ── Pass-1 empty NACP falls through to Pass-2 ────────────────────────────────

static void test_empty_nacp_falls_to_stem() {
    // Empty nacp_name → skip Pass-1, use file stem "mgba" → Emulator.
    CategoryResult r = Classify("", "mgba");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Emulator));
    ASSERT_EQ(r.glyph, 'E');
    TEST_PASS("empty_nacp_falls_to_stem");
}

static void test_null_nacp_falls_to_stem() {
    // nullptr nacp_name → skip Pass-1, use stem "hatsify" → BackupDump.
    CategoryResult r = Classify(nullptr, "hatsify");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::BackupDump));
    ASSERT_EQ(r.glyph, 'B');
    TEST_PASS("null_nacp_falls_to_stem");
}

// ── Pass-2 (file stem) tests ─────────────────────────────────────────────────

static void test_stem_tinfoil_file_manager() {
    // stem "tinfoil" → FileManager.
    CategoryResult r = Classify("", "tinfoil");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::FileManager));
    ASSERT_EQ(r.glyph, 'F');
    ASSERT_EQ(r.r, 50u);
    ASSERT_EQ(r.g, 130u);
    ASSERT_EQ(r.b, 80u);
    TEST_PASS("stem_tinfoil_file_manager");
}

static void test_stem_qos_mock() {
    // stem "qos-mock" → QosApp.
    CategoryResult r = Classify("", "qos-mock");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::QosApp));
    ASSERT_EQ(r.glyph, 'Q');
    TEST_PASS("stem_qos_mock");
}

static void test_stem_case_insensitive_daybreak() {
    // stem "Daybreak" (mixed case) → SystemTool.
    CategoryResult r = Classify("", "Daybreak");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::SystemTool));
    ASSERT_EQ(r.glyph, 'S');
    ASSERT_EQ(r.r, 110u);
    ASSERT_EQ(r.g, 80u);
    ASSERT_EQ(r.b, 140u);
    TEST_PASS("stem_case_insensitive_daybreak");
}

static void test_stem_ftpd_utility() {
    // stem "ftpd" → Utility.
    CategoryResult r = Classify("", "ftpd");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Utility));
    ASSERT_EQ(r.glyph, 'U');
    ASSERT_EQ(r.r, 170u);
    ASSERT_EQ(r.g, 130u);
    ASSERT_EQ(r.b, 30u);
    TEST_PASS("stem_ftpd_utility");
}

static void test_stem_warmboot_backup() {
    // stem "warmboot-extractor" contains "warmboot" → BackupDump.
    CategoryResult r = Classify("", "warmboot-extractor");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::BackupDump));
    ASSERT_EQ(r.glyph, 'B');
    TEST_PASS("stem_warmboot_backup");
}

// ── Unknown fallback ─────────────────────────────────────────────────────────

static void test_unknown_fallback() {
    // Nothing matches → Unknown, glyph '?', rgb(80,80,80).
    CategoryResult r = Classify("", "xyzzy_unrecognised");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Unknown));
    ASSERT_EQ(r.glyph, '?');
    ASSERT_EQ(r.r, 80u);
    ASSERT_EQ(r.g, 80u);
    ASSERT_EQ(r.b, 80u);
    TEST_PASS("unknown_fallback");
}

static void test_both_null_unknown() {
    // Both nullptr → Unknown.
    CategoryResult r = Classify(nullptr, nullptr);
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Unknown));
    ASSERT_EQ(r.glyph, '?');
    TEST_PASS("both_null_unknown");
}

static void test_both_empty_unknown() {
    // Both empty strings → Unknown.
    CategoryResult r = Classify("", "");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::Unknown));
    ASSERT_EQ(r.glyph, '?');
    TEST_PASS("both_empty_unknown");
}

// ── Pass-1 wins over Pass-2 when both would match differently ────────────────

static void test_nacp_wins_over_stem() {
    // NACP "goldleaf" → FileManager; stem "retroarch" would → Emulator.
    // Pass-1 must win.
    CategoryResult r = Classify("goldleaf", "retroarch");
    ASSERT_EQ(static_cast<int>(r.category), static_cast<int>(NroCategory::FileManager));
    TEST_PASS("nacp_wins_over_stem");
}

int main() {
    test_nacp_exact_retroarch();
    test_nacp_case_insensitive_goldleaf();
    test_nacp_substring_lockpick_rcm();
    test_nacp_qos_app();
    test_nacp_nxdumptool();
    test_nacp_hbmenu_utility();
    test_empty_nacp_falls_to_stem();
    test_null_nacp_falls_to_stem();
    test_stem_tinfoil_file_manager();
    test_stem_qos_mock();
    test_stem_case_insensitive_daybreak();
    test_stem_ftpd_utility();
    test_stem_warmboot_backup();
    test_unknown_fallback();
    test_both_null_unknown();
    test_both_empty_unknown();
    test_nacp_wins_over_stem();
    fprintf(stderr, "All QdIconCategory tests PASSED\n");
    return 0;
}
