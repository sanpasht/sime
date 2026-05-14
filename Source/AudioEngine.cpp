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
bool AudioEngine::hasSample(int soundId) const
{
    return sampleLibrary_.count(soundId) > 0;
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

// ---------------------------------------------------------------------------
void AudioEngine::generateViolinTone(int soundId, float frequencyHz, double durationSec)
{
    constexpr double kGenRate = 44100.0;
    const int numSamples = static_cast<int>(kGenRate * durationSec);

    juce::AudioBuffer<float> buf(1, numSamples);
    float* data = buf.getWritePointer(0);

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float baseInc = twoPi * frequencyHz / static_cast<float>(kGenRate);

    // Vibrato parameters
    const float vibratoRate = 5.5f;
    const float vibratoDepth = 6.0f;
    const float vibratoInc = twoPi * vibratoRate / static_cast<float>(kGenRate);

    const int attackSamples  = std::min(static_cast<int>(kGenRate * 0.08), numSamples / 4);
    const int releaseSamples = std::min(static_cast<int>(kGenRate * 0.15), numSamples / 4);

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i);

        float vibrato = vibratoDepth * std::sin(vibratoInc * t);
        float phase = (baseInc + twoPi * vibrato / static_cast<float>(kGenRate)) * t;

        // Fundamental + harmonics to give a richer, string-like timbre
        float sample = std::sin(phase) * 0.55f
                     + std::sin(phase * 2.0f) * 0.25f
                     + std::sin(phase * 3.0f) * 0.12f
                     + std::sin(phase * 4.0f) * 0.06f;

        // Envelope: gentle attack, sustain, gentle release
        float env = 1.0f;
        if (i < attackSamples)
            env = static_cast<float>(i) / static_cast<float>(attackSamples);
        else if (i > numSamples - releaseSamples)
            env = static_cast<float>(numSamples - i) / static_cast<float>(releaseSamples);

        data[i] = sample * env * 0.35f;
    }

    sampleLibrary_[soundId] = std::move(buf);
}

// ---------------------------------------------------------------------------
void AudioEngine::generatePianoTone(int soundId, float frequencyHz, double durationSec)
{
    constexpr double kGenRate = 44100.0;
    const int numSamples = static_cast<int>(kGenRate * durationSec);

    juce::AudioBuffer<float> buf(1, numSamples);
    float* data = buf.getWritePointer(0);

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float baseInc = twoPi * frequencyHz / static_cast<float>(kGenRate);

    // Harmonic amplitudes (piano has strong initial harmonics that decay)
    const float harmonics[] = { 1.0f, 0.5f, 0.35f, 0.15f, 0.08f, 0.04f };
    const int numHarmonics = 6;

    // Exponential decay time constant — higher harmonics decay faster
    const float decayBase = static_cast<float>(numSamples) * 0.4f;

    const int attackSamples = std::min(static_cast<int>(kGenRate * 0.005), numSamples / 4);

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i);
        float sample = 0.0f;

        for (int h = 0; h < numHarmonics; ++h)
        {
            float harmFreq = baseInc * static_cast<float>(h + 1);
            float harmDecay = std::exp(-t / (decayBase / static_cast<float>(h + 1)));
            sample += std::sin(harmFreq * t) * harmonics[h] * harmDecay;
        }

        // Sharp attack
        float env = 1.0f;
        if (i < attackSamples)
            env = static_cast<float>(i) / static_cast<float>(attackSamples);

        data[i] = sample * env * 0.35f;
    }

    sampleLibrary_[soundId] = std::move(buf);
}

// ---------------------------------------------------------------------------
void AudioEngine::generateDrumHit(int soundId, int drumType, double durationSec)
{
    constexpr double kGenRate = 44100.0;
    const int numSamples = static_cast<int>(kGenRate * durationSec);

    juce::AudioBuffer<float> buf(1, numSamples);
    float* data = buf.getWritePointer(0);

    const float twoPi = juce::MathConstants<float>::twoPi;
    juce::Random rng;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i);
        float tSec = t / static_cast<float>(kGenRate);
        float sample = 0.0f;

        if (drumType == 0)
        {
            // Kick: pitch-dropping sine (150Hz → 50Hz) + fast decay
            float freq = 50.0f + 100.0f * std::exp(-tSec * 30.0f);
            float phase = twoPi * freq * t / static_cast<float>(kGenRate);
            float body = std::sin(phase) * std::exp(-tSec * 8.0f);
            float click = (rng.nextFloat() * 2.0f - 1.0f) * std::exp(-tSec * 80.0f) * 0.3f;
            sample = body + click;
        }
        else if (drumType == 1)
        {
            // Snare: low tone + noise burst
            float tone = std::sin(twoPi * 180.0f * t / static_cast<float>(kGenRate))
                       * std::exp(-tSec * 15.0f) * 0.5f;
            float noise = (rng.nextFloat() * 2.0f - 1.0f) * std::exp(-tSec * 12.0f) * 0.7f;
            sample = tone + noise;
        }
        else
        {
            // Hi-hat: high-frequency filtered noise, very short
            float noise = (rng.nextFloat() * 2.0f - 1.0f);
            float highPass = noise * std::exp(-tSec * 40.0f);
            sample = highPass * 0.6f;
        }

        data[i] = sample * 0.5f;
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
    else if (ev.type == SequencerEventType::Stop)
        handleStopEvent(ev);
    else if (ev.type == SequencerEventType::Movement)
    {
        // Update spatial position of playing voice
        for (auto& voice : activeVoices_)
        {
            if (voice.blockSerial == ev.blockSerial && !voice.isFinished())
            {
                // Update pan based on X position (simple spatial audio)
                float pan = juce::jmap(ev.blockX, -20.0f, 20.0f, -1.0f, 1.0f);
                voice.pan = juce::jlimit(-1.0f, 1.0f, pan);
                
                // Recalculate gains
                float leftGain = (1.0f - voice.pan) * 0.5f;
                float rightGain = (1.0f + voice.pan) * 0.5f;
                voice.leftGain = voice.gain * leftGain;
                voice.rightGain = voice.gain * rightGain;
                
                DBG("Updated spatial audio for block " << ev.blockSerial 
                    << " at X=" << ev.blockX << ", pan=" << voice.pan);
            }
        }
    }
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

    applySpatialPosition(voice, ev.blockX, ev.blockY, ev.blockZ);

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

void AudioEngine::applySpatialPosition(ActiveVoice& voice, float x, float y, float z)
{
    // XZ plane distance from listener at origin
    const float distance = std::sqrt(x * x + z * z);

    // Volume from Euclidean distance
    constexpr float refDist = 5.0f;
    voice.gain = juce::jlimit(0.0f, 1.0f, refDist / (refDist + distance));

    // Y controls pitch only
    voice.pitchRate = juce::jlimit(0.25f, 4.0f,
        std::pow(2.0f, y / 12.0f));

    // Direction in XZ plane
    const float angle = std::atan2(x, z);

    // Stereo pan from direction
    const float pan = std::sin(angle);
    voice.pan = juce::jlimit(-1.0f, 1.0f, pan);

    // Front/back effect: rear sounds slightly reduced
    const float frontBack = std::cos(angle);
    const float rearAttenuation = juce::jmap(frontBack, -1.0f, 1.0f, 0.65f, 1.0f);

    // Equal-power stereo panning
    const float panAngle = (voice.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    voice.leftGain  = voice.gain * rearAttenuation * std::cos(panAngle);
    voice.rightGain = voice.gain * rearAttenuation * std::sin(panAngle);
}