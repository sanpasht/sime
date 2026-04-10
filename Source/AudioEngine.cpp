#include "AudioEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

// ---------------------------------------------------------------------------
AudioEngine::AudioEngine()
{
    formatManager_.registerBasicFormats();   // WAV, AIFF, OGG, FLAC, MP3 (platform-dependent)
}

// ---------------------------------------------------------------------------
AudioEngine::~AudioEngine()
{
    stop();
}

// ============================================================================
// Sample library
// ============================================================================

bool AudioEngine::loadSample(int soundId, const juce::File& audioFile)
{
    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager_.createReaderFor(audioFile));

    if (reader == nullptr)
        return false;

    const auto numChannels = static_cast<int>(reader->numChannels);
    const auto numSamples  = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> buf (numChannels, numSamples);
    reader->read(&buf, 0, numSamples, 0, true, true);

    sampleLibrary_[soundId] = std::move(buf);
    return true;
}

// ---------------------------------------------------------------------------
void AudioEngine::clearSamples()
{
    sampleLibrary_.clear();
}

// ---------------------------------------------------------------------------
void AudioEngine::generateTestTone(int soundId, float frequencyHz, double durationSec)
{
    constexpr double kGenRate = 44100.0;
    const int numSamples = static_cast<int>(kGenRate * durationSec);

    juce::AudioBuffer<float> buf(1, numSamples);
    float* data = buf.getWritePointer(0);

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float phaseInc = twoPi * frequencyHz / static_cast<float>(kGenRate);

    // Sine wave with a short fade-in/out to avoid clicks
    const int fadeSamples = std::min(256, numSamples / 2);

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = std::sin(phaseInc * static_cast<float>(i));

        float envelope = 1.0f;
        if (i < fadeSamples)
            envelope = static_cast<float>(i) / static_cast<float>(fadeSamples);
        else if (i > numSamples - fadeSamples)
            envelope = static_cast<float>(numSamples - i) / static_cast<float>(fadeSamples);

        data[i] = sample * envelope * 0.4f;
    }

    sampleLibrary_[soundId] = std::move(buf);
}

// ============================================================================
// Event ingestion (message thread)
// ============================================================================

void AudioEngine::processEvents(const std::vector<SequencerEvent>& events)
{
    // Write events into the FIFO; the audio callback drains them.
    const int numToWrite = static_cast<int>(events.size());

    int start1, size1, start2, size2;
    fifo_.prepareToWrite(numToWrite, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        queue_[start1 + i].ev = events[i];
    for (int i = 0; i < size2; ++i)
        queue_[start2 + i].ev = events[size1 + i];

    fifo_.finishedWrite(size1 + size2);
}

// ============================================================================
// AudioSource interface
// ============================================================================

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    sampleRate_ = sampleRate;
    blockSize_  = samplesPerBlockExpected;
    activeVoices_.clear();
    activeVoices_.reserve(32);
}

// ---------------------------------------------------------------------------
void AudioEngine::releaseResources()
{
    activeVoices_.clear();
}

// ---------------------------------------------------------------------------
void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    // ---- 1. Drain the event queue ---------------------------------------
    {
        int start1, size1, start2, size2;
        fifo_.prepareToRead(fifo_.getNumReady(), start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            dispatchEvent(queue_[start1 + i].ev);
        for (int i = 0; i < size2; ++i)
            dispatchEvent(queue_[start2 + i].ev);

        fifo_.finishedRead(size1 + size2);
    }

    // ---- 2. Mix all active voices into the output buffer ----------------
    auto* outputBuffer = bufferToFill.buffer;
    const int outputChannels = outputBuffer->getNumChannels();
    const int numSamples     = bufferToFill.numSamples;
    const int startSample    = bufferToFill.startSample;

    for (auto& voice : activeVoices_)
    {
        if (voice.buffer == nullptr) continue;

        const int totalSrc = voice.buffer->getNumSamples();
        auto* outL = outputBuffer->getWritePointer(0, startSample);
        auto* outR = (outputChannels > 1)
                     ? outputBuffer->getWritePointer(1, startSample)
                     : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const int srcIdx = static_cast<int>(voice.samplePositionF);
            if (srcIdx >= totalSrc) break;

            const float sample = voice.buffer->getSample(0, srcIdx);

            outL[i] += sample * voice.leftGain;
            if (outR) outR[i] += sample * voice.rightGain;

            voice.samplePositionF += voice.pitchRate;
        }
    }

    // ---- 3. Remove finished voices --------------------------------------
    activeVoices_.erase(
        std::remove_if(activeVoices_.begin(), activeVoices_.end(),
                       [](const ActiveVoice& v) { return v.isFinished() || v.stopping; }),
        activeVoices_.end());
}

// ============================================================================
// Device management
// ============================================================================

void AudioEngine::start()
{
    // Use the default audio output; two output channels, no inputs
    deviceManager_.initialiseWithDefaultDevices(0, 2);
    player_.setSource(this);
    deviceManager_.addAudioCallback(&player_);
}

// ---------------------------------------------------------------------------
void AudioEngine::stop()
{
    deviceManager_.removeAudioCallback(&player_);
    player_.setSource(nullptr);
}

// ============================================================================
// Private helpers (called from audio thread)
// ============================================================================

void AudioEngine::dispatchEvent(const SequencerEvent& ev)
{
    if (ev.type == SequencerEventType::Start)
        handleStartEvent(ev);
    else
        handleStopEvent(ev);
}

// ---------------------------------------------------------------------------
void AudioEngine::handleStartEvent(const SequencerEvent& ev)
{
    auto it = sampleLibrary_.find(ev.soundId);
    if (it == sampleLibrary_.end())
        return;

    ActiveVoice voice;
    voice.blockSerial     = ev.blockSerial;
    voice.soundId         = ev.soundId;
    voice.samplePositionF = 0.0f;
    voice.buffer          = &it->second;

    // Z → proximity gain: inverse distance falloff
    constexpr float refDist = 5.0f;
    const float dist = std::abs(ev.blockZ);
    voice.gain = juce::jlimit(0.0f, 1.0f, refDist / (refDist + dist));

    // Y → pitch: each grid unit = one semitone
    voice.pitchRate = juce::jlimit(0.25f, 4.0f,
                                   std::pow(2.0f, ev.blockY / 12.0f));

    // X → stereo pan: ±20 grid units maps to full left/right
    voice.pan = juce::jlimit(-1.0f, 1.0f, ev.blockX / 20.0f);
    const float angle = (voice.pan + 1.0f) * 0.25f
                        * juce::MathConstants<float>::pi;
    voice.leftGain  = voice.gain * std::cos(angle);
    voice.rightGain = voice.gain * std::sin(angle);

    activeVoices_.push_back(voice);
}

// ---------------------------------------------------------------------------
void AudioEngine::handleStopEvent(const SequencerEvent& ev)
{
    // Mark matching voices as stopping; they will be removed next block.
    // For a prototype, immediate cut-off is acceptable.  To add a short
    // fade-out envelope, set a fadeout counter here instead.
    for (auto& voice : activeVoices_)
    {
        if (voice.blockSerial == ev.blockSerial)
            voice.stopping = true;
    }
}