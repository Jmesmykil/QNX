/**
 * Plutonium library
 * @file audio_Sfx.hpp
 * @brief Sfx (sound effects) support
 * @author XorTroll
 * @copyright XorTroll
 */

#pragma once
#include <SDL2/SDL_mixer.h>
#include <pu/pu_Include.hpp>

namespace pu::audio {

    /**
     * @brief Type representing a sound effect.
     */
    using Sfx = Mix_Chunk*;

    /**
     * @brief Loads a sound effect from the specified path.
     * @note pu::audio::Initialize must be called before using this function.
     * @note The sound effect must be manually freed with pu::audio::DestroySfx when it is no longer needed.
     * @param path Path to the sound effect file.
     * @return Sound effect loaded, or NULL if an error occurred.
     */
    Sfx LoadSfx(const std::string &path);

    /**
     * @brief Preloads a sound effect into the engine-level SFX cache so that a
     *        subsequent LoadSfx() for the same path returns instantly without
     *        touching the SD card again.  Safe to call during boot / layout
     *        LoadSfx() — the cache entry is reference-counted and DestroySfx()
     *        on a cached chunk is a no-op (the engine owns the lifetime).
     *
     * QOS-AUDIO-BOOTCHIME (2026-06-19): the boot-chime LoadSfx() call in the
     * MenuApplication render callback was synchronous on the hot-path.  Calling
     * PreloadSfx() from QdLaunchpadHostLayout::LoadSfx() (or any early site)
     * guarantees the chunk is already in the cache before the render callback
     * fires, eliminating the SD-card stall from the frame budget.
     *
     * @param path Path to the sound effect file.
     */
    void PreloadSfx(const std::string &path);

    /**
     * @brief Plays a sound effect.
     * @note pu::audio::Initialize must be called before using this function.
     * @param sfx Sound effect to play. If NULL is passed, nothing will happen.
     */
    void PlaySfx(Sfx sfx);

    /**
     * @brief Destroys a sound effect.
     * @note Chunks that were loaded via the PreloadSfx cache are NOT freed here;
     *       the engine retains ownership.  All other chunks are freed normally.
     * @param sfx Sound effect to destroy. If NULL is passed, nothing will happen.
     */
    void DestroySfx(Sfx &sfx);

    /**
     * @brief Frees every chunk held by the engine-level SFX preload cache.
     *        Call once from pu::audio::Finalize() or MenuApplication::Finalize().
     */
    void FlushSfxCache();

}
