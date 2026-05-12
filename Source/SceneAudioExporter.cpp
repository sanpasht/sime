#include "SceneAudioExporter.h"
#include "SequencerEngine.h"
#include "TransportClock.h"

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
        float  leftGain        = 1.0f;
        float  rightGain       = 1.0f;

        const juce::AudioBuffer<float>* buffer = nullptr;

        bool isFinished() const noexcept
        {
            return buffer == nullptr
                || static_cast<int>(samplePositionF) >= buffer->getNumSamples();
        }
    };

    void handleStartEvent(const SequencerEvent&                            ev,
                          std::vector<MixerVoice>&                         voices,
                          const std::unordered_map<int, juce::AudioBuffer<float>>& lib)
    {
        auto it = lib.find(ev.soundId);
        if (it == lib.end())
            return;

        MixerVoice voice;
        voice.blockSerial     = ev.blockSerial;
        voice.soundId         = ev.soundId;
        voice.samplePositionF = 0.0f;
        voice.buffer          = &it->second;

        constexpr float refDist = 5.0f;
        const float dist = std::abs(ev.blockZ);
        voice.gain = juce::jlimit(0.0f, 1.0f, refDist / (refDist + dist));

        voice.pitchRate = juce::jlimit(0.25f, 4.0f,
                                       std::pow(2.0f, ev.blockY / 12.0f));

        voice.pan = juce::jlimit(-1.0f, 1.0f, ev.blockX / 20.0f);
        const float angle = (voice.pan + 1.0f) * 0.25f
                              * juce::MathConstants<float>::pi;
        voice.leftGain  = voice.gain * std::cos(angle);
        voice.rightGain = voice.gain * std::sin(angle);

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

    void handleMovementEvent(const SequencerEvent& ev, std::vector<MixerVoice>& voices)
    {
        for (auto& voice : voices)
        {
            if (voice.blockSerial == ev.blockSerial && !voice.isFinished())
            {
                float pan = juce::jmap(ev.blockX, -20.0f, 20.0f, -1.0f, 1.0f);
                pan = juce::jlimit(-1.0f, 1.0f, pan);
                voice.pan = pan;

                const float leftGain  = (1.0f - pan) * 0.5f;
                const float rightGain = (1.0f + pan) * 0.5f;
                voice.leftGain  = voice.gain * leftGain;
                voice.rightGain = voice.gain * rightGain;
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
                        const std::unordered_map<int, juce::AudioBuffer<float>>& lib)
    {
        for (const auto& ev : events)
        {
            applyMovementToBlocks(ev, blocks);

            if (ev.type == SequencerEventType::Start)
                handleStartEvent(ev, voices, lib);
            else if (ev.type == SequencerEventType::Stop)
                handleStopEvent(ev, voices);
            else if (ev.type == SequencerEventType::Movement)
                handleMovementEvent(ev, voices);
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

            for (int i = 0; i < n; ++i)
            {
                const int srcIdx = static_cast<int>(voice.samplePositionF);
                if (srcIdx >= totalSrc)
                    break;

                const float sample = voice.buffer->getSample(0, srcIdx);
                outL[i] += sample * voice.leftGain;
                outR[i] += sample * voice.rightGain;
                voice.samplePositionF += voice.pitchRate;
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

        const auto events = sequencer.update(clock, blocks);
        dispatchEvents(events, blocks, voices, sampleLibrary);

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
