// qd_Visibility.cpp — implementation.
//
// I/O pattern mirrors SaveFavorites() at qd_DesktopIcons.cpp:688: write to
// .tmp, fflush+fclose, remove canonical (Horizon RenameFile returns EEXIST
// otherwise), rename, fsdevCommitDevice to push Horizon write-back to SD.
//
// Per-call savings: hidden state changes are infrequent (user toggles), so
// SetHidden persists synchronously.  No batched-write optimisation needed.

#include <ul/menu/qdesktop/qd_Visibility.hpp>

#include <ul/ul_Result.hpp>

#include <switch.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace ul::menu::qdesktop {

// ── File paths (same prefix as the other qos-* stores) ───────────────────────
static constexpr const char *kVisibilityPath    =
    "sdmc:/ulaunch/qos-visibility.toml";
static constexpr const char *kVisibilityTmpPath =
    "sdmc:/ulaunch/qos-visibility.toml.tmp";

// ── Singleton ────────────────────────────────────────────────────────────────

QdVisibility& QdVisibility::Get() {
    static QdVisibility instance;
    return instance;
}

// ── EnsureLoaded ─────────────────────────────────────────────────────────────

void QdVisibility::EnsureLoaded() const {
    if (loaded_) return;
    loaded_ = true;   // mark even on missing file so we don't retry

    FILE *f = fopen(kVisibilityPath, "rb");
    if (f == nullptr) {
        // First boot or never-hidden: empty store.
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f) != nullptr) {
        // Strip trailing CR/LF.
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) continue;
        // Accept any non-empty line; encoding-validation is the caller's
        // responsibility (favorites parser does the same).
        hidden_set_.insert(std::string(line));
    }
    fclose(f);

    UL_LOG_INFO("qdesktop: QdVisibility loaded %zu hidden entries from %s",
                hidden_set_.size(), kVisibilityPath);
}

// ── Queries ──────────────────────────────────────────────────────────────────

bool QdVisibility::IsHidden(const std::string& stable_id) const {
    if (stable_id.empty()) return false;
    EnsureLoaded();
    return hidden_set_.find(stable_id) != hidden_set_.end();
}

// ── Mutators ─────────────────────────────────────────────────────────────────

void QdVisibility::SetHidden(const std::string& stable_id, bool hidden) {
    if (stable_id.empty()) return;
    EnsureLoaded();

    const bool was_hidden = (hidden_set_.find(stable_id) != hidden_set_.end());
    if (was_hidden == hidden) {
        return;   // no-op
    }

    if (hidden) {
        hidden_set_.insert(stable_id);
    } else {
        hidden_set_.erase(stable_id);
    }

    if (!Save()) {
        UL_LOG_WARN("qdesktop: QdVisibility::SetHidden: persist failed for %s (%s)",
                    stable_id.c_str(), hidden ? "hide" : "show");
        // In-memory state already mutated; on-disk lag is acceptable.
    } else {
        UL_LOG_INFO("qdesktop: QdVisibility::SetHidden: %s -> %s (total=%zu)",
                    stable_id.c_str(), hidden ? "hidden" : "visible",
                    hidden_set_.size());
    }
}

void QdVisibility::Prune(const std::unordered_set<std::string>& alive_set) {
    EnsureLoaded();
    if (hidden_set_.empty()) return;

    size_t dropped = 0;
    for (auto it = hidden_set_.begin(); it != hidden_set_.end(); ) {
        if (alive_set.find(*it) == alive_set.end()) {
            it = hidden_set_.erase(it);
            ++dropped;
        } else {
            ++it;
        }
    }
    if (dropped > 0) {
        UL_LOG_INFO("qdesktop: QdVisibility::Prune dropped %zu stale entries", dropped);
        (void)Save();
    }
}

void QdVisibility::Reload() {
    hidden_set_.clear();
    loaded_ = false;
    EnsureLoaded();
}

void QdVisibility::Clear() {
    EnsureLoaded();
    if (hidden_set_.empty()) return;
    hidden_set_.clear();
    (void)Save();
    UL_LOG_INFO("qdesktop: QdVisibility cleared");
}

// ── Save (atomic tmp+rename) ─────────────────────────────────────────────────

bool QdVisibility::Save() const {
    mkdir("sdmc:/ulaunch", 0777);   // idempotent

    FILE *f = fopen(kVisibilityTmpPath, "wb");
    if (f == nullptr) {
        UL_LOG_WARN("qdesktop: QdVisibility::Save: fopen(tmp) failed errno=%d", errno);
        return false;
    }

    for (const auto& id : hidden_set_) {
        if (fprintf(f, "%s\n", id.c_str()) < 0) {
            fclose(f);
            UL_LOG_WARN("qdesktop: QdVisibility::Save: fprintf failed");
            return false;
        }
    }
    fflush(f);
    fclose(f);

    // Horizon FsService RenameFile returns EEXIST when destination exists.
    if (remove(kVisibilityPath) != 0 && errno != ENOENT) {
        UL_LOG_WARN("qdesktop: QdVisibility::Save: remove(canonical) failed errno=%d", errno);
        // continue — rename may still succeed on some FS
    }

    if (rename(kVisibilityTmpPath, kVisibilityPath) != 0) {
        UL_LOG_WARN("qdesktop: QdVisibility::Save: rename failed errno=%d", errno);
        return false;
    }

    const Result commit_rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(commit_rc)) {
        UL_LOG_WARN("qdesktop: QdVisibility::Save: fsdevCommitDevice rc=0x%x", commit_rc);
    }
    return true;
}

} // namespace ul::menu::qdesktop
