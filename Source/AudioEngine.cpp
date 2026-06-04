#include "AudioEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

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

void AudioEngine::setSampleBuffer(int soundId, juce::AudioBuffer<float> buffer)
{
    sampleLibrary_[soundId] = std::move(buffer);
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

    // ---- 0a. Stop -> kill every voice instantly ------------------------
    if (killAllVoices_.exchange(false))
        activeVoices_.clear();

    // ---- 0b. Pause -> output silence and freeze voice positions --------
    // We still drain the event queue (so a queued Stop completes), but we
    // skip the per-voice advance entirely.  Voices resume from the exact
    // sample they were on when the user hits Play again.
    if (audioPaused_.load())
    {
        int s1, sz1, s2, sz2;
        fifo_.prepareToRead(fifo_.getNumReady(), s1, sz1, s2, sz2);
        for (int i = 0; i < sz1; ++i) dispatchEvent(queue_[s1 + i].ev);
        for (int i = 0; i < sz2; ++i) dispatchEvent(queue_[s2 + i].ev);
        fifo_.finishedRead(sz1 + sz2);
        return;
    }

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

    const float rate = playbackRate_.load();

    for (auto& voice : activeVoices_)
    {
        if (voice.buffer == nullptr) continue;

        const int totalSrc = voice.buffer->getNumSamples();
        auto* outL = outputBuffer->getWritePointer(0, startSample);
        auto* outR = (outputChannels > 1)
                     ? outputBuffer->getWritePointer(1, startSample)
                     : nullptr;

        const float step = voice.pitchRate * voice.blockRate * voice.dopplerRate * rate;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── Loop-buffer silence between repeats ──────────────────────
            if (voice.loopBuffer && voice.loopGapRemaining > 0.0f)
            {
                voice.loopGapRemaining -= 1.0f;
                continue;       // emit silence for this output sample
            }

            int srcIdx = static_cast<int>(voice.samplePositionF);
            if (srcIdx >= totalSrc)
            {
                if (!voice.loopBuffer)
                    break;

                if (voice.loopGapSamples > 0.0f)
                {
                    // Start the silence gap before the next loop iteration.
                    voice.loopGapRemaining = voice.loopGapSamples;
                    voice.samplePositionF  = 0.0f;
                    continue;
                }

                // Tight loop: cheap modulo on the float cursor, keep the
                // fractional part to avoid clicks.
                const float wrapTo = std::fmod(voice.samplePositionF,
                                               static_cast<float>(totalSrc));
                voice.samplePositionF = wrapTo;
                srcIdx = static_cast<int>(voice.samplePositionF);
                if (srcIdx >= totalSrc) break;   // pathological (empty buffer)
            }

            const float sample = voice.buffer->getSample(0, srcIdx);

            outL[i] += sample * voice.leftGain;
            if (outR) outR[i] += sample * voice.rightGain;

            voice.samplePositionF += step;
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
        for (auto& voice : activeVoices_)
        {
            if (voice.blockSerial == ev.blockSerial && !voice.isFinished())
            {
                applySpatialPosition(voice,
                                    ev.blockX,
                                    ev.blockY,
                                    ev.blockZ);

                voice.posX = ev.blockX;
                voice.posY = ev.blockY;
                voice.posZ = ev.blockZ;

                if (ev.hasVelocity)
                {
                    voice.velX = ev.velocityX;
                    voice.velY = ev.velocityY;
                    voice.velZ = ev.velocityZ;
                }

                voice.dopplerRate = computeDopplerRate(
                    voice.posX, voice.posY, voice.posZ,
                    voice.velX, voice.velY, voice.velZ
                );

                DBG("Updated spatial audio for block "
                    << ev.blockSerial
                    << " pos=(" << ev.blockX << ", "
                    << ev.blockY << ", "
                    << ev.blockZ << ")"
                    << ", doppler=" << voice.dopplerRate);
            }
        }
    }
}

// ---------------------------------------------------------------------------
float AudioEngine::computeDopplerRate(float srcX, float srcY, float srcZ,
                                      float vx,  float vy,  float vz) const noexcept
{
    if (!dopplerEnabled_.load())
        return 1.0f;

    const float lx = listenerX_.load();
    const float ly = listenerY_.load();
    const float lz = listenerZ_.load();

    const float dx = lx - srcX;
    const float dy = ly - srcY;
    const float dz = lz - srcZ;

    const float distSq = dx * dx + dy * dy + dz * dz;
    if (distSq < 1e-4f)
        return 1.0f;                       // listener sits on top of source

    const float invDist = 1.0f / std::sqrt(distSq);
    // Source velocity component along source→listener direction.
    // Positive  = source moving toward the listener  (pitch up)
    // Negative  = source moving away                  (pitch down)
    const float vRadial = (vx * dx + vy * dy + vz * dz) * invDist;

    const float c = speedOfSound_.load();
    // Clamp the denominator to keep the rate finite if v_r approaches c.
    const float denom = juce::jmax(c * 0.4f, c - vRadial);
    const float rate  = c / denom;
    return juce::jlimit(0.5f, 2.0f, rate);
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
    voice.loopBuffer      = ev.loopBuffer;
    voice.blockRate       = juce::jlimit(0.1f, 8.0f, ev.playbackRateOverride);
    voice.loopGapSamples  = std::max(0.0f,
                                     ev.loopBufferSec * static_cast<float>(sampleRate_));
    voice.loopGapRemaining = 0.0f;

    applySpatialPosition(voice, ev.blockX, ev.blockY, ev.blockZ);

    voice.posX = ev.blockX;
    voice.posY = ev.blockY;
    voice.posZ = ev.blockZ;
    voice.velX = ev.hasVelocity ? ev.velocityX : 0.0f;
    voice.velY = ev.hasVelocity ? ev.velocityY : 0.0f;
    voice.velZ = ev.hasVelocity ? ev.velocityZ : 0.0f;
    voice.dopplerRate = computeDopplerRate(voice.posX, voice.posY, voice.posZ,
                                           voice.velX, voice.velY, voice.velZ);

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

void AudioEngine::computeSpatialGainsStatic(float lx, float ly, float lz,
                                            float fxIn, float fyIn, float fzIn,
                                            float rxIn, float ryIn, float rzIn,
                                            float sensitivity,
                                            float x, float y, float z,
                                            float& outGain, float& outPan,
                                            float& outPitchRate,
                                            float& outLeft, float& outRight) noexcept
{
    const float relX = x - lx;
    const float relY = y - ly;
    const float relZ = z - lz;

    const float dist = std::sqrt(relX * relX + relY * relY + relZ * relZ);

    // 1 grid unit = 1 metre.  Higher sensitivity → smaller refDist → steeper falloff.
    const float refDist = 3.0f / juce::jmax(0.35f, sensitivity);
    outGain = (dist < 1e-3f)
            ? 1.0f
            : juce::jlimit(0.0f, 1.0f, refDist / (refDist + dist));

    // World Y → pitch (one semitone per grid unit).
    outPitchRate = juce::jlimit(0.25f, 4.0f, std::pow(2.0f, y / 12.0f));

    // Camera-relative horizontal pan: project onto listener forward / right.
    float fx = fxIn, fy = fyIn, fz = fzIn;
    float rx = rxIn, ry = ryIn, rz = rzIn;
    const float fwdLen = std::sqrt(fx * fx + fy * fy + fz * fz);
    const float rgtLen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (fwdLen > 1e-4f) { fx /= fwdLen; fy /= fwdLen; fz /= fwdLen; }
    if (rgtLen > 1e-4f) { rx /= rgtLen; ry /= rgtLen; rz /= rgtLen; }

    const float forward = relX * fx + relY * fy + relZ * fz;
    const float right   = relX * rx + relY * ry + relZ * rz;

    const float panAngle = std::atan2(right, forward + 1e-4f);
    outPan = juce::jlimit(-1.0f, 1.0f, std::sin(panAngle));

    // Stronger rear attenuation so "behind you" is obvious in headphones.
    const float frontNorm = (dist > 1e-3f) ? (forward / dist) : 1.0f;
    const float rearAtten = juce::jmap(juce::jlimit(-1.0f, 1.0f, frontNorm),
                                       -1.0f, 1.0f, 0.35f, 1.0f);

    const float eqPan = (outPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    outLeft  = outGain * rearAtten * std::cos(eqPan);
    outRight = outGain * rearAtten * std::sin(eqPan);
}

void AudioEngine::computeSpatialGains(float x, float y, float z,
                                      float& outGain, float& outPan,
                                      float& outPitchRate,
                                      float& outLeft, float& outRight) const noexcept
{
    computeSpatialGainsStatic(listenerX_.load(),  listenerY_.load(),  listenerZ_.load(),
                              listenerFwdX_.load(), listenerFwdY_.load(), listenerFwdZ_.load(),
                              listenerRightX_.load(), listenerRightY_.load(), listenerRightZ_.load(),
                              spatialSensitivity_.load(),
                              x, y, z,
                              outGain, outPan, outPitchRate, outLeft, outRight);
}

void AudioEngine::applySpatialPosition(ActiveVoice& voice, float x, float y, float z)
{
    computeSpatialGains(x, y, z,
                          voice.gain, voice.pan, voice.pitchRate,
                          voice.leftGain, voice.rightGain);
}

AudioEngine::SpatialReadout AudioEngine::measureSourceAt(float x, float y, float z) const noexcept
{
    SpatialReadout r;
    float pan = 0.f, pitch = 1.f, L = 0.f, R = 0.f;
    computeSpatialGains(x, y, z, r.gainLinear, pan, pitch, L, R);

    const float lx = listenerX_.load();
    const float ly = listenerY_.load();
    const float lz = listenerZ_.load();
    const float dx = x - lx, dy = y - ly, dz = z - lz;
    r.distanceMetres = std::sqrt(dx * dx + dy * dy + dz * dz);
    r.approxDb = (r.gainLinear > 1e-5f)
               ? 20.0f * std::log10(r.gainLinear)
               : -80.0f;
    return r;
}