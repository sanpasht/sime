// ─────────────────────────────────────────────────────────────────────────────
// VoicePool.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "VoicePool.h"

void VoicePool::preallocate(int count)
{
    maxVoices = (count > AudioConfig::kMaxVoices)
                    ? AudioConfig::kMaxVoices : count;
    for (auto& v : voices)
        v.reset();
}

Voice* VoicePool::activateVoice(uint64_t blockId, const float* samplesL,
                                 const float* samplesR, int totalSamples,
                                 int nativeSampleRate, int startSampleOffset,
                                 float gainLinear, float attenuationRadius,
                                 float spread, Vec3f position, bool looping)
{
    for (int i = 0; i < maxVoices; ++i)
    {
        if (!voices[i].active)
        {
            Voice& v = voices[i];
            v.active           = true;
            v.blockId          = blockId;
            v.samplesL         = samplesL;
            v.samplesR         = samplesR;
            v.totalSamples     = totalSamples;
            v.nativeSampleRate = nativeSampleRate;
            v.readPosition     = static_cast<double>(startSampleOffset);
            v.gainLinear       = gainLinear;
            v.attenuationRadius = attenuationRadius;
            v.spread           = spread;
            v.position         = position;
            v.looping          = looping;
            v.smoothGainL.reset(0.0f);
            v.smoothGainR.reset(0.0f);
            return &v;
        }
    }
    return nullptr;
}

void VoicePool::deactivateVoice(uint64_t blockId)
{
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].active && voices[i].blockId == blockId)
        {
            voices[i].reset();
            return;
        }
    }
}

void VoicePool::deactivateAll()
{
    for (int i = 0; i < maxVoices; ++i)
        voices[i].reset();
}

Voice* VoicePool::findVoice(uint64_t blockId)
{
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].active && voices[i].blockId == blockId)
            return &voices[i];
    }
    return nullptr;
}

int VoicePool::getActiveCount() const
{
    int count = 0;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].active)
            ++count;
    }
    return count;
}
