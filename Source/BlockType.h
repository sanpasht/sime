#pragma once
// ---------------------------------------------------------------------------
// BlockType.h — Block category enum and associated helpers.
//
// Deliberately free of MathUtils.h dependencies so it can be included early
// in the translation-unit chain.  Pulls in juce::Colour for color helpers.
// ---------------------------------------------------------------------------

#include <JuceHeader.h>

enum class BlockType
{
    // ── Synth / special (existing) ───────────────────────────────────────
    Violin = 0,
    Piano,
    Drum,
    Custom,
    Listener,

    // ── Strings ──────────────────────────────────────────────────────────
    Viola,
    Cello,
    DoubleBass,
    Banjo,
    Mandolin,
    Guitar,
    ElectricGuitar,

    // ── Woodwinds ────────────────────────────────────────────────────────
    Flute,
    Oboe,
    Clarinet,
    BassClarinet,
    Bassoon,
    Contrabassoon,
    CorAnglais,

    // ── Brass ────────────────────────────────────────────────────────────
    FrenchHorn,
    Trumpet,
    Trombone,
    Tuba,
    Saxophone,

    // ── Percussion ───────────────────────────────────────────────────────
    Percussion,

    // sentinel — keep last
    _Count
};

inline const char* blockTypeName(BlockType t)
{
    switch (t)
    {
        case BlockType::Violin:        return "Violin";
        case BlockType::Piano:         return "Piano";
        case BlockType::Drum:          return "Drum";
        case BlockType::Custom:        return "Custom";
        case BlockType::Listener:      return "Listener";
        case BlockType::Viola:         return "Viola";
        case BlockType::Cello:         return "Cello";
        case BlockType::DoubleBass:    return "DoubleBass";
        case BlockType::Banjo:         return "Banjo";
        case BlockType::Mandolin:      return "Mandolin";
        case BlockType::Guitar:        return "Guitar";
        case BlockType::ElectricGuitar:return "ElectricGuitar";
        case BlockType::Flute:         return "Flute";
        case BlockType::Oboe:          return "Oboe";
        case BlockType::Clarinet:      return "Clarinet";
        case BlockType::BassClarinet:  return "BassClarinet";
        case BlockType::Bassoon:       return "Bassoon";
        case BlockType::Contrabassoon: return "Contrabassoon";
        case BlockType::CorAnglais:    return "CorAnglais";
        case BlockType::FrenchHorn:    return "FrenchHorn";
        case BlockType::Trumpet:       return "Trumpet";
        case BlockType::Trombone:      return "Trombone";
        case BlockType::Tuba:          return "Tuba";
        case BlockType::Saxophone:     return "Saxophone";
        case BlockType::Percussion:    return "Percussion";
        case BlockType::_Count:        return "?";
    }
    return "Unknown";
}

/// Friendlier user-facing display name (some types collapse compound words).
inline const char* blockTypeDisplayName(BlockType t)
{
    switch (t)
    {
        case BlockType::DoubleBass:    return "Double Bass";
        case BlockType::ElectricGuitar:return "Electric Guitar";
        case BlockType::BassClarinet:  return "Bass Clarinet";
        case BlockType::CorAnglais:    return "Cor Anglais";
        case BlockType::FrenchHorn:    return "French Horn";
        default:                       return blockTypeName(t);
    }
}

/// Convert a string ("Violin", "Banjo", …) back into the enum value.
/// Used by SoundLibrary when it parses the block_type column from the CSV.
inline BlockType blockTypeFromName(const juce::String& s)
{
    for (int i = 0; i < (int)BlockType::_Count; ++i)
    {
        auto bt = static_cast<BlockType>(i);
        if (s.equalsIgnoreCase(blockTypeName(bt)))
            return bt;
    }
    return BlockType::Custom;
}

/// First (default) soundId for each instrument type — used by synth fallbacks.
/// Library-backed types start with -1 (unassigned) until the user picks one.
inline int blockTypeDefaultSoundId(BlockType t)
{
    switch (t)
    {
        case BlockType::Violin:   return 100;   // synth violin (legacy fallback)
        case BlockType::Piano:    return 200;   // synth piano
        case BlockType::Drum:     return 300;   // synth drum kick
        default:                  return -1;
    }
}

/// Distinct color per block type (for rendering + UI highlights).
inline juce::Colour blockTypeColor(BlockType t)
{
    switch (t)
    {
        case BlockType::Violin:        return juce::Colour(0xffd9261c);  // crimson
        case BlockType::Piano:         return juce::Colour(0xff3366cc);  // blue
        case BlockType::Drum:          return juce::Colour(0xff2eaa44);  // green
        case BlockType::Custom:        return juce::Colour(0xffe0e0e8);  // light gray
        case BlockType::Listener:      return juce::Colour(0xffdd7010);  // orange

        case BlockType::Viola:         return juce::Colour(0xffaa2818);  // dark red
        case BlockType::Cello:         return juce::Colour(0xff8b4520);  // burnt orange
        case BlockType::DoubleBass:    return juce::Colour(0xff5a2a14);  // dark brown
        case BlockType::Banjo:         return juce::Colour(0xffcc8b2e);  // tan
        case BlockType::Mandolin:      return juce::Colour(0xff8a972e);  // olive
        case BlockType::Guitar:        return juce::Colour(0xffd4af37);  // gold
        case BlockType::ElectricGuitar:return juce::Colour(0xff2a78d4);  // electric blue

        case BlockType::Flute:         return juce::Colour(0xffc9d4ea);  // pale silver
        case BlockType::Oboe:          return juce::Colour(0xffc78f1e);  // amber
        case BlockType::Clarinet:      return juce::Colour(0xff5e2a14);  // dark wood
        case BlockType::BassClarinet:  return juce::Colour(0xff3d1c0f);  // very dark wood
        case BlockType::Bassoon:       return juce::Colour(0xff8e6e1e);  // ochre
        case BlockType::Contrabassoon: return juce::Colour(0xff5b4818);  // dark ochre
        case BlockType::CorAnglais:    return juce::Colour(0xffa0522d);  // rust

        case BlockType::FrenchHorn:    return juce::Colour(0xff8a4f2a);  // bronze
        case BlockType::Trumpet:       return juce::Colour(0xfff3c027);  // bright gold
        case BlockType::Trombone:      return juce::Colour(0xffb86a1a);  // copper
        case BlockType::Tuba:          return juce::Colour(0xff6b4f1a);  // dark gold
        case BlockType::Saxophone:     return juce::Colour(0xffd9a533);  // brass

        case BlockType::Percussion:    return juce::Colour(0xff1f7042);  // forest green
        case BlockType::_Count:        return juce::Colour(0xff888899);
    }
    return juce::Colour(0xff888899);
}

/// Logical UI grouping for the toolbar dropdown menu.
enum class BlockCategory { Synth, Strings, Woodwinds, Brass, Percussion, Special };

inline BlockCategory blockTypeCategory(BlockType t)
{
    switch (t)
    {
        case BlockType::Piano:         return BlockCategory::Synth;

        case BlockType::Violin:
        case BlockType::Viola:
        case BlockType::Cello:
        case BlockType::DoubleBass:
        case BlockType::Banjo:
        case BlockType::Mandolin:
        case BlockType::Guitar:
        case BlockType::ElectricGuitar:
            return BlockCategory::Strings;

        case BlockType::Flute:
        case BlockType::Oboe:
        case BlockType::Clarinet:
        case BlockType::BassClarinet:
        case BlockType::Bassoon:
        case BlockType::Contrabassoon:
        case BlockType::CorAnglais:
        case BlockType::Saxophone:
            return BlockCategory::Woodwinds;

        case BlockType::FrenchHorn:
        case BlockType::Trumpet:
        case BlockType::Trombone:
        case BlockType::Tuba:
            return BlockCategory::Brass;

        case BlockType::Drum:
        case BlockType::Percussion:
            return BlockCategory::Percussion;

        case BlockType::Custom:
        case BlockType::Listener:
        case BlockType::_Count:
            return BlockCategory::Special;
    }
    return BlockCategory::Special;
}

inline const char* blockCategoryName(BlockCategory c)
{
    switch (c)
    {
        case BlockCategory::Synth:      return "Synth";
        case BlockCategory::Strings:    return "Strings";
        case BlockCategory::Woodwinds:  return "Woodwinds";
        case BlockCategory::Brass:      return "Brass";
        case BlockCategory::Percussion: return "Percussion";
        case BlockCategory::Special:    return "Special";
    }
    return "?";
}
