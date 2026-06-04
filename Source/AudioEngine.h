#pragma once

#include "SequencerEvent.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
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
    float  pitchRate        = 1.0f; ///< Spatial Y pitch shift (e.g. 1.0 = no shift)
    float  blockRate        = 1.0f; ///< Per-block playback mode rate (Stretch / Speed)
    float  dopplerRate      = 1.0f; ///< Per-voice rate multiplier from Doppler effect
    float  leftGain         = 1.0f; ///< Precomputed left channel gain (gain * pan law)
    float  rightGain        = 1.0f; ///< Precomputed right channel gain
    bool   loopBuffer       = false; ///< When true, sample position wraps at buffer end

    // Loop-with-buffer state (only meaningful when loopBuffer == true)
    float  loopGapSamples    = 0.0f; ///< Silence to insert between repeats (samples)
    float  loopGapRemaining  = 0.0f; ///< Counts down during the inter-loop silence

    // ── Last known source position + velocity (for Doppler recompute) ────────
    float  posX             = 0.0f;
    float  posY             = 0.0f;
    float  posZ             = 0.0f;
    float  velX             = 0.0f;
    float  velY             = 0.0f;
    float  velZ             = 0.0f;

    const juce::AudioBuffer<float>* buffer = nullptr;

    bool isFinished() const noexcept
    {
        if (buffer == nullptr) return true;
        if (loopBuffer)        return false;   // looping voices only end via Stop event
        return static_cast<int>(samplePositionF) >= buffer->getNumSamples();
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

    /// Copy a rendered/in-memory buffer into the sample library (message thread).
    void setSampleBuffer(int soundId, juce::AudioBuffer<float> buffer);

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

    /// Tape-speed fast forward. 1.0 = normal, 2.0 = double speed, etc.
    /// Safe to call from any thread (the audio callback reads atomically).
    void setPlaybackRate(double rate) noexcept
    {
        playbackRate_.store(static_cast<float>(juce::jlimit(0.25, 8.0, rate)));
    }

    double getPlaybackRate() const noexcept { return playbackRate_.load(); }

    // ── Spatial listener (camera-relative mix) ──────────────────────────────
    /// World position of the audio listener (usually the camera, or the
    /// frozen anchor when anchor mode is on).  1 grid unit = 1 metre.
    void setListenerPosition(float x, float y, float z) noexcept
    {
        listenerX_.store(x);
        listenerY_.store(y);
        listenerZ_.store(z);
    }

    /// Listener forward + right unit vectors (full 3-D forward, horizontal
    /// right).  Updated each frame from the camera so pan and front/back
    /// follow where you are looking.
    void setListenerOrientation(float fwdX, float fwdY, float fwdZ,
                                float rightX, float rightY, float rightZ) noexcept
    {
        listenerFwdX_.store(fwdX);
        listenerFwdY_.store(fwdY);
        listenerFwdZ_.store(fwdZ);
        listenerRightX_.store(rightX);
        listenerRightY_.store(rightY);
        listenerRightZ_.store(rightZ);
    }

    /// 0.5 = gentler distance curve, 1.0 = default, 2.0 = aggressive falloff.
    void setSpatialSensitivity(float s) noexcept
    {
        spatialSensitivity_.store(juce::jlimit(0.25f, 3.0f, s));
    }

    float getSpatialSensitivity() const noexcept { return spatialSensitivity_.load(); }

    /// Read-only spatial metrics for a source at world (x,y,z) using the
    /// same formulas as the live mix (safe from the message thread).
    struct SpatialReadout
    {
        float distanceMetres = 0.f;
        float gainLinear     = 1.f;
        float approxDb       = 0.f;   ///< 20·log10(gain), clamped
    };

    SpatialReadout measureSourceAt(float x, float y, float z) const noexcept;

    /// Pure spatial math, no atomics, no engine state.  Used by both the live
    /// audio thread and the offline exporter so they cannot drift apart.
    static void computeSpatialGainsStatic(float lx, float ly, float lz,
                                          float fwdX, float fwdY, float fwdZ,
                                          float rightX, float rightY, float rightZ,
                                          float sensitivity,
                                          float srcX, float srcY, float srcZ,
                                          float& outGain, float& outPan,
                                          float& outPitchRate,
                                          float& outLeft, float& outRight) noexcept;

    void setDopplerEnabled(bool enabled) noexcept { dopplerEnabled_.store(enabled); }
    bool isDopplerEnabled() const noexcept        { return dopplerEnabled_.load(); }

    /// Speed of sound in world (grid) units per second.  1 unit = 1 metre.
    void setSpeedOfSound(float c) noexcept
    {
        speedOfSound_.store(juce::jlimit(5.0f, 343.0f, c));
    }

    // ── Transport-side audio gates (Pause / Stop) ───────────────────────────
    /// When true, getNextAudioBlock outputs silence and DOES NOT advance any
    /// voice positions.  This is how Pause freezes the audio instantly.
    void setAudioPaused(bool paused) noexcept { audioPaused_.store(paused); }
    bool isAudioPaused() const noexcept       { return audioPaused_.load(); }

    /// Queues an immediate "kill all voices" for the audio thread.  Used by
    /// Stop and Seek so the next callback drops every in-flight voice without
    /// waiting for them to ring out.
    void killAllVoices() noexcept             { killAllVoices_.store(true); }

private:
    // ---- Sample library -------------------------------------------------
    std::unordered_map<int, juce::AudioBuffer<float>> sampleLibrary_;
    juce::AudioFormatManager formatManager_;


    void applySpatialPosition(ActiveVoice& voice, float x, float y, float z);
    void computeSpatialGains(float x, float y, float z,
                             float& outGain, float& outPan,
                             float& outPitchRate,
                             float& outLeft, float& outRight) const noexcept;

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

    std::atomic<float> playbackRate_ { 1.0f };

    // Spatial listener state (audio-thread reads, message thread writes)
    std::atomic<float> listenerX_       { 0.0f };
    std::atomic<float> listenerY_       { 0.0f };
    std::atomic<float> listenerZ_       { 0.0f };
    std::atomic<float> listenerFwdX_    { 0.0f };
    std::atomic<float> listenerFwdY_    { 0.0f };
    std::atomic<float> listenerFwdZ_    { -1.0f };
    std::atomic<float> listenerRightX_ { 1.0f };
    std::atomic<float> listenerRightY_  { 0.0f };
    std::atomic<float> listenerRightZ_  { 0.0f };
    std::atomic<float> spatialSensitivity_ { 1.0f };
    std::atomic<bool>  dopplerEnabled_  { false };
    std::atomic<float> speedOfSound_    { 25.0f }; ///< grid-units / sec (25 m/s)

    // Transport-side flags (audio-thread reads, message thread writes)
    std::atomic<bool>  audioPaused_    { false };
    std::atomic<bool>  killAllVoices_  { false };

    /// Compute the Doppler rate multiplier for a given source position
    /// and velocity, given the current listener position and speed of sound.
    float computeDopplerRate(float srcX, float srcY, float srcZ,
                             float vx,  float vy,  float vz) const noexcept;

    // ---- Internal helpers (called from audio thread) --------------------
    void dispatchEvent   (const SequencerEvent& ev);
    void handleStartEvent(const SequencerEvent& ev);
    void handleStopEvent (const SequencerEvent& ev);
};