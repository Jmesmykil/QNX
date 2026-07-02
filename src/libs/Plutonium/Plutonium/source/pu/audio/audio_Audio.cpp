#include <pu/audio/audio_Audio.hpp>

namespace pu::audio {

    bool Initialize(s32 mix_flags) {
        if(Mix_Init(mix_flags) != mix_flags) {
            return false;
        }

        // QOS-AUDIO-LATENCY (2026-06-19): buffer reduced 4096→1024 for snappy SFX
        // (~93 ms → ~23 ms at 44100 Hz).  Mix_AllocateChannels(24) prevents rapid
        // SFX from cutting each other off when all 8 default channels are busy.
        if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 1024) == -1) {
            return false;
        }
        Mix_AllocateChannels(24);

        return true;
    }

    void Finalize() {
        // QOS-AUDIO-BOOTCHIME (2026-06-19): flush the SFX preload cache before
        // closing the audio device so cached Mix_Chunks are freed while the mixer
        // is still alive.
        FlushSfxCache();
        Mix_CloseAudio();
        Mix_Quit();
    }

}
