#pragma once
// ---------------------------------------------------------------------------
// SoundLibrary.h — In-memory index over CSV/sound_library.csv.
//
// Responsibilities
// ----------------
//   * Parse CSV/sound_library.csv at startup (no WAV decoding).
//   * Group entries by BlockType for instant filtering.
//   * Provide substring search across (note, duration, dynamic, articulation,
//     key, articulation, filename).
//   * Lazily register a sample with the AudioEngine the first time someone
//     asks for it (path -> int soundId).
//
// Threading
// ---------
// All public methods run on the message thread.  ensureLoaded() forwards a
// blocking AudioEngine::loadSample() call which is safe before audio starts
// or, for a prototype, while audio runs (decoding briefly stalls the calling
// thread but never the audio callback).
// ---------------------------------------------------------------------------

#include <JuceHeader.h>
#include "BlockType.h"

#include <unordered_map>
#include <vector>

class AudioEngine;

struct SoundEntry
{
    juce::String relativePath;     ///< "Instruments/violin/Violin_A3_..."
    juce::File   fullPath;          ///< Absolute path resolved against root
    BlockType    blockType = BlockType::Custom;

    // Parsed metadata (any may be empty)
    juce::String instrument;
    juce::String note;
    juce::String duration;
    juce::String dynamic;
    juce::String articulation;
    juce::String key;
    juce::String bpm;

    juce::String displayName;       ///< Pre-built "A3  /  Forte  /  Arco  /  0.25s"

    int soundId = -1;               ///< Assigned on first ensureLoaded()
};

class SoundLibrary
{
public:
    SoundLibrary() = default;

    /// Parse the CSV.  soundsRoot is the absolute path to the SIME/Sounds/ folder.
    bool load(const juce::File& csvFile, const juce::File& soundsRoot);

    int                count() const                  { return (int)entries_.size(); }
    const SoundEntry&  at(int idx) const              { return entries_[idx]; }

    /// Indices of all entries belonging to a given block type (already filtered).
    const std::vector<int>& indicesFor(BlockType t) const;

    /// Substring search within a block type.  Empty query returns indicesFor(t).
    std::vector<int> search(BlockType t, const juce::String& query) const;

    /// Lookup by stored relative path (used when loading saved scenes).
    int findByRelativePath(const juce::String& relPath) const;

    /// Ensure the WAV is registered with the AudioEngine.  Returns the
    /// runtime soundId, or -1 if loading failed.
    int ensureLoaded(int entryIdx, AudioEngine& engine);

    /// Return entryIdx for a given soundId (after ensureLoaded was called).
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
