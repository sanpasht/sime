#pragma once
// ---------------------------------------------------------------------------
// SoundLibrary.h — In-memory index over CSV/sound_library.csv.
//
// Responsibilities
// ----------------
//   * Parse CSV/sound_library.csv at startup (text only, no WAV decoding).
//   * Group entries by BlockType for instant filtering.
//   * Substring search across (note, duration, dynamic, articulation, key).
//   * Lazily register a sample with the AudioEngine the first time someone
//     asks for it (path -> int soundId).
//
// Threading
// ---------
// All public methods run on the message thread.  ensureLoaded() forwards a
// blocking AudioEngine::loadSample() call which is safe before audio starts
// or while audio runs (decoding briefly stalls the calling thread but never
// the audio callback — the audio thread only reads sampleLibrary_).
// ---------------------------------------------------------------------------

#include <JuceHeader.h>
#include "BlockType.h"

#include <unordered_map>
#include <vector>

class AudioEngine;

struct SoundEntry
{
    juce::String relativePath;     ///< "Instruments/violin/Violin_A3_…"
    juce::File   fullPath;          ///< Absolute path resolved against root
    BlockType    blockType = BlockType::Custom;

    juce::String instrument;
    juce::String note;
    juce::String duration;
    juce::String dynamic;
    juce::String articulation;
    juce::String key;
    juce::String bpm;

    juce::String displayName;       ///< pre-built primary line (e.g. "A3 Forte Arco")
    juce::String displaySub;        ///< secondary line (e.g. "0.25s · banjo")

    int soundId = -1;               ///< Assigned on first ensureLoaded()
};

class SoundLibrary
{
public:
    SoundLibrary() = default;

    /// Parse the CSV.  soundsRoot is the absolute path to the SIME/Sounds/ folder.
    bool load(const juce::File& csvFile, const juce::File& soundsRoot);

    bool               isLoaded() const noexcept       { return !entries_.empty(); }
    int                count() const noexcept          { return (int)entries_.size(); }
    const SoundEntry&  at(int idx) const               { return entries_[idx]; }

    /// Indices of all entries belonging to a given block type.
    const std::vector<int>& indicesFor(BlockType t) const;

    /// Substring search within a block type.  Empty query returns indicesFor(t).
    std::vector<int> search(BlockType t, const juce::String& query) const;

    /// Lookup by stored relative path (used when loading saved scenes).
    int findByRelativePath(const juce::String& relPath) const;

    /// Ensure the WAV is registered with the AudioEngine.  Returns the
    /// runtime soundId, or -1 if loading failed.
    int ensureLoaded(int entryIdx, AudioEngine& engine);

    int entryForSoundId(int soundId) const;

private:
    juce::File                                   root_;
    std::vector<SoundEntry>                      entries_;
    std::unordered_map<int, std::vector<int>>    byType_;     // BlockType (int) -> entry indices
    std::unordered_map<juce::String, int>        byPath_;     // relativePath -> entry idx
    std::unordered_map<int, int>                 byId_;       // runtime soundId -> entry idx

    int  nextSoundId_ = 10000;          // safely above synth IDs (100/200/300)
    static const std::vector<int>     kEmpty;

    void buildDisplayName(SoundEntry& e);
};
