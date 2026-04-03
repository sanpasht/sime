// ─────────────────────────────────────────────────────────────────────────────
// AudioClip.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "AudioClip.h"

std::shared_ptr<AudioClip> AudioClip::loadFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        DBG("AudioClip::loadFromFile — file not found: " + file.getFullPathName());
        return nullptr;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader == nullptr)
    {
        DBG("AudioClip::loadFromFile — unsupported format: " + file.getFullPathName());
        return nullptr;
    }

    auto clip = std::shared_ptr<AudioClip>(new AudioClip());

    int channels = static_cast<int>(reader->numChannels);
    int length   = static_cast<int>(reader->lengthInSamples);

    if (channels > 2)
        channels = 2;

    clip->samples.setSize(channels, length);
    reader->read(&clip->samples, 0, length, 0, true, channels > 1);

    clip->numSamples  = length;
    clip->numChannels = channels;
    clip->sampleRate  = static_cast<int>(reader->sampleRate);
    clip->filePath    = file.getFullPathName();
    clip->clipId      = juce::Uuid().toString();

    DBG("AudioClip loaded: " + file.getFileName()
        + " | " + juce::String(clip->numSamples) + " samples"
        + " | " + juce::String(clip->sampleRate) + " Hz"
        + " | " + juce::String(clip->numChannels) + " ch");

    return clip;
}

const float* AudioClip::getReadPointer(int channel) const
{
    if (channel < 0 || channel >= numChannels)
        return nullptr;
    return samples.getReadPointer(channel);
}
