#pragma once
 
// ---------------------------------------------------------------------------
// SequencerEvent
//
// A lightweight value type produced by SequencerEngine and consumed by
// AudioEngine.  Carries just enough info to act on a voice without coupling
// the two systems together.
// ---------------------------------------------------------------------------
 
enum class SequencerEventType
{
    Start,  ///< Begin playing soundId for this block
    Stop    ///< Stop the voice that was started for this block
};
 
struct SequencerEvent
{
    SequencerEventType type    = SequencerEventType::Start;
    int blockSerial            = -1;  ///< Identifies which block triggered this event
    int soundId                = -1;  ///< Which sample to play / stop
    double triggerTimeSec      = 0.0; ///< Transport time at which the event fired
};