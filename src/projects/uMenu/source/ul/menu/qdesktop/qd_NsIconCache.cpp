// qd_NsIconCache.cpp — NS-service JPEG icon cache for system applets (v1.9.0).
// See qd_NsIconCache.hpp for API documentation.
#include <ul/menu/qdesktop/qd_NsIconCache.hpp>
#include <ul/ul_Result.hpp>
#include <SDL2/SDL_image.h>
#include <cstring>
#include <cstdlib>

namespace ul::menu::qdesktop {

// NsApplicationControlData is ~393 KB — heap-allocate, never stack.
// Declared in switch/services/ns.h as:
//   struct NsApplicationControlData { NacpStruct nacp; u8 icon[0x20000]; };
// sizeof(NacpStruct) == 0x4000, sizeof(icon) == 0x20000 → total == 0x24000 == 147456 bytes.
// The JPEG data we want lives in the icon[] field (bytes after the NacpStruct).

QdNsIconCache::QdNsIconCache() : ns_open_(false) {}

QdNsIconCache::~QdNsIconCache() {
    Clear();
}

bool QdNsIconCache::EnsureNsOpen() {
    if (ns_open_) {
        return true;
    }
    const Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        // NS service unavailable — running without internet/NS, or too early in boot.
        return false;
    }
    ns_open_ = true;
    return true;
}

SDL_Texture *QdNsIconCache::Get(u64 program_id, SDL_Renderer *renderer) {
    // Return cached result immediately (nullptr means "tried and failed").
    auto it = cache_.find(program_id);
    if (it != cache_.end()) {
        return it->second;
    }

    // Not cached yet — attempt NS lookup.
    SDL_Texture *result = nullptr;

    if (!EnsureNsOpen()) {
        // NS unavailable — cache nullptr so we don't retry every frame.
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
    return result;
}

void QdNsIconCache::Evict(u64 program_id) {
    auto it = cache_.find(program_id);
    if (it == cache_.end()) {
        return;
    }
    if (it->second != nullptr) {
        SDL_DestroyTexture(it->second);
    }
    cache_.erase(it);
}

void QdNsIconCache::Clear() {
    for (auto &kv : cache_) {
        if (kv.second != nullptr) {
            SDL_DestroyTexture(kv.second);
        }
    }
    cache_.clear();
    if (ns_open_) {
        nsExit();
        ns_open_ = false;
    }
}

QdNsIconCache &GetSharedNsIconCache() {
    static QdNsIconCache s_cache;
    return s_cache;
}

} // namespace ul::menu::qdesktop
