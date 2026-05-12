#pragma once

#include "SequencerEvent.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <unordered_map>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// ActiveVoice
//
// Represents one currently-playing sample instance.  AudioEngine keeps a flat
// list of these and advances each one during the audio callback.
// ---------------------------------------------------------------------------
struct ActiveVoice
{
    int    blockSerial      = -1;
    int    soundId          = -1;
    float  samplePositionF  = 0.0f; ///< Fractional read cursor (supports pitch shifting)
    bool   stopping         = false;
    float  gain             = 1.0f;
    float  pan              = 0.0f; ///< -1 = full left, 0 = center, +1 = full right
    float  pitchRate        = 1.0f; ///< Playback rate multiplier (1.0 = normal, 2.0 = octave up)
    float  leftGain         = 1.0f; ///< Precomputed left channel gain (gain * pan law)
    float  rightGain        = 1.0f; ///< Precomputed right channel gain

    const juce::AudioBuffer<float>* buffer = nullptr;

    bool isFinished() const noexcept
    {
        return buffer == nullptr
            || static_cast<int>(samplePositionF) >= buffer->getNumSamples();
    }
};

// ---------------------------------------------------------------------------
// AudioEngine
//
// Application-level audio layer built on top of JUCE primitives.
//
// Responsibilities
// ----------------
// 1. Own and manage a library of loaded audio samples (soundId → buffer).
// 2. Receive SequencerEvents from the SequencerEngine and translate them into
//    voice start / stop operations.
// 3. Mix all active voices into the output buffer inside the audio callback.
//
// Threading model
// ---------------
// * loadSample() / clearSamples() must be called from the message thread
//   (before audio starts, or with the device stopped).
// * processEvents() is called from the message / render thread and queues
//   events into a lock-free structure consumed by the audio callback.
// * getNextAudioBlock() runs on the audio thread and must not allocate or lock.
//
// For a prototype, the simplest safe approach is to ensure loadSample() is
// called only during setup (before audio starts), and to use a
// juce::AbstractFifo-backed queue for cross-thread event delivery.
// ---------------------------------------------------------------------------
class AudioEngine : public juce::AudioSource
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // -----------------------------------------------------------------------
    // Sample library management
    // Call from the message thread, ideally before audio starts.
    // -----------------------------------------------------------------------

    /// Load an audio file from disk and associate it with soundId.
    /// Returns true on success.
    bool loadSample(int soundId, const juce::File& audioFile);

    /// Synthesize a sine-wave test tone and store it in the sample library.
    /// Safe to call from message thread before start().
    void generateTestTone(int soundId, float frequencyHz, double durationSec);

    /// Synth a violin-like tone: vibrato + harmonics with sustained envelope.
    void generateViolinTone(int soundId, float frequencyHz, double durationSec);

    /// Synth a piano-like tone: sharp attack, exponential decay, rich harmonics.
    void generatePianoTone(int soundId, float frequencyHz, double durationSec);

    /// Synth a drum hit: kick (low thump), snare (noise burst), or hi-hat (click).
    /// type: 0 = kick, 1 = snare, 2 = hi-hat
    void generateDrumHit(int soundId, int drumType, double durationSec);

    /// Remove all loaded samples (call with audio stopped).
    void clearSamples();

    /// Returns true if sampleLibrary_ already contains this soundId.
    bool hasSample(int soundId) const;

    // -----------------------------------------------------------------------
    // Event ingestion
    // Call from the same thread that runs SequencerEngine::update()
    // (message / render thread).  Events are queued and consumed in the
    // audio callback.
    // -----------------------------------------------------------------------
    void processEvents(const std::vector<SequencerEvent>& events);

    // -----------------------------------------------------------------------
    // juce::AudioSource interface
    // -----------------------------------------------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // -----------------------------------------------------------------------
    // Convenience: wire the engine to the default audio device
    // -----------------------------------------------------------------------
    void start();
    void stop();

    /// Sample rate last set by prepareToPlay (matches live playback).
    double getOutputSampleRate() const noexcept { return sampleRate_; }

    /// Read-only sample map for offline export (do not call while mutating library).
    const std::unordered_map<int, juce::AudioBuffer<float>>& getSampleLibrary() const noexcept
    {
        return sampleLibrary_;
    }

private:
    // ---- Sample library -------------------------------------------------
    std::unordered_map<int, juce::AudioBuffer<float>> sampleLibrary_;
    juce::AudioFormatManager formatManager_;

    // ---- Active voices (audio-thread-owned) -----------------------------
    std::vector<ActiveVoice> activeVoices_;

    // ---- Cross-thread event queue ---------------------------------------
    // Simple FIFO; small and bounded.  For a prototype, 256 events per
    // frame is more than enough.
    static constexpr int kFifoCapacity = 256;

    struct PendingEvent { SequencerEvent ev; };
    juce::AbstractFifo              fifo_  { kFifoCapacity };
    std::array<PendingEvent, 256>   queue_ {};

    // ---- JUCE plumbing --------------------------------------------------
    juce::AudioDeviceManager        deviceManager_;
    juce::AudioSourcePlayer         player_;

    double sampleRate_    = 44100.0;
    int    blockSize_     = 512;

    // ---- Internal helpers (called from audio thread) --------------------
    void dispatchEvent   (const SequencerEvent& ev);
    void handleStartEvent(const SequencerEvent& ev);
    void handleStopEvent (const SequencerEvent& ev);
};