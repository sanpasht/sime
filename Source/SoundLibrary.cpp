// ---------------------------------------------------------------------------
// SoundLibrary.cpp
// ---------------------------------------------------------------------------

#include "SoundLibrary.h"
#include "AudioEngine.h"

const std::vector<int> SoundLibrary::kEmpty {};

namespace
{
    /// Split a CSV line that may contain quoted fields containing commas.
    static juce::StringArray parseCsvLine(const juce::String& line)
    {
        juce::StringArray out;
        juce::String cur;
        bool inQuotes = false;
        for (auto c : line)
        {
            if (c == '"')                         inQuotes = !inQuotes;
            else if (c == ',' && !inQuotes)     { out.add(cur); cur.clear(); }
            else                                  cur += c;
        }
        out.add(cur);
        return out;
    }
}

// ---------------------------------------------------------------------------

bool SoundLibrary::load(const juce::File& csvFile, const juce::File& soundsRoot)
{
    root_ = soundsRoot;

    juce::FileInputStream stream(csvFile);
    if (!stream.openedOk())
        return false;

    juce::String header = stream.readNextLine();
    auto colNames = parseCsvLine(header);

    auto indexOf = [&](const char* name) -> int
    {
        for (int i = 0; i < colNames.size(); ++i)
            if (colNames[i].equalsIgnoreCase(name))
                return i;
        return -1;
    };

    const int cNew  = indexOf("new_path");
    const int cType = indexOf("block_type");
    const int cInst = indexOf("instrument");
    const int cNote = indexOf("note");
    const int cDur  = indexOf("duration");
    const int cDyn  = indexOf("dynamic");
    const int cArt  = indexOf("articulation");
    const int cKey  = indexOf("key");
    const int cBpm  = indexOf("bpm");

    if (cNew < 0 || cType < 0)
        return false;

    entries_.clear();
    entries_.reserve(14000);
    byType_.clear();
    byPath_.clear();

    while (!stream.isExhausted())
    {
        juce::String line = stream.readNextLine();
        if (line.isEmpty()) continue;
        auto cells = parseCsvLine(line);
        if (cells.size() <= cNew) continue;

        SoundEntry e;
        e.relativePath = cells[cNew].trim();
        e.fullPath     = root_.getChildFile(e.relativePath);
        e.blockType    = blockTypeFromName(cells[cType].trim());

        if (cInst >= 0 && cInst < cells.size()) e.instrument   = cells[cInst].trim();
        if (cNote >= 0 && cNote < cells.size()) e.note         = cells[cNote].trim();
        if (cDur  >= 0 && cDur  < cells.size()) e.duration     = cells[cDur ].trim();
        if (cDyn  >= 0 && cDyn  < cells.size()) e.dynamic      = cells[cDyn ].trim();
        if (cArt  >= 0 && cArt  < cells.size()) e.articulation = cells[cArt ].trim();
        if (cKey  >= 0 && cKey  < cells.size()) e.key          = cells[cKey ].trim();
        if (cBpm  >= 0 && cBpm  < cells.size()) e.bpm          = cells[cBpm ].trim();

        buildDisplayName(e);

        int idx = (int)entries_.size();
        entries_.push_back(std::move(e));

        byType_[(int)entries_.back().blockType].push_back(idx);
        byPath_[entries_.back().relativePath] = idx;
    }

    return !entries_.empty();
}

void SoundLibrary::buildDisplayName(SoundEntry& e)
{
    // Primary line: NOTE + DYNAMIC + ARTICULATION
    juce::StringArray primary;
    if (e.note.isNotEmpty())          primary.add(e.note.toUpperCase());
    if (e.dynamic.isNotEmpty())       primary.add(e.dynamic);
    if (e.articulation.isNotEmpty())  primary.add(e.articulation);

    e.displayName = primary.isEmpty()
        ? e.fullPath.getFileNameWithoutExtension()
        : primary.joinIntoString("  ");

    // Secondary line: duration · key · bpm · instrument
    juce::StringArray secondary;
    if (e.duration   .isNotEmpty()) secondary.add(e.duration);
    if (e.key        .isNotEmpty()) secondary.add(e.key);
    if (e.bpm        .isNotEmpty()) secondary.add(e.bpm + " bpm");
    if (e.instrument .isNotEmpty()) secondary.add(e.instrument);

    e.displaySub = secondary.joinIntoString("  ·  ");
}

const std::vector<int>& SoundLibrary::indicesFor(BlockType t) const
{
    auto it = byType_.find((int)t);
    return (it != byType_.end()) ? it->second : kEmpty;
}

std::vector<int> SoundLibrary::search(BlockType t, const juce::String& query) const
{
    const auto& src = indicesFor(t);
    if (query.isEmpty())
        return src;

    std::vector<int> out;
    out.reserve(src.size() / 4 + 8);

    for (int idx : src)
    {
        const auto& e = entries_[idx];
        if (e.note         .containsIgnoreCase(query) ||
            e.duration     .containsIgnoreCase(query) ||
            e.dynamic      .containsIgnoreCase(query) ||
            e.articulation .containsIgnoreCase(query) ||
            e.key          .containsIgnoreCase(query) ||
            e.relativePath .containsIgnoreCase(query) ||
            e.displayName  .containsIgnoreCase(query) ||
            e.displaySub   .containsIgnoreCase(query))
        {
            out.push_back(idx);
        }
    }
    return out;
}

int SoundLibrary::findByRelativePath(const juce::String& relPath) const
{
    auto it = byPath_.find(relPath);
    return (it != byPath_.end()) ? it->second : -1;
}

int SoundLibrary::ensureLoaded(int entryIdx, AudioEngine& engine)
{
    if (entryIdx < 0 || entryIdx >= (int)entries_.size())
        return -1;

    auto& e = entries_[entryIdx];
    if (e.soundId >= 0 && engine.hasSample(e.soundId))
        return e.soundId;

    if (e.soundId < 0)
        e.soundId = nextSoundId_++;

    if (!engine.hasSample(e.soundId))
    {
        if (!engine.loadSample(e.soundId, e.fullPath))
            return -1;
        byId_[e.soundId] = entryIdx;
    }
    return e.soundId;
}

int SoundLibrary::entryForSoundId(int soundId) const
{
    auto it = byId_.find(soundId);
    return (it != byId_.end()) ? it->second : -1;
}
