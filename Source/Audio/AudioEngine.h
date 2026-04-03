#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine.h  –  Top-level audio subsystem for SIME.
//
// Owned by MainComponent. Implements juce::AudioIODeviceCallback.
// Constructed/destroyed on message thread; callback fires on audio thread.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "AudioConfig.h"
#include "LockFreeTypes.h"
#include "TransportCommand.h"
#include "PlaybackTransport.h"
#include "DeviceManagerWrapper.h"
#include "SampleLibrary.h"
#include "VoicePool.h"
#include "Mixer.h"
#include "Scheduler.h"
#include "SceneSnapshot.h"
#include <atomic>
#include <cstdint>
#include <array>

struct AudioEngineStatus
{
    std::atomic<int>     activeVoices  { 0 };
    std::atomic<float>   peakLevelL    { 0.0f };
    std::atomic<float>   peakLevelR    { 0.0f };
    std::atomic<int64_t> underrunCount { 0 };
};

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    void pushTransportCommand(TransportCommand cmd);
    void pushSnapshot(const SceneSnapshot& snap);
    void loadTestClip(const juce::File& file);

    PlaybackTransport& getTransport() { return transport; }
    SampleLibrary&     getSampleLibrary() { return sampleLibrary; }
    const AudioEngineStatus& getStatus() const { return status; }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    DeviceManagerWrapper deviceWrapper;
    PlaybackTransport    transport;
    SampleLibrary        sampleLibrary;
    VoicePool            voicePool;
    Mixer                mixer;
    Scheduler            scheduler;
    AudioEngineStatus    status;

    SPSCQueue<TransportCommand, 64> commandQueue;
    AtomicSnapshotBuffer<SceneSnapshot> snapshotBuffer;

    static constexpr int kMaxBufferSize = 4096;
    std::array<float, kMaxBufferSize> scratchL {};
    std::array<float, kMaxBufferSize> scratchR {};

    int outputSampleRate = AudioConfig::kPreferredSampleRate;

    std::shared_ptr<AudioClip> testClip;
    std::atomic<bool> testClipReady { false };
};
