#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DeviceManagerWrapper.h  –  Thin wrapper around juce::AudioDeviceManager.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "AudioConfig.h"

class DeviceManagerWrapper
{
public:
    bool openDefaultDevice(int sampleRate = AudioConfig::kPreferredSampleRate,
                           int bufferSize = AudioConfig::kPreferredBufferSize);
    void close();

    int getSampleRate()  const { return activeSampleRate; }
    int getBufferSize()  const { return activeBufferSize; }

    juce::AudioDeviceManager&       getManager()       { return deviceManager; }
    const juce::AudioDeviceManager& getManager() const { return deviceManager; }

private:
    juce::AudioDeviceManager deviceManager;
    int activeSampleRate = 0;
    int activeBufferSize = 0;
};
