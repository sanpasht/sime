#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Mixer.h  –  Innermost audio loop.
//
// Iterates active voices, reads samples, resamples, applies stereo gains,
// accumulates into the output buffer, and applies a soft limiter.
// Audio thread only.
// ─────────────────────────────────────────────────────────────────────────────

#include "VoicePool.h"
#include "AudioConfig.h"
#include "Spatializer.h"
#include "../MathUtils.h"

class Mixer
{
public:
    void process(Voice* voices, int maxVoices,
                 float* outL, float* outR, int numSamples,
                 int outputSampleRate,
                 const Vec3f& listenerPos,
                 const Vec3f& listenerRight);

private:
    static float softClip(float x);
};
