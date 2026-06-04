#pragma once

#include "SynthPatch.h"
#include <JuceHeader.h>

class SynthRenderer
{
public:
    static constexpr double kDefaultSampleRate = 44100.0;

    /// Render a mono patch buffer (includes release tail).
    static juce::AudioBuffer<float> render(const SynthPatch& patch,
                                           double sampleRate = kDefaultSampleRate);

    /// Write mono or stereo buffer to a 16-bit PCM WAV file.
    static bool writeWav(const juce::AudioBuffer<float>& buffer,
                         const juce::File& outputFile,
                         double sampleRate = kDefaultSampleRate);
};
