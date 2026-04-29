// qd_NsIconCache.hpp — NS-service JPEG icon cache for system applets (v1.9.0).
// Queries nsGetApplicationControlData() for a Nintendo program ID, decodes the
// embedded JPEG via SDL2_image, and caches the resulting SDL_Texture*.
//
// Usage:
//   QdNsIconCache &cache = GetSharedNsIconCache();
//   SDL_Texture *tex = cache.Get(0x010000000000100DUL, renderer); // Album
//   if (tex) { /* blit tex */ } else { /* fall back to baked PNG */ }
//
// The cache owns every texture it produces.  Do NOT call SDL_DestroyTexture on
// textures returned from Get(); the destructor handles cleanup.
//
// Thread safety: NOT thread-safe.  All calls must come from the render thread.
#pragma once
#include <switch.h>
#include <SDL2/SDL.h>
#include <unordered_map>
#include <cstdint>

namespace ul::menu::qdesktop {

class QdNsIconCache {
public:
    QdNsIconCache();
    ~QdNsIconCache();

    // Return a cached SDL_Texture* for program_id, or nullptr on failure.
    // On the first call for a given program_id:
    //   1. Calls nsInitialize() once (idempotent; ns_open_ guards it).
    //   2. Tries NsApplicationControlSource_Storage (full icon quality).
    //   3. Falls back to NsApplicationControlSource_CacheOnly on 0x196002
    //      (permission-denied when running as library applet).
    //   4. Wraps NsApplicationControlData::icon in SDL_RWFromConstMem and
    //      decodes via IMG_LoadTexture_RW.
    //   5. Stores result (nullptr on decode failure) in cache_.
    // Subsequent calls for the same program_id return the cached pointer
    // immediately without hitting NS again.
    SDL_Texture *Get(u64 program_id, SDL_Renderer *renderer);

    // Drop a single entry from the cache, destroying its texture.
    // No-op if the entry is not present.
    void Evict(u64 program_id);

    // Destroy all cached textures and close the NS service handle.
    void Clear();

private:
    // Open NS service handle once; idempotent.
    // Returns true if the handle is open (either already was or just opened).
    bool EnsureNsOpen();

    bool ns_open_;
    std::unordered_map<u64, SDL_Texture *> cache_;
};

// Process-global singleton — shared between Desktop and Launchpad.
// Lifetime: first call through process exit.
QdNsIconCache &GetSharedNsIconCache();

// Program IDs for Switch system applets whose icons can be fetched via NS.
// Derived from AppletId constants in switch/services/applet.h.
// Settings and Themes are uMenu-internal panels with no NS registration;
// they are excluded from this table and continue to use baked-PNG assets.
namespace NsAppletProgramId {
    static constexpr u64 Album       = 0x010000000000100DULL; // photoViewer
    static constexpr u64 Cabinet     = 0x0100000000001002ULL; // cabinet (Amiibo)
    static constexpr u64 Controller  = 0x0100000000001003ULL; // controller
    static constexpr u64 MiiEdit     = 0x0100000000001009ULL; // miiEdit
    static constexpr u64 WebBrowser  = 0x010000000000100AULL; // LibAppletWeb
    static constexpr u64 UserPage    = 0x0100000000001013ULL; // myPage
} // namespace NsAppletProgramId

} // namespace ul::menu::qdesktop
