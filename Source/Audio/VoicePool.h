#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// VoicePool.h  –  Pre-allocated pool of Voice objects for the audio thread.
//
// All memory is allocated once at init. No heap during playback.
// Audio thread only (except preallocate which runs on message thread at init).
// ─────────────────────────────────────────────────────────────────────────────

#include "AudioConfig.h"
#include "ParameterSmoother.h"
#include "../MathUtils.h"
#include <cstdint>
#include <array>

struct Voice
{
    bool        active         = false;
    uint64_t    blockId        = 0;
    const float* samplesL      = nullptr;
    const float* samplesR      = nullptr;
    int         totalSamples   = 0;
    int         nativeSampleRate = 0;
    double      readPosition   = 0.0;
    float       gainLinear     = 1.0f;
    float       attenuationRadius = AudioConfig::kDefaultAttenRadius;
    float       spread         = 0.0f;
    Vec3f       position;
    bool        looping        = false;

    ParameterSmoother smoothGainL;
    ParameterSmoother smoothGainR;

    void reset()
    {
        active = false;
        blockId = 0;
        samplesL = nullptr;
        samplesR = nullptr;
        totalSamples = 0;
        nativeSampleRate = 0;
        readPosition = 0.0;
        gainLinear = 1.0f;
        attenuationRadius = AudioConfig::kDefaultAttenRadius;
        spread = 0.0f;
        position = {};
        looping = false;
        smoothGainL.reset(0.0f);
        smoothGainR.reset(0.0f);
    }
};

class VoicePool
{
public:
    void preallocate(int maxVoices);

    Voice* activateVoice(uint64_t blockId, const float* samplesL,
                         const float* samplesR, int totalSamples,
                         int nativeSampleRate, int startSampleOffset,
                         float gainLinear, float attenuationRadius,
                         float spread, Vec3f position, bool looping);

    void deactivateVoice(uint64_t blockId);
    void deactivateAll();

    Voice* findVoice(uint64_t blockId);
    int    getActiveCount() const;
    int    getMaxVoices()   const { return maxVoices; }

    Voice* getVoicesArray() { return voices.data(); }

private:
    std::array<Voice, AudioConfig::kMaxVoices> voices {};
    int maxVoices = 0;
};
