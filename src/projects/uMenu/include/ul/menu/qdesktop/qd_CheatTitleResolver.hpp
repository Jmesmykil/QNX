// qd_CheatTitleResolver.hpp — async NACP title-name resolver for the Cheats UI.
//
// v3.6 absorb wave 1: replaces the "TID 0x<hex>" fallback labels in the Cheats
// window with real NACP-derived game names.  Resolution runs on a detached
// background thread so the Cheats window opens instantly even with 2,500+ TID
// directories present.  Results are cached to
// sdmc:/ulaunch/cache/cheat_titles.tsv so subsequent boots show real names
// from the first frame.
//
// Design constraints:
//   - Never block the main thread.  W13-B-HOTFIX permanently retired the
//     synchronous N×nsGetApplicationControlData pattern that caused
//     multi-second Switch-splash hangs.
//   - Filter by nsListApplicationRecord first — only attempt NACP lookups
//     for TIDs that are actually installed on this Switch (≤ 100 in practice,
//     vs 2,500+ raw TID dirs on community-cheat-DB-bulk-installed setups).
//   - Cache on disk so subsequent opens are O(1) name lookups.
//   - Cache file format: tab-separated `<tid_hex_lower>\t<utf8_name>\n`.
//     Plain-text intentionally (easy to diff, no parser fragility).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ul::menu::qdesktop {

class QdCheatTitleResolver {
public:
    // No instances — pure static interface.
    QdCheatTitleResolver()  = delete;
    ~QdCheatTitleResolver() = delete;

    // ── Cache lookup ──────────────────────────────────────────────────────
    //
    // Returns the cached display name for @p tid, or an empty string if no
    // cache entry exists.  Thread-safe.  Lazy-loads the on-disk cache on
    // first call; subsequent calls are an in-memory map lookup.
    static std::string Lookup(std::uint64_t tid);

    // ── Async resolution ──────────────────────────────────────────────────
    //
    // Kick off a detached background thread that:
    //   1. Calls nsListApplicationRecord once to build the installed-TID set.
    //   2. For each input tid in @p tids: if installed AND not already
    //      cached, calls nsGetApplicationControlData to extract the NACP
    //      name (current system language, then English fallback, then any
    //      non-empty language).
    //   3. Writes the updated cache to disk.
    //   4. Invokes @p on_resolved on the bg thread once at the end.  The
    //      callback is responsible for marking the UI dirty so a subsequent
    //      OnRender pass picks up the new names via Lookup().
    //
    // Idempotent: if a resolution pass is already running, this call is a
    // no-op (the existing pass already covers whatever TIDs were known).
    //
    // No-op if @p tids is empty.
    static void StartResolve(const std::vector<std::uint64_t> &tids,
                              std::function<void()> on_resolved);

    // ── State ─────────────────────────────────────────────────────────────
    //
    // True while a background resolution pass is in flight.
    static bool IsBusy();

    // Number of entries currently in the in-memory cache (lazy-loaded on
    // first Lookup or StartResolve call).
    static std::size_t CacheSize();
};

}  // namespace ul::menu::qdesktop
