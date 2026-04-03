// ─────────────────────────────────────────────────────────────────────────────
// Mixer.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Mixer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void Mixer::process(Voice* voices, int maxVoices,
                    float* outL, float* outR, int numSamples,
                    int outputSampleRate,
                    const Vec3f& listenerPos,
                    const Vec3f& listenerRight)
{
    std::memset(outL, 0, sizeof(float) * static_cast<size_t>(numSamples));
    std::memset(outR, 0, sizeof(float) * static_cast<size_t>(numSamples));

    for (int v = 0; v < maxVoices; ++v)
    {
        Voice& voice = voices[v];
        if (!voice.active || voice.samplesL == nullptr)
            continue;

        const float* srcL = voice.samplesL;
        const float* srcR = (voice.samplesR != nullptr) ? voice.samplesR : voice.samplesL;
        const int totalSamples = voice.totalSamples;

        const double resampleRatio = static_cast<double>(voice.nativeSampleRate)
                                   / static_cast<double>(outputSampleRate);

        // Compute spatial gains
        float spatialL = 1.0f, spatialR = 1.0f;
        Spatializer::computeGains(voice.position, listenerPos, listenerRight,
                                  voice.attenuationRadius, voice.spread,
                                  spatialL, spatialR);

        float targetGainL = voice.gainLinear * spatialL;
        float targetGainR = voice.gainLinear * spatialR;

        float startGainL = voice.smoothGainL.current;
        float startGainR = voice.smoothGainR.current;
        voice.smoothGainL.next(targetGainL);
        voice.smoothGainR.next(targetGainR);
        float endGainL = voice.smoothGainL.current;
        float endGainR = voice.smoothGainR.current;

        const float invN = (numSamples > 1) ? 1.0f / static_cast<float>(numSamples - 1) : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            double readPos = voice.readPosition;
            int idx0 = static_cast<int>(readPos);
            float frac = static_cast<float>(readPos - static_cast<double>(idx0));

            if (idx0 >= totalSamples)
            {
                if (voice.looping)
                {
                    voice.readPosition = 0.0;
                    idx0 = 0;
                    frac = 0.0f;
                }
                else
                {
                    voice.active = false;
                    break;
                }
            }

            int idx1 = idx0 + 1;
            if (idx1 >= totalSamples)
                idx1 = voice.looping ? 0 : idx0;

            float sL = srcL[idx0] + (srcL[idx1] - srcL[idx0]) * frac;
            float sR = srcR[idx0] + (srcR[idx1] - srcR[idx0]) * frac;

            float t = static_cast<float>(i) * invN;
            float gL = startGainL + (endGainL - startGainL) * t;
            float gR = startGainR + (endGainR - startGainR) * t;

            outL[i] += sL * gL;
            outR[i] += sR * gR;

            voice.readPosition += resampleRatio;
        }
    }

    constexpr float drive = AudioConfig::kOutputLimiterDrive;
    constexpr float invDrive = 1.0f / drive;
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = softClip(outL[i] * drive) * invDrive;
        outR[i] = softClip(outR[i] * drive) * invDrive;
    }
}

float Mixer::softClip(float x)
{
    return std::tanh(x);
}
