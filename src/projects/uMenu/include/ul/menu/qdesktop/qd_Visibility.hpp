// qd_Visibility.hpp — Per-entry visibility ("hide from dock/desktop") store.
//
// Phase Z2.0b primitive.  Used by upcoming Z2.3+ context-menu "Hide" actions.
//
// Model
// -----
// A flat set of hidden stable_ids.  Any entry whose stable_id is in the set
// is treated as hidden by the renderers (dock, favorites strip, desktop
// folder grid — wired in subsequent Z2 sub-phases).
//
// stable_id encoding mirrors qos-favorites.toml so the same key shape works
// across both stores:
//   "app:<hex16>"      — Switch Application by 16-char NCM TitleId
//   "nro:<full_sd_path>" — homebrew NRO by SD-path
//   "builtin:<name>"   — built-in icon (Vault/Monitor/...)
//   "special:<name>"   — special slot
//
// Persistence
// -----------
// sdmc:/ulaunch/qos-visibility.toml — one stable_id per line.  Atomic
// tmp+rename + fsdevCommitDevice (same pattern as SaveFavorites at
// qd_DesktopIcons.cpp:688).  Singleton lazy-loads on first access.
//
// Lifetime: process singleton — survives uMenu Finalize / re-init for the
// duration of the process.  On uMenu cold start the file is re-read.
//
// Thread safety: not thread-safe; uMenu is single-threaded for UI input
// (matches favorites store).

#pragma once

#include <string>
#include <unordered_set>

namespace ul::menu::qdesktop {

class QdVisibility {
public:
    // Process singleton.  First call triggers lazy-load from disk.
    static QdVisibility& Get();

    QdVisibility(const QdVisibility&)            = delete;
    QdVisibility& operator=(const QdVisibility&) = delete;
    QdVisibility(QdVisibility&&)                 = delete;
    QdVisibility& operator=(QdVisibility&&)      = delete;

    // O(1) lookup.  Empty stable_id always returns false (visible).
    bool IsHidden(const std::string& stable_id) const;

    // Toggle hidden state and persist immediately.  Logs a warning on save
    // failure but does not throw — the in-memory state still mutates so the
    // current session reflects the user's intent.
    void SetHidden(const std::string& stable_id, bool hidden);

    // Number of currently-hidden entries (for diagnostics / UI).
    size_t HiddenCount() const { return hidden_set_.size(); }

    // Drop entries whose stable_id is NOT in alive_set.  Called at uMenu
    // startup after the entry registry is built so dead references (deleted
    // apps, removed NROs) don't accumulate.  Persists if any pruning happened.
    void Prune(const std::unordered_set<std::string>& alive_set);

    // Force-reload from disk.  Useful for test or settings-reset paths.
    void Reload();

    // Drop everything and persist.  Used by "Reset visibility" in Settings.
    void Clear();

private:
    QdVisibility() = default;
    ~QdVisibility() = default;

    void EnsureLoaded() const;
    bool Save() const;

    mutable bool                              loaded_ = false;
    mutable std::unordered_set<std::string>   hidden_set_;
};

} // namespace ul::menu::qdesktop
