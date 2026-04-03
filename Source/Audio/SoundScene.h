#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SoundScene.h  –  Owns all SoundBlock objects. Editor-side state.
//
// Message thread only. Generates SceneSnapshots for the audio thread.
// ─────────────────────────────────────────────────────────────────────────────

#include "SoundBlock.h"
#include "SceneSnapshot.h"
#include "SampleLibrary.h"
#include <vector>
#include <cstdint>

class SoundScene
{
public:
    uint64_t addBlock(const SoundBlock& block);
    void     removeBlock(uint64_t id);
    SoundBlock* getBlock(uint64_t id);
    const std::vector<SoundBlock>& getAllBlocks() const { return blocks; }
    int getBlockCount() const { return static_cast<int>(blocks.size()); }

    SceneSnapshot buildSnapshot(const Vec3f& listenerPos,
                                const Vec3f& listenerForward,
                                const Vec3f& listenerRight,
                                double transportTimeSec,
                                bool isPlaying,
                                const SampleLibrary& library) const;

    bool saveToFile(const juce::File& file) const;
    bool loadFromFile(const juce::File& file, SampleLibrary& library);
    void clear();

private:
    std::vector<SoundBlock> blocks;
    uint64_t nextId = 1;
};
