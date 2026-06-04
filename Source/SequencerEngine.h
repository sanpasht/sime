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
    /// Scheduled sounds (BlockEntry::soundSchedule) play on synthetic voice
    /// serials so a Stop for the block's real region never cuts them.  A unique
    /// serial per (block, entry) keeps them independent of one another too.
    static constexpr int kScheduledSerialBase = 2'000'000;
    static constexpr int kMaxScheduledPerBlock = 1'000;
    static int scheduledSerial(int blockSerial, int entryIndex) noexcept
    {
        return kScheduledSerialBase
             + blockSerial * kMaxScheduledPerBlock
             + entryIndex;
    }

    /// Scan all blocks against the current transport time.
    /// Updates hasStarted / isPlaying / hasFinished on each BlockEntry.
    /// Returns Start and Stop events for the AudioEngine to act on.
    /// Call once per frame from the GL / render thread.
    std::vector<SequencerEvent> update(const TransportClock&    clock,
                                       std::vector<BlockEntry>& blocks);
                                       
    /// Process recorded movement for blocks during playback
    static void updateBlockMovement(std::vector<BlockEntry>& blocks, 
                                     double currentTime);

    /// Reset all block playback state — call after transport stop or loop wrap.
    static void resetAllBlocks(std::vector<BlockEntry>& blocks) noexcept;

    /// Snap each moving block's visual position to where it would be at
    /// transport time @p timeSec, honouring movementDurationSec and
    /// movementYOffset.  Used after seek / scrub so blocks jump to the
    /// correct on-path position without playing audio.  Returns true if any
    /// block's position changed (so callers can flag the renderer dirty).
    static bool snapBlockPositionsToTime(std::vector<BlockEntry>& blocks,
                                         double timeSec) noexcept;

    /// Snap a single block's visual position to where it would be at transport
    /// time @p timeSec (same rules as snapBlockPositionsToTime, for one block).
    /// Returns true if the position changed.  Used by per-block movement freeze
    /// to re-sync only the block that just un-froze.
    static bool snapBlockToTime(BlockEntry& b, double timeSec) noexcept;

private:
    std::vector<SequencerEvent> eventBuffer_;
};