#pragma once

#include <JuceHeader.h>
#include "BlockEntry.h"

#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// SceneAudioExporter
//
// Offline bounce of the mixed stereo output for the full scene timeline
// (0 … max block end time), using the same SequencerEngine rules and the
// same per-voice gain / pan / pitch behaviour as AudioEngine.
// ---------------------------------------------------------------------------
namespace SceneAudioExporter
{
    enum class Format
    {
        Wav,   ///< PCM in WAV (lossless, 16-bit)
        Flac,  ///< FLAC (lossless, if JUCE_USE_FLAC)
        Aiff,  ///< PCM in AIFF (lossless, 16-bit)
        Ogg    ///< Ogg Vorbis (lossy, if JUCE_USE_OGGVORBIS)
    };

    [[nodiscard]] const char* formatFileSuffix(Format f) noexcept;
    [[nodiscard]] juce::String formatWildcard(Format f);
    [[nodiscard]] juce::String formatDescription(Format f);

    /// Renders the scene and writes `outputFile`. On failure returns false and
    /// sets `errorMessage`.
    bool bounceToFile(const std::vector<BlockEntry>&           blocks,
                      const std::unordered_map<int, juce::AudioBuffer<float>>& sampleLibrary,
                      double                                 outputSampleRate,
                      const juce::File&                      outputFile,
                      Format                                 format,
                      juce::String&                          errorMessage);
}
