#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SequencerEngine.h
// ─────────────────────────────────────────────────────────────────────────────

// MathUtils must come before BlockEntry so Vec3i is defined.
#include "MathUtils.h"
#include "BlockEntry.h"
#include "SequencerEvent.h"
#include "TransportClock.h"

#include <vector>

class SequencerEngine
{
public:
    /// Scan all blocks against the current transport time.
    /// Updates hasStarted / isPlaying / hasFinished on each BlockEntry.
    /// Returns Start and Stop events for the AudioEngine to act on.
    /// Call once per frame from the GL / render thread.
    std::vector<SequencerEvent> update(const TransportClock&    clock,
                                       std::vector<BlockEntry>& blocks);

    /// Reset all block playback state — call after transport stop or loop wrap.
    static void resetAllBlocks(std::vector<BlockEntry>& blocks) noexcept;

private:
    std::vector<SequencerEvent> eventBuffer_;
};