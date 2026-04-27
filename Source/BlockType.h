#pragma once
// ---------------------------------------------------------------------------
// BlockType.h — Block category enum and associated helpers.
//
// Deliberately free of MathUtils.h / JUCE dependencies so it can be
// included early in the translation-unit include chain.
// ---------------------------------------------------------------------------

enum class BlockType
{
    Violin = 0,
    Piano,
    Drum,
    Custom
};

inline const char* blockTypeName(BlockType t)
{
    switch (t)
    {
        case BlockType::Violin: return "Violin";
        case BlockType::Piano:  return "Piano";
        case BlockType::Drum:   return "Drum";
        case BlockType::Custom: return "Custom";
    }
    return "Unknown";
}

/// First (default) soundId for each instrument type.
/// Custom blocks start with -1 (unassigned) until a WAV is picked.
inline int blockTypeDefaultSoundId(BlockType t)
{
    switch (t)
    {
        case BlockType::Violin: return 100;
        case BlockType::Piano:  return 200;
        case BlockType::Drum:   return 300;
        case BlockType::Custom: return -1;
    }
    return -1;
}
