#include "SceneAudioExporter.h"
#include "SequencerEngine.h"
#include "TransportClock.h"
#include "AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace SceneAudioExporter
{
namespace
{
    constexpr int    kOutChannels     = 2;
    constexpr int    kChunkSamples    = 64;
    constexpr double kMaxDurationSec  = 20.0 * 60.0;

    double computeSceneDuration(const std::vector<BlockEntry>& blocks)
    {
        double m = 0.0;
        for (const auto& b : blocks)
            m = std::max(m, b.endTimeSec());
        return m;
    }

    // Mirrors ActiveVoice + AudioEngine voice path (must stay in sync).
    struct MixerVoice
    {
        int    blockSerial     = -1;
        int    soundId         = -1;
        float  samplePositionF = 0.0f;
        bool   stopping        = false;
        float  gain            = 1.0f;
        float  pan             = 0.0f;
        float  pitchRate       = 1.0f;
        float  blockRate       = 1.0f;
        float  dopplerRate     = 1.0f;
        float  leftGain        = 1.0f;
        float  rightGain       = 1.0f;
        bool   loopBuffer      = false;
        float  loopGapSamples  = 0.0f;
        float  loopGapRemaining = 0.0f;

        float  posX = 0.f, posY = 0.f, posZ = 0.f;
        float  velX = 0.f, velY = 0.f, velZ = 0.f;

        const juce::AudioBuffer<float>* buffer = nullptr;

        bool isFinished() const noexcept
        {
            if (buffer == nullptr) return true;
            if (loopBuffer)        return false;
            return static_cast<int>(samplePositionF) >= buffer->getNumSamples();
        }
    };

    void applyListenerGainsAt(MixerVoice&         v,
                              const ListenerPose& L,
                              const Vec3f&        listenerPos,
                              const Vec3f&        listenerFwd,
                              const Vec3f&        listenerRight)
    {
        AudioEngine::computeSpatialGainsStatic(
            listenerPos.x,   listenerPos.y,   listenerPos.z,
            listenerFwd.x,   listenerFwd.y,   listenerFwd.z,
            listenerRight.x, listenerRight.y, listenerRight.z,
            L.sensitivity,
            v.posX, v.posY, v.posZ,
            v.gain, v.pan, v.pitchRate,
            v.leftGain, v.rightGain);
    }

    /// Resolve the listener pose at @p t — static fields when no path, else
    /// sampled from `cameraPath`.  Always returns normalised forward/right.
    void resolveListener(const ListenerPose& L, double t,
                         Vec3f& outPos, Vec3f& outFwd, Vec3f& outRight)
    {
        if (L.pathFollow && !L.cameraPath.empty())
        {
            CameraPose fallback;
            fallback.pos      = Vec3f{ L.posX, L.posY, L.posZ };
            fallback.yawRad   = std::atan2(L.fwdX, -L.fwdZ);
            fallback.pitchRad = std::asin(juce::jlimit(-1.0f, 1.0f, L.fwdY));

            const auto p = CameraPathUtil::sample(L.cameraPath, t, fallback);
            outPos = p.pos;
            CameraPathUtil::poseDirs(p, outFwd, outRight);
        }
        else
        {
            outPos   = Vec3f{ L.posX, L.posY, L.posZ };
            outFwd   = Vec3f{ L.fwdX, L.fwdY, L.fwdZ };
            outRight = Vec3f{ L.rightX, L.rightY, L.rightZ };
        }
    }

    void handleStartEvent(const SequencerEvent&                            ev,
                          std::vector<MixerVoice>&                         voices,
                          const std::unordered_map<int, juce::AudioBuffer<float>>& lib,
                          const ListenerPose&                              listener,
                          const Vec3f&                                     lpos,
                          const Vec3f&                                     lfwd,
                          const Vec3f&                                     lright,
                          double                                            writerSampleRate)
    {
        auto it = lib.find(ev.soundId);
        if (it == lib.end())
            return;

        MixerVoice voice;
        voice.blockSerial     = ev.blockSerial;
        voice.soundId         = ev.soundId;
        voice.samplePositionF = 0.0f;
        voice.buffer          = &it->second;
        voice.loopBuffer      = ev.loopBuffer;
        voice.blockRate       = juce::jlimit(0.1f, 8.0f, ev.playbackRateOverride);
        voice.loopGapSamples  = std::max(0.0f,
                                         ev.loopBufferSec * static_cast<float>(writerSampleRate));
        voice.loopGapRemaining = 0.0f;

        voice.posX = ev.blockX;
        voice.posY = ev.blockY;
        voice.posZ = ev.blockZ;
        voice.velX = ev.hasVelocity ? ev.velocityX : 0.0f;
        voice.velY = ev.hasVelocity ? ev.velocityY : 0.0f;
        voice.velZ = ev.hasVelocity ? ev.velocityZ : 0.0f;
        voice.dopplerRate = 1.0f;   // Doppler still excluded from offline render.

        applyListenerGainsAt(voice, listener, lpos, lfwd, lright);

        voices.push_back(voice);
    }

    void handleStopEvent(const SequencerEvent& ev, std::vector<MixerVoice>& voices)
    {
        for (auto& voice : voices)
        {
            if (voice.blockSerial == ev.blockSerial)
                voice.stopping = true;
        }
    }

    void handleMovementEvent(const SequencerEvent& ev,
                             std::vector<MixerVoice>& voices,
                             const ListenerPose&      listener,
                             const Vec3f&             lpos,
                             const Vec3f&             lfwd,
                             const Vec3f&             lright)
    {
        for (auto& voice : voices)
        {
            if (voice.blockSerial == ev.blockSerial && !voice.isFinished())
            {
                voice.posX = ev.blockX;
                voice.posY = ev.blockY;
                voice.posZ = ev.blockZ;
                if (ev.hasVelocity)
                {
                    voice.velX = ev.velocityX;
                    voice.velY = ev.velocityY;
                    voice.velZ = ev.velocityZ;
                }
                applyListenerGainsAt(voice, listener, lpos, lfwd, lright);
            }
        }
    }

    void applyMovementToBlocks(const SequencerEvent& ev, std::vector<BlockEntry>& blocks)
    {
        if (ev.type != SequencerEventType::Movement)
            return;

        for (auto& b : blocks)
        {
            if (b.serial == ev.blockSerial)
            {
                b.pos = { static_cast<int>(ev.blockX),
                          static_cast<int>(ev.blockY),
                          static_cast<int>(ev.blockZ) };
                break;
            }
        }
    }

    void dispatchEvents(const std::vector<SequencerEvent>& events,
                        std::vector<BlockEntry>&           blocks,
                        std::vector<MixerVoice>&           voices,
                        const std::unordered_map<int, juce::AudioBuffer<float>>& lib,
                        const ListenerPose&                 listener,
                        const Vec3f&                        lpos,
                        const Vec3f&                        lfwd,
                        const Vec3f&                        lright,
                        double                              writerSampleRate)
    {
        for (const auto& ev : events)
        {
            applyMovementToBlocks(ev, blocks);

            if (ev.type == SequencerEventType::Start)
                handleStartEvent(ev, voices, lib, listener,
                                 lpos, lfwd, lright, writerSampleRate);
            else if (ev.type == SequencerEventType::Stop)
                handleStopEvent(ev, voices);
            else if (ev.type == SequencerEventType::Movement)
                handleMovementEvent(ev, voices, listener, lpos, lfwd, lright);
        }
    }

    void mixChunk(juce::AudioBuffer<float>& chunk, std::vector<MixerVoice>& voices)
    {
        chunk.clear();
        const int n = chunk.getNumSamples();
        auto* outL = chunk.getWritePointer(0);
        auto* outR = chunk.getWritePointer(1);

        for (auto& voice : voices)
        {
            if (voice.buffer == nullptr)
                continue;

            const int totalSrc = voice.buffer->getNumSamples();

            const float step = voice.pitchRate * voice.blockRate * voice.dopplerRate;

            for (int i = 0; i < n; ++i)
            {
                if (voice.loopBuffer && voice.loopGapRemaining > 0.0f)
                {
                    voice.loopGapRemaining -= 1.0f;
                    continue;
                }

                int srcIdx = static_cast<int>(voice.samplePositionF);
                if (srcIdx >= totalSrc)
                {
                    if (!voice.loopBuffer)
                        break;

                    if (voice.loopGapSamples > 0.0f)
                    {
                        voice.loopGapRemaining = voice.loopGapSamples;
                        voice.samplePositionF  = 0.0f;
                        continue;
                    }

                    voice.samplePositionF = std::fmod(voice.samplePositionF,
                                                     static_cast<float>(totalSrc));
                    srcIdx = static_cast<int>(voice.samplePositionF);
                    if (srcIdx >= totalSrc) break;
                }

                const float sample = voice.buffer->getSample(0, srcIdx);
                outL[i] += sample * voice.leftGain;
                outR[i] += sample * voice.rightGain;
                voice.samplePositionF += step;
            }
        }

        voices.erase(
            std::remove_if(voices.begin(), voices.end(),
                           [](const MixerVoice& v) { return v.isFinished() || v.stopping; }),
            voices.end());
    }

    juce::AudioFormat* pickFormat(juce::AudioFormatManager& mgr, Format f)
    {
        switch (f)
        {
            case Format::Wav:  return mgr.findFormatForFileExtension("wav");
            case Format::Flac: return mgr.findFormatForFileExtension("flac");
            case Format::Aiff: return mgr.findFormatForFileExtension("aif");
            case Format::Ogg:  return mgr.findFormatForFileExtension("ogg");
            default:           return nullptr;
        }
    }

    double pickWriterSampleRate(juce::AudioFormat* af, double preferred)
    {
        auto rates = af->getPossibleSampleRates();
        if (rates.isEmpty())
            return preferred;

        for (int r : rates)
            if (std::abs(static_cast<double>(r) - preferred) < 0.5)
                return static_cast<double>(r);

        double best = static_cast<double>(rates.getFirst());
        double bestErr = 1.0e30;
        for (int r : rates)
        {
            const double rd = static_cast<double>(r);
            const double err = std::abs(rd - preferred);
            if (err < bestErr)
            {
                bestErr = err;
                best = rd;
            }
        }
        return best;
    }

    juce::AudioFormatWriterOptions writerOptionsFor(Format f, double sampleRate)
    {
        auto o = juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withNumChannels(kOutChannels);

        if (f == Format::Ogg)
            return o.withQualityOptionIndex(4);

        return o.withBitsPerSample(16);
    }
} // namespace

const char* formatFileSuffix(Format f) noexcept
{
    switch (f)
    {
        case Format::Wav:  return ".wav";
        case Format::Flac: return ".flac";
        case Format::Aiff: return ".aif";
        case Format::Ogg:  return ".ogg";
        default:           return ".wav";
    }
}

juce::String formatWildcard(Format f)
{
    switch (f)
    {
        case Format::Wav:  return "*.wav";
        case Format::Flac: return "*.flac";
        case Format::Aiff: return "*.aiff;*.aif";
        case Format::Ogg:  return "*.ogg";
        default:           return "*.wav";
    }
}

juce::String formatDescription(Format f)
{
    switch (f)
    {
        case Format::Wav:  return "WAV (PCM, lossless)";
        case Format::Flac: return "FLAC (lossless)";
        case Format::Aiff: return "AIFF (PCM, lossless)";
        case Format::Ogg:  return "Ogg Vorbis (lossy)";
        default:           return "WAV";
    }
}

bool bounceToFile(const std::vector<BlockEntry>&                            blocksIn,
                  const std::unordered_map<int, juce::AudioBuffer<float>>& sampleLibrary,
                  double                                                   outputSampleRate,
                  const ListenerPose&                                      listener,
                  const juce::File&                                        outputFile,
                  Format                                                   format,
                  juce::String&                                            errorMessage)
{
    errorMessage.clear();

    if (outputSampleRate < 8000.0 || outputSampleRate > 384000.0)
    {
        errorMessage = "Invalid sample rate for export.";
        return false;
    }

    std::vector<BlockEntry> blocks = blocksIn;
    for (auto& b : blocks)
        b.resetPlaybackState();

    const double durationSec = computeSceneDuration(blocks);
    if (durationSec <= 0.0)
    {
        errorMessage = "Nothing to export: the scene has zero length on the timeline.";
        return false;
    }

    if (durationSec > kMaxDurationSec)
    {
        errorMessage = "Scene is too long to export (limit is 20 minutes).";
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    juce::AudioFormat* af = pickFormat(formatManager, format);
    if (af == nullptr)
    {
        errorMessage = "This build does not include the selected export format.";
        return false;
    }

    const double rate = pickWriterSampleRate(af, outputSampleRate);
    const auto totalSamples = static_cast<int>(std::ceil(durationSec * rate));
    if (totalSamples <= 0)
    {
        errorMessage = "Computed export length is invalid.";
        return false;
    }

    juce::AudioBuffer<float> rendered(kOutChannels, totalSamples);
    rendered.clear();

    TransportClock clock;
    clock.stop();
    clock.setLooping(false, 0.0);
    clock.seekTo(0.0);
    clock.start();

    // Phase 1: cache each block's sample length so Stretch / Speed modes
    // compute the correct rate during offline rendering (mirrors the live
    // GL render path).  `rate` here is the writer sample rate.
    for (auto& b : blocks)
    {
        auto itLib = sampleLibrary.find(b.soundId);
        b.sampleNaturalDurationSec = (itLib != sampleLibrary.end() && rate > 0.0)
            ? (itLib->second.getNumSamples() / rate)
            : 0.0;
    }

    // Start every block (and its scheduled sounds) from a clean slate so the
    // offline render fires the same events the live transport would from t=0.
    SequencerEngine::resetAllBlocks(blocks);

    SequencerEngine sequencer;
    std::vector<MixerVoice> voices;
    voices.reserve(64);

    juce::AudioBuffer<float> chunk(kOutChannels, kChunkSamples);
    int written = 0;

    while (written < totalSamples)
    {
        const int thisBlock = std::min(kChunkSamples, totalSamples - written);
        if (thisBlock <= 0)
            break;

        const double dt = static_cast<double>(thisBlock) / rate;
        clock.update(dt);

        // Exports honour the per-block isMuted flag but ignore the per-type
        // toolbar filter — those are view conveniences, not musical intent.
        for (auto& bk : blocks)
            bk.effectiveMuted = bk.isMuted;

        const double chunkTime = clock.currentTimeSec();
        Vec3f lpos, lfwd, lright;
        resolveListener(listener, chunkTime, lpos, lfwd, lright);

        // Re-mix any still-playing voices for the new listener pose so a
        // sustained note slides through pan / gain as the camera moves.
        if (listener.pathFollow && !listener.cameraPath.empty())
        {
            for (auto& v : voices)
                if (!v.isFinished() && !v.stopping)
                    applyListenerGainsAt(v, listener, lpos, lfwd, lright);
        }

        const auto events = sequencer.update(clock, blocks);
        dispatchEvents(events, blocks, voices, sampleLibrary,
                       listener, lpos, lfwd, lright, rate);

        if (thisBlock == kChunkSamples)
            mixChunk(chunk, voices);
        else
        {
            chunk.setSize(kOutChannels, thisBlock, false, false, true);
            mixChunk(chunk, voices);
        }

        for (int ch = 0; ch < kOutChannels; ++ch)
            rendered.copyFrom(ch, written, chunk, ch, 0, thisBlock);

        written += thisBlock;
    }

    std::unique_ptr<juce::FileOutputStream> fileOut(
        std::make_unique<juce::FileOutputStream>(outputFile));

    if (! fileOut->openedOk())
    {
        errorMessage = "Could not open the output file for writing.";
        return false;
    }

    std::unique_ptr<juce::OutputStream> out(std::move(fileOut));
    auto opts = writerOptionsFor(format, rate);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        af->createWriterFor(out, opts));

    if (writer == nullptr)
    {
        errorMessage = "Could not create an audio writer for this format and sample rate.";
        return false;
    }

    if (! writer->writeFromAudioSampleBuffer(rendered, 0, totalSamples))
    {
        errorMessage = "Writing the audio file failed.";
        return false;
    }

    writer.reset();
    return true;
}

} // namespace SceneAudioExporter
