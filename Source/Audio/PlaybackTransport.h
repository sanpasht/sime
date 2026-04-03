#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PlaybackTransport.h  –  Global playback state machine.
//
// UI thread calls play/pause/stop/seekTo.
// Audio thread calls advanceTime and reads state via atomics.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>

class PlaybackTransport
{
public:
    void play()
    {
        playing.store(true, std::memory_order_release);
    }

    void pause()
    {
        playing.store(false, std::memory_order_release);
    }

    void stop()
    {
        playing.store(false, std::memory_order_release);
        currentTimeSec.store(0.0, std::memory_order_release);
    }

    void seekTo(double seconds)
    {
        currentTimeSec.store(seconds, std::memory_order_release);
    }

    void togglePlayPause()
    {
        bool wasPlaying = playing.load(std::memory_order_acquire);
        playing.store(!wasPlaying, std::memory_order_release);
    }

    bool isPlaying() const
    {
        return playing.load(std::memory_order_acquire);
    }

    double getCurrentTime() const
    {
        return currentTimeSec.load(std::memory_order_acquire);
    }

    void advanceTime(double deltaSec)
    {
        double t = currentTimeSec.load(std::memory_order_relaxed);
        currentTimeSec.store(t + deltaSec, std::memory_order_release);
    }

private:
    std::atomic<bool>   playing       { false };
    std::atomic<double> currentTimeSec { 0.0 };
};
