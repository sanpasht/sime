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
    int    blockSerial    = -1;   ///< Block that triggered this voice
    int    soundId        = -1;   ///< Which sample is being played
    int    samplePosition = 0;    ///< Read cursor within the sample buffer (in samples)
    bool   stopping       = false;///< Marked true when a Stop event arrives; fades then removes
    float  gain           = 1.0f; ///< Per-voice output gain (0–1)

    // Pointer into the sample library (non-owning; owned by AudioEngine)
    const juce::AudioBuffer<float>* buffer = nullptr;

    bool isFinished() const noexcept
    {
        return buffer == nullptr || samplePosition >= buffer->getNumSamples();
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

    /// Remove all loaded samples (call with audio stopped).
    void clearSamples();

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