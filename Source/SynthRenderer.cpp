#include "SynthRenderer.h"
#include <cmath>

namespace
{
    float oscSample(SynthPatch::Waveform wf, float phase) noexcept
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        while (phase >= twoPi) phase -= twoPi;
        while (phase < 0.0f)   phase += twoPi;

        switch (wf)
        {
            case SynthPatch::Waveform::Sine:
                return std::sin(phase);
            case SynthPatch::Waveform::Square:
                return phase < juce::MathConstants<float>::pi ? 1.0f : -1.0f;
            case SynthPatch::Waveform::Saw:
                return (phase / juce::MathConstants<float>::pi) - 1.0f;
            case SynthPatch::Waveform::Triangle:
            default:
            {
                const float t = phase / twoPi;
                return 4.0f * std::abs(t - 0.5f) - 1.0f;
            }
        }
    }

    struct LowPassFilter
    {
        float z1 = 0.0f, z2 = 0.0f;

        float process(float in, float cutoffHz, float resonance, float sr) noexcept
        {
            cutoffHz = juce::jlimit(20.0f, sr * 0.45f, cutoffHz);
            const float q = juce::jmap(resonance, 0.0f, 1.0f, 0.707f, 8.0f);
            const float g = std::tan(juce::MathConstants<float>::pi * cutoffHz / sr);
            const float k = 1.0f / q;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            const float v3 = in - z2;
            const float v1 = a1 * z1 + a2 * v3;
            const float v2 = z2 + a2 * z1 + a3 * v3;
            z1 = 2.0f * v1 - z1;
            z2 = 2.0f * v2 - z2;
            return v2;
        }
    };

    enum class EnvStage { Attack, Decay, Sustain, Release, Done };

    float advanceEnvelope(EnvStage& stage, float& level,
                          float attack, float decay, float sustain, float release,
                          float dt, bool noteHeld) noexcept
    {
        switch (stage)
        {
            case EnvStage::Attack:
                if (attack <= 1.0e-6f)
                {
                    level = 1.0f;
                    stage = EnvStage::Decay;
                }
                else
                {
                    level += dt / attack;
                    if (level >= 1.0f)
                    {
                        level = 1.0f;
                        stage = EnvStage::Decay;
                    }
                }
                break;

            case EnvStage::Decay:
                if (decay <= 1.0e-6f)
                {
                    level = sustain;
                    stage = EnvStage::Sustain;
                }
                else
                {
                    level -= dt / decay * (1.0f - sustain);
                    if (level <= sustain + 1.0e-5f)
                    {
                        level = sustain;
                        stage = EnvStage::Sustain;
                    }
                }
                break;

            case EnvStage::Sustain:
                level = sustain;
                if (!noteHeld)
                    stage = EnvStage::Release;
                break;

            case EnvStage::Release:
                if (release <= 1.0e-6f)
                {
                    level = 0.0f;
                    stage = EnvStage::Done;
                }
                else
                {
                    level -= dt / release * level;
                    if (level <= 1.0e-5f)
                    {
                        level = 0.0f;
                        stage = EnvStage::Done;
                    }
                }
                break;

            case EnvStage::Done:
                level = 0.0f;
                break;
        }
        return level;
    }
}

juce::AudioBuffer<float> SynthRenderer::render(const SynthPatch& patch, double sampleRate)
{
    const float sr = static_cast<float>(sampleRate);
    const float freqHz = SynthPatch::midiToHz(patch.midiNote);
    const float phaseInc = juce::MathConstants<float>::twoPi * freqHz / sr;

    const float attack  = juce::jmax(0.001f, patch.attackSec);
    const float decay   = juce::jmax(0.001f, patch.decaySec);
    const float sustain = juce::jlimit(0.0f, 1.0f, patch.sustainLevel);
    const float release = juce::jmax(0.001f, patch.releaseSec);

    const int holdSamples    = static_cast<int>(std::ceil(patch.durationSec * sampleRate));
    const int releaseSamples = static_cast<int>(std::ceil(release * sampleRate));
    const int totalSamples   = holdSamples + releaseSamples + 1;

    juce::AudioBuffer<float> buf(1, totalSamples);
    buf.clear();
    float* out = buf.getWritePointer(0);

    LowPassFilter filter;
    float phase = 0.0f;
    EnvStage stage = EnvStage::Attack;
    float envLevel = 0.0f;

    for (int i = 0; i < totalSamples; ++i)
    {
        const bool noteHeld = i < holdSamples;
        const float env = advanceEnvelope(stage, envLevel, attack, decay, sustain, release,
                                          1.0f / sr, noteHeld);

        const float raw = oscSample(patch.waveform, phase) * env * patch.masterGain;
        const float filtered = filter.process(raw,
                                              patch.filterCutoffHz,
                                              patch.filterResonance,
                                              sr);
        out[i] = juce::jlimit(-1.0f, 1.0f, filtered);

        phase += phaseInc;
        if (stage == EnvStage::Done && envLevel <= 0.0f)
        {
            buf.setSize(1, i + 1, true, true, true);
            break;
        }
    }

    const int n = buf.getNumSamples();
    const int fade = juce::jmin(128, n / 4);
    for (int i = 0; i < fade; ++i)
    {
        const float g = static_cast<float>(i) / static_cast<float>(fade);
        out[n - fade + i] *= g;
    }

    return buf;
}

bool SynthRenderer::writeWav(const juce::AudioBuffer<float>& buffer,
                             const juce::File& outputFile,
                             double sampleRate)
{
    if (buffer.getNumSamples() <= 0)
        return false;

    outputFile.getParentDirectory().createDirectory();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(),
                            sampleRate,
                            static_cast<unsigned int>(juce::jmax(1, buffer.getNumChannels())),
                            16,
                            {},
                            0));

    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
