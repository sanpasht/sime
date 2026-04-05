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

        const int srcChannels  = voice.buffer->getNumChannels();
        const int remaining    = voice.buffer->getNumSamples() - voice.samplePosition;
        const int toCopy       = std::min(numSamples, remaining);

        for (int ch = 0; ch < outputChannels; ++ch)
        {
            // Map output channel to source channel (handles mono → stereo)
            const int srcCh = std::min(ch, srcChannels - 1);

            outputBuffer->addFrom(ch,
                                  startSample,
                                  *voice.buffer,
                                  srcCh,
                                  voice.samplePosition,
                                  toCopy,
                                  voice.gain);
        }

        voice.samplePosition += toCopy;
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
        return;   // Sound not loaded; silently ignore

    ActiveVoice voice;
    voice.blockSerial    = ev.blockSerial;
    voice.soundId        = ev.soundId;
    voice.samplePosition = 0;
    voice.buffer         = &it->second;
    voice.gain           = 1.0f;
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