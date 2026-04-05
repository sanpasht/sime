#pragma once

// ---------------------------------------------------------------------------
// TransportClock
//
// A simple timeline clock driven by explicit update() calls.
// Callers (e.g. ViewComponent::renderOpenGL) call update(dt) each frame and
// query currentTimeSec() to feed the SequencerEngine.
//
// Thread safety: not thread-safe by default; call only from one thread
// (typically the render / message thread).  If you later need to call
// currentTimeSec() from the audio callback, guard reads with an atomic or
// move timing fully into the audio callback.
// ---------------------------------------------------------------------------
class TransportClock
{
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------
    TransportClock() = default;

    // -----------------------------------------------------------------------
    // Transport control
    // -----------------------------------------------------------------------

    /// Begin or resume playback from the current position.
    void start() noexcept;

    /// Pause playback; currentTimeSec() is frozen until start() is called.
    void pause() noexcept;

    /// Stop playback and rewind to time 0.
    void stop() noexcept;

    // -----------------------------------------------------------------------
    // Per-frame update
    //
    // Call once per frame (or per audio buffer) with the elapsed wall-clock
    // seconds since the last call.  Has no effect while paused or stopped.
    // -----------------------------------------------------------------------
    void update(double deltaTimeSec) noexcept;

    // -----------------------------------------------------------------------
    // State queries
    // -----------------------------------------------------------------------

    double currentTimeSec() const noexcept { return currentTime_; }

    bool isPlaying() const noexcept { return playing_; }
    bool isPaused()  const noexcept { return paused_;  }
    bool isStopped() const noexcept { return !playing_ && !paused_; }

    // -----------------------------------------------------------------------
    // Optional looping
    // -----------------------------------------------------------------------

    void  setLooping(bool loop, double loopEndSec = 0.0) noexcept;
    bool  isLooping()    const noexcept { return looping_; }
    double loopEndSec()  const noexcept { return loopEnd_; }

    // -----------------------------------------------------------------------
    // Direct seek (useful for editor scrubbing)
    // -----------------------------------------------------------------------
    void seekTo(double timeSec) noexcept;

private:
    double currentTime_ = 0.0;
    bool   playing_     = false;
    bool   paused_      = false;

    // Loop settings
    bool   looping_     = false;
    double loopEnd_     = 0.0;   ///< Loop wraps when currentTime_ >= loopEnd_
};
