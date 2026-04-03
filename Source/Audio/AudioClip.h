#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioClip.h  –  Immutable, preloaded PCM audio buffer.
//
// Once constructed via loadFromFile(), the sample data is never mutated.
// Safe to read from any thread.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <memory>

class AudioClip
{
public:
    static std::shared_ptr<AudioClip> loadFromFile(const juce::File& file);

    const float* getReadPointer(int channel) const;
    int          getNumSamples()  const { return numSamples; }
    int          getNumChannels() const { return numChannels; }
    int          getSampleRate()  const { return sampleRate; }
    const juce::String& getFilePath() const { return filePath; }
    const juce::String& getClipId()   const { return clipId; }

private:
    AudioClip() = default;

    juce::AudioBuffer<float> samples;
    int         numSamples  = 0;
    int         numChannels = 0;
    int         sampleRate  = 0;
    juce::String filePath;
    juce::String clipId;
};
