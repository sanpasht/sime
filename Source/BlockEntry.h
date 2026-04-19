#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BlockEntry.h
//
// IMPORTANT: MathUtils.h must be included before this file wherever BlockEntry
// is used.  Vec3i is defined there.  This header deliberately does NOT include
// MathUtils.h itself to avoid double-inclusion ordering issues in translation
// units that already pull in the full JUCE chain.
//
// Safe include order (in every .cpp and .h that needs BlockEntry):
//
//   #include "MathUtils.h"
//   #include "BlockEntry.h"
//
// ViewPortComponent.h already does this, so any .cpp that includes
// ViewPortComponent.h gets both automatically.
// ─────────────────────────────────────────────────────────────────────────────

#include "BlockType.h"
#include <string>
struct MovementKeyFrame
{
    double timeSec;   // Time relative to block start
    Vec3i  position;  // Absolute world position at this keyframe
};
struct BlockEntry
{
    // ── Identity ──────────────────────────────────────────────────────────────
    int       serial    = 0;
    BlockType blockType = BlockType::Violin;
    Vec3i     pos;               ///< Requires Vec3i from MathUtils.h

    // ── Audio mapping ─────────────────────────────────────────────────────────
    int         soundId        = -1;   ///< -1 = silent / unassigned
    std::string customFilePath;        ///< Non-empty for Custom blocks with user WAV

    // ── Timing (seconds relative to transport origin) ─────────────────────────
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    // Recording state
    bool isRecordingMovement = false;
    double recordingStartTime = 0.0;
    Vec3i recordingStartPos;

    // recorded movement data
    bool hasRecordedMovement = false; ///< Whether this block has any recorded movement keyframes
    std::vector<MovementKeyFrame> recordedMovement; ///< Optional per-block movement path for sequenced motion
    bool durationLocked = false;

    // Playback state for movement
    size_t currentKeyframeIndex = 0;
    std::vector<bool> triggeredKeyframes;

    // ── Playback state (written by SequencerEngine each frame) ────────────────
    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    double endTimeSec() const noexcept { return startTimeSec + durationSec; }

    void resetPlaybackState()
    {
        hasStarted = false;
        hasFinished = false;
        isPlaying = false;
        currentKeyframeIndex = 0;
        
        triggeredKeyframes.clear();
        if (hasRecordedMovement && !recordedMovement.empty())
        {
            triggeredKeyframes.resize(recordedMovement.size(), false);
        }
    }
};