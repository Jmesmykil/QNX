// qd_NsIconCache.cpp — NS-service JPEG icon cache for system applets (v1.9.0).
// See qd_NsIconCache.hpp for API documentation.
#include <ul/menu/qdesktop/qd_NsIconCache.hpp>
#include <ul/menu/qdesktop/qd_ResourceLedger.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL_image.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace ul::menu::qdesktop {

// NsApplicationControlData is ~393 KB — heap-allocate, never stack.
// Declared in switch/services/ns.h as:
//   struct NsApplicationControlData { NacpStruct nacp; u8 icon[0x20000]; };
// sizeof(NacpStruct) == 0x4000, sizeof(icon) == 0x20000 → total == 0x24000 == 147456 bytes.
// The JPEG data we want lives in the icon[] field (bytes after the NacpStruct).

// W5-TRANSITIONS #4: main.cpp:222 calls nsInitialize() unconditionally before
// uMenu starts; the NS session remains open for the process lifetime.
// QdNsIconCache used to open a SECOND session here — removed to eliminate the
// double-init.  ns_open_ is retained (set to true) so the Clear() guard path
// does not call nsExit() on behalf of the cache, which would under-exit the
// main session.  The main.cpp nsExit() in __appExit is the correct teardown.

QdNsIconCache::QdNsIconCache() : ns_open_(true) {}

QdNsIconCache::~QdNsIconCache() {
    // Release texture memory only; do NOT call nsExit() — main session is owned
    // by main.cpp:222 and torn down in __appExit.
    for (auto &kv : cache_) {
        // W6-LEDGER: untrack before destroying texture.
        auto hit = ledger_handles_.find(kv.first);
        if (hit != ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(hit->second);
        }
        if (kv.second != nullptr) {
            SDL_DestroyTexture(kv.second);
        }
    }
    cache_.clear();
    lru_order_.clear();  // A4-OPT-06: clear LRU list in dtor
    ledger_handles_.clear();
    ns_open_ = false;
}

bool QdNsIconCache::EnsureNsOpen() {
    // NS session is always open via main.cpp; just report availability.
    return ns_open_;
}

SDL_Texture *QdNsIconCache::Get(u64 program_id, SDL_Renderer *renderer) {
    // Return cached result immediately (nullptr means "tried and failed").
    auto it = cache_.find(program_id);
    if (it != cache_.end()) {
        // A4-OPT-06: move to MRU front on hit (nullptr sentinels skip LRU).
        if (it->second != nullptr) {
            lru_order_.remove(program_id);
            lru_order_.push_front(program_id);
        }
        return it->second;
    }

    // Not cached yet — attempt NS lookup.
    SDL_Texture *result = nullptr;

    if (!ns_open_) {
        // NS session not available; cache nullptr so we don't retry every frame.
        cache_[program_id] = nullptr;
        return nullptr;
    }

    // Heap-allocate the 393 KB struct (too large for the Switch stack).
    NsApplicationControlData *ctrl = static_cast<NsApplicationControlData *>(
        std::malloc(sizeof(NsApplicationControlData)));
    if (ctrl == nullptr) {
        cache_[program_id] = nullptr;
        return nullptr;
    }

    u64 actual_size = 0;

    // Try Storage source first (full-quality, may be blocked at library-applet privilege).
    Result rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        program_id,
        ctrl,
        sizeof(NsApplicationControlData),
        &actual_size);

    if (R_FAILED(rc)) {
        // 0x196002 = PermissionDenied when running as library applet.
        // Fall back to CacheOnly — less reliable but permitted from our context.
        rc = nsGetApplicationControlData(
            NsApplicationControlSource_CacheOnly,
            program_id,
            ctrl,
            sizeof(NsApplicationControlData),
            &actual_size);
    }

    if (R_SUCCEEDED(rc) && actual_size > sizeof(NacpStruct)) {
        // icon[] starts immediately after the NacpStruct.
        const u8 *icon_ptr  = ctrl->icon;
        const u64 icon_size = actual_size - sizeof(NacpStruct);

        if (icon_size > 0 && icon_size <= 0x20000) {
            // Wrap the JPEG bytes in an SDL_RWops without copying.
            // freesrc=1 tells IMG_LoadTexture_RW to close the RWops for us.
            SDL_RWops *rw = SDL_RWFromConstMem(icon_ptr,
                                               static_cast<int>(icon_size));
            if (rw != nullptr) {
                result = IMG_LoadTexture_RW(renderer, rw, /*freesrc=*/1);
                // rw is freed by IMG_LoadTexture_RW (freesrc==1).
            }
        }
    }

    std::free(ctrl);

    // Cache the result (nullptr if decode failed — avoids retrying every frame).
    cache_[program_id] = result;

    // A4-OPT-06: maintain LRU order for successful texture entries.
    // nullptr sentinels (decode failures) are not tracked in lru_order_ so
    // they never count toward the cap and are never evicted.
    if (result != nullptr) {
        lru_order_.push_front(program_id);

        // Evict the least-recently-used entry when over cap.
        // SDL_DestroyTexture is render-thread-safe here (Get() is called from
        // the render path — A4-RF-03).
        while (lru_order_.size() > kMaxEntries) {
            const u64 evict_id = lru_order_.back();
            lru_order_.pop_back();
            auto ev = cache_.find(evict_id);
            if (ev != cache_.end()) {
                // W6-LEDGER: untrack before destroying.
                auto lh = ledger_handles_.find(evict_id);
                if (lh != ledger_handles_.end()) {
                    UL_LEDGER_UNTRACK(lh->second);
                    ledger_handles_.erase(lh);
                }
                if (ev->second != nullptr) {
                    SDL_DestroyTexture(ev->second);
                }
                cache_.erase(ev);
            }
        }
    }

    // W6-LEDGER: track only successful decodes (nullptr = miss sentinel, not a resource).
    if (result != nullptr) {
        int tw = 0, th = 0;
        SDL_QueryTexture(result, nullptr, nullptr, &tw, &th);
        const size_t tex_bytes = (tw > 0 && th > 0)
            ? static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u : 0u;
        char ledger_tag[32];
        snprintf(ledger_tag, sizeof(ledger_tag), "ns:%016llx",
                 (unsigned long long)program_id);
        ledger_handles_[program_id] = UL_LEDGER_TRACK(
            QdResKind::IconCache, ledger_tag, tex_bytes);
    }
    return result;
}

void QdNsIconCache::Evict(u64 program_id) {
    auto it = cache_.find(program_id);
    if (it == cache_.end()) {
        return;
    }
    // W6-LEDGER: untrack before destroying.
    auto hit = ledger_handles_.find(program_id);
    if (hit != ledger_handles_.end()) {
        UL_LEDGER_UNTRACK(hit->second);
        ledger_handles_.erase(hit);
    }
    if (it->second != nullptr) {
        SDL_DestroyTexture(it->second);
    }
    cache_.erase(it);
    // A4-OPT-06: remove from LRU list.
    lru_order_.remove(program_id);
}

void QdNsIconCache::Clear() {
    // Destroy cached textures only.  nsExit() is NOT called here — the NS
    // session is owned by main.cpp and torn down in __appExit (W5-TRANSITIONS #4).
    for (auto &kv : cache_) {
        // W6-LEDGER: untrack each entry before destroying.
        auto hit = ledger_handles_.find(kv.first);
        if (hit != ledger_handles_.end()) {
            UL_LEDGER_UNTRACK(hit->second);
        }
        if (kv.second != nullptr) {
            SDL_DestroyTexture(kv.second);
        }
    }
    cache_.clear();
    lru_order_.clear();  // A4-OPT-06: also clear LRU list
    ledger_handles_.clear();
}

QdNsIconCache &GetSharedNsIconCache() {
    static QdNsIconCache s_cache;
    return s_cache;
}

} // namespace ul::menu::qdesktop
