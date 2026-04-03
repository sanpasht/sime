// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "AudioEngine.h"
#include <cstring>
#include <cmath>
#include <algorithm>

AudioEngine::AudioEngine()
{
    voicePool.preallocate(AudioConfig::kMaxVoices);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::initialise()
{
    if (!deviceWrapper.openDefaultDevice())
        return;

    outputSampleRate = deviceWrapper.getSampleRate();
    deviceWrapper.getManager().addAudioCallback(this);
}

void AudioEngine::shutdown()
{
    deviceWrapper.getManager().removeAudioCallback(this);
    deviceWrapper.close();
}

void AudioEngine::pushTransportCommand(TransportCommand cmd)
{
    commandQueue.tryPush(cmd);
}

void AudioEngine::pushSnapshot(const SceneSnapshot& snap)
{
    snapshotBuffer.write(snap);
}

void AudioEngine::loadTestClip(const juce::File& file)
{
    testClip = sampleLibrary.loadSync(file);
    if (testClip != nullptr)
    {
        DBG("Test clip loaded: " + testClip->getClipId());
        testClipReady.store(true, std::memory_order_release);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio callback — runs on the audio thread. NO locks, NO allocations.
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (numSamples > kMaxBufferSize)
        numSamples = kMaxBufferSize;

    // Read the latest scene snapshot (lock-free)
    const SceneSnapshot& snap = snapshotBuffer.read();

    // Drain transport commands
    TransportCommand cmd;
    while (commandQueue.tryPop(cmd))
    {
        switch (cmd.type)
        {
            case TransportCommandType::Play:   transport.play();                 break;
            case TransportCommandType::Pause:  transport.pause();                break;
            case TransportCommandType::Stop:   transport.stop(); voicePool.deactivateAll(); break;
            case TransportCommandType::Seek:
                transport.seekTo(cmd.seekTarget);
                voicePool.deactivateAll();
                break;
        }
        scheduler.handleTransportCommand(cmd, voicePool, snap);
    }

    bool playing = transport.isPlaying();

    // Schedule: activate/deactivate voices based on snapshot and transport time
    if (playing)
    {
        SceneSnapshot liveSnap = snap;
        liveSnap.transportTimeSec = transport.getCurrentTime();
        liveSnap.isPlaying = true;
        scheduler.processSnapshot(liveSnap, voicePool);
    }

    // Mix active voices with spatial data from snapshot
    float* mixL = scratchL.data();
    float* mixR = scratchR.data();
    mixer.process(voicePool.getVoicesArray(), voicePool.getMaxVoices(),
                  mixL, mixR, numSamples, outputSampleRate,
                  snap.listenerPos, snap.listenerRight);

    // Copy to output
    if (numOutputChannels >= 1 && outputChannelData[0] != nullptr)
        std::memcpy(outputChannelData[0], mixL, sizeof(float) * static_cast<size_t>(numSamples));
    if (numOutputChannels >= 2 && outputChannelData[1] != nullptr)
        std::memcpy(outputChannelData[1], mixR, sizeof(float) * static_cast<size_t>(numSamples));
    for (int ch = 2; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
            std::memset(outputChannelData[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
    }

    // Track peak levels
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        peakL = std::max(peakL, std::abs(mixL[i]));
        peakR = std::max(peakR, std::abs(mixR[i]));
    }
    status.peakLevelL.store(peakL, std::memory_order_relaxed);
    status.peakLevelR.store(peakR, std::memory_order_relaxed);

    // Advance transport time if playing
    if (playing)
    {
        double dt = static_cast<double>(numSamples) / static_cast<double>(outputSampleRate);
        transport.advanceTime(dt);
    }

    // Update status
    status.activeVoices.store(voicePool.getActiveCount(), std::memory_order_relaxed);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
        outputSampleRate = static_cast<int>(device->getCurrentSampleRate());
}

void AudioEngine::audioDeviceStopped()
{
    voicePool.deactivateAll();
}
