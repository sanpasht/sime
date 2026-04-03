#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SoundBlock.h  –  Authoring-time description of one sound event.
//
// Lives in editor/UI state (SoundScene). Message thread only.
// Never passed directly to the audio thread.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "../MathUtils.h"
#include "AudioConfig.h"
#include <cstdint>

struct SoundBlock
{
    uint64_t    id                 = 0;
    Vec3f       position;
    float       startTime          = 0.0f;
    float       duration           = 0.0f;   // 0 = play to end of clip
    juce::String clipId;
    float       gainDb             = 0.0f;
    float       pitchSemitones     = 0.0f;   // deferred for MVP
    bool        looping            = false;
    float       attenuationRadius  = AudioConfig::kDefaultAttenRadius;
    float       spread             = 0.0f;
    bool        active             = true;

    float getGainLinear() const
    {
        if (gainDb <= -60.0f) return 0.0f;
        return std::pow(10.0f, gainDb / 20.0f);
    }
};
