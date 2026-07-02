#include <pu/audio/audio_Sfx.hpp>
#include <unordered_map>
#include <string>

// QOS-AUDIO-BOOTCHIME (2026-06-19): engine-level SFX preload cache.
// PreloadSfx() populates this map; LoadSfx() returns the cached chunk on a
// hit so hot-path callers (e.g. the MenuApplication render callback that loads
// BootChime.wav on the first frame after login) never touch the SD card.
// DestroySfx() skips chunks owned by the cache; FlushSfxCache() frees them all.
namespace {
    std::unordered_map<std::string, pu::audio::Sfx> g_sfx_cache;
}

namespace pu::audio {

    Sfx LoadSfx(const std::string &path) {
        // Cache hit: return the preloaded chunk directly.
        auto it = g_sfx_cache.find(path);
        if(it != g_sfx_cache.end()) {
            return it->second;
        }
        return Mix_LoadWAV(path.c_str());
    }

    void PreloadSfx(const std::string &path) {
        if(path.empty()) {
            return;
        }
        if(g_sfx_cache.count(path)) {
            return;  // already cached
        }
        Sfx chunk = Mix_LoadWAV(path.c_str());
        if(chunk != nullptr) {
            g_sfx_cache[path] = chunk;
        }
    }

    void PlaySfx(Sfx sfx) {
        Mix_PlayChannel(-1, sfx, 0);
    }

    void DestroySfx(Sfx &sfx) {
        if(sfx == nullptr) {
            return;
        }
        // Do NOT free chunks owned by the preload cache; the cache manages their
        // lifetime.  A caller that loaded the chunk via LoadSfx (cache hit) holds
        // a borrowed pointer, not an owned one.
        for(const auto &kv : g_sfx_cache) {
            if(kv.second == sfx) {
                sfx = nullptr;
                return;
            }
        }
        Mix_FreeChunk(sfx);
        sfx = nullptr;
    }

    void FlushSfxCache() {
        for(auto &kv : g_sfx_cache) {
            if(kv.second != nullptr) {
                Mix_FreeChunk(kv.second);
            }
        }
        g_sfx_cache.clear();
    }

}
