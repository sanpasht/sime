// ─────────────────────────────────────────────────────────────────────────────
// DeviceManagerWrapper.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "DeviceManagerWrapper.h"

bool DeviceManagerWrapper::openDefaultDevice(int sampleRate, int bufferSize)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate     = static_cast<double>(sampleRate);
    setup.bufferSize     = bufferSize;
    setup.outputChannels.setRange(0, 2, true);
    setup.inputChannels.clear();

    juce::String err = deviceManager.initialise(0, 2, nullptr, true, {}, &setup);

    if (err.isNotEmpty())
    {
        DBG("DeviceManagerWrapper::openDefaultDevice FAILED: " + err);
        return false;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        activeSampleRate = static_cast<int>(device->getCurrentSampleRate());
        activeBufferSize = device->getCurrentBufferSizeSamples();
        DBG("Audio device opened: " + device->getName()
            + " @ " + juce::String(activeSampleRate) + " Hz"
            + ", buffer " + juce::String(activeBufferSize));
    }

    return true;
}

void DeviceManagerWrapper::close()
{
    deviceManager.closeAudioDevice();
    activeSampleRate = 0;
    activeBufferSize = 0;
}
