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

#include "MathUtils.h"
#include "BlockType.h"
#include <string>
#include <vector>

struct MovementKeyFrame
{
    double timeSec;   // Time relative to block start
    Vec3i  position;  // Absolute world position at this keyframe
};

/// A single scheduled mute window for a block.  When the playhead is inside
/// [startSec, startSec + durationSec), the block is silenced (movement and
/// other state still play).  Blocks may carry any number of these — see
/// `BlockEntry::muteWindows` and the Mute Schedule popup.
struct MuteWindow
{
    double startSec    = 0.0;
    double durationSec = 0.0;

    bool isActive() const noexcept { return durationSec > 0.0; }
    double endSec()  const noexcept { return startSec + durationSec; }
    bool contains(double t) const noexcept
    {
        return isActive() && t >= startSec && t < endSec();
    }
};

/// A single scheduled sound for a block.  Distinct from looping and from the
/// block's main region: it plays the given sound (which must belong to the
/// block's instrument type) once, starting at `startSec`, optionally cut at
/// `startSec + durationSec`.  A block may carry any number of these so it can
/// play e.g. note A at 5s and note B at 45s.  See `BlockEntry::soundSchedule`
/// and the Sound Schedule popup.
struct SoundEvent
{
    double      startSec    = 0.0;
    double      durationSec = 1.0;   ///< play window; sample is cut at the end if longer
    int         soundId     = -1;    ///< runtime sound id (resolved from relativePath)
    std::string relativePath;        ///< library-relative path, used to persist + reload

    /// When true the scheduled sound loops (seamlessly via the audio-thread loop
    /// buffer) for the whole [startSec, endSec) window, with `loopGapSec` seconds
    /// of silence inserted between repeats (0 = tight loop).  Mirrors the block's
    /// own Loop mode but scoped to this single scheduled note.  Persisted.
    bool        loop        = false;
    double      loopGapSec  = 0.0;

    // ── Runtime-only sequencer state (NOT persisted) ─────────────────────────
    bool started  = false;
    bool finished = false;

    double endSec() const noexcept { return startSec + std::max(0.05, durationSec); }
};

/// Per-block playback behaviour for the WAV vs the block's region duration.
///
/// Natural  – sound plays once; if shorter than the region, the rest is silent.
/// Loop     – sound buffer loops continuously inside the audio thread until the
///            region's stop event fires.  Closes the "2 seconds won't play"
///            gap that the old sequencer-retrigger loop had.
/// Stretch  – sound is slowed (rate < 1) so it fills the whole region.  Pitch
///            drops as a side effect — true pitch-preserving time-stretch
///            (WSOLA / phase vocoder) is a future feature.
/// Speed    – sound is sped up (rate > 1) to finish inside the region.  Pitch
///            rises as a side effect, same caveat as Stretch.
enum class BlockPlaybackMode : uint8_t
{
    Natural = 0,
    Loop    = 1,
    Stretch = 2,
    Speed   = 3
};

inline const char* blockPlaybackModeName(BlockPlaybackMode m) noexcept
{
    switch (m)
    {
        case BlockPlaybackMode::Natural: return "Natural";
        case BlockPlaybackMode::Loop:    return "Loop";
        case BlockPlaybackMode::Stretch: return "Stretch (slow)";
        case BlockPlaybackMode::Speed:   return "Speed (fast)";
    }
    return "Natural";
}

/// One-shot duration/movement reconciliation actions, applied from the sidebar
/// when a block's sound length and movement length disagree.  Each sets a
/// combination of region duration, movement duration and playbackMode so the
/// sequencer (which already fits sound to region via Stretch/Speed and movement
/// to effectiveMovementDuration) produces the intended behaviour.
enum class DurationSyncAction : uint8_t
{
    MatchDurationToSound = 0,  ///< region = sound length (existing "Match" button)
    DistortSoundToMovement,    ///< speed/slow the SOUND to fit the movement length (audio affected -> warn)
    DistortMovementToSound,    ///< stretch/compress the MOVEMENT to the sound length (audio natural)
    HardCutAtMovement          ///< region = movement length; sound plays natural and is cut at the end
};

struct TimeRange
{
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    int currentKeyframeIndex = 0;
    int loopIterationsFired  = 0;

    std::vector<bool> triggeredKeyframes;

    double endTimeSec() const
    {
        return startTimeSec + durationSec;
    }

    void resetPlaybackState()
    {
        hasStarted = false;
        hasFinished = false;
        isPlaying = false;
        currentKeyframeIndex = 0;
        loopIterationsFired = 0;
        triggeredKeyframes.clear();
    }
};

struct BlockEntry
{
    // ── Identity ──────────────────────────────────────────────────────────────
    int       serial    = 0;
    BlockType blockType = BlockType::Violin;
    Vec3i     pos;               ///< Requires Vec3i from MathUtils.h
    Vec3f     colour;

    // ── Audio mapping ─────────────────────────────────────────────────────────
    int         soundId        = -1;   ///< -1 = silent / unassigned
    std::string customFilePath;        ///< Non-empty for Custom blocks with user WAV

    // ── Timing (seconds relative to transport origin) ─────────────────────────
    double startTimeSec = 0.0;
    double durationSec  = 1.0;

    std::vector<TimeRange> timesList;

    // ── Loop ──────────────────────────────────────────────────────────────────
    // LEGACY (kept for old .sime file back-compat).  New code should use
    // playbackMode == BlockPlaybackMode::Loop instead.  Load reconstructs the
    // mode from these fields; save writes both so older builds still parse.
    bool   isLooping           = false;
    double loopDurationSec     = 4.0;
    int    loopIterationsFired = 0;      ///< Runtime: legacy retrigger counter

    // ── Playback behaviour (Phase 1 movement work) ───────────────────────────
    BlockPlaybackMode playbackMode      = BlockPlaybackMode::Natural;

    /// Movement playback length, in seconds.  0 = use the block's own region
    /// duration (durationSec).  Lets the user prolong the motion path without
    /// changing the audio region width.
    double            movementDurationSec = 0.0;

    /// World-space Y offset applied to every recorded keyframe when playing
    /// back movement.  Lets the user lift / lower the whole recorded path
    /// after the fact, without re-recording (initial motion capture is still
    /// XZ + Shift+scroll).
    int               movementYOffset     = 0;

    /// Returns the effective movement duration in seconds — `movementDurationSec`
    /// if > 0, otherwise the block's region `durationSec`.
    double effectiveMovementDuration() const noexcept
    {
        return movementDurationSec > 0.001 ? movementDurationSec : durationSec;
    }

    /// Natural length of the WAV sample assigned to this block, in seconds.
    /// Cached by the GL render path each time the sample library changes; the
    /// SequencerEngine reads it to compute Stretch / Speed rates.  0 means
    /// "unknown" (rate falls back to 1.0).
    double sampleNaturalDurationSec = 0.0;

    // Recording state
    bool isRecordingMovement = false;
    double recordingStartTime = 0.0;
    Vec3i recordingStartPos;

    /// Multi-segment recording (runtime only).  When the user records a movement
    /// while the playhead is past the block's start, the new keyframes are spliced
    /// into the existing path at this block-relative offset instead of replacing
    /// it.  `recordedMovementBackup` keeps the prior path so Cancel can restore it.
    double recordingTimeOffset = 0.0;
    std::vector<MovementKeyFrame> recordedMovementBackup;

    // recorded movement data
    bool hasRecordedMovement = false; ///< Block has a saved movement path (keyframes on disk)
    bool movementEnabled     = true;  ///< When false, path is kept but not played during transport

    /// When true the recorded movement path loops: at the end of the path the
    /// block instantly teleports back to its first keyframe (start position) and
    /// replays, repeating until the region ends.  This is the ONLY situation in
    /// which a block is allowed to teleport — normal playback always steps along
    /// the path.  Persisted.
    bool movementLoop = false;
    /// Runtime loop counter used to detect path wraps so we can re-arm the
    /// keyframe triggers each loop (NOT persisted).
    int  movementLoopIndex = 0;

    /// Runtime-only per-block movement freeze (NOT persisted).  When true this
    /// single block holds its current position during playback even if it has a
    /// path — same semantics as the global "Freeze Move" toolbar toggle, but
    /// scoped to one block (sidebar "Freeze this block" toggle).  Un-freezing
    /// resumes motion from the current transport time.
    bool movementFrozen      = false;
    /// GL-thread transition tracker for movementFrozen / global freeze (runtime).
    bool wasMovementFrozen   = false;
    std::vector<MovementKeyFrame> recordedMovement; ///< Optional per-block movement path for sequenced motion
    bool durationLocked = false;

    // ── Per-block UI / playback flags (v7) ───────────────────────────────────
    /// When true the sequencer skips Start / Stop events for this block —
    /// the path still animates but no audio is emitted.
    bool isMuted = false;

    /// When true the renderer skips drawing this block (and its highlight /
    /// arrows).  Selection / sequencing still work — purely a viewport-clean
    /// helper for composers focusing on a subset of the scene.
    bool isHidden = false;

    /// When > 0 and the block is in Loop mode, the audio thread inserts this
    /// many seconds of silence between successive plays of the sample.
    /// 0 = tight loop (the existing behaviour).
    double loopBufferSec = 0.0;

    /// Legacy single-window mute fields (v8 scene format).
    /// Kept here only so old `.sime` files still round-trip cleanly — the
    /// engine now reads `muteWindows` and load() converts these into the
    /// first entry on import.  Save no longer writes them.
    double muteStartSec = 0.0;
    double muteEndSec   = 0.0;

    /// User-defined mute schedule.  Each entry silences this block while the
    /// playhead is inside [startSec, startSec + durationSec).  Empty list =
    /// no scheduled mutes (the "Mute (forever)" toggle is independent).
    std::vector<MuteWindow> muteWindows;

    /// User-defined sound schedule.  Each entry plays a sound (of this block's
    /// type) at its startSec.  Lets one block fire several different notes over
    /// time (e.g. violin A at 5s, violin B at 45s) without extra blocks.
    std::vector<SoundEvent> soundSchedule;

    /// Returns true if `t` falls inside any of the active mute windows.
    bool isInsideAnyMuteWindow(double t) const noexcept
    {
        for (const auto& w : muteWindows)
            if (w.contains(t))
                return true;
        return false;
    }

    // ── Runtime-only flags (NOT persisted) ───────────────────────────────────
    /// Re-computed every sequencer tick from
    ///   isMuted || (per-type indefinite mute toggled by the toolbar).
    /// SequencerEngine uses this in place of isMuted for the "indefinite"
    /// silence test; it stays out of SceneFile so the toolbar's transient
    /// view-state never leaks into saved scenes.
    bool effectiveMuted = false;

    /// Tracks whether the previous sequencer tick saw this block as muted
    /// (either indefinite or window).  Used to detect transitions so we can
    /// cut / resume the live voice mid-region.  Reset in resetPlaybackState().
    bool wasMutedLastTick = false;

    /// Reset playback-mode fields to factory defaults (does not clear movement
    /// keyframes or position / timing).
    void resetPlaybackDefaults() noexcept
    {
        playbackMode        = BlockPlaybackMode::Natural;
        movementDurationSec = 0.0;
        movementYOffset     = 0;
        isLooping           = false;
        loopDurationSec     = 4.0;
    }

    // Playback state for movement
    size_t currentKeyframeIndex = 0;
    std::vector<bool> triggeredKeyframes;

    // ── Playback state (written by SequencerEngine each frame) ────────────────
    bool hasStarted  = false;
    bool hasFinished = false;
    bool isPlaying   = false;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// End time used for sequencer / transport bookkeeping.
    /// Loop blocks span their full loopDurationSec; non-loop blocks use durationSec.
    double endTimeSec() const noexcept
    {
        return startTimeSec + (isLooping ? loopDurationSec : durationSec);
    }
    bool overlaps(double aStart, double aDuration, double bStart, double bDuration)
    {
        const double aEnd = aStart + aDuration;
        const double bEnd = bStart + bDuration;

        return aStart < bEnd && aEnd > bStart;
    }
    bool addTimeRange(double start, double duration)
    {
        if (start < 0.0 || duration <= 0.05)
            return false;


        if (overlaps(start, duration, startTimeSec, durationSec))
            return false;
        for (const auto& t : timesList)
        {
            if (overlaps(start, duration, t.startTimeSec, t.durationSec))
                return false;
        }

        timesList.push_back({ start, duration });
        return true;
    }

    void resetPlaybackState()
    {
        hasStarted  = false;
        hasFinished = false;
        isPlaying   = false;
        currentKeyframeIndex = 0;
        loopIterationsFired  = 0;
        wasMutedLastTick     = false;

        triggeredKeyframes.clear();
        if (hasRecordedMovement && !recordedMovement.empty())
        {
            triggeredKeyframes.resize(recordedMovement.size(), false);
        }

        for (auto& se : soundSchedule)
        {
            se.started  = false;
            se.finished = false;
        }

        movementLoopIndex = -1;
    }

    /// User-facing display name like "Violin 1", "Piano 3".
    ///
    /// The number is the 1-based position of this block in the list of all
    /// blocks of the *same type* (preserving insertion order).  This is what
    /// the user sees in the sidebar / timeline / info panel; the internal
    /// `serial` field is still the stable unique ID used by the sequencer.
    ///
    /// Deletion automatically renumbers, because we recompute from the
    /// current vector every time the list is rebuilt.
    static juce::String displayName(const BlockEntry& block,
                                    const std::vector<BlockEntry>& allBlocks)
    {
        int ordinal = 0;
        for (const auto& b : allBlocks)
        {
            if (b.blockType == block.blockType)
            {
                ++ordinal;
                if (b.serial == block.serial)
                    break;
            }
        }
        if (ordinal == 0) ordinal = 1;   // fallback if not found

        return juce::String(blockTypeDisplayName(block.blockType))
             + " " + juce::String(ordinal);
    }

    static Vec3f getBlockColor(BlockType type, int soundId)
    {
        // Custom / Synth blocks vary by soundId so different user WAVs look distinct.
        if (type == BlockType::Custom || type == BlockType::Synth)
        {
            static const Vec3f kPalette[] = {
                { 0.92f, 0.92f, 0.92f },   // white
                { 0.95f, 0.85f, 0.20f },   // yellow
                { 0.20f, 0.85f, 0.85f },   // cyan
                { 0.85f, 0.38f, 0.85f },   // magenta
                { 0.95f, 0.55f, 0.18f },   // orange
                { 0.65f, 0.48f, 0.90f },   // purple
            };
            constexpr int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);
            int idx = ((soundId % kPaletteSize) + kPaletteSize) % kPaletteSize;
            return kPalette[idx];
        }

        // Every other type: delegate to the canonical color helper in BlockType.h.
        auto c = blockTypeColor(type);
        return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
    }
};
